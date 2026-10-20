#!/usr/bin/env bash
# COMBO OFF SWEEP for AccomplishedAlchemist -- the wide version of the aa_co_* fixtures in test/combo_off/.
# See test/aa_combo_off_sweep.py for the classes (a / b / c / c' / d / e) and the two populations.
# A REPORT, not a gate: exits nonzero only on a harness error.
#
# Usage:  bash test/aa_combo_off_sweep.sh                 # synthetic matrix + 40 autonomous games
#         bash test/aa_combo_off_sweep.sh --quick         # synthetic core + 10 games
#         bash test/aa_combo_off_sweep.sh --reuse-games   # re-mine the game logs already on disk
#         MTG_BIN=path AA_SWEEP_SCRATCH=build/scratch/... bash test/aa_combo_off_sweep.sh
# In-flight fixtures and game logs go to AA_SWEEP_SCRATCH (default logs/aa_combo_off_sweep); point it at the
# build volume for a heavy run (CLAUDE.md: regenerable churn off the 9p mount).
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(dirname "$here")"
BIN="${MTG_BIN:-$root/build/Release/mtg}"
[ -f "$BIN" ] || BIN="$root/build/Release/mtg.exe"
if [ ! -f "$BIN" ]; then echo "ERROR: $BIN not found -- build Release first (./build.sh)." >&2; exit 2; fi
cd "$root" || exit 2
exec python3 "$here/aa_combo_off_sweep.py" "$@"
