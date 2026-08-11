import { defineStore } from 'pinia'

type Theme = 'dark' | 'light'

export const useAppStore = defineStore('app', {
  state: () => ({
    theme: 'dark' as Theme,
    currentScanId: null as string | null,
    device: '—',
    deviceOnline: false,
  }),
  actions: {
    applyTheme() {
      document.documentElement.setAttribute('data-theme', this.theme)
    },
    toggleTheme() {
      this.theme = this.theme === 'dark' ? 'light' : 'dark'
      this.applyTheme()
    },
    setScan(id: string | null) {
      this.currentScanId = id
    },
  },
})
