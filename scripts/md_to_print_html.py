#!/usr/bin/env python3
"""Render a docs/design Markdown page as ONE self-contained, print-ready HTML file (stdlib only).

Open the output in a browser and print (or "Save as PDF"). Covers the subset these result
documents use: headings, pipe tables, fenced code, bullet lists, paragraphs, **bold**, *italic*,
`code`, [links](url), and raw HTML lines (the page-break divs and the <style> block pass through).

    python3 scripts/md_to_print_html.py docs/deck-update-summaries/<name>.md   (writes <name>.html beside it)
"""
import html
import re
import sys
from pathlib import Path

BASE_CSS = """
/* ONE stylesheet for screen AND print, so the fit script's on-screen measurement is what prints. */
@page { size: A4 portrait; margin: 12mm; }
html { background: #888; }
body { margin: 0; font-family: -apple-system, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; color: #111;
       font-size: 9pt; line-height: 1.25; }
section.page { box-sizing: border-box; width: 186mm; margin: 6mm auto; background: #fff; padding: 0;
               overflow: hidden; break-after: page; page-break-after: always; break-inside: avoid; }
section.page:last-child { break-after: auto; page-break-after: auto; }
.sheet { padding: 4mm; }
h1 { font-size: 14pt; margin: 0 0 2mm 0; border-bottom: 1.5px solid #333; padding-bottom: 1mm; }
h2 { font-size: 10.5pt; margin: 3mm 0 1mm 0; }
p { margin: 1.2mm 0; }
ul { margin: 1.2mm 0; padding-left: 5mm; } li { margin: 0.5mm 0; }
table { border-collapse: collapse; margin: 1mm 0 2mm 0; font-size: 8.5pt; }
th, td { border: 1px solid #999; padding: 1px 4px; vertical-align: top; }
th { background: #eee; -webkit-print-color-adjust: exact; print-color-adjust: exact; }
td.num, th.num { text-align: right; font-variant-numeric: tabular-nums; white-space: nowrap; }
.pair { display: grid; grid-template-columns: 1fr 1fr; gap: 4mm; align-items: start; }
.pair table { width: 100%; }
code { font-family: Consolas, "DejaVu Sans Mono", monospace; font-size: 0.92em; background: #f3f3f3; padding: 0 2px; }
pre { background: #f6f6f6; border: 1px solid #ddd; padding: 1.5mm; font-size: 7.5pt; white-space: pre-wrap; margin: 1mm 0; }
pre code { background: none; padding: 0; }
a { color: inherit; text-decoration: none; }
section.page { position: relative; }
section.page::after { content: "printed at " attr(data-scale); position: absolute; top: 1mm; right: 2mm;
                      font-size: 7pt; color: #c00; }
@media print { html { background: none; } section.page { margin: 0; } .sheet { padding: 0; }
               section.page::after { content: none; } }
"""

# Shrink-to-fit: scale any section taller than the printable A4 height (297 - 2x12 mm) down to fit.
# Measured with zoom reset, so re-running (resize, beforeprint) is idempotent.
FIT_JS = """
function fitPages() {
  const probe = document.createElement('div');
  probe.style.cssText = 'position:absolute;visibility:hidden;height:100mm;width:1px';
  document.body.appendChild(probe);
  const pxPerMm = probe.getBoundingClientRect().height / 100; probe.remove();
  const avail = (297 - 24 - 3) * pxPerMm;          // 3 mm safety for printer rounding
  document.querySelectorAll('section.page > .sheet').forEach(s => {
    s.style.zoom = 1;
    const pad = parseFloat(getComputedStyle(s).paddingTop) * 2;
    const h = s.scrollHeight - (matchMedia('print').matches ? 0 : pad);
    s.style.zoom = h > avail ? (avail / h).toFixed(4) : 1;
    s.parentElement.dataset.scale = Math.round(100 * s.style.zoom) + '%';
  });
}
window.addEventListener('load', fitPages);
window.addEventListener('beforeprint', fitPages);
"""

NUM = re.compile(r"^[\s*]*[−+\-]?[\d.,]+[%×xt]?(\s*/\s*[\d.,]+)?[\s*]*$")


def inline(text):
    # Code spans become placeholders first, so emphasis that WRAPS code (**Stompy ran with `x`.**)
    # still converts, and nothing inside a code span is ever read as emphasis.
    codes = []

    def stash(m):
        codes.append("<code>" + html.escape(m.group(1)) + "</code>")
        return f"\x00{len(codes) - 1}\x00"

    p = re.sub(r"`([^`]+)`", stash, text)
    p = html.escape(p, quote=False)
    p = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r'<a href="\2">\1</a>', p)
    p = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", p)
    p = re.sub(r"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])", r"<em>\1</em>", p)
    return re.sub(r"\x00(\d+)\x00", lambda m: codes[int(m.group(1))], p)


def cells(row):
    row = row.strip()
    if row.startswith("|"):
        row = row[1:]
    if row.endswith("|"):
        row = row[:-1]
    return [c.strip() for c in row.split("|")]


def render(md):
    lines = md.split("\n")
    out, i, n = [], 0, len(lines)
    para = []

    def flush():
        if para:
            out.append("<p>" + inline(" ".join(para)) + "</p>")
            para.clear()

    while i < n:
        line = lines[i]
        s = line.strip()
        if s.startswith("<style"):
            flush()
            j = i
            while j < n and "</style>" not in lines[j]:
                j += 1
            i = j + 1      # dropped: BASE_CSS is the one stylesheet (see its header)
            continue
        if s.startswith("<") and s.endswith(">"):
            flush(); out.append(s); i += 1; continue
        if s.startswith("```"):
            flush()
            j = i + 1
            while j < n and not lines[j].strip().startswith("```"):
                j += 1
            out.append("<pre><code>" + html.escape("\n".join(lines[i + 1:j])) + "</code></pre>")
            i = j + 1
            continue
        m = re.match(r"^(#{1,6})\s+(.*)$", s)
        if m:
            flush()
            k = len(m.group(1))
            out.append(f"<h{k}>{inline(m.group(2))}</h{k}>")
            i += 1
            continue
        if s.startswith("|") and i + 1 < n and re.match(r"^\|?\s*:?-{2,}", lines[i + 1].strip()):
            flush()
            head = cells(s)
            rows = []
            j = i + 2
            while j < n and lines[j].strip().startswith("|"):
                rows.append(cells(lines[j]))
                j += 1
            numcol = [all(NUM.match(r[c]) or r[c] == "" for r in rows if c < len(r)) and
                      any(r[c] for r in rows if c < len(r)) for c in range(len(head))]
            t = ["<table><thead><tr>"]
            t += [f'<th{" class=num" if numcol[c] else ""}>{inline(h)}</th>' for c, h in enumerate(head)]
            t.append("</tr></thead><tbody>")
            for r in rows:
                t.append("<tr>" + "".join(
                    f'<td{" class=num" if c < len(numcol) and numcol[c] else ""}>{inline(v)}</td>'
                    for c, v in enumerate(r)) + "</tr>")
            t.append("</tbody></table>")
            out.append("".join(t))
            i = j
            continue
        if re.match(r"^\s*[-*]\s+", line):
            flush()
            items = []
            j = i
            while j < n and (re.match(r"^\s*[-*]\s+", lines[j]) or
                             (lines[j].startswith("  ") and lines[j].strip() and items)):
                if re.match(r"^\s*[-*]\s+", lines[j]):
                    items.append(re.sub(r"^\s*[-*]\s+", "", lines[j]))
                else:
                    items[-1] += " " + lines[j].strip()
                j += 1
            out.append("<ul>" + "".join(f"<li>{inline(it)}</li>" for it in items) + "</ul>")
            i = j
            continue
        if not s:
            flush(); i += 1; continue
        para.append(s)
        i += 1
    flush()
    return "\n".join(out)


def main():
    src = Path(sys.argv[1])
    dst = Path(sys.argv[2]) if len(sys.argv) > 2 else src.with_suffix(".html")
    md = src.read_text(encoding="utf-8")
    title = next((l[2:].strip() for l in md.split("\n") if l.startswith("# ")), src.stem)
    body = render(md)
    # One <section> per printed page, split at the document's own page-break divs.
    chunks = re.split(r'<div style="page-break-after: always;"></div>', body)
    pages = []
    for c in chunks:
        # "Cards added" + "Cards removed" side by side: each is an h2 followed by its table.
        c = re.sub(r'(<h2>Cards added</h2>\s*<table>.*?</table>)\s*(<h2>Cards removed</h2>\s*<table>.*?</table>)',
                   r'<div class="pair"><div>\1</div><div>\2</div></div>', c, flags=re.S)
        if c.strip():
            pages.append('<section class="page"><div class="sheet">' + c + '</div></section>')
    body = "\n".join(pages)
    dst.write_text("<!DOCTYPE html>\n<html lang=en><head><meta charset=utf-8>"
                   f"<title>{html.escape(title)}</title><style>{BASE_CSS}</style><script>{FIT_JS}</script></head>"
                   f"<body>\n{body}\n</body></html>\n", encoding="utf-8")
    print(dst)


if __name__ == "__main__":
    main()
