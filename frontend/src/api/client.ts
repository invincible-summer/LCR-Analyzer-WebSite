import axios from 'axios'

export const http = axios.create({
  // Relative in dev (proxied by Vite) and in prod (served same-origin by backend).
  baseURL: '/api',
  timeout: 30_000,
})

http.interceptors.response.use(
  (r) => r,
  (err) => {
    const msg = err?.response?.data?.detail || err?.message || 'request failed'
    return Promise.reject(new Error(typeof msg === 'string' ? msg : JSON.stringify(msg)))
  },
)
