#!/usr/bin/env node
// Native <-> WASM parity gate: runs the same small cases through the native
// CLI (--json) and the committed WASM artifact and compares the report
// contract. Guards against "C++ sources changed but the committed wasm
// binary is stale".
//
// Usage: node parity.mjs [path-to-native-lcr-binary]
//   or:  LCR_NATIVE_BIN=... node parity.mjs
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import { execFileSync } from 'node:child_process'
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import createLcr from '../../frontend/src/wasm/lcr.js'

const here = dirname(fileURLToPath(import.meta.url))
const root = join(here, '../..')
const wasmDir = join(root, 'frontend/src/wasm')
const native =
  process.argv[2] ||
  process.env.LCR_NATIVE_BIN ||
  '/tmp/lcr-v4-build/lcr'

let checks = 0
function ok(cond, what) {
  ++checks
  if (!cond) throw new Error('parity failed: ' + what)
}
const near = (a, b, rtol = 1e-6, atol = 1e-9) =>
  Math.abs(a - b) <= atol + rtol * Math.max(Math.abs(a), Math.abs(b))

const N = 32
const fs = Array.from({ length: N }, (_, i) => 10 * Math.pow(1e5, i / (N - 1)))
const w = f => 2 * Math.PI * f
// Noise-free truths: constant R, parallel R||C, series R+L(DCR), 2x series R.
const rData = fs.map(f => [f, 1200, 0])
const rcData = fs.map(f => {
  const g = 1 / 1200, b = w(f) * 2e-7, d = g * g + b * b
  return [f, g / d, -b / d]
})
const rlData = fs.map(f => [f, 49, w(f) * 1e-3])
const rrData = fs.map(f => [f, 1.2e7, 0])

const tmp = mkdtempSync(join(tmpdir(), 'lcr-parity-'))
const dataFile = join(tmp, 'measurements.txt')
function writeData(rows) {
  writeFileSync(dataFile, rows.length + '\n' + rows.map(r => r.join(' ')).join('\n') + '\n')
}
function nativeJson(args, rows) {
  writeData(rows)
  const out = execFileSync(native, [...args, '--measurements', dataFile, '--json'], {
    encoding: 'utf8',
    maxBuffer: 1 << 28,
  })
  return JSON.parse(out)
}

const m = await createLcr({ locateFile: p => join(wasmDir, p) })
const f64 = v => { const p = m._malloc(v.length * 8); m.HEAPF64.set(v, p / 8); return p }
const i32 = v => { const p = m._malloc(v.length * 4); m.HEAP32.set(v, p / 4); return p }
function wasmRun(entry, rows, extra) {
  const ptrs = [f64(rows.map(r => r[0])), f64(rows.map(r => r[1])), f64(rows.map(r => r[2]))]
  const ptr = entry(...ptrs, rows.length, ...extra)
  const text = m.UTF8ToString(ptr)
  m._lcr_free(ptr)
  for (const p of ptrs) m._free(p)
  for (const p of extra.filter(x => typeof x !== 'number')) m._free(p)
  return JSON.parse(text)
}

function compare(a, b, what) {
  ok(a.try === b.try, what + ': try')
  ok(a.selection.criterion === b.selection.criterion, what + ': selection criterion')
  ok(a.selection.qualified === b.selection.qualified, what + ': selection qualified')
  ok(a.noise_model === b.noise_model, what + ': noise model')
  ok(a.stats.generated === b.stats.generated, what + ': generated count')
  ok(a.stats.structures === b.stats.structures, what + ': structures count')
  ok(a.candidates.length === b.candidates.length, what + ': candidate/class count')
  for (let i = 0; i < a.candidates.length; ++i) {
    const x = a.candidates[i], y = b.candidates[i]
    ok(x.topology === y.topology, `${what}: cand${i} topology`)
    ok(x.n_params === y.n_params, `${what}: cand${i} n_params`)
    // Noise-floor fits (wrmse < 1e-8) sit on the last-bit optimum: platform
    // libm differences survive logarithmic amplification in AICc, so those
    // candidates only need to agree on "essentially zero residual".
    const floor = Math.max(x.wrmse, y.wrmse) < 1e-8
    if (floor)
      ok(Math.abs(x.wrmse - y.wrmse) < 1e-6 && Math.abs(x.rss - y.rss) < 1e-9,
         `${what}: cand${i} noise-floor metrics`)
    else {
      ok(near(x.wrmse, y.wrmse), `${what}: cand${i} wrmse`)
      ok(near(x.rss, y.rss), `${what}: cand${i} rss`)
    }
    if (x.aicc !== null)
      ok(y.aicc !== null && (floor ? Number.isFinite(y.aicc) : near(x.aicc, y.aicc, 1e-4)),
         `${what}: cand${i} aicc`)
    else ok(y.aicc === null, `${what}: cand${i} aicc null`)
    ok(x.selection.eligible === y.selection.eligible, `${what}: cand${i} eligibility`)
    ok(JSON.stringify(x.selection.reasons) === JSON.stringify(y.selection.reasons),
       `${what}: cand${i} reasons`)
    // Adjacency structure must match exactly; fitted edge values inside it
    // are covered by the parameter comparison below with explicit tolerance.
    const adjKey = adj =>
      JSON.stringify({
        v: adj.v,
        slots: adj.slots.map(s => ({ u: s.u, j: s.j, edges: s.edges.map(e => e.t) })),
      })
    ok(adjKey(x.adjacency) === adjKey(y.adjacency), `${what}: cand${i} adjacency`)
    for (let g = 0; g < x.groups.length; ++g) {
      ok(JSON.stringify(x.groups[g].value_expr) === JSON.stringify(y.groups[g].value_expr),
         `${what}: cand${i} group${g} value_expr`)
      ok(JSON.stringify(x.groups[g].value_bounds) === JSON.stringify(y.groups[g].value_bounds),
         `${what}: cand${i} group${g} value_bounds`)
      ok(JSON.stringify(x.groups[g].members) === JSON.stringify(y.groups[g].members),
         `${what}: cand${i} group${g} members`)
    }
    for (let p = 0; p < x.diagnostics.parameters.length; ++p) {
      const xp = x.diagnostics.parameters[p], yp = y.diagnostics.parameters[p]
      ok(xp.id === yp.id && xp.quantity === yp.quantity && xp.free === yp.free &&
         xp.fixed === yp.fixed, `${what}: cand${i} param${p} descriptor`)
      ok(near(xp.value, yp.value, 1e-5), `${what}: cand${i} param${p} value`)
    }
  }
}

// Case 1: Try1 on a single R, exact device count.
m._lcr_configure(0, 0, 0, 0, 0, 0)
compare(
  nativeJson(['try1', '--exact-n', '1'], rData),
  wasmRun(m._lcr_try1, rData, [1, 0, 0, 8]),
  'try1-single-R',
)

// Case 2: Try1 on parallel R||C data (selection across mixed candidates).
m._lcr_configure(0, 0, 0, 0, 0, 0)
compare(
  nativeJson(['try1', '--exact-n', '2'], rcData),
  wasmRun(m._lcr_try1, rcData, [2, 0, 0, 8]),
  'try1-RC',
)

// Case 3: Try2 pure R Exact with certification.
m._lcr_configure(0, 0, 0, 0, 0, 0)
writeFileSync(join(tmp, 'comp.txt'), 'R 470\n')
compare(
  nativeJson(['try2', '--components', join(tmp, 'comp.txt')], fs.map(f => [f, 470, 0])),
  wasmRun(m._lcr_try2, fs.map(f => [f, 470, 0]), [i32([82]), f64([470]), f64([0]), i32([1]), 1, 8]),
  'try2-exact-R',
)

// Case 4: Try3 two series R -> aggregate group beyond single-device rMax.
m._lcr_configure(0, 0, 0, 0, 0, 0)
writeFileSync(join(tmp, 'topo.txt'), '3\n0 1\n1\nR\nR\n')
compare(
  nativeJson(['try3', '--topology', join(tmp, 'topo.txt')], rrData),
  wasmRun(m._lcr_try3, rrData, [i32([0, 2]), i32([2, 1]), i32([82, 82]), 2]),
  'try3-series-R',
)

// Case 5: Try3 series R + L(DCR) absorption.
m._lcr_configure(0, 0, 0, 0, 0, 0)
writeFileSync(join(tmp, 'topo.txt'), '3\n0 1\n1\nR\nL\n')
compare(
  nativeJson(['try3', '--topology', join(tmp, 'topo.txt')], rlData),
  wasmRun(m._lcr_try3, rlData, [i32([0, 2]), i32([2, 1]), i32([82, 76]), 2]),
  'try3-RL',
)

rmSync(tmp, { recursive: true, force: true })
console.log(checks + ' checks passed')
