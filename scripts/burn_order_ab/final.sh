#!/bin/bash
# The closing step, run UNDER the box lock (boxrun.sh batch bash final.sh): the whole-corpus viewer-protocol
# replay (every saved reference of every deck) with the shipped binary (mtg.final, the new defaults), once with
# the ORIGINAL replay checker and once with the patched one (sub-decisions of one plan answered in the plan's
# new cast order), so the checker change is judged per reference on the same binary. Output under
# build/scratch/burn-order/vpc/.
S=/workspaces/MagicDeckTester/build/scratch/burn-order
WT=$(dirname "$(dirname "$(dirname "$(readlink -f "$0")")")")
V=$S/vpc
mkdir -p "$V"
LOG=$V/final.log
echo "start $(date -u +%FT%TZ)" > "$LOG"
env -C "$WT" MTG_BIN="$S/bin/mtg.final" python3 "$V/vpc_orig.py" --threads 20 > "$V/orig_final.out" 2>&1
echo "orig checker rc=$? $(date -u +%FT%TZ)" >> "$LOG"
env -C "$WT" MTG_BIN="$S/bin/mtg.final" python3 test/viewer_protocol_check.py --threads 20 > "$V/new_final.out" 2>&1
echo "patched checker rc=$? $(date -u +%FT%TZ)" >> "$LOG"
env -C "$WT" MTG_BIN="$S/bin/mtg.base" python3 "$V/vpc_orig.py" --threads 20 > "$V/orig_base.out" 2>&1
echo "orig checker, base binary rc=$? $(date -u +%FT%TZ)" >> "$LOG"
echo "end $(date -u +%FT%TZ)" >> "$LOG"
