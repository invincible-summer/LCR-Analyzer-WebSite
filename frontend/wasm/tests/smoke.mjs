// smoke.mjs — node smoke test for the built wasm module (exercises the same
// C ABI the browser worker uses).  Run: node tests/smoke.mjs
import createLcrModule from '../../src/wasm/lcr_wasm.js'
import { readFile } from 'node:fs/promises'

const wasmBinary = await readFile(new URL('../../src/wasm/lcr_wasm.wasm', import.meta.url))
// node has no fetch(file://) — inject the compiled instance directly; the
// browser worker instead uses locateFile + normal fetch.
const M = await createLcrModule({
  instantiateWasm: (info, success) => {
    WebAssembly.instantiate(wasmBinary, info).then(({ instance }) => success(instance))
  },
})

console.log('version:', M.UTF8ToString(M._lcr_version()))

// tiny complex helpers
const cx = (re, im) => ({ re, im })
const add = (a, b) => cx(a.re + b.re, a.im + b.im)
const mul = (a, b) => cx(a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re)
const div = (a, b) => {
  const d = b.re * b.re + b.im * b.im
  return cx((a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d)
}
const zR = (r) => cx(r, 0)
const zC = (c, w) => div(cx(1, 0), cx(0, w * c))
const zL = (l, dcr, w) => cx(dcr, w * l)

// ground truth: series R 50 + L 1mH (DCR 2) + C 1µF, 100 Hz .. 1 MHz
const n = 40
const f = []
for (let i = 0; i < n; i++) f.push(10 ** (2 + (4 * i) / (n - 1)))
const Zseries = f.map((fq) => {
  const w = 2 * Math.PI * fq
  return add(add(zR(50), zL(1e-3, 2, w)), zC(1e-6, w))
})

function mallocF64(arr) {
  const p = M._malloc(arr.length * 8)
  for (let i = 0; i < arr.length; i++) M.HEAPF64[(p >> 3) + i] = arr[i]
  return p
}
const takeJson = (ptr) => {
  const s = M.UTF8ToString(ptr)
  M._lcr_free(ptr)
  return JSON.parse(s)
}

// ---- try1 ----
{
  const pf = mallocF64(f)
  const pr = mallocF64(Zseries.map((z) => z.re))
  const pi = mallocF64(Zseries.map((z) => z.im))
  const t0 = performance.now()
  const r = takeJson(M._lcr_try1(pf, pr, pi, n, 0, 0, 5))
  const dt = (performance.now() - t0).toFixed(0)
  const best = r.candidates?.[0]
  console.log(`try1 ${dt}ms ok=${r.ok} classes=${r.stats?.n_classes} best wrmse=${best?.wrmse?.toExponential(2)} devices=${best?.devices}`)
  if (!r.ok || best.wrmse > 1e-6) { console.error('TRY1 FAIL', JSON.stringify(r).slice(0, 300)); process.exit(1) }
  M._free(pf); M._free(pr); M._free(pi)
}

// ---- try2 (known multiset) ----
{
  const pf = mallocF64(f)
  const pr = mallocF64(Zseries.map((z) => z.re))
  const pi = mallocF64(Zseries.map((z) => z.im))
  const kinds = M._malloc(3), vals = M._malloc(24), dcrs = M._malloc(24), counts = M._malloc(12)
  'RCL'.split('').forEach((k, i) => (M.HEAPU8[kinds + i] = k.charCodeAt(0)))
  ;[50, 1e-6, 1e-3].forEach((v, i) => (M.HEAPF64[(vals >> 3) + i] = v))
  ;[0, 0, 2].forEach((v, i) => (M.HEAPF64[(dcrs >> 3) + i] = v))
  ;[1, 1, 1].forEach((v, i) => (M.HEAP32[(counts >> 2) + i] = v))
  const t0 = performance.now()
  const r = takeJson(M._lcr_try2(pf, pr, pi, n, kinds, vals, dcrs, counts, 3, 5))
  const dt = (performance.now() - t0).toFixed(0)
  const best = r.candidates?.[0]
  console.log(`try2 ${dt}ms ok=${r.ok} best wrmse=${best?.wrmse?.toExponential(2)} stats=${JSON.stringify(r.stats)}`)
  if (!r.ok || best.wrmse > 1e-9) { console.error('TRY2 FAIL'); process.exit(1) }
  M._free(pf); M._free(pr); M._free(pi); M._free(kinds); M._free(vals); M._free(dcrs); M._free(counts)
}

// ---- try3 ladder: 0 -L- 2 -C- 1, 2 -R- 1 ----
{
  const Zladder = f.map((fq) => {
    const w = 2 * Math.PI * fq
    return add(zL(1e-3, 2, w), div(mul(zC(1e-6, w), zR(50)), add(zC(1e-6, w), zR(50))))
  })
  const pf = mallocF64(f)
  const pr = mallocF64(Zladder.map((z) => z.re))
  const pi = mallocF64(Zladder.map((z) => z.im))
  const us = M._malloc(12), vs = M._malloc(12), kk = M._malloc(3)
  ;[0, 1, 1].forEach((v, i) => (M.HEAP32[(us >> 2) + i] = v))
  ;[2, 2, 2].forEach((v, i) => (M.HEAP32[(vs >> 2) + i] = v))
  ;['L', 'C', 'R'].forEach((k, i) => (M.HEAPU8[kk + i] = k.charCodeAt(0)))
  const t0 = performance.now()
  const r = takeJson(M._lcr_try3(pf, pr, pi, n, us, vs, kk, 3))
  const dt = (performance.now() - t0).toFixed(0)
  const best = r.candidates?.[0]
  console.log(`try3 ${dt}ms ok=${r.ok} wrmse=${best?.wrmse?.toExponential(2)} groups=${r.try3?.groups?.length} jacRank=${r.try3?.jac_rank}`)
  if (!r.ok || best.wrmse > 1e-6) { console.error('TRY3 FAIL'); process.exit(1) }
  // error path: port 1 missing
  const r2 = takeJson(M._lcr_try3(pf, pr, pi, n, us, vs, kk, 2))
  console.log('try3 error path:', r2.ok === false && r2.code)
  M._free(pf); M._free(pr); M._free(pi); M._free(us); M._free(vs); M._free(kk)
}

console.log('WASM smoke OK')
