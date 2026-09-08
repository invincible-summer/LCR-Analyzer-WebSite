// real4_wasm.mjs — run the four real datasets through the WASM module (the
// exact code path the browser worker uses) and print the top candidates.
// usage: node tests/real4_wasm.mjs
import createLcrModule from '../../src/wasm/lcr_wasm.js'
import { readFile } from 'node:fs/promises'

const wasmBinary = await readFile(new URL('../../src/wasm/lcr_wasm.wasm', import.meta.url))
const M = await createLcrModule({
  instantiateWasm: (info, success) => {
    WebAssembly.instantiate(wasmBinary, info).then(({ instance }) => success(instance))
  },
})

function mallocF64(arr) {
  const p = M._malloc(arr.length * 8)
  for (let i = 0; i < arr.length; i++) M.HEAPF64[(p >> 3) + i] = arr[i]
  return p
}
function mallocI32(arr) {
  const p = M._malloc(arr.length * 4)
  for (let i = 0; i < arr.length; i++) M.HEAP32[(p >> 2) + i] = arr[i]
  return p
}
function mallocChars(str) {
  const p = M._malloc(str.length + 1)
  for (let i = 0; i < str.length; i++) M.HEAPU8[p + i] = str.charCodeAt(i)
  M.HEAPU8[p + str.length] = 0
  return p
}
const takeJson = (ptr) => {
  const s = M.UTF8ToString(ptr)
  M._lcr_free(ptr)
  return JSON.parse(s)
}

async function loadCsv(name) {
  const txt = await readFile(new URL('../../../examples/' + name, import.meta.url), 'utf8')
  const f = [], re = [], im = []
  for (const line of txt.split('\n')) {
    const l = line.trim()
    if (!l || l.startsWith('#')) continue
    const p = l.split(',')
    f.push(parseFloat(p[0])); re.push(parseFloat(p[1])); im.push(parseFloat(p[2]))
  }
  return { f, re, im, n: f.length }
}

const CASES = {
  1: { rows: [['R', 9.93e3, 0], ['C', 1.027e-7, 0]], edges: [[0, 1, 'R'], [0, 1, 'C']] },
  2: { rows: [['L', 0.1, 345], ['C', 1e-6, 0]], edges: [[0, 1, 'L'], [0, 1, 'C']] },
  3: { rows: [['L', 0.1, 345], ['C', 1e-7, 0], ['C', 1e-6, 0]], edges: [[0, 2, 'L'], [0, 2, 'C'], [2, 1, 'C']] },
  4: { rows: [['R', 200, 0], ['R', 50, 0], ['L', 820e-6, 13], ['C', 1e-6, 0]], edges: [[0, 2, 'R'], [0, 2, 'L'], [0, 2, 'C'], [2, 1, 'R']] },
}

for (const i of [1, 2, 3, 4]) {
  const { f, re, im, n } = await loadCsv(`data${i}.csv`)
  const pf = mallocF64(f), pr = mallocF64(re), pi = mallocF64(im)
  console.log(`--- data${i} (n=${n}) ---`)

  // try1
  {
    const t0 = performance.now()
    const r = takeJson(M._lcr_try1(pf, pr, pi, n, 0, 0, 6))
    const dt = (performance.now() - t0).toFixed(0)
    if (!r.ok) console.log(`  try1 ERROR ${r.code} ${r.error || ''}`)
    else {
      for (const c of r.candidates.slice(0, 4)) {
        console.log(`  try1 #${c.rank} dev=${c.devices} p=${c.n_params} wRMSE=${(c.wrmse * 100).toFixed(3)}% ${c.topology || ''}`)
      }
      console.log(`  try1 ${dt}ms`)
    }
  }
  // try2
  {
    const rows = CASES[i].rows
    const kinds = mallocChars(rows.map(r0 => r0[0]).join(''))
    const vals = mallocF64(rows.map(r0 => r0[1]))
    const dcrs = mallocF64(rows.map(r0 => r0[2]))
    const counts = mallocI32(rows.map(() => 1))
    const t0 = performance.now()
    const r = takeJson(M._lcr_try2(pf, pr, pi, n, kinds, vals, dcrs, counts, rows.length, 5))
    const dt = (performance.now() - t0).toFixed(0)
    if (!r.ok) console.log(`  try2 ERROR ${r.code} ${r.error || ''}`)
    else {
      for (const c of r.candidates.slice(0, 3)) {
        console.log(`  try2 #${c.rank} wRMSE=${(c.wrmse * 100).toFixed(3)}% refined=${c.refined} ${c.topology || ''}`)
      }
      console.log(`  try2 ${dt}ms`)
    }
    M._free(kinds); M._free(vals); M._free(dcrs); M._free(counts)
  }
  // try3
  {
    const ed = CASES[i].edges
    const us = mallocI32(ed.map(e => e[0]))
    const vs = mallocI32(ed.map(e => e[1]))
    const kk = mallocChars(ed.map(e => e[2]).join(''))
    const t0 = performance.now()
    const r = takeJson(M._lcr_try3(pf, pr, pi, n, us, vs, kk, ed.length))
    const dt = (performance.now() - t0).toFixed(0)
    if (!r.ok) console.log(`  try3 ERROR ${r.code} ${r.error || ''}`)
    else {
      const c = r.candidates[0]
      console.log(`  try3 wRMSE=${(c.wrmse * 100).toFixed(3)}% ${dt}ms diag=${JSON.stringify(r.try3 || {}).slice(0, 100)}`)
    }
    M._free(us); M._free(vs); M._free(kk)
  }
  M._free(pf); M._free(pr); M._free(pi)
}
