# limbo-c++ — proprietary software, all rights reserved.
# Copyright (c) 2026 Paranthaman
# See LICENSE. No permission is granted to copy, modify, or redistribute
# this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

#!/usr/bin/env python3
"""Parse .github/workflows/*.yml so a malformed workflow fails loudly.

GitHub rejects a workflow with invalid YAML *before* creating a job, which shows
up as a run that "fails" in 0s with no log and refuses to re-run ("its workflow
file may be broken"). Catching that locally is much cheaper than debugging it
from the Actions UI.

Usage: python3 tools/check_workflows.py [repo_root]
Exit code 1 if any workflow file is unparseable.
"""
from __future__ import annotations

import pathlib
import sys

try:
    import yaml
except ImportError:  # pragma: no cover
    print("PyYAML not installed; skipping workflow parse check", file=sys.stderr)
    raise SystemExit(0)

# A mapping key that YAML 1.1 turns into the boolean True -- GitHub still wants
# the literal `on:` key, so flag it if someone writes it quoted or as True.
ON_ALIASES = {True, "on", "On", "ON"}


def main() -> int:
    if len(sys.argv) > 1:
        root = pathlib.Path(sys.argv[1]).resolve()
    else:
        root = pathlib.Path(__file__).resolve().parent.parent

    wf_dir = root / ".github" / "workflows"
    if not wf_dir.is_dir():
        print(f"no workflow dir at {wf_dir}", file=sys.stderr)
        return 2

    files = sorted(wf_dir.glob("*.yml")) + sorted(wf_dir.glob("*.yaml"))
    if not files:
        print("no workflow files found")
        return 0

    bad = 0
    for path in files:
        rel = path.relative_to(root)
        try:
            doc = yaml.safe_load(path.read_text(encoding="utf-8"))
        except yaml.YAMLError as exc:
            bad += 1
            print(f"FAIL {rel}: {exc}", file=sys.stderr)
            continue

        if not isinstance(doc, dict):
            bad += 1
            print(f"FAIL {rel}: top level is {type(doc).__name__}, expected mapping", file=sys.stderr)
            continue

        # `on:` parses to the boolean True under YAML 1.1 -- normal, not an error.
        trigger = next((doc[k] for k in doc if k in ON_ALIASES), None)
        if trigger is None:
            bad += 1
            print(f"FAIL {rel}: no `on:` trigger block", file=sys.stderr)
            continue
        if "jobs" not in doc:
            bad += 1
            print(f"FAIL {rel}: no `jobs:` block", file=sys.stderr)
            continue

        triggers = list(trigger) if isinstance(trigger, dict) else [trigger]
        print(f"ok   {rel}: triggers={triggers} jobs={list(doc['jobs'])}")

    if bad:
        print(f"\n{bad} workflow file(s) invalid", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
