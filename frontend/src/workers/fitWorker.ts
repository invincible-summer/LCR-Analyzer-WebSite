import { validateFitJob } from '../lib/fitValidation'
import createLcr from '../wasm/lcr.js'
import wasmUrl from '../wasm/lcr.wasm?url'
import type { FitJob } from '../lib/fitTypes'
import { adaptReport } from '../lib/fitAdapter'

self.addEventListener('message', async (event: MessageEvent<FitJob>) => {
  const job = event.data
  try {
    validateFitJob(job)
    const m = await createLcr({ locateFile: () => wasmUrl })
    const allocated: number[] = []
    let result = 0
    try {
      const put = (values: number[], integer = false) => {
        const p = m._malloc(values.length * (integer ? 4 : 8))
        if (!p) throw new Error('计算内存不足')
        allocated.push(p)
        if (integer) m.HEAP32.set(values, p / 4)
        else m.HEAPF64.set(values, p / 8)
        return p
      }
      const args = [put(job.points.map(p=>p.f)),put(job.points.map(p=>p.re)),put(job.points.map(p=>p.im)),job.points.length]
      m._lcr_configure(job.mode === 'Fast' ? 1 : 0,job.budget??0,job.seconds??0,job.try===2?job.tolerance??0:0,job.try===2?job.dcrTolerance??0:0,job.robust?1:0)
      if(job.try===1) result=m._lcr_try1(...args,job.exactN??0,job.maxN??0,job.topK??8)
      else if(job.try===2) result=m._lcr_try2(...args,put(job.components.map(c=>c.kind.charCodeAt(0)),true),put(job.components.map(c=>c.value)),put(job.components.map(c=>c.dcr)),put(job.components.map(c=>c.count),true),job.components.length,job.topK??8)
      else result=m._lcr_try3(...args,put(job.edges.map(e=>e.u),true),put(job.edges.map(e=>e.v),true),put(job.edges.map(e=>e.kind.charCodeAt(0)),true),job.edges.length)
      if(!result) throw new Error('算法未返回响应')
      const raw=JSON.parse(m.UTF8ToString(result))
      self.postMessage({ response:raw.ok===false?raw:adaptReport(raw,job) })
    } finally {
      if(result)m._lcr_free(result)
      allocated.forEach(p=>m._free(p))
    }
  } catch(e) { self.postMessage({error:e instanceof Error?e.message:String(e)}) }
})
