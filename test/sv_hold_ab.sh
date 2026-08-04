#!/usr/bin/env bash
# Paired-seed A/B for the Shard Volley hold heuristic (MTG_SV_HOLD).
#
# Each arm is ONE pooled batch over N_SEEDS jobs (CLAUDE.md: one work queue, one tail), and the
# two arms share the same seed layout, so every job is a matched pair -> paired t-test on the
# per-job avg turn-to-win (the metric; unwon = max_turns+1, negative delta = better).
#
# Seed layout: base = BLOCK + i*SPACING with SPACING >= GAMES, so no two jobs share a game id
# (game identity is base_seed + game_index; overlapping bases silently REPLAY games and fake a
# zero-variance result -- see docs/design and the A/B seed rules).
#
# Usage: test/sv_hold_ab.sh <block_base> <label> [arm_env_a] [arm_env_b]
#   e.g. test/sv_hold_ab.sh 200000 train            # default arms: SV_HOLD=0 (shipped) vs default
set -euo pipefail
cd "$(dirname "$0")/.."

BLOCK=${1:-200000}
LABEL=${2:-train}
ARM_A_ENV=${3:-MTG_SV_HOLD=0}      # control: shipped behaviour
ARM_B_ENV=${4:-MTG_SV_HOLD=1}      # treatment: the hold heuristic (strict)

# Per-arm binary, so an arm can be a DIFFERENT build (control = the branch tip in a worktree)
# rather than only a different env flag. Both default to the working-tree build.
BIN_A=${BIN_A:-./build/Release/mtg}
BIN_B=${BIN_B:-./build/Release/mtg}
N_SEEDS=${N_SEEDS:-20}
GAMES=${GAMES:-5000}
SPACING=${SPACING:-10000}
DECK=${DECK:-decks/burn/burn.txt}
OUT=logs/sv_hold_ab/$LABEL
mkdir -p "$OUT"

python3 - "$OUT/manifest.json" "$DECK" "$BLOCK" "$N_SEEDS" "$GAMES" "$SPACING" <<'PY'
import json, sys
out, deck, block, n, games, spacing = sys.argv[1], sys.argv[2], *map(int, sys.argv[3:7])
jobs = [dict(name=f"s{block + i*spacing}", deck=deck, games=games, seed=block + i*spacing)
        for i in range(n)]
assert spacing >= games, "seed spacing must be >= games/job or jobs replay each other's games"
json.dump(dict(jobs=jobs), open(out, "w"), indent=1)
PY

for arm in A B; do
  eval "kv=\$ARM_${arm}_ENV"
  eval "bin=\$BIN_${arm}"
  echo "=== arm $arm ($kv $bin) ==="
  env $kv "$bin" --batch "$OUT/manifest.json" --threads "${THREADS:-0}" \
      --game-log-dir "$OUT/wins_$arm" 2>/dev/null | tee "$OUT/arm_$arm.txt" | tail -1
done

python3 - "$OUT" "$ARM_A_ENV" "$ARM_B_ENV" <<'PY'
import re, sys, statistics as st
out, a_env, b_env = sys.argv[1:4]
def read(p):
    d = {}
    for ln in open(p):
        m = re.match(r"(\S+): played=(\d+) avg=([\d.]+) digest=(\S+)", ln)
        if m: d[m[1]] = (int(m[2]), float(m[3]), m[4])
    return d
A, B = read(f"{out}/arm_A.txt"), read(f"{out}/arm_B.txt")
keys = sorted(set(A) & set(B))
ids = sum(A[k][0] for k in keys)
print(f"paired seeds {len(keys)}  games/arm {ids}")
da = [B[k][1] - A[k][1] for k in keys]
mean_a = sum(A[k][0]*A[k][1] for k in keys)/ids
mean_b = sum(B[k][0]*B[k][1] for k in keys)/ids
m, sd = st.mean(da), (st.stdev(da) if len(da) > 1 else 0.0)
t = m/(sd/len(da)**0.5) if sd else float("nan")
ci = 1.96*sd/len(da)**0.5
print(f"\nA {a_env:<20} : {mean_a:.5f}")
print(f"B {b_env:<20} : {mean_b:.5f}")
print(f"delta (B-A): {m:+.5f}   paired t = {t:+.2f}   sd={sd:.5f}   95% CI: {m-ci:+.5f} .. {m+ci:+.5f}")
better = sum(1 for d in da if d < -1e-12); worse = sum(1 for d in da if d > 1e-12)
print(f"better/worse/tied: {better}/{worse}/{len(da)-better-worse}")
print("[negative = B better; metric = avg turn-to-win, unwon = max_turns+1]")
print("digests differ on:", sum(1 for k in keys if A[k][2] != B[k][2]), "of", len(keys), "jobs")
PY
