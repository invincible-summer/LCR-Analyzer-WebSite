import { defineStore } from 'pinia'
import * as api from '../api'

export const useScanStore = defineStore('scan', {
  state: () => ({
    scans: [] as api.ScanSummary[],
    currentId: null as string | null,
    detail: null as api.ScanDetail | null,
    loading: false,
    error: '' as string,
    loadedAt: 0,
  }),
  getters: {
    measurements: (s): api.Measurement[] => s.detail?.measurements ?? [],
    current: (s): api.ScanSummary | null =>
      s.scans.find((x) => x.id === s.currentId) ?? s.detail ?? null,
  },
  actions: {
    async loadScans() {
      this.scans = await api.listScans()
      if (!this.currentId && this.scans.length) {
        await this.select(this.scans[0].id)
      }
    },
    async select(id: string | null) {
      this.currentId = id
      this.detail = null
      if (!id) return
      this.loading = true
      this.error = ''
      try {
        this.detail = await api.getScan(id)
        this.loadedAt = Date.now()
      } catch (e: any) {
        this.error = e.message || String(e)
      } finally {
        this.loading = false
      }
    },
    async refresh() {
      await this.loadScans()
      if (this.currentId) await this.select(this.currentId)
    },
  },
})
