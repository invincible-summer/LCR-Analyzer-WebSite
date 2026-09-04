// lcrWasm.ts — client-side API around the fit worker: run FitJobs (Try1/2/3)
// off the UI thread.  One job at a time; `cancel()` terminates and respawns
// the worker (the C++ engines have no mid-run interrupt hook).

import type { FitJob, FitResponse } from './fitTypes'

interface WorkerMessage {
  type: 'ok' | 'err'
  jobId: number
  response?: FitResponse
  error?: string
}

let worker: Worker | null = null
let jobId = 0
let pending: ((msg: WorkerMessage) => void) | null = null
let workerBroken = false

function spawn(): Worker {
  const w = new Worker(new URL('../workers/fitWorker.ts', import.meta.url), {
    type: 'module',
  })
  w.addEventListener('message', (ev: MessageEvent<WorkerMessage>) => {
    const cb = pending
    pending = null
    cb?.(ev.data)
  })
  w.addEventListener('error', (ev) => {
    const cb = pending
    pending = null
    workerBroken = true
    cb?.({ type: 'err', jobId: -1, error: ev.message || 'worker 异常' })
  })
  return w
}

function ensureWorker(): Worker {
  if (!worker || workerBroken) {
    workerBroken = false
    worker = spawn()
  }
  return worker
}

/** Run one fit job; rejects on transport/module errors. */
export function runFitJob(job: FitJob): Promise<FitResponse> {
  if (pending) return Promise.reject(new Error('已有计算在进行中，请先取消'))
  const w = ensureWorker()
  return new Promise<FitResponse>((resolve, reject) => {
    const myId = ++jobId
    pending = (msg) => {
      if (msg.type === 'ok' && msg.response) resolve(msg.response)
      else reject(new Error(msg.error || '计算失败'))
    }
    // Vue reactive proxies are not structured-cloneable — serialise to plain data
    w.postMessage({ type: 'run', jobId: myId, job: JSON.parse(JSON.stringify(job)) })
  })
}

/** Kill the running job (terminate + respawn on next use). */
export function cancelFitJob(): void {
  if (worker) {
    worker.terminate()
    worker = null
  }
  const cb = pending
  pending = null
  workerBroken = false
  cb?.({ type: 'err', jobId: -1, error: '已取消' })
}
