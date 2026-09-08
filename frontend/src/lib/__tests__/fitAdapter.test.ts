import { describe,it,expect,vi,afterEach } from 'vitest'
import { adaptReport, type NativeReport } from '../fitAdapter'
import { runFitJob,cancelFitJob } from '../lcrWasm'
const report: NativeReport = {
 schema:'lcr.native.v4',try:3,elapsed:1,mode:'Strict',termination:'complete',enumeration_complete:true,continuous_global_certified:false,hypothesis_family:'KNOWN_REDUCED_GRAPH',stats:{generated:1,structures:1,evaluated:1,numerical_failures:0},
 candidates:[{rank:1,devices:1,n_params:1,wrmse:0,max_rel:0,rss:0,aicc:null,adjacency:{v:2,slots:[{u:0,j:1,edges:[{t:'R',p:20,d:0}]}]},theory:{f:[1],re:[20],im:[0]},diagnostics:{verdict:'LOCAL',optimizer:'done',rank:1,condition:null,starts:16,converged_starts:16,weak:[0],at_bound:[]},groups:[{u:0,v:1,kind:'R',value:20,dcr:0,members:[0,1],mode:'series'}],dropped:[2]}]
}
describe('native report adaptation',()=>{
 it('preserves null diagnostics, physical groups and dropped edges',()=>{
  const r=adaptReport(report,{try:3,points:[],edges:[{u:0,v:2,kind:'R'},{u:2,v:1,kind:'R'},{u:0,v:3,kind:'C'}]})
  if(!r.ok||r.try!==3)throw Error('wrong type')
  expect(r.candidates[0].aicc).toBeNull();expect(r.try3.jac_cond).toBeNull()
  expect(r.try3.groups[0].weak).toEqual(['0']);expect(r.try3.edges.map(e=>e.status)).toEqual(['merged','merged','dropped'])
  expect(r.search.certified).toBe(false)
 })
 it('handles empty partial searches without a fake candidate',()=>{
  const r=adaptReport({...report,candidates:[],enumeration_complete:false,termination:'time_budget'},{try:3,points:[],edges:[]})
  expect(r.ok && r.candidates).toEqual([]);expect(r.ok && r.search.complete).toBe(false)
 })
})
class FakeWorker {
 static latest: FakeWorker
 onmessage: ((e: {data:unknown})=>void)|null=null
 onerror: ((e:{message:string})=>void)|null=null
 onmessageerror: (()=>void)|null=null
 terminate=vi.fn();postMessage=vi.fn()
 constructor(){FakeWorker.latest=this}
}
afterEach(()=>{cancelFitJob();vi.unstubAllGlobals()})
describe('worker lifecycle',()=>{
 it('settles cancellation and permits another run',async()=>{
  vi.stubGlobal('Worker',FakeWorker)
  const p=runFitJob({try:1,points:[]});const rejected=expect(p).rejects.toThrow('已取消')
  cancelFitJob();await rejected;expect(FakeWorker.latest.terminate).toHaveBeenCalledOnce()
  const q=runFitJob({try:1,points:[]});FakeWorker.latest.onmessage!({data:{response:{ok:false,code:'bad_input',error:'test'}}});expect(await q).toHaveProperty('ok',false)
 })
 it('rejects concurrent jobs and disposes failing workers',async()=>{
  vi.stubGlobal('Worker',FakeWorker)
  const p=runFitJob({try:1,points:[]});const failed=expect(p).rejects.toThrow('load failed')
  await expect(runFitJob({try:2,points:[],components:[]})).rejects.toThrow('已有计算')
  FakeWorker.latest.onerror!({message:'load failed'});await failed;expect(FakeWorker.latest.terminate).toHaveBeenCalledOnce()
 })
})

import { validateFitJob } from '../fitValidation'
describe('browser request validation',()=>{
 const points=[10,100,1000,10000].map(f=>({f,re:1000,im:0}))
 it('rejects fractional topology labels before typed-array coercion',()=>{
  expect(()=>validateFitJob({try:3,points,edges:[{u:0,v:1.5,kind:'R'}]})).toThrow()
 })
 it('accepts pure R Exact and validates explicit tolerance',()=>{
  const job={try:2 as const,points,components:[{kind:'R' as const,value:1000,dcr:0,count:1}]}
  expect(()=>validateFitJob(job)).not.toThrow()
  expect(()=>validateFitJob({...job,robust:true})).toThrow()
  expect(()=>validateFitJob({...job,robust:true,tolerance:.1})).not.toThrow()
 })
 it('rejects nonfinite points and invalid budgets',()=>{
  expect(()=>validateFitJob({try:1,points,seconds:-1})).toThrow()
  expect(()=>validateFitJob({try:1,points:[...points,{f:1,re:Infinity,im:0}]})).toThrow()
  expect(()=>validateFitJob({try:1,points,exactN:1.5})).toThrow()
 })
})
