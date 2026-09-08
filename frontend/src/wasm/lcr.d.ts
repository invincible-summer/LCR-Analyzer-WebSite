export interface LcrModule {
  HEAPF64: Float64Array
  HEAP32: Int32Array
  _malloc(size: number): number
  _free(ptr: number): void
  _lcr_free(ptr: number): void
  _lcr_version(): number
  _lcr_configure(fast: number, budget: number, seconds: number, tolerance: number, dcr: number, robust: number): void
  _lcr_try1(...args: number[]): number
  _lcr_try2(...args: number[]): number
  _lcr_try3(...args: number[]): number
  UTF8ToString(ptr: number): string
}
export default function createLcr(options?: { locateFile?: (path: string) => string; wasmBinary?: Uint8Array }): Promise<LcrModule>
