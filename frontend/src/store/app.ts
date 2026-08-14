import { defineStore } from 'pinia'

export const useAppStore = defineStore('app', {
  state: () => ({
    currentScanId: null as string | null,
    device: '—',
    deviceOnline: false,
  }),
  actions: {
    setScan(id: string | null) {
      this.currentScanId = id
    },
  },
})
