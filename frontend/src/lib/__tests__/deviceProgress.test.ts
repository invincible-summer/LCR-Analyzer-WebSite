// deviceProgress.test.ts —— receiveDataset 的 onProgress 字节级进度（plan.md §14.3）
//
// 用手写的假 GATT session（只实现 receiveDataset 实际触碰到的方法）驱动
// 完整接收流程：验证 progress 从首帧起有值、单调不减、最终到 1，
// 且收到的 CSV 与发送字节完全一致（CRC32 校验通过）。
import { describe, expect, it } from 'vitest'
import { receiveDataset, type LcrDeviceSession } from '../ble/lcrDevice'
import { LCR_CONTROL_UUID, LCR_DATA_UUID, LCR_METADATA_UUID, LCR_SERVICE_UUID, LCR_STATUS_UUID } from '../ble/protocol'
import { crc32 } from '../ble/crc32'

class FakeChar {
  uuid: string
  listeners: Record<string, (e: unknown) => void> = {}
  readValue?: () => Promise<ArrayBuffer>
  written: number[] = []
  constructor(uuid: string) { this.uuid = uuid }
  addEventListener(_t: string, h: (e: unknown) => void): Promise<void> { this.listeners['data'] = h; return Promise.resolve() }
  removeEventListener(): Promise<void> { return Promise.resolve() }
  startNotifications(): Promise<void> { return Promise.resolve() }
  stopNotifications(): Promise<void> { return Promise.resolve() }
  writeValueWithResponse(v: Uint8Array): Promise<void> { this.written.push(v[0]!); return Promise.resolve() }
  // 测试侧注入一帧（模拟 characteristicvaluechanged）
  emit(frame: Uint8Array): void {
    const h = this.listeners['data']
    if (!h) throw new Error('no listener')
    h({ target: { value: new DataView(frame.buffer as ArrayBuffer) } })
  }
}

function makeFakeSession(csv: string, kind: 'ONE_PORT_Z' | 'TWO_PORT_H') {
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

  const session: LcrDeviceSession = {
    device: {
      addEventListener: () => undefined,
      removeEventListener: () => undefined,
      gatt: undefined,
    } as unknown as LcrDeviceSession['device'],
    server: {
      getPrimaryService: () =>
        Promise.resolve({
          getCharacteristic: (uuid: string) =>
            Promise.resolve(chars.find((c) => c.uuid === uuid) ?? statusCh),
        }),
    } as unknown as LcrDeviceSession['server'],
    disconnect: () => undefined,
  }

  // START_TRANSFER 后按 13 字节/帧分帧推送
  const startDelivering = () => {
    let seq = 0
    for (let off = 0; off < bytes.length; off += 13) {
      const payload = bytes.slice(off, off + 13)
      const frame = new Uint8Array(8 + payload.length)
      frame[0] = 0x4c; frame[1] = 0x43; frame[2] = 1
      frame[3] = kind === 'ONE_PORT_Z' ? 0 : 1
      frame[4] = seq & 0xff; frame[5] = (seq >> 8) & 0xff
      frame[6] = payload.length & 0xff; frame[7] = (payload.length >> 8) & 0xff
      frame.set(payload, 8)
      const s = seq
      setTimeout(() => dataCh.emit(frame), 1 + s * 3)
      seq++
    }
  }
  const origWrite = controlCh.writeValueWithResponse.bind(controlCh)
  controlCh.writeValueWithResponse = (v: Uint8Array) => {
    if (v[0] === 0x01) startDelivering()
    return origWrite(v)
  }
  return session
}

describe('receiveDataset onProgress（BLE 字节级进度）', () => {
  it('progress 单调从 0->1，收到的 CSV 与发送字节一致', async () => {
    const csv = [
      '# lcr-dataset=one-port-z',
      '# schema=lcr-z-csv-v2',
      '# protocol=1',
      '# firmware=4.1.0',
      '# measurement_backend=DO_NOT_TOUCH_lcr_api',
      '# calibration_state=cal:3/10,open:ok,short:--',
      'f,re,im',
      ...Array.from({ length: 8 }, (_, i) => `${100 * (i + 1)},${1000 + i},${(i % 3) - 1}`),
    ].join('\n')
    const session = makeFakeSession(csv, 'ONE_PORT_Z')
    const seen: Array<{ received: number; total: number }> = []
    const ds = await receiveDataset(session, (received, total) => seen.push({ received, total }))
    expect(ds.kind).toBe('ONE_PORT_Z')
    expect(ds.csvText).toBe(csv)
    // 进度：有回调、首回调即 >0、单调不减、末回调 received==total
    expect(seen.length).toBeGreaterThanOrEqual(4)
    expect(seen[0].received).toBeGreaterThan(0)
    for (let i = 1; i < seen.length; ++i)
      expect(seen[i].received).toBeGreaterThanOrEqual(seen[i - 1].received)
    const last = seen[seen.length - 1]
    expect(last.received).toBe(last.total)
    expect(last.total).toBe(new TextEncoder().encode(csv).length)
    // 归一化 progress 终值 == 1
    expect(last.received / last.total).toBe(1)
  })
})
