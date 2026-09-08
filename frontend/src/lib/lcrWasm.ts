import type { FitJob, FitResponse } from './fitTypes'
export const FIT_AVAILABLE = true
let active: { worker: Worker; reject: (reason: Error) => void } | undefined
export function runFitJob(job: FitJob): Promise<FitResponse> {
  if (active) return Promise.reject(new Error('已有计算正在运行，请等待或取消'))
  return new Promise((resolve,reject)=>{
    const worker=new Worker(new URL('../workers/fitWorker.ts',import.meta.url),{type:'module'})
    active={worker,reject}
    const done=()=>{worker.terminate();if(active?.worker===worker)active=undefined}
    worker.onmessage=({data})=>{done();if(data.error)reject(new Error(data.error));else resolve(data.response)}
    worker.onerror=(event)=>{done();reject(new Error(event.message||'计算 Worker 加载失败'))}
    worker.onmessageerror=()=>{done();reject(new Error('计算响应无法解码'))}
    try { worker.postMessage(JSON.parse(JSON.stringify(job))) } catch(e) {done();reject(e)}
  })
}
export function cancelFitJob(): void {
  if(!active)return
  const job=active;active=undefined;job.worker.terminate();job.reject(new Error('已取消'))
}
