#!/usr/bin/env python3
"""QR1 regression: generic cycling seeds must preserve EDF s5/gi4's old menu.

Run from the repo root after building two binaries:
  python3 test/viewer_cycle_seed_valve_check.py PRE_CYCLING_MTG NEW_MTG

PRE_CYCLING_MTG must come from the immediately preceding integrated tree, before
the generic cycling viewer changes. The recorded EDF frame at the 8192-position
valve lost Mariposa Military Base in the old QR1 implementation. Reuse the full
reference-prefix checker rather than duplicating intent resolution or plan JSON
comparison. That checker also fails if zero main-phase frames were inspected.
"""
import subprocess
import sys
from pathlib import Path

if len(sys.argv) != 3:
    sys.exit("usage: viewer_cycle_seed_valve_check.py PRE_CYCLING_MTG NEW_MTG")
root = Path(__file__).resolve().parent.parent
args = [sys.executable, str(root / "test/viewer_menu_prefix_check.py"),
        str(Path(sys.argv[1]).resolve()), str(Path(sys.argv[2]).resolve()), "1",
        str(root / "references/EldraziDisplacerFlicker/claude_s5_gi4.json")]
sys.exit(subprocess.run(args, cwd=root).returncode)
