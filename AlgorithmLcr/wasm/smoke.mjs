#!/usr/bin/env node
// WASM smoke gate: exercises the committed frontend/src/wasm/lcr.{js,wasm}
// artifacts through the C ABI the worker actually uses. Run via
// `pnpm test:wasm` (frontend/package.json) or directly.
import { fileURLToPath } from 'node:url'
import { dirname, join } from 'node:path'
import createLcr from '../../frontend/src/wasm/lcr.js'

const here = dirname(fileURLToPath(import.meta.url))
const wasmDir = join(here, '../../frontend/src/wasm')

let checks = 0
function ok(cond, what) {
  ++checks
  if (!cond) throw new Error('smoke failed: ' + what)
}

const m = await createLcr({ locateFile: p => join(wasmDir, p) })

const f64 = values => {
  const p = m._malloc(values.length * 8)
  if (!p) throw new Error('out of memory')
  m.HEAPF64.set(values, p / 8)
  return p
}
const i32 = values => {
  const p = m._malloc(values.length * 4)
  if (!p) throw new Error('out of memory')
  m.HEAP32.set(values, p / 4)
  return p
}
function call(entry, buffers, args) {
  const ptr = m[entry](...buffers, ...args)
  const text = m.UTF8ToString(ptr)
  m._lcr_free(ptr)
  for (const b of buffers) m._free(b)
  return JSON.parse(text)
}

const N = 32
const fs = Array.from({ length: N }, (_, i) => 10 * Math.pow(1e5, i / (N - 1)))
const sample = fn => ({
  f: fs,
  re: fs.map(fn).map(z => z.re),
  im: fs.map(fn).map(z => z.im),
})

// 1 & 9: module version and report schema revision.
ok(
  m.UTF8ToString(m._lcr_version()) === 'lcr.native.v4 revision 2 / wasm 4.1.0',
  'version string',
)

// 2: Try1 on noise-free single R returns a finite well-fit candidate.
{
  const d = sample(() => ({ re: 1200, im: 0 }))
  m._lcr_configure(0, 0, 0, 0, 0, 0)
  const r = call('_lcr_try1', [f64(d.f), f64(d.re), f64(d.im), N], [1, 0, 0, 8])
  ok(r.schema === 'lcr.native.v4', 'schema string')
  ok(r.schema_revision === 2, 'schema revision 2')
  ok(r.candidates.length >= 1 && r.candidates[0].wrmse < 1e-7, 'try1 single R fit')
}

// 3: Try2 pure-R Exact reaches the finite-space certification.
{
  const d = sample(() => ({ re: 470, im: 0 }))
  m._lcr_configure(0, 0, 0, 0, 0, 0)
  const r = call(
    '_lcr_try2',
    [f64(d.f), f64(d.re), f64(d.im), N],
    [i32([82]), f64([470]), f64([0]), i32([1]), 1, 8],
  )
  ok(r.continuous_global_certified === true, 'try2 exact certification')
}

// 4: Try3 two series R reduces to one aggregate group whose domain exceeds
//    the single-device rMax.
{
  const d = sample(() => ({ re: 1.2e7, im: 0 }))
  m._lcr_configure(0, 0, 0, 0, 0, 0)
  const r = call(
    '_lcr_try3',
    [f64(d.f), f64(d.re), f64(d.im), N],
    [i32([0, 2]), i32([2, 1]), i32([82, 82]), 2],
  )
  const c = r.candidates[0]
  ok(c.groups.length === 1 && c.groups[0].members.length === 2, 'series merge group')
  ok(c.groups[0].value_bounds[1] > 1e7, 'aggregate bounds beyond rMax')
  ok(Math.abs(c.groups[0].value - 1.2e7) / 1.2e7 < 1e-6, 'aggregate value recovered')
}
// 4b: Try3 known topology with 9 internal nodes — beyond the canonical
//     labeling helper limit — fits and reports preserve-label topology keys.
{
  const d = sample(() => ({ re: 10000, im: 0 }))
  m._lcr_configure(0, 0, 0, 0, 0, 0)
  const us = [0, 2, 3, 4, 5, 6, 7, 8, 9, 10]
  const vs = [2, 3, 4, 5, 6, 7, 8, 9, 10, 1]
  const r = call(
    '_lcr_try3',
    [f64(d.f), f64(d.re), f64(d.im), N],
    [i32(us), i32(vs), i32(us.map(() => 82)), 10],
  )
  const c = r.candidates[0]
  ok(
    c.wrmse < 1e-8 && c.groups.length === 1 && c.groups[0].members.length === 10,
    'try3 chain beyond 8 internal nodes reduces to one group',
  )
  ok(
    c.original_topology_key.includes('0,2:R') &&
      c.original_topology_key.includes('1,10:R'),
    'try3 original key preserves labels',
  )
}

// 5: fixed-zero-DCR parameter status. Try2 tolerance keeps nominal zero DCR
//    fixed (never at-bound); Try3 wide bounds hit zero as a free boundary.
{
  const d = sample(f => ({ re: 0, im: 2 * Math.PI * f * 1e-3 }))
  m._lcr_configure(0, 0, 0, 0.2, 0, 0)
  const r = call(
    '_lcr_try2',
    [f64(d.f), f64(d.re), f64(d.im), N],
    [i32([76]), f64([1e-3]), f64([0]), i32([1]), 1, 8],
  )
  const cand = r.candidates.find(c => c.devices === 1) || r.candidates[0]
  const dcr = cand.diagnostics.parameters.find(p => p.quantity === 'dcr')
  ok(
    dcr && dcr.fixed === true && dcr.at_bound === false && dcr.free === false,
    'nominal zero DCR is fixed, not at-bound',
  )
  m._lcr_configure(0, 0, 0, 0, 0, 0)
  const t3 = call(
    '_lcr_try3',
    [f64(d.f), f64(d.re), f64(d.im), N],
    [i32([0]), i32([1]), i32([76]), 1],
  )
  const p3 = t3.candidates[0].diagnostics.parameters.find(p => p.quantity === 'dcr')
  ok(
    p3 && p3.free === true && p3.fixed === false && p3.at_bound === true,
    'wide-bound zero DCR is at-bound, not fixed',
  )
}

// 6: port-open topology returns the structured error contract.
{
  const d = sample(() => ({ re: 10, im: 0 }))
  m._lcr_configure(0, 0, 0, 0, 0, 0)
  const r = call(
    '_lcr_try3',
    [f64(d.f), f64(d.re), f64(d.im), N],
    [i32([0, 2]), i32([2, 3]), i32([82, 82]), 2],
  )
  ok(r.ok === false && r.code === 'port_open', 'port-open error contract')
}

// 7: optional covariance setter reaches the GLS path and stays authoritative.
{
  const d = sample(() => ({ re: 1, im: 0 }))
  m._lcr_configure(0, 0, 0, 0, 0, 0)
  const rr = f64(new Array(N).fill(2))
  const ri = f64(new Array(N).fill(0.5))
  const ii = f64(new Array(N).fill(1))
  m._lcr_set_covariance(rr, ri, ii, N)
  let r = call('_lcr_try1', [f64(d.f), f64(d.re), f64(d.im), N], [1, 0, 0, 8])
  ok(r.noise_model === 'supplied_covariance', 'covariance noise model')
  ok(r.candidates.length >= 1 && Number.isFinite(r.candidates[0].wrmse), 'GLS run finite')
  const bad = f64(new Array(N).fill(-1))
  m._lcr_set_covariance(bad, ri, ii, N)
  r = call('_lcr_try1', [f64(d.f), f64(d.re), f64(d.im), N], [1, 0, 0, 8])
  ok(r.ok === false && r.code === 'bad_input', 'non-SPD covariance rejected')
  m._free(bad)
  m._free(rr)
  m._free(ri)
  m._free(ii)
  m._lcr_set_covariance(0, 0, 0, 0)
  r = call('_lcr_try1', [f64(d.f), f64(d.re), f64(d.im), N], [1, 0, 0, 8])
  ok(r.noise_model === 'relative_unknown_scale', 'null covariance clears to relative')
}

// 8: response lcr_free plus input _free on every path; repeat runs are stable.
{
  const d = sample(() => ({ re: 470, im: 0 }))
  m._lcr_configure(0, 0, 0, 0, 0, 0)
  for (let k = 0; k < 2; ++k) {
    const r = call('_lcr_try1', [f64(d.f), f64(d.re), f64(d.im), N], [1, 0, 0, 8])
    ok(r.candidates.length >= 1, 'repeat run ' + k)
  }
}

// maxDepth must stay independent of maxN: default depth 4, never forced to
// the device-count ceiling.
{
  const d = sample(() => ({ re: 1200, im: 0 }))
  m._lcr_configure_search(0, 0, 0, 12, 180, 1, 1e-6)
  const r = call('_lcr_try1', [f64(d.f), f64(d.re), f64(d.im), N], [0, 3, 0, 8])
  ok(r.max_depth === 4, 'maxDepth defaults to 4, independent of maxN')
  ok(r.max_n === 3 && r.stats.generated > 0, 'search ran with maxN=3')
  const deep = call('_lcr_try1', [f64(d.f), f64(d.re), f64(d.im), N], [0, 0, 2, 8])
  ok(deep.max_depth === 2, 'explicit maxDepth honored')
}

console.log(checks + ' checks passed')
