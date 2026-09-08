import type {
  CandidateDiagnostics,
  CompKind,
  FitCandidate,
  FitJob,
  FitResponse,
  SelectionInfo,
  Try3Group,
} from './fitTypes'
import { graphToNetlist } from './adjacency'

interface ReductionExpr {
  op: 'value' | 'dcr' | 'sum' | 'hsum'
  edge?: number
  children?: ReductionExpr[]
}

export interface NativeGroup {
  gid: number
  u: number
  v: number
  kind: CompKind
  value: number
  dcr: number
  members: number[]
  mode: string
  value_bounds: [number, number] | null
  dcr_bounds: [number, number] | null
  parameter_ids: number[]
  value_expr: ReductionExpr
  dcr_expr: ReductionExpr | null
}

export interface NativeCandidate extends FitCandidate {
  diagnostics: CandidateDiagnostics
  groups: NativeGroup[]
  dropped: number[]
  selection: SelectionInfo
}
export interface NativeReport {
  schema: string
  schema_revision: number
  engine_version: string
  try: 1 | 2 | 3
  elapsed: number
  mode: string
  termination: string
  enumeration_complete: boolean
  continuous_global_certified: boolean
  hypothesis_family: string
  noise_model: string
  equivalence_metric: string
  equivalence_threshold: number
  selection: { criterion: string; qualified: boolean }
  stats: { generated: number; structures: number; evaluated: number; numerical_failures: number }
  candidates: NativeCandidate[]
}

/** Human-readable aggregate summaries, e.g. "R1+R2" / "DCR1+DCR2+R3". */
function summarizeExpr(
  e: ReductionExpr,
  kindOf: (edge: number) => CompKind,
): string {
  if (e.op === 'value') return `${kindOf(e.edge ?? 0)}${(e.edge ?? 0) + 1}`
  if (e.op === 'dcr') return `DCR${(e.edge ?? 0) + 1}`
  const sep = e.op === 'sum' ? '+' : '||'
  return (e.children ?? []).map((c) => summarizeExpr(c, kindOf)).join(sep)
}

function paramLabel(p: { kind: CompKind; edge: number; quantity: 'value' | 'dcr' }): string {
  return p.quantity === 'dcr' ? `DCR${p.edge + 1}` : `${p.kind}${p.edge + 1}`
}

export function adaptReport(r: NativeReport, job: FitJob): FitResponse {
  if (r.schema !== 'lcr.native.v4' || r.try !== job.try)
    throw new Error('算法响应版本不匹配')
  const candidates = r.candidates.map((c) => ({
    ...c,
    sp: graphToNetlist(c.adjacency) !== null,
    structure: c.topology,
  }))
  const base = {
    ok: true as const,
    elapsed: r.elapsed,
    candidates,
    search: {
      mode: r.mode,
      termination: r.termination,
      complete: r.enumeration_complete,
      certified: r.continuous_global_certified,
      family: r.hypothesis_family,
      failures: r.stats.numerical_failures,
      equivalence_metric: r.equivalence_metric,
      equivalence_threshold: r.equivalence_threshold,
      selection: r.selection,
    },
  }
  if (job.try === 1)
    return {
      ...base,
      try: 1,
      stats: {
        generated: r.stats.generated,
        structures: r.stats.structures,
        evaluated: r.stats.evaluated,
        classes: candidates.length,
      },
    }
  if (job.try === 2)
    return {
      ...base,
      try: 2,
      stats: {
        generated: r.stats.generated,
        structures: r.stats.structures,
        evaluated: r.stats.evaluated,
        classes: candidates.length,
        components: job.components.reduce((n, c) => n + c.count, 0),
        refined: candidates.filter((c) => c.refined).length,
        elapsed_engine: r.elapsed,
      },
    }
  const c = r.candidates[0]
  // Group diagnostics come from explicit parameter descriptors keyed by the
  // group's parameter_ids — never from inferred parameter ordering.
  const kindOf = (edge: number): CompKind => job.edges[edge]?.kind ?? 'R'
  const paramsById = new Map((c?.diagnostics.parameters ?? []).map((p) => [p.id, p]))
  const groups: Try3Group[] = (c?.groups ?? []).map((g) => {
    const weak: string[] = []
    const atBound: string[] = []
    const fixed: string[] = []
    for (const id of g.parameter_ids) {
      const p = paramsById.get(id)
      if (!p) continue
      if (p.fixed) fixed.push(paramLabel(p))
      else {
        if (p.weak) weak.push(paramLabel(p))
        if (p.at_bound) atBound.push(paramLabel(p))
      }
    }
    return {
      gid: g.gid,
      kind: g.kind,
      u: g.u,
      v: g.v,
      members: g.members,
      mode: g.mode,
      value: { v1: g.value, v2: g.dcr },
      value_bounds: g.value_bounds
        ? { lo: g.value_bounds[0], hi: g.value_bounds[1] }
        : null,
      dcr_bounds: g.dcr_bounds ? { lo: g.dcr_bounds[0], hi: g.dcr_bounds[1] } : null,
      parameter_ids: g.parameter_ids,
      expression: {
        value: summarizeExpr(g.value_expr, kindOf),
        dcr: g.dcr_expr ? summarizeExpr(g.dcr_expr, kindOf) : null,
      },
      weak,
      at_bound: atBound,
      fixed,
    }
  })
  const stats = { n_groups: groups.length, n_starts_used: c?.diagnostics.starts ?? 0, seconds: r.elapsed }
  return {
    ...base,
    try: 3,
    stats,
    try3: {
      ...stats,
      ok: !!c,
      jac_rank: c?.diagnostics.rank ?? 0,
      jac_cond: c?.diagnostics.condition ?? null,
      n_passes: c?.diagnostics.converged_starts ?? 0,
      groups,
      edges: job.edges.map((e, index) => {
        const g = groups.find((g) => g.members.includes(index))
        return {
          index,
          kind: e.kind,
          status: g
            ? g.members.length > 1
              ? 'merged'
              : 'fitted'
            : c
              ? 'dropped'
              : 'unfitted',
          group: g?.gid ?? -1,
          note: g ? '' : c ? '端口不可见' : '没有可用拟合结果',
        }
      }),
      notes: c
        ? [c.diagnostics.verdict, c.diagnostics.optimizer]
        : ['未找到数值可靠的候选'],
    },
  }
}
