import { describe, expect, it } from 'vitest'
import { renderDoc } from '../docToc'
import { DOCS } from '../../docs'

describe('renderDoc', () => {
  it('renders headings with ids and extracts a TOC', () => {
    const { html, toc } = renderDoc('# A\n\ntext\n\n## B\n\nmore\n\n### C\n\nend\n')
    expect(html).toContain('<h1 id="dh-0">')
    expect(html).toContain('<h2 id="dh-1">')
    expect(toc).toEqual([
      { id: 'dh-0', level: 1, text: 'A' },
      { id: 'dh-1', level: 2, text: 'B' },
      { id: 'dh-2', level: 3, text: 'C' },
    ])
  })

  it('renders inline math and keeps the TOC text plain', () => {
    const { html, toc } = renderDoc('## 阻抗 $Z = R + jX$\n')
    expect(html).toContain('katex')
    expect(toc[0].text).not.toContain('$')
  })

  it('h4+ headings do not enter the TOC', () => {
    const { toc } = renderDoc('#### deep\n')
    expect(toc).toEqual([])
  })

  it('every shipped doc renders with a non-empty TOC', () => {
    for (const d of DOCS) {
      const { html, toc } = renderDoc(d.source)
      expect(html.length, d.key).toBeGreaterThan(500)
      expect(toc.length, d.key).toBeGreaterThan(0)
      // no raw unrendered markdown heading markers leaked into html
      expect(html, d.key).not.toMatch(/<h[123][^>]*>#+/)
    }
  })
})
