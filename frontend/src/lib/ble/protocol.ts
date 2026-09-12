// protocol.ts — LCR BLE GATT v1 协议常量 + 帧解码 + 重组器（纯逻辑，可单测）
//
// 与固件真源 ino/LCR_UI/ble_protocol.h / protocol/BLE_PROTOCOL_V1.md 一致：
//   Service:  6e6f0001-5f31-4c43-a001-6c63722d7631
//   Control:  6e6f0002-…  WRITE WITH RESPONSE（定长命令字节）
//   Status:   6e6f0003-…  READ + NOTIFY
//   Metadata: 6e6f0004-…  READ（UTF-8 JSON）
//   Data:     6e6f0005-…  NOTIFY（分帧 CSV）
//
// Data 帧布局：
//   [0]=’L’ [1]=’C’ [2]=protocol [3]=kind [4..5]=seq LE [6..7]=payload_len LE
//   [8..]=CSV bytes
// seq 为 uint16，在线上按 modulo 2^16 连续递增：0xffff 的下一帧是 0。
// v1 不改变帧布局；丢帧/CRC/停滞由接收层自动 ABORT + RESTART 整份重传。

import { crc32 } from './crc32'

export const LCR_SERVICE_UUID = '6e6f0001-5f31-4c43-a001-6c63722d7631'
export const LCR_CONTROL_UUID = '6e6f0002-5f31-4c43-a001-6c63722d7631'
export const LCR_STATUS_UUID = '6e6f0003-5f31-4c43-a001-6c63722d7631'
export const LCR_METADATA_UUID = '6e6f0004-5f31-4c43-a001-6c63722d7631'
export const LCR_DATA_UUID = '6e6f0005-5f31-4c43-a001-6c63722d7631'

export const PROTOCOL_VERSION = 1
export const BLE_SEQ_SPACE = 0x1_0000

export function nextBleSeq(seq: number): number {
  return (seq + 1) & 0xffff
}

export enum BleCommand {
  StartTransfer = 0x01,
  RestartTransfer = 0x02,
  AbortTransfer = 0x03,
  GetStatus = 0x04,
}

export type DatasetKind = 'ONE_PORT_Z' | 'TWO_PORT_H'

/**
 * metadata 特征的 JSON 形状（见 protocol/BLE_PROTOCOL_V1.md / CSV_SCHEMA_V2.md）。
 * v1（历史）：含 calibration_id/drive_vrms 语义（固件曾杜撰 factory-none）。
 * v2（4.1.0 起）：calibration_id 移除，改为诚实的 calibration_state
 * （lcr_api_cal_status 摘要；双端口为 raw_w_path）+ measurement_backend。
 * v1 字段在 v2 元数据中不存在，因此设为可选。
 */
export interface DatasetMetadata {
  protocol: number
  firmware: string
  session_id: number
  dataset_kind: DatasetKind
  schema: string
  point_count: number
  byte_count: number
  crc32: string
  /** v1 历史字段；v2 起不再发送（固件不再杜撰校准 ID） */
  calibration_id?: string
  /** v2：DNT 校准状态摘要（cal:3/10,open:ok,short:-- 或 raw_w_path） */
  calibration_state?: string
  /** v2：测量后端标识（DO_NOT_TOUCH_lcr_api） */
  measurement_backend?: string
}

/** Status 特征负载（15 字节）解析结果 */
export interface DeviceStatus {
  protocol: number
  state: number
  errorCode: number
  sessionId: number
  bytesSent: number
  bytesTotal: number
}

export class ProtocolError extends Error {
  constructor(message: string) {
    super(message)
    this.name = 'ProtocolError'
  }
}

export function parseStatus(value: DataView): DeviceStatus {
  if (value.byteLength < 15) throw new ProtocolError(`Status 特征长度不足：${value.byteLength}`)
  return {
    protocol: value.getUint8(0),
    state: value.getUint8(1),
    errorCode: value.getUint8(2),
    sessionId: value.getUint32(3, true),
    bytesSent: value.getUint32(7, true),
    bytesTotal: value.getUint32(11, true),
  }
}

export function parseMetadata(text: string): DatasetMetadata {
  let raw: Record<string, unknown>
  try {
    raw = JSON.parse(text) as Record<string, unknown>
  } catch {
    throw new ProtocolError('Metadata 不是合法 JSON')
  }
  // v1/v2 共同必需字段（calibration_id 仅 v1 存在，v2 用 calibration_state）
  const need = [
    'protocol', 'firmware', 'session_id', 'dataset_kind', 'schema',
    'point_count', 'byte_count', 'crc32',
  ] as const
  for (const k of need) if (!(k in raw)) throw new ProtocolError(`Metadata 缺少字段 ${k}`)
  const kind = raw['dataset_kind']
  if (kind !== 'ONE_PORT_Z' && kind !== 'TWO_PORT_H')
    throw new ProtocolError(`未知 dataset_kind：${String(kind)}`)
  const m = raw as unknown as DatasetMetadata
  if (m.protocol !== PROTOCOL_VERSION)
    throw new ProtocolError(
      `设备协议版本 ${m.protocol} 不被支持（本站支持 v${PROTOCOL_VERSION}，请升级固件）`,
    )
  if (!Number.isSafeInteger(m.byte_count) || m.byte_count <= 0)
    throw new ProtocolError(`Metadata byte_count 非法：${String(m.byte_count)}`)
  if (!Number.isSafeInteger(m.point_count) || m.point_count < 0)
    throw new ProtocolError(`Metadata point_count 非法：${String(m.point_count)}`)
  return m
}

export interface DataFrame {
  protocol: number
  kind: number
  seq: number
  payload: Uint8Array
}

/** 解码一个 Data 帧；帧头非法时返回 null（由调用方决定断开/重启） */
export function decodeFrame(buffer: ArrayBufferLike): DataFrame | null {
  const v =
    buffer instanceof DataView ? buffer : new DataView(buffer as ArrayBuffer)
  if (v.byteLength < 8) return null
  if (v.getUint8(0) !== 0x4c || v.getUint8(1) !== 0x43) return null // 'L''C'
  const payloadLen = v.getUint16(6, true)
  if (v.byteLength < 8 + payloadLen) return null
  return {
    protocol: v.getUint8(2),
    kind: v.getUint8(3),
    seq: v.getUint16(4, true),
    payload: new Uint8Array(buffer as ArrayBuffer, 8, payloadLen),
  }
}

/**
 * 按序重组 Data 帧为字节流（纯逻辑，vitest 直接覆盖）。
 * seq 必须从 0 连续递增并按 uint16 wrap；gap → ProtocolError，接收层自动
 * ABORT + RESTART_TRANSFER。这样不会把 65535→0 的合法回绕误判成丢包。
 */
export class DatasetAssembler {
  private chunks: Uint8Array[] = []
  private received = 0
  private nextSeq = 0

  push(frame: DataFrame): void {
    if (frame.protocol !== PROTOCOL_VERSION)
      throw new ProtocolError(`帧协议版本 ${frame.protocol} 不受支持`)
    if (frame.seq !== this.nextSeq)
      throw new ProtocolError(`seq 断裂：期望 ${this.nextSeq}，收到 ${frame.seq}`)
    this.chunks.push(frame.payload)
    this.received += frame.payload.length
    this.nextSeq = nextBleSeq(this.nextSeq)
  }

  reset(): void {
    this.chunks = []
    this.received = 0
    this.nextSeq = 0
  }

  get bytesReceived(): number {
    return this.received
  }

  /** 组装并校验 byte_count + CRC32（通过后返回原始 bytes） */
  finish(byteCount: number, crc32Hex: string): Uint8Array {
    if (this.received !== byteCount)
      throw new ProtocolError(`byte_count 不一致：收到 ${this.received}，应为 ${byteCount}`)
    const out = new Uint8Array(this.received)
    let o = 0
    for (const c of this.chunks) {
      out.set(c, o)
      o += c.length
    }
    const actual = crc32(out).toString(16).toUpperCase().padStart(8, '0')
    if (actual !== crc32Hex.toUpperCase())
      throw new ProtocolError(`CRC32 不匹配：收到 ${actual}，应为 ${crc32Hex}`)
    return out
  }
}
