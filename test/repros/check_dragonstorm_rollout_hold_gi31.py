#!/usr/bin/env python3
"""Focused search-quality repro for Dragonstorm smoke s1001 gi31."""

import pathlib
import re
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} /path/to/mtg", file=sys.stderr)
        return 2
    repo = pathlib.Path(__file__).resolve().parents[2]
    manifest = pathlib.Path(__file__).with_name("dragonstorm_rollout_hold_gi31.manifest.json")
    result = subprocess.run(
        [str(pathlib.Path(sys.argv[1]).resolve()), "--batch", str(manifest), "--threads", "1"],
        cwd=repo,
        check=True,
        capture_output=True,
        text=True,
    )
    expected = {"d0": "7.0000", "d3": "4.0000"}
    for depth, turns in expected.items():
        pattern = rf"^dragonstorm_rollout_hold_gi31_{depth}: played=1 avg={turns}(?: |$)"
        if not re.search(pattern, result.stdout, re.MULTILINE):
            print(result.stdout, file=sys.stderr)
            print(f"expected gi31 {depth} win on turn {int(float(turns))}", file=sys.stderr)
            return 1
    print("Dragonstorm gi31: d0 T7, searched d3 T4")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
