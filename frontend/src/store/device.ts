// device.ts — 本地 BLE 设备会话 store（与服务器 scan store 完全分离）
//
// 状态机（plan.md §7.4）：
//   unsupported → idle → chooser → connecting → ready → receiving
//   → validating → complete | error
// 错误信息必须区分：用户取消 chooser / GATT 断线 / 协议版本不匹配 /
// 数据集类型不符 / seq 断裂 / CRC 不匹配 / CSV 非法 / 浏览器不支持。
import { defineStore } from 'pinia'
import {
  chooseAndConnect,
  receiveDataset,
  webBluetoothSupported,
  type DeviceDataset,
  type LcrDeviceSession,
} from '../lib/ble/lcrDevice'
import type { DatasetMetadata } from '../lib/ble/protocol'

export type DeviceState =
  | 'unsupported'
  | 'idle'
  | 'chooser'
  | 'connecting'
  | 'ready'
  | 'receiving'
  | 'validating'
  | 'complete'
  | 'error'

export interface DeviceStoreState {
  phase: DeviceState
  error: string
  progress: number // 0..1（字节级）
  dataset: DeviceDataset | null
  /** 收到的原始 CSV（CRC 校验后的原始 bytes 解码），供“保存收到的 CSV” */
  savedAt: number
}

export const useDeviceStore = defineStore('device', {
  state: (): DeviceStoreState => ({
    phase: webBluetoothSupported() ? 'idle' : 'unsupported',
    error: '',
    progress: 0,
    dataset: null,
    savedAt: 0,
  }),
  getters: {
    supported: (s): boolean => s.phase !== 'unsupported',
    busy: (s): boolean =>
      s.phase === 'chooser' || s.phase === 'connecting' || s.phase === 'receiving' || s.phase === 'validating',
    metadata: (s): DatasetMetadata | null => s.dataset?.metadata ?? null,
  },
  actions: {
    reset() {
      if (this.busy) return
      this.phase = webBluetoothSupported() ? 'idle' : 'unsupported'
      this.error = ''
      this.progress = 0
      this.dataset = null
    },
    /** 完整流程：选设备 → 连接 → 接收 → 校验（从按钮点击调用） */
    async importFromDevice(): Promise<DeviceDataset | null> {
      if (this.phase === 'unsupported') return null
      if (this.busy) return null
      this.error = ''
      this.dataset = null
      this.progress = 0

      let session: LcrDeviceSession | null = null
      try {
        this.phase = 'chooser'
        session = await chooseAndConnect()
        this.phase = 'connecting'
        this.phase = 'ready'

        this.phase = 'receiving'
        const ds = await receiveDataset(session)

        this.phase = 'validating'
        // 字节流与 CRC 已在 receiveDataset 内校验；此处只留下数据集
        this.dataset = ds
        this.progress = 1
        this.phase = 'complete'
        this.savedAt = Date.now()
        return ds
      } catch (e) {
        this.error = e instanceof Error ? e.message : String(e)
        this.phase = 'error'
        return null
      } finally {
        session?.disconnect()
      }
    },
  },
})
