// graphLayout.ts — shared geometric layout for the Try3 graph editor and the
// generic (non-SP) result graph renderer.  Nodes sit on a circle (ports 0/1
// at the left/right horizontal extremes); parallel edges between the same
// node pair are fanned out as circular arcs so they never overlap.

export interface GraphNode {
  id: number
  x: number
  y: number
}

export interface LaidOutEdge {
  /** index of the edge within its parallel group (0 = straight-ish) */
  k: number
  /** total edges in this parallel group */
  n: number
  /** SVG path for the wire */
  path: string
  /** midpoint where the type/value badge sits */
  mx: number
  my: number
}

export function circleLayout(nodeIds: number[], width: number, height: number): GraphNode[] {
  const cx = width / 2
  const cy = height / 2
  const rx = width / 2 - 46
  const ry = height / 2 - 42
  const ids = [...nodeIds].sort((a, b) => a - b)
  const others = ids.filter((x) => x !== 0 && x !== 1)
  return ids.map((id, i) => {
    // port 0 at 180° (left), port 1 at 0° (right), internal nodes spread
    // symmetrically over the TOP arc (SVG y grows downward, so sin < 0 is up)
    let ang: number
    if (ids.length === 2) {
      ang = i === 0 ? Math.PI : 0
    } else if (id === 0) {
      ang = Math.PI
    } else if (id === 1) {
      ang = 0
    } else {
      const j = others.indexOf(id)
      const n = others.length
      const step = (Math.PI * 1.15) / n
      ang = 1.5 * Math.PI + (j - (n - 1) / 2) * step
    }
    return { id, x: cx + rx * Math.cos(ang), y: cy + ry * Math.sin(ang) }
  })
}

/**
 * Edge path between two nodes.  `k/n` fan out parallel edges: k=0 stays
 * straight, others alternate above/below with growing arc height.
 */
export function edgePath(
  a: { x: number; y: number },
  b: { x: number; y: number },
  k: number,
  n: number,
  nodeR: number,
): LaidOutEdge {
  const dx = b.x - a.x
  const dy = b.y - a.y
  const len = Math.hypot(dx, dy) || 1
  const ux = dx / len
  const uy = dy / len
  const x1 = a.x + ux * nodeR
  const y1 = a.y + uy * nodeR
  const x2 = b.x - ux * nodeR
  const y2 = b.y - uy * nodeR

  if (n <= 1 || k === 0) {
    const mx = (x1 + x2) / 2
    const my = (y1 + y2) / 2
    return { k, n, path: `M ${x1} ${y1} L ${x2} ${y2}`, mx, my }
  }
  // arc offset: alternate sides, growing with |k|
  const side = k % 2 === 1 ? 1 : -1
  const rank = Math.ceil(k / 2)
  const h = 18 + 16 * (rank - 1)
  // perpendicular unit
  const px = -uy
  const py = ux
  const cxm = (x1 + x2) / 2 + px * h * side
  const cym = (y1 + y2) / 2 + py * h * side
  // control point for a quadratic bezier; badge on the curve midpoint
  const mx = 0.25 * x1 + 0.5 * cxm + 0.25 * x2
  const my = 0.25 * y1 + 0.5 * cym + 0.25 * y2
  return { k, n, path: `M ${x1} ${y1} Q ${cxm} ${cym} ${x2} ${y2}`, mx, my }
}

export const GRAPH_NODE_R = 16
