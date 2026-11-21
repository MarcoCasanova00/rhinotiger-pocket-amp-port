# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hacha
# The site's pages in two languages, over what fm1-base's tools/make_site.py makes (or alone, for a preview):
#   SITE/index.html          the front page (app/site/landing.md)
#   SITE/manual/index.html   the manual (app/docs/MANUAL.md), its images in SITE/manual/img
#   SITE/manual/ja/index.html  the Japanese manual (app/docs/MANUAL.ja.md), the same images
# with base/tools/manual_html.py, then fixed for what it does not know: the firmware's name as the manuals write it
# (fm1.toml's is the files' RHINOTIGER), Japanese (its words, lang, no spaces where a line was wrapped), the way
# between the two languages, and one id per subsection.
#   python3 app/site/pages.py SITE_DIR [VERSION PACKAGE_NAME]
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

APP = Path(__file__).resolve().parents[1]
BASE = APP.parent / "base"
sys.path.insert(0, str(BASE / "tools"))
from build import read_conf  # noqa: E402

CJK = "　-ヿ㐀-鿿＀-￯"
BLOCK = re.compile(r"(#|\||```|---|\d+\. |- |!\[|> |\s*</?details)")
JA_WORDS = [                                   # base's English words on a Japanese page
    ('<html lang="en">', '<html lang="ja">'),
    ('>Skip to content<', '>本文へ<'),
    ('<h1>Manual</h1>', '<h1>マニュアル</h1>'),
    ('aria-label="Contents"><h2 id="contents">Contents</h2>', 'aria-label="目次"><h2 id="contents">目次</h2>'),
    ('>Emulator</a>', '>エミュレーター</a>'), ('>Install</a>', '>インストール</a>'),
    ('>Manual</a>', '>マニュアル</a>'), ('>Source</a>', '>ソース</a>'),
]


def join_cjk(md):
    """a wrapped line joined to the one before without a space where either side is Japanese (a browser would show
    the newline as a space between two characters)"""
    out = []
    for ln in md.splitlines():
        prev = out[-1] if out else ""
        cont = ln.strip() and prev.strip() and (ln.startswith("  ") or not BLOCK.match(ln)) \
            and not prev.startswith(("#", "|", "```", "!["))
        if cont and (re.search(f"[{CJK}]$", prev.rstrip()) or re.match(f"[{CJK}]", ln.strip())):
            out[-1] = prev.rstrip() + ln.strip()
        else:
            out.append(ln)
    return "\n".join(out) + "\n"


def render(md_text, dst, name, conf, home=False):
    with tempfile.TemporaryDirectory() as t:
        src = Path(t) / "page.md"
        src.write_text(md_text, encoding="utf-8")
        args = [sys.executable, str(BASE / "tools" / "manual_html.py"), str(src), str(dst), "--name", name,
                "--repo", conf.get("repo", ""), "--tagline", conf.get("tagline", "")]
        subprocess.run(args + (["--home"] if home else []), check=True, capture_output=True)
    return dst.read_text(encoding="utf-8")


def subsections(page):
    """### 5.1 MAIN: base gives every 5.x the section's id (s5); each its own (s5-1)"""
    return re.sub(r'<h3 id="s\d+">(\d+)\.(\d+)', r'<h3 id="s\1-\2">\1.\2', page)


def other_language(page, href, label, lang):
    """the [日本語](MANUAL.ja.md) line under the title -> a link at the end of the site's header"""
    page = re.sub(r'<p class="lede"><a href="MANUAL(\.ja)?\.md">[^<]*</a></p>\n', "", page)
    return page.replace("</nav></header>", f'<a href="{href}" lang="{lang}" hreflang="{lang}">{label}</a></nav></header>', 1)


def main(site, version="preview", pkg="rhinotiger.fwsc"):
    site = Path(site)
    conf = read_conf(APP / "fm1.toml")
    en = (APP / "docs" / "MANUAL.md").read_text(encoding="utf-8")
    name = re.search(r"^# (.+)$", en, re.M).group(1).strip()          # RhinoTiger
    (site / "manual" / "ja").mkdir(parents=True, exist_ok=True)

    landing = (APP / "site" / "landing.md").read_text(encoding="utf-8")
    landing = landing.replace("{{VERSION}}", version).replace("{{PKG}}", pkg)
    landing = re.sub(r"^# .+$", f"# {name}", landing, count=1, flags=re.M)
    render(landing, site / "index.html", name, conf, home=True)
    if (APP / "site" / "img").is_dir():
        shutil.copytree(APP / "site" / "img", site / "img", dirs_exist_ok=True)

    page = render(en, site / "manual" / "index.html", name, conf)
    page = other_language(subsections(page), "ja/", "日本語", "ja")
    (site / "manual" / "index.html").write_text(page, encoding="utf-8")
    shutil.copytree(APP / "docs" / "img", site / "manual" / "img", dirs_exist_ok=True)

    ja = (APP / "docs" / "MANUAL.ja.md").read_text(encoding="utf-8")
    ja = re.sub(r"^# .+$", f"# {name}", ja, count=1, flags=re.M)        # (the site's header comes with the name)
    ja = re.sub(r"^## 目次$", "## Contents", ja, count=1, flags=re.M)   # (base makes the contents of that one)
    page = render(join_cjk(ja), site / "manual" / "ja" / "index.html", name, conf)
    for a, b in JA_WORDS:
        page = page.replace(a, b)
    page = re.sub(r"<title>Manual · ", "<title>マニュアル · ", page)
    page = page.replace('href="../', 'href="../../').replace('src="img/', 'src="../img/')
    page = page.replace('href="../../manual/" aria-current', 'href="./" aria-current')
    page = other_language(subsections(page), "../", "English", "en")
    (site / "manual" / "ja" / "index.html").write_text(page, encoding="utf-8")
    print(f"pages: {site}: index.html, manual/index.html, manual/ja/index.html")


if __name__ == "__main__":
    if len(sys.argv) not in (2, 4):
        sys.exit("usage: python3 app/site/pages.py SITE_DIR [VERSION PACKAGE_NAME]")
    main(*sys.argv[1:])
