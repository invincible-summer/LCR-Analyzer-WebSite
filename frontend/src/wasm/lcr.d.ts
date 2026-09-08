export interface LcrModule {
  HEAPF64: Float64Array
  HEAP32: Int32Array
  _malloc(size: number): number
  _free(ptr: number): void
  _lcr_free(ptr: number): void
  _lcr_version(): number
  _lcr_configure(fast: number, budget: number, seconds: number, tolerance: number, dcr: number, robust: number): void
  _lcr_configure_search(fast: number, budget: number, seconds: number, starts: number, iterations: number, seed: number, equivalenceTolerance: number): void
  _lcr_configure_bounds(rMin: number, rMax: number, lMin: number, lMax: number, cMin: number, cMax: number, dcrMax: number, relativeFloor: number): void
  _lcr_set_covariance(rr: number, ri: number, ii: number, n: number): void
  _lcr_try1(f: number, re: number, im: number, n: number, exactN: number, maxN: number, maxDepth: number, topK: number): number
  _lcr_try2(f: number, re: number, im: number, n: number, kinds: number, values: number, dcrs: number, counts: number, rows: number, topK: number): number
  _lcr_try3(f: number, re: number, im: number, n: number, us: number, vs: number, kinds: number, m: number): number
  UTF8ToString(ptr: number): string
}
export default function createLcr(options?: { locateFile?: (path: string) => string; wasmBinary?: Uint8Array }): Promise<LcrModule>
