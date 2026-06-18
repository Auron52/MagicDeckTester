#!/usr/bin/env bash
# A/B: commit-the-line (MTG_FULL_DEPTH, d0-gated) vs baseline (default search),
# on the OVERNIGHT seed matrix. Same code, same seeds -- only the search mode
# differs, so per-case avg/won deltas are the A/B signal. Compares the two arms to
# EACH OTHER (not to the stale overnight GT). Run to completion in the background;
# results are deterministic so it can run undisturbed.
set -uo pipefail
cd "$(dirname "$0")/.."
BIN=./build/Release/mtg
M=test/logs/overnight/manifest.json
OUT=/tmp/ab_ov
mkdir -p "$OUT/base" "$OUT/ctl"

echo "[ab] baseline overnight start $(date -u +%H:%M:%S)"
MTG_LEGACY_SEARCH=1 "$BIN" --batch "$M" --threads 0 --game-log-dir "$OUT/base" > "$OUT/base.log" 2>"$OUT/base.err"
echo "[ab] baseline done $(date -u +%H:%M:%S); CTL overnight start"
MTG_FULL_DEPTH=1 "$BIN" --batch "$M" --threads 0 --game-log-dir "$OUT/ctl" > "$OUT/ctl.log" 2>"$OUT/ctl.err"
echo "[ab] CTL done $(date -u +%H:%M:%S); comparing"

python3 - "$OUT/base.log" "$OUT/ctl.log" <<'PY'
import re, sys
def parse(p):
    d={}
    for ln in open(p):
        m=re.match(r'(\S+): played=(\d+) won=(\d+).*avg=([0-9.]+)', ln)
        if m: d[m.group(1)]=(int(m.group(3)), float(m.group(4)))
    return d
base, ctl = parse(sys.argv[1]), parse(sys.argv[2])
worse=[]; better=0; same=0
print(f"{'case':28} {'base won/avg':>16} {'ctl won/avg':>16}  verdict")
for k in sorted(base):
    if k not in ctl: print(f"{k:28}  MISSING in ctl"); worse.append(k); continue
    bw,ba=base[k]; cw,ca=ctl[k]
    # CTL worse if fewer wins, or (equal wins and slower avg by > rounding)
    if cw < bw or (cw==bw and ca > ba + 1e-6):
        verdict="WORSE"; worse.append(k)
    elif cw > bw or ca < ba - 1e-6:
        verdict="better"; better+=1
    else:
        verdict="same"; same+=1
    print(f"{k:28} {bw:6d}/{ba:<9.4f} {cw:6d}/{ca:<9.4f}  {verdict}")
print(f"\nSUMMARY: {better} better, {same} same, {len(worse)} WORSE")
print("PASS: commit-the-line >= baseline on every case" if not worse
      else "FAIL: commit-the-line regressed on: " + ", ".join(worse))
PY
echo "[ab] DONE $(date -u +%H:%M:%S)"
