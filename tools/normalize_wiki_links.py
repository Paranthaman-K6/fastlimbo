# limbo-c++ — proprietary software, all rights reserved.
# Copyright (c) 2026 Paranthaman
# See LICENSE. No permission is granted to copy, modify, or redistribute
# this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#!/usr/bin/env python3
"""Normalise wiki-internal links so they work in BOTH renderers.

GitHub's native wiki happily resolves an extension-less link (`[Testing](Testing)`).
MkDocs does not: with directory URLs it emits the link verbatim, so the browser
resolves `Testing` against the current page's directory and 404s.

Adding the `.md` suffix satisfies both -- MkDocs rewrites it to a proper page URL,
and GitHub's wiki resolves `Testing.md` to the page just as happily.

Idempotent: re-running is a no-op. Anchors are preserved.

Usage: python3 tools/normalize_wiki_links.py [wiki_dir]
"""
from __future__ import annotations

import pathlib
import re
import sys

# ](Target) or ](Target#anchor) where Target is a bare page stem (no slash, no suffix)
LINK_RE = re.compile(r"\]\(([A-Za-z][A-Za-z0-9_-]*)((?:#[^)\s]*)?)\)")

# Pages that legitimately exist as targets inside wiki/
KNOWN = {
    "Home",
    "Getting-Started",
    "Configuration",
    "Velocity-Forwarding",
    "Architecture",
    "Protocol-Support",
    "Void-World",
    "Schematic-Support",
    "Hardening-Security",
    "Testing",
    "Troubleshooting",
    "Development",
}


def main() -> int:
    if len(sys.argv) > 1:
        root = pathlib.Path(sys.argv[1]).resolve()
    else:
        root = pathlib.Path(__file__).resolve().parent.parent / "wiki"
    if not root.is_dir():
        print(f"wiki dir not found: {root}", file=sys.stderr)
        return 2

    changed: list[str] = []
    unknown: set[str] = set()

    for path in sorted(root.rglob("*.md")):
        text = path.read_text(encoding="utf-8")

        def repl(m: re.Match[str]) -> str:
            target, anchor = m.group(1), m.group(2)
            if target not in KNOWN:
                unknown.add(target)
                return m.group(0)
            return f"]({target}.md{anchor})"

        new = LINK_RE.sub(repl, text)
        if new != text:
            path.write_text(new, encoding="utf-8")
            changed.append(str(path.relative_to(root)))

    if changed:
        print("rewritten:", ", ".join(changed))
    else:
        print("nothing to rewrite (already normalised)")

    # A link we did not recognise means the nav and the pages have drifted apart.
    if unknown:
        print(
            "WARNING: unrecognised link targets (add them to KNOWN if they are real pages): "
            + ", ".join(sorted(unknown)),
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
