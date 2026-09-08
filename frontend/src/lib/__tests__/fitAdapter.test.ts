import { describe,it,expect,vi,afterEach } from 'vitest'
import { adaptReport, type NativeReport } from '../fitAdapter'
import { runFitJob,cancelFitJob } from '../lcrWasm'
const report: NativeReport = {
 schema:'lcr.native.v4',schema_revision:2,engine_version:'4.1.0',try:3,elapsed:1,mode:'Strict',termination:'complete',
 enumeration_complete:true,continuous_global_certified:false,hypothesis_family:'KNOWN_REDUCED_GRAPH',
 noise_model:'relative_unknown_scale',equivalence_metric:'relative_curve',equivalence_threshold:1e-6,
 selection:{criterion:'NONE',qualified:false},
 stats:{generated:1,structures:1,evaluated:1,numerical_failures:0},
 candidates:[{
  rank:1,devices:1,n_params:1,wrmse:0,max_rel:0,rss:0,aicc:null,
  original_topology_key:'a',effective_topology_key:'b',effective_devices:1,
  adjacency:{v:2,slots:[{u:0,j:1,edges:[{t:'R',p:20,d:0}]}]},theory:{f:[1],re:[20],im:[0]},
  selection:{eligible:false,criterion:'NONE',score:null,delta:null,reasons:[]},
  diagnostics:{verdict:'LOCAL',optimizer:'converged_cost',numerical_status:'OK',identifiability_status:'FULL_RANK',fit_objective:0,
   rank:1,condition:null,singular_values:[1],standard_errors:[],approximate_ci95:[],weak:[0],at_bound:[],starts:16,best_start:0,
   converged_starts:16,agreeing_starts:14,robust_used:false,outlier_count:0,worst_backward_error:0,worst_rcond:1,
   parameters:[{id:0,edge:0,quantity:'value',kind:'R',value:20,lower:2e-3,upper:2e7,free:true,fixed:false,weak:true,at_bound:false,standard_error:null,ci95:null}]},
  groups:[{gid:0,u:0,v:1,kind:'R',value:20,dcr:0,members:[0,1],mode:'ser',value_bounds:[2e-3,2e7],dcr_bounds:null,parameter_ids:[0],
   value_expr:{op:'sum',children:[{op:'value',edge:0},{op:'value',edge:1}]},dcr_expr:null}],
  dropped:[2]}]
}
describe('native report adaptation',()=>{
 it('maps group diagnostics through explicit parameter descriptors',()=>{
  const r=adaptReport(report,{try:3,points:[],edges:[{u:0,v:2,kind:'R'},{u:2,v:1,kind:'R'},{u:0,v:3,kind:'C'}]})
  if(!r.ok||r.try!==3)throw Error('wrong type')
  expect(r.candidates[0].aicc).toBeNull();expect(r.try3.jac_cond).toBeNull()
  expect(r.try3.groups[0].weak).toEqual(['R1']);expect(r.try3.groups[0].fixed).toEqual([])
  expect(r.try3.groups[0].expression.value).toBe('R1+R2')
  expect(r.try3.groups[0].value_bounds).toEqual({lo:2e-3,hi:2e7})
  expect(r.try3.edges.map(e=>e.status)).toEqual(['merged','merged','dropped'])
  expect(r.search.certified).toBe(false)
  expect(r.search.selection).toEqual({criterion:'NONE',qualified:false})
  expect(r.search.equivalence_metric).toBe('relative_curve')
  expect(r.candidates[0].selection?.eligible).toBe(false)
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
 it('rejects try2 component counts beyond the engine hard limit 8',()=>{
  const job={try:2 as const,points,components:[{kind:'R' as const,value:1000,dcr:0,count:9}]}
  expect(()=>validateFitJob(job)).toThrow('数量不合法')
 })
 it('enforces maxDepth independent of maxN',()=>{
  expect(()=>validateFitJob({try:1,points,maxN:12,maxDepth:1})).not.toThrow()
  expect(()=>validateFitJob({try:1,points,maxN:3,maxDepth:0})).toThrow()
 })
 it('requires covariance all-or-none and SPD',()=>{
  const cov={rr:2,ri:.5,ii:1}
  expect(()=>validateFitJob({try:1,points:points.map((p,i)=>i<3?{...p,cov}:p)})).toThrow('整组')
  expect(()=>validateFitJob({try:1,points:points.map(p=>({...p,cov:{rr:-1,ri:0,ii:1}}))})).toThrow('正定')
  expect(()=>validateFitJob({try:1,points:points.map(p=>({...p,cov}))})).not.toThrow()
 })
})
