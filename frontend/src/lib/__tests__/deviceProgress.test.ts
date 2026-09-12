// deviceProgress.test.ts —— receiveDataset 的字节进度 + 自动重传/重连
//
// 用手写假 GATT session 驱动完整接收流程：验证 progress 单调、CRC 后 CSV
// 完整一致；注入 seq gap 验证同连接 ABORT+RESTART；连续耗尽 4 个 attempt
// 验证前端主动断开并重建 GATT 后可自动恢复。
import { describe, expect, it } from 'vitest'
import { receiveDataset, type LcrDeviceSession } from '../ble/lcrDevice'
import {
  BleCommand,
  LCR_CONTROL_UUID,
  LCR_DATA_UUID,
  LCR_METADATA_UUID,
  LCR_SERVICE_UUID,
  LCR_STATUS_UUID,
} from '../ble/protocol'
import { crc32 } from '../ble/crc32'

class FakeChar {
  uuid: string
  listeners: Record<string, (e: unknown) => void> = {}
  readValue?: () => Promise<ArrayBuffer>
  written: number[] = []
  constructor(uuid: string) { this.uuid = uuid }
  addEventListener(_t: string, h: (e: unknown) => void): void { this.listeners['data'] = h }
  removeEventListener(): void { delete this.listeners['data'] }
  startNotifications(): Promise<void> { return Promise.resolve() }
  stopNotifications(): Promise<void> { return Promise.resolve() }
  writeValueWithResponse(v: Uint8Array): Promise<void> { this.written.push(v[0]!); return Promise.resolve() }
  emit(frame: Uint8Array): void {
    const h = this.listeners['data']
    if (!h) throw new Error('no listener')
    h({ target: { value: new DataView(frame.buffer as ArrayBuffer) } })
  }
}

interface FakeOptions {
  /** 前 N 个 START/RESTART stream 故意制造 seq 0 -> 2 gap。 */
  gapAttempts?: number
}

interface FakeHarness {
  session: LcrDeviceSession
  controlCh: FakeChar
  reconnectCount: () => number
  disconnectCount: () => number
}

function makeFrame(kind: 'ONE_PORT_Z' | 'TWO_PORT_H', seq: number, payload: Uint8Array): Uint8Array {
  const frame = new Uint8Array(8 + payload.length)
  frame[0] = 0x4c; frame[1] = 0x43; frame[2] = 1
  frame[3] = kind === 'ONE_PORT_Z' ? 0 : 1
  frame[4] = seq & 0xff; frame[5] = (seq >> 8) & 0xff
  frame[6] = payload.length & 0xff; frame[7] = (payload.length >> 8) & 0xff
  frame.set(payload, 8)
  return frame
}

function makeFakeSession(
  csv: string,
  kind: 'ONE_PORT_Z' | 'TWO_PORT_H',
  options: FakeOptions = {},
): FakeHarness {
  const bytes = new TextEncoder().encode(csv)
  const meta = {
    protocol: 1,
    firmware: '4.1.0',
    session_id: 42,
    dataset_kind: kind,
    schema: kind === 'ONE_PORT_Z' ? 'lcr-z-csv-v2' : 'lcr-h-csv-v2',
    point_count: 8,
    byte_count: bytes.length,
    crc32: crc32(bytes).toString(16).toUpperCase().padStart(8, '0'),
    calibration_state: 'cal:3/10,open:ok,short:--',
    measurement_backend: 'DO_NOT_TOUCH_lcr_api',
  }
  const metaCh = new FakeChar(LCR_METADATA_UUID)
  metaCh.readValue = () =>
    Promise.resolve(new TextEncoder().encode(JSON.stringify(meta)).buffer as ArrayBuffer)
  const dataCh = new FakeChar(LCR_DATA_UUID)
  const statusCh = new FakeChar(LCR_STATUS_UUID)
  const controlCh = new FakeChar(LCR_CONTROL_UUID)
  const chars = [metaCh, dataCh, statusCh, controlCh]

  const server = {
    getPrimaryService: (_uuid: string) =>
      Promise.resolve({
        getCharacteristic: (uuid: string) =>
          Promise.resolve(chars.find((c) => c.uuid === uuid) ?? statusCh),
      }),
  } as unknown as LcrDeviceSession['server']

  let disconnectHandler: ((e: Event) => void) | undefined
  let reconnects = 0
  let disconnects = 0
  const gatt = {
    connected: true,
    connect: async () => {
      ++reconnects
      gatt.connected = true
      return server
    },
    disconnect: () => {
      ++disconnects
      gatt.connected = false
      disconnectHandler?.(new Event('gattserverdisconnected'))
    },
  }
  const device = {
    gatt,
    addEventListener: (type: string, h: EventListenerOrEventListenerObject) => {
      if (type === 'gattserverdisconnected' && typeof h === 'function')
        disconnectHandler = h
    },
    removeEventListener: (type: string, h: EventListenerOrEventListenerObject) => {
      if (type === 'gattserverdisconnected' && disconnectHandler === h)
        disconnectHandler = undefined
    },
  } as unknown as LcrDeviceSession['device']

  const session: LcrDeviceSession = {
    device,
    server,
    disconnect: () => gatt.disconnect(),
  }

  let streamAttempt = 0
  const startDelivering = (injectGap: boolean) => {
    if (injectGap) {
      const p0 = bytes.slice(0, Math.min(13, bytes.length))
      const p1 = bytes.slice(p0.length, Math.min(p0.length + 13, bytes.length))
      setTimeout(() => dataCh.emit(makeFrame(kind, 0, p0)), 2)
      if (p1.length)
        setTimeout(() => dataCh.emit(makeFrame(kind, 2, p1)), 5) // 故意跳过 seq=1
      return
    }

    let seq = 0
    for (let off = 0; off < bytes.length; off += 13) {
      const payload = bytes.slice(off, off + 13)
      const frame = makeFrame(kind, seq, payload)
      const s = seq
      setTimeout(() => dataCh.emit(frame), 2 + s * 3)
      seq++
    }
  }
  const origWrite = controlCh.writeValueWithResponse.bind(controlCh)
  controlCh.writeValueWithResponse = (v: Uint8Array) => {
    const cmd = v[0]
    if (cmd === BleCommand.StartTransfer || cmd === BleCommand.RestartTransfer) {
      const injectGap = streamAttempt < (options.gapAttempts ?? 0)
      streamAttempt++
      startDelivering(injectGap)
    }
    return origWrite(v)
  }

  return {
    session,
    controlCh,
    reconnectCount: () => reconnects,
    disconnectCount: () => disconnects,
  }
}

function sampleCsv(): string {
  return [
    '# lcr-dataset=one-port-z',
    '# schema=lcr-z-csv-v2',
    '# protocol=1',
    '# firmware=4.1.0',
    '# measurement_backend=DO_NOT_TOUCH_lcr_api',
    '# calibration_state=cal:3/10,open:ok,short:--',
    'f,re,im',
    ...Array.from({ length: 8 }, (_, i) => `${100 * (i + 1)},${1000 + i},${(i % 3) - 1}`),
  ].join('\n')
}

describe('receiveDataset BLE fragmented transfer', () => {
  it('progress 单调从 0->1，收到的 CSV 与发送字节一致', async () => {
    const csv = sampleCsv()
    const { session } = makeFakeSession(csv, 'ONE_PORT_Z')
    const seen: Array<{ received: number; total: number }> = []
    const ds = await receiveDataset(session, (received, total) => seen.push({ received, total }))
    expect(ds.kind).toBe('ONE_PORT_Z')
    expect(ds.csvText).toBe(csv)
    expect(seen.length).toBeGreaterThanOrEqual(4)
    expect(seen[0].received).toBeGreaterThan(0)
    for (let i = 1; i < seen.length; ++i)
      expect(seen[i].received).toBeGreaterThanOrEqual(seen[i - 1].received)
    const last = seen[seen.length - 1]
    expect(last.received).toBe(last.total)
    expect(last.total).toBe(new TextEncoder().encode(csv).length)
  })

  it('首轮 seq gap 后自动 ABORT + RESTART，用户无需重新点击', async () => {
    const csv = sampleCsv()
    const { session, controlCh } = makeFakeSession(csv, 'ONE_PORT_Z', { gapAttempts: 1 })
    const seen: Array<{ received: number; total: number }> = []
    const ds = await receiveDataset(session, (received, total) => seen.push({ received, total }))

    expect(ds.csvText).toBe(csv)
    expect(controlCh.written).toContain(BleCommand.StartTransfer)
    expect(controlCh.written).toContain(BleCommand.AbortTransfer)
    expect(controlCh.written).toContain(BleCommand.RestartTransfer)
    for (let i = 1; i < seen.length; ++i)
      expect(seen[i].received).toBeGreaterThanOrEqual(seen[i - 1].received)
    const last = seen[seen.length - 1]
    expect(last.received).toBe(last.total)
  })

  it('4 个同连接 attempt 全部 seq gap 后自动重建 GATT，再次 START 后成功', async () => {
    const csv = sampleCsv()
    const harness = makeFakeSession(csv, 'ONE_PORT_Z', { gapAttempts: 4 })
    const ds = await receiveDataset(harness.session)

    expect(ds.csvText).toBe(csv)
    expect(harness.disconnectCount()).toBe(1)
    expect(harness.reconnectCount()).toBe(1)
    // 第一个连接 START + 3*RESTART，重连后的新 connection 再从 START 开始。
    expect(harness.controlCh.written.filter((x) => x === BleCommand.StartTransfer).length).toBe(2)
    expect(harness.controlCh.written.filter((x) => x === BleCommand.RestartTransfer).length).toBe(3)
  })
})
