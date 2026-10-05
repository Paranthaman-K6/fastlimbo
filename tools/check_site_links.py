#!/usr/bin/env python3
"""Audit internal links in a built MkDocs site.

Checks every href/src in site/**/*.html:
  - absolute-path links (/foo/) must exist under site/
  - relative links must resolve to a real file or index.html
  - anchors must exist as id= or name= in the target document

Usage: python3 tools/check_site_links.py [site_dir]
Exit code 1 if any broken link is found.
"""
from __future__ import annotations

import pathlib
import re
import sys
from urllib.parse import unquote, urldefrag, urlparse

HREF_RE = re.compile(r'(?:href|src)\s*=\s*"([^"]+)"', re.I)
ID_RE = re.compile(r'\b(?:id|name)\s*=\s*"([^"]+)"', re.I)


def page_ids(html: str) -> set[str]:
    return {unquote(m) for m in ID_RE.findall(html)}


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    site = pathlib.Path(args[0] if args else "site").resolve()
    if not site.is_dir():
        print(f"no site dir at {site}", file=sys.stderr)
        return 2

    # Absolute links may be prefixed with the deployment base path (the path
    # component of site_url, e.g. /limbo/ for a project Pages site). Strip it
    # before resolving, otherwise every absolute link looks broken locally.
    base = "/"
    if "--base" in sys.argv:
        base = sys.argv[sys.argv.index("--base") + 1]
    if not base.startswith("/"):
        base = "/" + base
    if not base.endswith("/"):
        base += "/"

    # cache target document text once, we hit the same pages repeatedly
    id_cache: dict[pathlib.Path, set[str]] = {}

    def ids_for(path: pathlib.Path) -> set[str]:
        if path not in id_cache:
            id_cache[path] = page_ids(path.read_text(encoding="utf-8", errors="replace"))
        return id_cache[path]

    broken: list[str] = []
    checked = 0
    external = 0

    for page in sorted(site.rglob("*.html")):
        raw = page.read_text(encoding="utf-8", errors="replace")
        for link in HREF_RE.findall(raw):
            if link.startswith(("http://", "https://", "mailto:", "tel:", "//", "data:")):
                external += 1
                continue
            if link.startswith("javascript:"):
                # Material emits javascript:void(0) for tab toggles etc. Not a link.
                continue

            target, frag = urldefrag(link)
            target = unquote(target)
            if not target:
                continue  # pure anchor on the same page

            checked += 1
            if target.startswith("/"):
                rel = target.lstrip("/")
                if base != "/" and rel.startswith(base.lstrip("/")):
                    rel = rel[len(base.lstrip("/")) :]
                dest = site / rel
            else:
                dest = (page.parent / target).resolve()

            # normalise: directory URL -> index.html (".." and "/" land on a dir)
            candidates = [dest, dest / "index.html"]
            if dest.is_dir():
                candidates.insert(0, dest / "index.html")
            if dest.suffix == "":
                candidates.append(pathlib.Path(str(dest) + ".html"))

            hit = next((c for c in candidates if c.is_file()), None)
            if hit is None:
                # a bare directory we can't index is fine too (e.g. asset folders)
                if dest.is_dir():
                    continue
                broken.append(f"{page.relative_to(site)} -> {link}  (no such file)")
                continue

            if frag and hit.suffix == ".html":
                if unquote(frag) not in ids_for(hit):
                    broken.append(
                        f"{page.relative_to(site)} -> {link}  (missing anchor)"
                    )

    print(f"internal links checked : {checked}")
    print(f"external links skipped : {external}")
    print(f"pages scanned         : {len(list(site.rglob('*.html')))}")

    if broken:
        print(f"\nBROKEN ({len(broken)}):", file=sys.stderr)
        for b in broken:
            print("  " + b, file=sys.stderr)
        return 1

    print("\nAll internal links and anchors resolve.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
