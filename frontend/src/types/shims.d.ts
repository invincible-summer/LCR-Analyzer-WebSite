declare module 'markdown-it-texmath'

declare module '*.md?raw' {
  const src: string
  export default src
}

declare module '*.wasm?url' {
  const url: string
  export default url
}
