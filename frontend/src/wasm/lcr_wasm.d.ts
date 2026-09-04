// Type declaration for the Emscripten ES6 module built by wasm/build.sh.
// TS resolves the `../wasm/lcr_wasm.js` import in workers/fitWorker.ts to
// this .d.ts (bundler resolution maps .js → .d.ts).
export interface LcrWasmModule {
  _lcr_version: () => number
  _lcr_try1: (
    f: number, re: number, im: number, n: number,
    exactN: number, maxN: number, topK: number,
  ) => number
  _lcr_try2: (
    f: number, re: number, im: number, n: number,
    kinds: number, values: number, dcrs: number, counts: number, rows: number,
    topK: number,
  ) => number
  _lcr_try3: (
    f: number, re: number, im: number, n: number,
    us: number, vs: number, kinds: number, m: number,
  ) => number
  _lcr_free: (p: number) => void
  _malloc: (n: number) => number
  _free: (p: number) => void
  HEAPF64: Float64Array
  HEAP32: Int32Array
  HEAPU8: Uint8Array
  UTF8ToString: (p: number) => string
}

declare const createLcrModule: (opts?: Record<string, unknown>) => Promise<LcrWasmModule>
export default createLcrModule
