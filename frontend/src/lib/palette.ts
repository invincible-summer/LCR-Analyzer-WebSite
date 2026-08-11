// Palette mirrors the validated dataviz reference palette.
// Dark is the primary (instrument) theme; light is a full selected alternative.

export interface Palette {
  mode: 'dark' | 'light'
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

const DARK: Palette = {
  mode: 'dark',
  surface: '#1a1a19',
  page: '#0d0d0d',
  text: '#ffffff',
  text2: '#c3c2b7',
  text3: '#898781',
  grid: '#2c2c2a',
  baseline: '#383835',
  border: 'rgba(255,255,255,0.10)',
  accent: '#3987e5',
  series: ['#3987e5', '#d95926', '#199e70', '#c98500', '#d55181', '#9085e9', '#e66767'],
  status: { good: '#0ca30c', warning: '#fab219', serious: '#ec835a', critical: '#d03b3b' },
}

const LIGHT: Palette = {
  mode: 'light',
  surface: '#fcfcfb',
  page: '#f9f9f7',
  text: '#0b0b0b',
  text2: '#52514e',
  text3: '#898781',
  grid: '#e1e0d9',
  baseline: '#c3c2b7',
  border: 'rgba(11,11,11,0.10)',
  accent: '#2a78d6',
  series: ['#2a78d6', '#eb6834', '#1baf7a', '#eda100', '#e87ba4', '#4a3aa7', '#e34948'],
  status: { good: '#0ca30c', warning: '#fab219', serious: '#ec835a', critical: '#d03b3b' },
}

export const palettes = { dark: DARK, light: LIGHT }

export function getPalette(theme: 'dark' | 'light'): Palette {
  return theme === 'dark' ? DARK : LIGHT
}
