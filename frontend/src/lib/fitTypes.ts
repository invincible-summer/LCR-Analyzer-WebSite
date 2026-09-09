// Browser v4 JSON contract adapted from the shared native report (rev 2).

export type CompKind = 'R' | 'L' | 'C'

/** Per-point 2x2 Cartesian covariance [rr ri; ri ii] of (Re Z, Im Z). */
export interface Cov2 {
  rr: number
  ri: number
  ii: number
  source?: 'csv' | 'scan_polar_approx' | 'instrument'
}

/** One measurement point: Z = re + j·im at frequency f [Hz]. */
export interface ZPoint {
  f: number
  re: number
  im: number
  /** Optional measurement covariance; a job is all-or-none. */
  cov?: Cov2
}

/** Edge of the unified upper-triangle adjacency matrix (OUTPUT_FORMAT.md §1). */
export interface AdjEdge {
  t: CompKind
  /** R[Ω] / L[H] / C[F] */
  p: number
  /** series DC resistance of an inductor (L only, otherwise 0) */
  d: number
}

export interface AdjSlot {
  u: number
  j: number
  edges: AdjEdge[]
}

export interface Adjacency {
  v: number
  slots: AdjSlot[]
}

export interface TheoryCurve {
  f: number[]
  re: number[]
  im: number[]
}

/** Explicit optimizer parameter descriptor (native report rev 2). */
export interface ParameterDiagnostic {
  id: number
  edge: number
  quantity: 'value' | 'dcr'
  kind: CompKind
  value: number
  lower: number
  upper: number
  free: boolean
  fixed: boolean
  weak: boolean
  at_bound: boolean
  standard_error: number | null
  ci95: [number, number] | null
}

/**
 * Candidate selection tier. criterion: 'AICc' = qualified (calibrated delta
 * within the qualified set), 'AICc_PROVISIONAL' = unconverged regular
 * candidate (joins the AICc-scored ordering with its current score but is
 * never a calibrated qualification; delta stays null), 'NONE' =
 * diagnostic-only (see reasons). Run-level criterion may additionally be
 * 'AICc_PROVISIONAL_ORDER' (rank-1 is provisional) or
 * 'RSS_DIAGNOSTIC_FALLBACK' (robust runs / no scored candidate: diagnostic
 * ordering, not calibrated robust model selection).
 */
export interface SelectionInfo {
  eligible: boolean
  criterion: string
  score: number | null
  delta: number | null
  reasons: string[]
}

export interface CandidateDiagnostics {
  verdict: string
  optimizer: string
  numerical_status: string
  identifiability_status: string
  fit_objective: number | null
  rank: number
  condition: number | null
  singular_values: number[]
  standard_errors: number[]
  approximate_ci95: [number, number][]
  weak: number[]
  at_bound: number[]
  starts: number
  best_start: number
  converged_starts: number
  agreeing_starts: number
  robust_used: boolean
  outlier_count: number
  worst_backward_error: number
  worst_rcond: number
  parameters: ParameterDiagnostic[]
}

export interface FitCandidate {
  rank: number
  devices: number
  n_params: number
  wrmse: number
  max_rel: number
  aicc: number | null
  rss: number
  engine?: string // SP enumeration / rational auxiliary
  sp?: boolean // try2: series-parallel wiring?
  /** try2: values refined inside explicit tolerance bounds */
  refined?: boolean
  n_members?: number
  topology?: string // try1 canonical string / try2 structure key
  structure?: string
  original_topology_key?: string
  effective_topology_key?: string
  effective_devices?: number
  adjacency: Adjacency
  theory: TheoryCurve
  diagnostics?: CandidateDiagnostics
  selection?: SelectionInfo
}

// ---- job requests -----------------------------------------------------------

export interface SearchOptions {
  mode?: 'Strict' | 'Fast'
  budget?: number
  seconds?: number
  robust?: boolean
  /** equivalence-class merge tolerance over the observed frequency grid */
  equivalenceTolerance?: number
  /** multi-start / LM budget advanced controls */
  starts?: number
  iterations?: number
  seed?: number
}
export interface Try1Job extends SearchOptions {
  try: 1
  points: ZPoint[]
  /** exact normalized equivalent-model device count prior (undefined = free search) */
  exactN?: number
  maxN?: number
  /** SP nesting depth; independent of maxN (native default 4) */
  maxDepth?: number
  topK?: number
}

export interface ComponentSpec {
  kind: CompKind
  value: number
  /** L only: series DC resistance [Ω] */
  dcr: number
  count: number
}

export interface Try2Job extends SearchOptions {
  tolerance?: number
  dcrTolerance?: number
  try: 2
  points: ZPoint[]
  components: ComponentSpec[]
  topK?: number
}

export interface TopoEdge {
  u: number
  v: number
  kind: CompKind
}

export interface Try3Job extends SearchOptions {
  try: 3
  points: ZPoint[]
  edges: TopoEdge[]
}

export type FitJob = Try1Job | Try2Job | Try3Job

// ---- responses --------------------------------------------------------------

export interface Try1Stats {
  generated: number
  structures: number
  evaluated: number
  classes: number
}

export interface Try2Stats {
  generated: number
  structures: number
  evaluated: number
  classes: number
  components: number
  refined: number
  elapsed_engine: number
}

export interface Try3Group {
  gid: number
  kind: CompKind
  u: number
  v: number
  members: number[]
  mode: string // 'single' | 'par' | 'ser'
  value: { v1: number; v2: number }
  value_bounds: { lo: number; hi: number } | null
  dcr_bounds: { lo: number; hi: number } | null
  parameter_ids: number[]
  /** human-readable aggregate expression summaries, e.g. "R1+R2" */
  expression: { value: string; dcr: string | null }
  weak: string[]
  at_bound: string[]
  fixed: string[]
}

export interface Try3EdgeReport {
  index: number
  kind: CompKind
  status: string // 'fitted' | 'merged' | 'dropped'
  group: number
  note: string
}

export interface Try3Stats {
  n_groups: number
  n_starts_used: number
  seconds: number
}

/** try3 diagnostics — the glue nests these under `try3`, not `stats`. */
export interface Try3Diagnostics extends Try3Stats {
  ok: boolean
  jac_rank: number
  jac_cond: number | null
  n_passes: number
  groups: Try3Group[]
  edges: Try3EdgeReport[]
  notes: string[]
}

export type FitErrorCode = 'bad_input' | 'port_open' | 'internal'

export interface FitOkBase {
  ok: true
  try: 1 | 2 | 3
  elapsed: number
  search: {
    mode: string
    termination: string
    complete: boolean
    certified: boolean
    family: string
    failures: number
    equivalence_metric: string
    equivalence_threshold: number
    selection: { criterion: string; qualified: boolean }
  }
  candidates: FitCandidate[]
}

export type FitResponse =
  | (FitOkBase & { try: 1; stats: Try1Stats })
  | (FitOkBase & { try: 2; stats: Try2Stats })
  | (FitOkBase & { try: 3; stats: Try3Stats; try3: Try3Diagnostics })
  | { ok: false; code: FitErrorCode; error: string }

export function isFitOk(r: FitResponse): r is Extract<FitResponse, { ok: true }> {
  return r.ok === true
}

/** 中文错误映射（code → 友好文案），原始 detail 附后。 */
export function fitErrorText(r: Extract<FitResponse, { ok: false }>): string {
  const head: Record<FitErrorCode, string> = {
    bad_input: '输入不合法',
    port_open: '端口开路：拓扑在 0–1 端口间不导通',
    internal: '引擎内部错误',
  }
  return `${head[r.code]}：${r.error}`
}
