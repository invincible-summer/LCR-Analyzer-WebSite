// Single-source palette for the light "scientific journal" theme.
// The CSS custom properties in style.css mirror these values exactly —
// change both together.

export interface Palette {
  mode: 'light'
  surface: string
  page: string
  text: string
  text2: string
  text3: string
  grid: string
  baseline: string
  border: string
  accent: string
  series: string[]
  status: { good: string; warning: string; serious: string; critical: string }
}

const LIGHT: Palette = {
  mode: 'light',
  surface: '#ffffff',
  page: '#f7f7f5',
  text: '#1a1a1a',
  text2: '#50504c',
  text3: '#8a897f',
  grid: '#e8e7e1',
  baseline: '#c9c8c0',
  border: 'rgba(26, 26, 26, 0.12)',
  accent: '#2456a6',
  series: ['#3b6fb6', '#d96e2b', '#218a63', '#b58b00', '#b0568c', '#6a5fb5', '#c44d4d'],
  status: { good: '#1c8a3d', warning: '#a0741a', serious: '#c25a2e', critical: '#c02929' },
}

export const palettes = { light: LIGHT }

export function getPalette(_theme?: string): Palette {
  return LIGHT
}
