import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

// Backend the dev server proxies to. Override with LCR_BACKEND=http://host:port
const backend = process.env.LCR_BACKEND || 'http://localhost:8000'

export default defineConfig({
  plugins: [vue()],
  server: {
    port: 5173,
    proxy: {
      '/api': backend,
      '/ws': { target: backend, ws: true },
    },
  },
})
