// lcrDevice.ts — Web Bluetooth 设备会话（仅负责 BLE，不做拟合）
//
// 职责边界（plan.md §7.1）：
//   1. 用户点击触发 requestDevice()（service filter 用 v1 UUID）；
//   2. GATT connect → 读 Metadata → 订阅 Status/Data notifications；
//   3. 发送 START_TRANSFER；
//   4. 按 seq 重组字节流，校验 byte_count + CRC32 + protocol/schema；
//   5. seq gap / 非法帧 / CRC / 停滞自动 ABORT + RESTART；GATT 断线自动重连；
//   6. TextDecoder('utf-8') 得到 CSV，返回 dataset —— 拟合交给 parseZCsv。
//
// BLE 单 notification 有 ATT_MTU-3 上限，因此固件自动把整份 CSV 分成许多
// 小帧；前端不假定一次通知能装完整数据集。浏览器能力检测在页面加载时进行：
// 不支持 Web Bluetooth（或非 secure context）时上层按钮禁用并给明确提示。
// 本文件不 import 任何拟合器/解析器（除协议层）。

import {
  BleCommand,
  DatasetAssembler,
  DatasetMetadata,
  LCR_CONTROL_UUID,
  LCR_DATA_UUID,
  LCR_METADATA_UUID,
  LCR_SERVICE_UUID,
  LCR_STATUS_UUID,
  ProtocolError,
  decodeFrame,
  parseMetadata,
} from './protocol'

export type DeviceDataset =
  | { kind: 'ONE_PORT_Z'; csvText: string; metadata: DatasetMetadata; rawBytes: Uint8Array }
  | { kind: 'TWO_PORT_H'; csvText: string; metadata: DatasetMetadata; rawBytes: Uint8Array }

const MAX_TRANSFER_ATTEMPTS = 4 // 1 次 START + 最多 3 次 RESTART
const MAX_RECONNECT_ATTEMPTS = 2
const TRANSFER_STALL_TIMEOUT_MS = 15_000
const RETRY_DRAIN_MS = 120
const RECONNECT_BASE_DELAY_MS = 250
const RECEIVE_POLL_MS = 50

class GattDisconnectedError extends ProtocolError {
  constructor() {
    super('GATT 连接中断')
    this.name = 'GattDisconnectedError'
  }
}

const sleep = (ms: number) => new Promise<void>((resolve) => setTimeout(resolve, ms))

/** Web Bluetooth 可用性（secure context + API 存在） */
export function webBluetoothSupported(): boolean {
  return (
    typeof navigator !== 'undefined' &&
    'bluetooth' in navigator &&
    navigator.bluetooth != null &&
    typeof navigator.bluetooth.requestDevice === 'function'
  )
}

export interface LcrDeviceSession {
  device: BluetoothDevice
  server: BluetoothRemoteGATTServer
  disconnect: () => void
}

/** 让用户选择 LCR 设备并完成 GATT 连接（必须由用户手势触发） */
export async function chooseAndConnect(): Promise<LcrDeviceSession> {
  if (!webBluetoothSupported()) throw new ProtocolError('当前浏览器不支持 Web Bluetooth（需 Chrome/Edge + HTTPS/localhost）')
  let device: BluetoothDevice
  try {
    device = await navigator.bluetooth.requestDevice({
      filters: [{ services: [LCR_SERVICE_UUID] }],
    })
  } catch (e) {
    // 用户取消 chooser 不是错误连接，单独抛出便于 UI 区分
    if (e instanceof DOMException && e.name === 'NotFoundError')
      throw new ProtocolError('已取消设备选择')
    throw e
  }
  const server = await device.gatt!.connect()
  return {
    device,
    server,
    disconnect: () => {
      try {
        device.gatt?.disconnect()
      } catch {
        /* 已断开 */
      }
    },
  }
}

async function readMetadata(server: BluetoothRemoteGATTServer): Promise<DatasetMetadata> {
  const svc = await server.getPrimaryService(LCR_SERVICE_UUID)
  const meta = await svc.getCharacteristic(LCR_METADATA_UUID)
  const value = await meta.readValue()
  return parseMetadata(new TextDecoder('utf-8').decode(value))
}

function expectedKindByte(meta: DatasetMetadata): number {
  return meta.dataset_kind === 'ONE_PORT_Z' ? 0 : 1
}

function errorText(err: unknown): string {
  return err instanceof Error ? err.message : String(err)
}

/**
 * 在一个已经建立的 GATT connection 上接收整份数据。通知分帧由 seq 重组；
 * seq gap、坏帧、CRC 错误或 15 s 无新增字节都会在同一连接内自动
 * ABORT_TRANSFER → drain → RESTART_TRANSFER，最多 3 次重试。
 */
async function receiveDatasetOnConnection(
  session: LcrDeviceSession,
  onProgress?: (received: number, total: number) => void,
): Promise<DeviceDataset> {
  const svc = await session.server.getPrimaryService(LCR_SERVICE_UUID)
  const meta = await readMetadata(session.server)

  const statusCh = await svc.getCharacteristic(LCR_STATUS_UUID)
  const dataCh = await svc.getCharacteristic(LCR_DATA_UUID)
  const controlCh = await svc.getCharacteristic(LCR_CONTROL_UUID)

  const assembler = new DatasetAssembler()
  const frameKind = expectedKindByte(meta)
  let stopped = false
  let linkFailure: unknown = null
  let attemptFailure: unknown = null
  let acceptingData = false
  let waitingForSeq0 = true
  let lastProgressAt = 0

  const onDisconnected = () => {
    if (!stopped) linkFailure = new GattDisconnectedError()
  }
  session.device.addEventListener('gattserverdisconnected', onDisconnected)

  const onData = (e: Event) => {
    if (!acceptingData || attemptFailure || linkFailure) return
    const ch = e.target as BluetoothRemoteGATTCharacteristic
    if (!ch.value) return
    try {
      const frame = decodeFrame(ch.value.buffer)
      if (!frame) throw new ProtocolError('收到非法/截断 BLE Data 帧')
      if (frame.kind !== frameKind)
        throw new ProtocolError(`dataset kind 不匹配：期望 ${frameKind}，收到 ${frame.kind}`)

      // RESTART command 进入设备主 loop 前，BLE host 里可能还有上一轮通知。
      // 每轮只从 seq=0 的新流开始；其它旧帧直接丢弃，避免它们污染重组器。
      if (waitingForSeq0) {
        if (frame.seq !== 0) return
        waitingForSeq0 = false
      }

      assembler.push(frame)
      if (assembler.bytesReceived > meta.byte_count)
        throw new ProtocolError(
          `收到数据超过 byte_count：${assembler.bytesReceived} > ${meta.byte_count}`,
        )
      lastProgressAt = Date.now()
      onProgress?.(assembler.bytesReceived, meta.byte_count)
    } catch (err) {
      attemptFailure = err
    }
  }
  dataCh.addEventListener('characteristicvaluechanged', onData)
  await dataCh.startNotifications()
  await statusCh.startNotifications()

  try {
    let lastAttemptError: unknown = null

    for (let attempt = 0; attempt < MAX_TRANSFER_ATTEMPTS; ++attempt) {
      assembler.reset()
      attemptFailure = null
      acceptingData = true
      waitingForSeq0 = true
      lastProgressAt = Date.now()

      const command = attempt === 0 ? BleCommand.StartTransfer : BleCommand.RestartTransfer
      try {
        await controlCh.writeValueWithResponse(new Uint8Array([command]))

        while (assembler.bytesReceived < meta.byte_count) {
          if (linkFailure) throw linkFailure
          if (attemptFailure) throw attemptFailure
          if (Date.now() - lastProgressAt > TRANSFER_STALL_TIMEOUT_MS)
            throw new ProtocolError(`接收停滞超过 ${TRANSFER_STALL_TIMEOUT_MS / 1000}s`)
          await sleep(RECEIVE_POLL_MS)
        }

        acceptingData = false
        if (linkFailure) throw linkFailure
        if (attemptFailure) throw attemptFailure

        // byte_count 到齐后立即做整份 CRC；任何丢通知、重复或乱序最终都不能
        // 越过这个门槛。CRC 失败会进入下面的自动 RESTART。
        const bytes = assembler.finish(meta.byte_count, meta.crc32)
        const csvText = new TextDecoder('utf-8').decode(bytes)
        const kind = meta.dataset_kind
        return kind === 'ONE_PORT_Z'
          ? { kind, csvText, metadata: meta, rawBytes: bytes }
          : { kind: 'TWO_PORT_H', csvText, metadata: meta, rawBytes: bytes }
      } catch (err) {
        acceptingData = false
        lastAttemptError = err
        if (linkFailure) throw linkFailure
        if (attempt + 1 >= MAX_TRANSFER_ATTEMPTS) {
          throw new ProtocolError(
            `BLE 数据传输在自动重试 ${MAX_TRANSFER_ATTEMPTS - 1} 次后仍失败：${errorText(err)}`,
          )
        }

        // 先让设备停止当前 stream，再给已经进入 host/controller queue 的旧通知
        // 一个短 drain 窗口。下一轮 waitingForSeq0 还会进一步过滤残留帧。
        try {
          await controlCh.writeValueWithResponse(new Uint8Array([BleCommand.AbortTransfer]))
        } catch {
          if (linkFailure) throw linkFailure
        }
        await sleep(RETRY_DRAIN_MS)
      }
    }

    throw lastAttemptError ?? new ProtocolError('BLE 数据传输失败')
  } finally {
    stopped = true
    acceptingData = false
    try {
      await controlCh.writeValueWithResponse(new Uint8Array([BleCommand.AbortTransfer]))
    } catch {
      /* 已完成或链路已断，abort 失败可忽略 */
    }
    await dataCh.stopNotifications().catch(() => undefined)
    await statusCh.stopNotifications().catch(() => undefined)
    dataCh.removeEventListener('characteristicvaluechanged', onData)
    session.device.removeEventListener('gattserverdisconnected', onDisconnected)
  }
}

/**
 * 接收一个封存数据集：读 metadata → 订阅 notifications → 自动分帧重组与重试。
 *
 * 稳定性策略分两层：
 * 1. 同一 GATT 连接内：seq/CRC/停滞自动 RESTART，最多 3 次；
 * 2. 真实 GATT 断线：不重新弹设备 chooser，使用已经授权的 BluetoothDevice
 *    自动 reconnect，最多 2 次，再重新读 metadata 并从 seq 0 开始。
 *
 * onProgress 对重传/重连保持单调，不会因为内部 assembler reset 让 UI 进度倒退。
 */
export async function receiveDataset(
  session: LcrDeviceSession,
  onProgress?: (received: number, total: number) => void,
): Promise<DeviceDataset> {
  let reportedReceived = 0
  let reportedTotal = 0
  const monotonicProgress = (received: number, total: number) => {
    if (reportedTotal !== total) {
      reportedTotal = total
      reportedReceived = 0
    }
    reportedReceived = Math.min(total, Math.max(reportedReceived, received))
    onProgress?.(reportedReceived, total)
  }

  let lastError: unknown = null
  for (let reconnectAttempt = 0; reconnectAttempt <= MAX_RECONNECT_ATTEMPTS; ++reconnectAttempt) {
    try {
      return await receiveDatasetOnConnection(session, monotonicProgress)
    } catch (err) {
      lastError = err
      const gatt = session.device.gatt
      const disconnected = err instanceof GattDisconnectedError || gatt?.connected === false
      if (!disconnected || reconnectAttempt >= MAX_RECONNECT_ATTEMPTS || !gatt) throw err

      await sleep(RECONNECT_BASE_DELAY_MS * (reconnectAttempt + 1))
      try {
        session.server = await gatt.connect()
      } catch (reconnectErr) {
        lastError = reconnectErr
        if (reconnectAttempt >= MAX_RECONNECT_ATTEMPTS - 1)
          throw new ProtocolError(
            `GATT 自动重连失败：${errorText(reconnectErr)}`,
          )
      }
    }
  }

  throw lastError ?? new ProtocolError('BLE 数据接收失败')
}
