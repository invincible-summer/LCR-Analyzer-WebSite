import type { FitCandidate, FitJob, FitResponse, Try3Group } from './fitTypes'
import { graphToNetlist } from './adjacency'

export interface NativeCandidate extends FitCandidate {
  diagnostics: { verdict: string; optimizer: string; rank: number; condition: number | null; starts: number; converged_starts: number; weak: number[]; at_bound: number[] }
  groups: { members: number[]; mode: string; u: number; v: number; kind: 'R'|'L'|'C'; value: number; dcr: number }[]
  dropped: number[]
}
export interface NativeReport {
  schema: string; try: 1 | 2 | 3; elapsed: number; mode: string; termination: string
  enumeration_complete: boolean; continuous_global_certified: boolean; hypothesis_family: string
  stats: { generated: number; structures: number; evaluated: number; numerical_failures: number }
  candidates: NativeCandidate[]
}
export function adaptReport(r: NativeReport, job: FitJob): FitResponse {
  if (r.schema !== 'lcr.native.v4' || r.try !== job.try) throw new Error('算法响应版本不匹配')
  const candidates = r.candidates.map(c => ({ ...c, sp: graphToNetlist(c.adjacency) !== null, structure: c.topology }))
  const base = { ok: true as const, elapsed: r.elapsed, candidates,
    search: { mode: r.mode, termination: r.termination, complete: r.enumeration_complete,
      certified: r.continuous_global_certified, family: r.hypothesis_family, failures: r.stats.numerical_failures } }
  if (job.try === 1) return { ...base, try: 1, stats: { n_library: r.stats.generated, n_pruned_kept: r.stats.evaluated, n_classes: candidates.length } }
  if (job.try === 2) return { ...base, try: 2, stats: { n_candidates: r.stats.generated, n_structures: r.stats.structures, n_funnel_kept: r.stats.evaluated, n_components: job.components.reduce((n,c)=>n+c.count,0), n_refined: candidates.filter(c=>c.refined).length, elapsed_engine: r.elapsed } }
  const c = r.candidates[0]
  let parameter = 0
  const groups: Try3Group[] = (c?.groups ?? []).map((g,gid)=>{
    const e = { t:g.kind,p:g.value,d:g.dcr }; const s={u:g.u,j:g.v}
    const indices = e.t === 'L' ? [parameter++, parameter++] : [parameter++]
    return { gid, kind:e.t,u:s.u,v:s.j,members:c.groups[gid]?.members??[],mode:c.groups[gid]?.mode??'single',
      value:{v1:e.p,v2:e.d},weak:indices.filter(i=>c.diagnostics.weak.includes(i)).map(String),at_bound:indices.filter(i=>c.diagnostics.at_bound.includes(i)).map(String) }
  })
  const stats = { n_groups: groups.length, n_starts_used:c?.diagnostics.starts??0,seconds:r.elapsed }
  return { ...base, try:3,stats,try3:{...stats,ok:!!c,jac_rank:c?.diagnostics.rank??0,jac_cond:c?.diagnostics.condition??null,n_passes:c?.diagnostics.converged_starts??0,groups,
    edges:job.edges.map((e,index)=>{const g=groups.find(g=>g.members.includes(index));return {index,kind:e.kind,status:g?(g.members.length>1?'merged':'fitted'):(c?'dropped':'unfitted'),group:g?.gid??-1,note:g?'':c?'端口不可见':'没有可用拟合结果'} }),
    notes:c?[c.diagnostics.verdict,c.diagnostics.optimizer]:['未找到数值可靠的候选'] } }
}
