// fitWorker.ts — runs the lcr_wasm module off the UI thread.
//
// Protocol (postMessage):
//   → { type: 'run', jobId, job: FitJob }
//   ← { type: 'ok',  jobId, response: FitResponse }
//   ← { type: 'err', jobId, error: string }   (module load failure etc.)
// A busy worker never receives a second 'run' — the client serialises jobs
// and cancels by terminate()+respawn.

import createLcrModule from '../wasm/lcr_wasm.js'
import wasmUrl from '../wasm/lcr_wasm.wasm?url'
import type { LcrWasmModule } from '../wasm/lcr_wasm.js'
import type { FitJob, FitResponse, ZPoint } from '../lib/fitTypes'

let mod: LcrWasmModule | null = null
let loadError: string | null = null
let loading: Promise<LcrWasmModule> | null = null

async function ensureModule(): Promise<LcrWasmModule> {
  if (mod) return mod
  if (loadError) throw new Error(loadError)
  if (!loading) {
    loading = createLcrModule({ locateFile: () => wasmUrl }).then(
      (m) => {
        mod = m
        return m
      },
      (e: unknown) => {
        loadError = e instanceof Error ? e.message : String(e)
        throw new Error(`WASM 模块加载失败：${loadError}`)
      },
    )
  }
  return loading
}

function writeF64(m: LcrWasmModule, arr: number[] | Float64Array): number {
  const ptr = m._malloc(arr.length * 8)
  for (let i = 0; i < arr.length; i++) m.HEAPF64[(ptr >> 3) + i] = arr[i]
  return ptr
}

function pointsToArrays(points: ZPoint[]): [number[], number[], number[]] {
  const f: number[] = [], re: number[] = [], im: number[] = []
  for (const p of points) {
    f.push(p.f)
    re.push(p.re)
    im.push(p.im)
  }
  return [f, re, im]
}

function takeJson(m: LcrWasmModule, ptr: number): FitResponse {
  const s = m.UTF8ToString(ptr)
  m._lcr_free(ptr)
  return JSON.parse(s) as FitResponse
}

async function runJob(job: FitJob): Promise<FitResponse> {
  const m = await ensureModule()
  const [f, re, im] = pointsToArrays(job.points)
  const n = job.points.length
  const pf = writeF64(m, f)
  const pr = writeF64(m, re)
  const pi = writeF64(m, im)
  try {
    if (job.try === 1) {
      return takeJson(
        m,
        m._lcr_try1(
          pf, pr, pi, n,
          job.exactN && job.exactN > 0 ? job.exactN : 0,
          job.maxN && job.maxN > 0 ? job.maxN : 0,
          job.topK ?? 5,
        ),
      )
    }
    if (job.try === 2) {
      const rows = job.components.length
      const kindsP = m._malloc(rows)
      const valsP = m._malloc(rows * 8)
      const dcrsP = m._malloc(rows * 8)
      const cntsP = m._malloc(rows * 4)
      try {
        job.components.forEach((c, i) => {
          m.HEAPU8[kindsP + i] = c.kind.charCodeAt(0)
          m.HEAPF64[(valsP >> 3) + i] = c.value
          m.HEAPF64[(dcrsP >> 3) + i] = c.dcr
          m.HEAP32[(cntsP >> 2) + i] = c.count
        })
        return takeJson(
          m,
          m._lcr_try2(pf, pr, pi, n, kindsP, valsP, dcrsP, cntsP, rows, job.topK ?? 5),
        )
      } finally {
        m._free(kindsP)
        m._free(valsP)
        m._free(dcrsP)
        m._free(cntsP)
      }
    }
    // try 3
    const m3 = job.edges.length
    const usP = m._malloc(m3 * 4)
    const vsP = m._malloc(m3 * 4)
    const ksP = m._malloc(m3)
    try {
      job.edges.forEach((e, i) => {
        m.HEAP32[(usP >> 2) + i] = e.u
        m.HEAP32[(vsP >> 2) + i] = e.v
        m.HEAPU8[ksP + i] = e.kind.charCodeAt(0)
      })
      return takeJson(m, m._lcr_try3(pf, pr, pi, n, usP, vsP, ksP, m3))
    } finally {
      m._free(usP)
      m._free(vsP)
      m._free(ksP)
    }
  } finally {
    m._free(pf)
    m._free(pr)
    m._free(pi)
  }
}

self.addEventListener('message', async (ev: MessageEvent) => {
  const data = ev.data as { type: string; jobId: number; job?: FitJob }
  if (data?.type !== 'run' || !data.job) return
  try {
    const response = await runJob(data.job)
    self.postMessage({ type: 'ok', jobId: data.jobId, response })
  } catch (e) {
    self.postMessage({
      type: 'err',
      jobId: data.jobId,
      error: e instanceof Error ? e.message : String(e),
    })
  }
})

export {}
