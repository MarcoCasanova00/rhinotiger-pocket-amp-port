# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hacha
# Puts RhinoTiger's colours (theme.css: the screen's black, the parts' neon hues) last in the <head> of every page of
# a site made by fm1-base's tools/make_site.py and app/site/pages.py, with its fonts; and on a page with contents, the
# contents again beside the text (shown on a wide window, the section in view marked).
#   python3 app/site/theme.py SITE_DIR          (as ./site.sh does)
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MARK = "<!-- rhinotiger theme -->"
FONTS = ('<link rel="preconnect" href="https://fonts.googleapis.com">\n'
         '<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>\n'
         '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Chakra+Petch:wght@500;600;700'
         '&family=IBM+Plex+Sans:wght@400;600&family=Noto+Sans+JP:wght@400;700&display=swap">\n')
HERE_JS = """<script>
(() => {  // the contents beside the text: the section in view
  const links = [...document.querySelectorAll('aside.toc a')];
  const heads = links.map(a => document.getElementById(a.hash.slice(1))).filter(Boolean);
  if (!heads.length) return;
  const mark = () => {
    let i = 0;
    heads.forEach((h, j) => { if (h.getBoundingClientRect().top < innerHeight * .3) i = j; });
    links.forEach((a, j) => a.classList.toggle('here', j === i));
  };
  addEventListener('scroll', mark, {passive: true}); mark();
})();
</script>
"""


def side_contents(html):
    """the contents (base's nav.contents in the text) copied into an <aside> beside <main>"""
    m = re.search(r'<nav class="contents" aria-label="([^"]+)"><h2 id="contents">([^<]+)</h2>(.*?)</nav>', html, re.S)
    if not m or '<div class="page">' not in html:
        return html
    items = re.sub(r'(<a href="#s(\d+)">)', r'\1<span class="n">\2</span>', m.group(3))
    aside = f'<aside class="toc" aria-label="{m.group(1)}"><h2>{m.group(2)}</h2>{items}</aside>\n'
    html = html.replace('<div class="page">', '<div class="page has-toc">', 1)
    html = html.replace('<main id="content"', aside + '<main id="content"', 1)
    return html.replace("</body>", HERE_JS + "</body>", 1)


def main(site):
    css = (HERE / "theme.css").read_text(encoding="utf-8")
    for p in sorted(Path(site).rglob("*.html")):
        html = p.read_text(encoding="utf-8")
        if MARK in html:
            continue
        i = html.find("</head>")
        if i < 0:
            raise SystemExit(f"theme.py: {p}: no </head>")
        html = html[:i] + f"{MARK}\n{FONTS}<style>\n{css}</style>\n" + html[i:]
        p.write_text(side_contents(html), encoding="utf-8")
        print(f"theme: {p}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: python3 app/site/theme.py SITE_DIR")
    main(sys.argv[1])
