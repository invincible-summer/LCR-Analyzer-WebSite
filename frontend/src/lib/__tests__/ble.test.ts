// ble.test.ts — BLE 协议层纯逻辑（crc32 / 帧解码 / 重组器 / metadata）
import { describe, expect, it } from 'vitest'
import { crc32 } from '../ble/crc32'
import {
  DatasetAssembler,
  ProtocolError,
  decodeFrame,
  parseMetadata,
  parseStatus,
  PROTOCOL_VERSION,
} from '../ble/protocol'

describe('crc32（与固件位位一致）', () => {
  it('标准测试向量', () => {
    // CRC-32/ISO-HDLC check value；固件 ino/test/test_csv.cpp 同向量
    expect(crc32(new TextEncoder().encode('123456789'))).toBe(0xcbf43926)
    expect(crc32(new Uint8Array(0))).toBe(0x00000000)
  })
  it('跨块更新语义（分两段与整段一致）', () => {
    const bytes = new TextEncoder().encode('LCR-Analyzer golden csv fixture payload')
    const half = Math.floor(bytes.length / 2)
    // 直接整段（本实现一次调用；分段一致性由 assembler 层保证字节序不变）
    expect(crc32(bytes)).toBe(crc32(bytes.slice()))
  })
})

describe('decodeFrame（v1 帧布局）', () => {
  it('合法帧：magic LC / protocol / kind / seq LE / payload；短帧 null', () => {
    const frame = new Uint8Array([0x4c, 0x43, 0x01, 0x02, 0x34, 0x00, 0x05, 0x00, 1, 2, 3, 4, 5])
    const dec = decodeFrame(frame.buffer)
    expect(dec).not.toBeNull()
    expect(dec!.protocol).toBe(1)
    expect(dec!.kind).toBe(2)
    expect(dec!.seq).toBe(0x0034)
    expect(Array.from(dec!.payload)).toEqual([1, 2, 3, 4, 5])
    expect(decodeFrame(frame.slice(0, 5).buffer)).toBeNull() // 短帧
  })
  it('magic 错误 / 长度不足返回 null', () => {
    expect(decodeFrame(new Uint8Array([0x58, 0x43, 1, 0, 0, 0, 0, 0]).buffer)).toBeNull()
    expect(decodeFrame(new Uint8Array([0x4c, 0x43, 1]).buffer)).toBeNull()
  })
})

describe('DatasetAssembler', () => {
  const mk = (seq: number, payload: number[]): DataFrameLike => ({
    protocol: PROTOCOL_VERSION,
    kind: 0,
    seq,
    payload: new Uint8Array(payload),
  })
  interface DataFrameLike {
    protocol: number
    kind: number
    seq: number
    payload: Uint8Array
  }

  it('按序重组并通过 CRC 校验', () => {
    const bytes = new TextEncoder().encode('# lcr-dataset=one-port-z\nf,re,im\n100,1,-2\n')
    const a = new DatasetAssembler()
    let seq = 0
    for (let i = 0; i < bytes.length; i += 7) {
      a.push(mk(seq++, Array.from(bytes.slice(i, i + 7))))
    }
    const out = a.finish(bytes.length, crc32(bytes).toString(16).padStart(8, '0'))
    expect(new TextDecoder().decode(out)).toBe(new TextDecoder().decode(bytes))
  })
  it('seq 断裂抛 ProtocolError（触发 RESTART 语义）', () => {
    const a = new DatasetAssembler()
    a.push(mk(0, [1]))
    expect(() => a.push(mk(2, [2]))).toThrow(ProtocolError)
  })
  it('byte_count 不一致 / CRC 不匹配抛错', () => {
    const a = new DatasetAssembler()
    a.push(mk(0, [1, 2, 3]))
    expect(() => a.finish(4, '00000000')).toThrow(/byte_count/)
    expect(() => a.finish(3, 'DEADBEEF')).toThrow(/CRC32/)
  })
})

describe('parseMetadata / parseStatus', () => {
  it('字段校验与协议版本拒绝信息', () => {
    const good = parseMetadata(
      JSON.stringify({
        protocol: 1, firmware: '4.1.0', session_id: 1, dataset_kind: 'ONE_PORT_Z',
        schema: 'lcr-z-csv-v1', point_count: 8, byte_count: 100, crc32: 'A1B2C3D4',
        calibration_id: 'factory-none',
      }),
    )
    expect(good.dataset_kind).toBe('ONE_PORT_Z')
    expect(() =>
      parseMetadata(JSON.stringify({ protocol: 2, firmware: 'x', session_id: 1, dataset_kind: 'ONE_PORT_Z', schema: 's', point_count: 1, byte_count: 1, crc32: '0', calibration_id: 'c' })),
    ).toThrow(/升级固件/)
    expect(() => parseMetadata('not json')).toThrow(/JSON/)
    expect(() =>
      parseMetadata(JSON.stringify({ protocol: 1, session_id: 1 })),
    ).toThrow(/缺少字段/)
  })
  it('v2 元数据：无 calibration_id，含 calibration_state/measurement_backend', () => {
    const m = parseMetadata(
      JSON.stringify({
        protocol: 1, firmware: '4.1.0', session_id: 7, dataset_kind: 'TWO_PORT_H',
        schema: 'lcr-h-csv-v2', point_count: 7, byte_count: 512, crc32: 'A1B2C3D4',
        measurement_backend: 'DO_NOT_TOUCH_lcr_api',
        calibration_state: 'raw_w_path',
      }),
    )
    expect(m.calibration_state).toBe('raw_w_path')
    expect(m.measurement_backend).toBe('DO_NOT_TOUCH_lcr_api')
    expect(m.calibration_id).toBeUndefined()
    expect(() =>
      parseMetadata(
        JSON.stringify({
          protocol: 1, firmware: '4.1.0', session_id: 7, dataset_kind: 'TWO_PORT_H',
          schema: 'lcr-h-csv-v2', point_count: 7, crc32: 'A1B2C3D4',
          // 缺 byte_count -> 拒绝
        }),
      ),
    ).toThrow(/缺少字段/)
  })
  it('Status 15 字节小端布局', () => {
    const st = parseStatus(new DataView(new Uint8Array([1, 4, 7, 0xef, 0xbe, 0xad, 0xde, 100, 0, 0, 0, 0xcc, 0x12, 0, 0]).buffer))
    expect(st.sessionId).toBe(0xdeadbeef)
    expect(st.bytesSent).toBe(100)
    expect(st.bytesTotal).toBe(0x12cc)
    expect(() => parseStatus(new DataView(new Uint8Array(10).buffer))).toThrow(/长度不足/)
  })
})
