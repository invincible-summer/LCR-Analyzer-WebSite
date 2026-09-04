// fitTypes.ts — TypeScript mirror of the wasm glue JSON contract
// (frontend/wasm/src/*_glue.cpp).  Keep both sides in sync; DESIGN.md is the
// authoritative description.

export type CompKind = 'R' | 'L' | 'C'

/** One measurement point: Z = re + j·im at frequency f [Hz]. */
export interface ZPoint {
  f: number
  re: number
  im: number
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

export interface FitCandidate {
  rank: number
  devices: number
  n_params: number
  wrmse: number
  max_rel: number
  aicc: number
  rss: number
  engine?: string // try1: 'A' | 'B'
  sp?: boolean // try2: series-parallel wiring?
  n_members?: number
  topology?: string // try1 canonical string / try2 structure key
  structure?: string
  adjacency: Adjacency
  theory: TheoryCurve
}

// ---- job requests -----------------------------------------------------------

export interface Try1Job {
  try: 1
  points: ZPoint[]
  /** exact device count prior (undefined = free search) */
  exactN?: number
  maxN?: number
  topK?: number
}

export interface ComponentSpec {
  kind: CompKind
  value: number
  /** L only: series DC resistance [Ω] */
  dcr: number
  count: number
}

export interface Try2Job {
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

export interface Try3Job {
  try: 3
  points: ZPoint[]
  edges: TopoEdge[]
}

export type FitJob = Try1Job | Try2Job | Try3Job

// ---- responses --------------------------------------------------------------

export interface Try1Stats {
  n_library: number
  n_pruned_kept: number
  n_classes: number
}

export interface Try2Stats {
  n_candidates: number
  n_structures: number
  n_funnel_kept: number
  n_components: number
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
  weak: string[]
  at_bound: string[]
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
  jac_cond: number
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
