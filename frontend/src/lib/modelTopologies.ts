/**
 * Maps the 8 fixed named topologies from backend `dsp/topology_fit.py` into
 * Netlist tree structures so they can be rendered as true schematics by `Schematic.vue`.
 */

import type { Netlist } from '../api'

export function topologyToNetlist(
  modelName: string,
  params: Record<string, number>,
): Netlist | null {
  if (!params) return null

  const get = (k: string, def = 0) => (typeof params[k] === 'number' ? params[k] : def)

  switch (modelName) {
    case 'series_RLC':
      return {
        type: 'series',
        children: [
          { type: 'R', R: get('R') },
          { type: 'L', L: get('L') },
          { type: 'C', C: get('C') },
        ],
      }

    case 'series_RC':
      return {
        type: 'series',
        children: [
          { type: 'R', R: get('R') },
          { type: 'C', C: get('C') },
        ],
      }

    case 'series_RL':
      return {
        type: 'series',
        children: [
          { type: 'R', R: get('R') },
          { type: 'L', L: get('L') },
        ],
      }

    case 'parallel_RLC':
      return {
        type: 'parallel',
        children: [
          { type: 'R', R: get('R') },
          { type: 'L', L: get('L') },
          { type: 'C', C: get('C') },
        ],
      }

    case 'parallel_RC':
      return {
        type: 'parallel',
        children: [
          { type: 'R', R: get('R') },
          { type: 'C', C: get('C') },
        ],
      }

    case 'parallel_RL':
      return {
        type: 'parallel',
        children: [
          { type: 'R', R: get('R') },
          { type: 'L', L: get('L') },
        ],
      }

    case 'R_LC_parallel':
      return {
        type: 'series',
        children: [
          { type: 'R', R: get('R') },
          {
            type: 'parallel',
            children: [
              { type: 'L', L: get('L') },
              { type: 'C', C: get('C') },
            ],
          },
        ],
      }

    case 'Rs_Rp_C':
      return {
        type: 'series',
        children: [
          { type: 'R', R: get('Rs') },
          {
            type: 'parallel',
            children: [
              { type: 'R', R: get('Rp') },
              { type: 'C', C: get('C') },
            ],
          },
        ],
      }

    default:
      return null
  }
}
