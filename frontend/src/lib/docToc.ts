// docToc.ts — anchored markdown rendering for the docs viewer: same
// markdown-it + texmath pipeline as Markdown.vue, plus stable heading ids
// and a heading table-of-contents extracted from the token stream.

import MarkdownIt from 'markdown-it'
import texmath from 'markdown-it-texmath'
import katex from 'katex'

export interface TocEntry {
  id: string
  level: number // 1..3
  text: string
}

export interface RenderedDoc {
  html: string
  toc: TocEntry[]
}

function plainHeading(content: string): string {
  // strip inline math back to a readable approximation for the TOC list
  return content
    .replace(/\$\$?[^$]*\$\$?/g, (m) => m.replace(/\$/g, '').replace(/\\[a-zA-Z]+/g, '').trim())
    .replace(/[*_`]/g, '')
    .trim()
}

export function renderDoc(source: string): RenderedDoc {
  const toc: TocEntry[] = []
  let hIdx = 0
  const md = new MarkdownIt({ html: false, linkify: true, breaks: false, typographer: true })
  md.use(texmath, { engine: katex, delimiters: 'dollars', throwOnError: false })
  md.renderer.rules.heading_open = (tokens, idx) => {
    const tag = tokens[idx].tag
    if (tag === 'h1' || tag === 'h2' || tag === 'h3') {
      const id = `dh-${hIdx++}`
      const text = plainHeading(tokens[idx + 1]?.content ?? '')
      toc.push({ id, level: Number(tag[1]), text })
      return `<${tag} id="${id}">`
    }
    return `<${tag}>`
  }
  return { html: md.render(source), toc }
}
