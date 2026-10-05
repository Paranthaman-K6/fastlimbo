# limbo-c++ — proprietary software, all rights reserved.
# Copyright (c) 2026 Paranthaman
# See LICENSE. No permission is granted to copy, modify, or redistribute
# this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#!/usr/bin/env python3
"""Rewrite wiki links that escape docs_dir for the GitHub Pages (MkDocs) build.

MkDocs serves wiki/ as the site root, so `../src/...` style links resolve to
nothing. Anything that lives in the repo becomes an absolute blob/tree URL.
Internal working notes (TODO.md, docs/*-notes.md, docs/research/) are not
published, so those references degrade to plain text instead of dead links.
"""
import pathlib
import sys

REPO = "https://github.com/Paranthaman-K6/limbo"
BLOB = f"{REPO}/blob/main"
TREE = f"{REPO}/tree/main"

# Published files: relative path -> absolute URL
BLOBS = {
    "docs/ARCHITECTURE.md": f"{BLOB}/docs/ARCHITECTURE.md",
    "LICENSE": f"{BLOB}/LICENSE",
    "src/protocol/versions.h": f"{BLOB}/src/protocol/versions.h",
    "src/security/limits.h": f"{BLOB}/src/security/limits.h",
    "src/world/void_chunk.cpp": f"{BLOB}/src/world/void_chunk.cpp",
}
TREES = {
    "tests/fuzz/": f"{TREE}/tests/fuzz",
    "tests/integration/": f"{TREE}/tests/integration",
    "src/": f"{TREE}/src",
}

# exact (file, old, new) edits
EDITS = [
    # ---- placeholder repo slug -------------------------------------------
    (
        "Getting-Started.md",
        "git clone https://github.com/your-org/limbo-cpp.git",
        "git clone https://github.com/Paranthaman-K6/limbo.git",
    ),
    (
        "_Footer.md",
        "[GitHub](https://github.com/your-org/limbo-cpp) "
        "\u2022 [Issues](https://github.com/your-org/limbo-cpp/issues) "
        "\u2022 [MIT License](../LICENSE)",
        "[GitHub](https://github.com/Paranthaman-K6/limbo) "
        "\u2022 [Issues](https://github.com/Paranthaman-K6/limbo/issues) "
        "\u2022 [License](https://github.com/Paranthaman-K6/limbo/blob/main/LICENSE)",
    ),
    # ---- published source links ------------------------------------------
    (
        "Architecture.md",
        "> **Source of truth:** [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md) "
        "\u2014 this page is a user-friendly digest.",
        "> **Source of truth:** [`docs/ARCHITECTURE.md`]"
        f"({BLOB}/docs/ARCHITECTURE.md) \u2014 this page is a user-friendly digest.",
    ),
    (
        "Architecture.md",
        "All version logic lives in [`src/protocol/versions.h`](../src/protocol/versions.h).",
        "All version logic lives in [`src/protocol/versions.h`]"
        f"({BLOB}/src/protocol/versions.h).",
    ),
    (
        "Hardening-Security.md",
        "> **Sources:** [docs/hardening-notes.md](../docs/hardening-notes.md), "
        "[`src/security/limits.h/.cpp`](../src/security/limits.h), "
        "[`tests/fuzz/`](../tests/fuzz/)",
        "> **Sources:** [`src/security/limits.h`]"
        f"({BLOB}/src/security/limits.h) and "
        f"[`tests/fuzz/`]({TREE}/tests/fuzz) \u2014 the hardening threat model "
        "was worked out from the packet-parsing surface itself.",
    ),
    (
        "Hardening-Security.md",
        "**Implementation:** [`src/security/limits.h/.cpp`](../src/security/limits.h) "
        "\u2014 sliding window (1s buckets) per connection.",
        "**Implementation:** [`src/security/limits.h`]"
        f"({BLOB}/src/security/limits.h) \u2014 sliding window (1s buckets) per connection.",
    ),
    (
        "Protocol-Support.md",
        "> **Source:** [docs/research/limbo-references.md](../docs/research/limbo-references.md) "
        "+ [`src/protocol/versions.h`](../src/protocol/versions.h)",
        "> **Source:** packet IDs were cross-checked against the public "
        "protocol references listed in [Architecture](Architecture) and encoded as "
        f"data in [`src/protocol/versions.h`]({BLOB}/src/protocol/versions.h).",
    ),
    (
        "Void-World.md",
        "> **Source:** [`src/world/void_chunk.cpp`](../src/world/void_chunk.cpp), "
        "[docs/research/limbo-references.md \u00a73](../docs/research/limbo-references.md#3-chunk--void--spawn-recipe)",
        "> **Source:** [`src/world/void_chunk.cpp`]"
        f"({BLOB}/src/world/void_chunk.cpp), derived from the client-side "
        "spawn/void chunk behaviour documented on [Void World](Void-World).",
    ),
    # ---- unpublished internal notes -> plain text -------------------------
    (
        "Configuration.md",
        "Planned: `LIMBO_BIND`, `LIMBO_PORT`, `LIMBO_FORWARDING_SECRET`, etc. for "
        "container deployments. See [TODO.md](../TODO.md).",
        "Planned: `LIMBO_BIND`, `LIMBO_PORT`, `LIMBO_FORWARDING_SECRET`, etc. for "
        "container deployments. Tracked on the internal task board.",
    ),
    (
        "Development.md",
        "2. Pick a TODO item from [TODO.md](../TODO.md) or propose new",
        "2. Pick a task item from the internal task board or propose new",
    ),
    (
        "Home.md",
        "See [TODO.md](../TODO.md) for the live task board.",
        "Progress is tracked on an internal task board; the per-component state "
        "table above is the public summary.",
    ),
    (
        "Home.md",
        "MIT \u2014 see [LICENSE](../LICENSE) (to be added).",
        "Proprietary \u2014 all rights reserved. See [LICENSE]"
        f"({BLOB}/LICENSE). Author and maintainer: "
        "[**Paranthaman**](https://github.com/Paranthaman-K6).",
    ),
    (
        "Schematic-Support.md",
        "> **Status:** \U0001f7e1 In progress \u2014 see [TODO.md](../TODO.md) agent C/schematic",
        "> **Status:** \U0001f7e1 In progress \u2014 tracked on the internal task board.",
    ),
    (
        "Schematic-Support.md",
        "**Decision recorded in** [docs/research/limbo-references.md \u00a74]"
        "(../docs/research/limbo-references.md#4-schematic-scope-decision).",
        "**Decision:** the Sponge `.schem` v2 reader targets the narrow case only "
        "\u2014 a single pasted schematic at spawn, inflated with the bundled "
        "[`third_party/miniz`](https://github.com/Paranthaman-K6/limbo/tree/main/third_party) "
        "deflate implementation. Full block palettes and NBT-driven schematics are "
        "explicitly out of scope.",
    ),
    (
        "Schematic-Support.md",
        "- [docs/schematic-notes.md](../docs/schematic-notes.md) \u2014 raw research notes",
        "- [Schematic Specification]"
        "(https://github.com/SpongePowered/Schematic-Specification) \u2014 upstream format spec",
    ),
    (
        "Velocity-Forwarding.md",
        "> **Sources:** [Velocity docs](https://docs.papermc.io/velocity/player-information-forwarding), "
        "[Velocity source](https://github.com/PaperMC/Velocity), "
        "[docs/research/limbo-references.md \u00a72](../docs/research/limbo-references.md#2-velocity-modern-forwarding)",
        "> **Sources:** [Velocity docs]"
        "(https://docs.papermc.io/velocity/player-information-forwarding) and "
        "[Velocity source](https://github.com/PaperMC/Velocity).",
    ),
]

# Generic regex fallbacks for anything the explicit table missed.
GENERIC = [
    (r"\]\(\.\./LICENSE\)", f"]({BLOBS['LICENSE']})"),
    (r"\]\(\.\./src/([A-Za-z0-9_./-]+)\)", r"](https://github.com/Paranthaman-K6/limbo/blob/main/src/\1)"),
    (r"\]\(\.\./tests/([A-Za-z0-9_./-]+)\)", r"](https://github.com/Paranthaman-K6/limbo/tree/main/tests/\1)"),
    (r"\]\(\.\./docs/([A-Za-z0-9_./-]+)\)", r"](https://github.com/Paranthaman-K6/limbo/blob/main/docs/\1)"),
]


def main() -> int:
    # tools/fix_wiki_links.py -> repo root -> wiki/
    root = pathlib.Path(__file__).resolve().parent.parent / "wiki"
    if not root.is_dir():
        print(f"wiki dir not found: {root}", file=sys.stderr)
        return 2
    failures = []
    touched = set()

    for name, old, new in EDITS:
        path = root / name
        text = path.read_text(encoding="utf-8")
        if old not in text:
            failures.append(f"{name}: pattern not found -> {old[:70]!r}")
            continue
        path.write_text(text.replace(old, new), encoding="utf-8")
        touched.add(name)

    import re

    for path in sorted(root.glob("*.md")):
        text = original = path.read_text(encoding="utf-8")
        for pattern, repl in GENERIC:
            text = re.sub(pattern, repl, text)
        if text != original:
            path.write_text(text, encoding="utf-8")
            touched.add(path.name)

    print("rewritten:", ", ".join(sorted(touched)))
    if failures:
        print("\nFAILURES:", file=sys.stderr)
        for f in failures:
            print("  " + f, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
