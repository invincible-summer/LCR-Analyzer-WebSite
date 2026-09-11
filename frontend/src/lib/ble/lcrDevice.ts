// lcrDevice.ts — Web Bluetooth 设备会话（仅负责 BLE，不做拟合）
//
// 职责边界（plan.md §7.1）：
//   1. 用户点击触发 requestDevice()（service filter 用 v1 UUID）；
//   2. GATT connect → 读 Metadata → 订阅 Status/Data notifications；
//   3. 发送 START_TRANSFER；
//   4. 按 seq 重组字节流，校验 byte_count + CRC32 + protocol/schema；
//   5. TextDecoder('utf-8') 得到 CSV，返回 dataset —— 拟合交给 parseZCsv。
//
// 浏览器能力检测在页面加载时进行：不支持 Web Bluetooth（或非 secure
// context）时上层按钮禁用并给明确提示，不当成普通「连接失败」。
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

/**
 * 接收一个封存数据集：读 metadata → 订阅 data/status → START →
 * seq 重组 → byte_count/CRC32 校验 → CSV 文本。
 * 中途断线 / seq gap / CRC 错误：抛错（调用方可重连后 RESTART）。
 * onProgress：assembler 每收到新 payload 后回调（received/total 字节），
 * 供 store 把 progress 从 0 单调推进到 1（plan.md §14.3）。
 */
export async function receiveDataset(
  session: LcrDeviceSession,
  onProgress?: (received: number, total: number) => void,
): Promise<DeviceDataset> {
  const svc = await session.server.getPrimaryService(LCR_SERVICE_UUID)
  const meta = await readMetadata(session.server)

  const statusCh = await svc.getCharacteristic(LCR_STATUS_UUID)
  const dataCh = await svc.getCharacteristic(LCR_DATA_UUID)
  const controlCh = await svc.getCharacteristic(LCR_CONTROL_UUID)

  const assembler = new DatasetAssembler()
  let stopped = false
  const failures: unknown[] = []

  const onDisconnected = () => {
    if (!stopped) failures.push(new ProtocolError('GATT 连接中断'))
  }
  session.device.addEventListener('gattserverdisconnected', onDisconnected)

  const onData = (e: Event) => {
    const ch = e.target as BluetoothRemoteGATTCharacteristic
    if (!ch.value) return
    try {
      const frame = decodeFrame(ch.value.buffer)
      if (frame) {
        assembler.push(frame)
        onProgress?.(assembler.bytesReceived, meta.byte_count)
      }
    } catch (err) {
      failures.push(err)
    }
  }
  await dataCh.addEventListener('characteristicvaluechanged', onData)
  await dataCh.startNotifications()
  await statusCh.startNotifications()

  try {
    await controlCh.writeValueWithResponse(new Uint8Array([BleCommand.StartTransfer]))

    // 等待整份字节流到齐（或失败）。设备按 4 帧/poll 泵出，12KB/12B 帧
    // 最坏 ~700 帧；轮询间隔 100ms 足够宽松。
    const deadline = Date.now() + 120_000
    while (assembler.bytesReceived < meta.byte_count) {
      if (failures.length) throw failures[0]
      if (Date.now() > deadline) throw new ProtocolError('接收超时（120s）')
      await new Promise((r) => setTimeout(r, 100))
    }
    // 再等一小拍，确保设备侧状态稳定
    await new Promise((r) => setTimeout(r, 50))
    if (failures.length) throw failures[0]

    const bytes = assembler.finish(meta.byte_count, meta.crc32)
    const csvText = new TextDecoder('utf-8').decode(bytes)
    const kind = meta.dataset_kind
    return kind === 'ONE_PORT_Z'
      ? { kind, csvText, metadata: meta, rawBytes: bytes }
      : { kind: 'TWO_PORT_H', csvText, metadata: meta, rawBytes: bytes }
  } finally {
    stopped = true
    try {
      await controlCh.writeValueWithResponse(new Uint8Array([BleCommand.AbortTransfer]))
    } catch {
      /* 传输完成后的 abort 失败可忽略 */
    }
    await dataCh.stopNotifications().catch(() => undefined)
    await statusCh.stopNotifications().catch(() => undefined)
    await dataCh.removeEventListener('characteristicvaluechanged', onData)
    session.device.removeEventListener('gattserverdisconnected', onDisconnected)
  }
}
