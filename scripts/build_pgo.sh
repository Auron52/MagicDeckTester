#!/usr/bin/env bash
# Profile-guided + link-time-optimised build of mtg / mtg-analyze -> build/PGO/.
#
# Invoked as `./build.sh pgo <deck-dir>...` (never directly needed). Linux/macOS GCC or Clang only.
#
# WHY: measured on the SelesnyaLifegain keep-gen workload (MTG_KEEP_BENCH, 1000 size-7 floor
# rollouts at d2/b3, interleaved A/B, thread-CPU time): Release 129-135 ms/rollout, PGO 103-111,
# PGO+LTO 99-100 -- 1.32x, with the rollout digest and the play digest BYTE-IDENTICAL to Release.
# PGO/LTO change code layout, inlining and branch prediction only; they do not change what the
# program computes (no -ffast-math, no -march), so every result is the same as build/Release's.
# See docs/design/selesnya-keepgen-bulk-cost.md.
#
# HOW: three steps, all in ONE single-config tree (build/pgo-work/) because GCC keys each .gcda profile
# on the object file's path -- a second tree would silently find no profile at all.
#   1. configure + build with -fprofile-generate (instrumented, Release -O3);
#   2. TRAIN: play a short batch of games for each <deck-dir> at that deck's MULLIGAN-GEN settings
#      (value_play.mull_gen_depth / mull_gen_budget_ms, else play depth/budget), with its profile
#      and value sidecar -- the same search the keep-gen rollouts run;
#   3. reconfigure with -fprofile-use -fprofile-partial-training + LTO, rebuild, copy the two
#      binaries to build/PGO/ and stamp build/PGO/SRC_TREE with `git rev-parse HEAD:src`.
# Code the training never executed is still optimised normally (-fprofile-partial-training), so a
# deck that was not in the training set runs correctly -- just without the PGO gain.
#
# The stamp is what makes the binary safe to use: scripts/mullgen.sh (MTG_GEN_PGO=1) refuses a
# build/PGO binary whose SRC_TREE does not match the current src tree, so a stale PGO binary can
# never run a generation on an engine other than the checked-out one.
#
# Env: PGO_GAMES (default 48 per deck), PGO_THREADS (default: nproc).
set -euo pipefail
cd "$(dirname "$0")/.."

if [ "$#" -lt 1 ]; then
  echo "build.sh pgo: name at least one deck directory to train on, e.g." >&2
  echo "  ./build.sh pgo decks/SelesnyaLifegain" >&2
  exit 2
fi
case "$(uname -s)" in
  Linux|Darwin) ;;
  *) echo "build.sh pgo: Linux/macOS (GCC/Clang) only -- use ./build.sh on Windows" >&2; exit 2 ;;
esac
for d in "$@"; do
  [ -d "$d" ] || { echo "build.sh pgo: not a directory: $d" >&2; exit 2; }
done
if [ -n "$(git status --porcelain --untracked-files=no -- src 2>/dev/null)" ]; then
  echo "build.sh pgo: src/ has uncommitted changes -- commit first (the SRC_TREE stamp must name" >&2
  echo "  the exact source the binary was built from)" >&2
  exit 2
fi

TREE=build/pgo-work   # NOT build/pgo: /workspaces is case-INSENSITIVE, so that path IS build/PGO
PROF="$PWD/$TREE/profdata"
OUTDIR=build/PGO
jobs="${PGO_THREADS:-$(nproc 2>/dev/null || echo 4)}"
games="${PGO_GAMES:-48}"

echo ">>> pgo 1/3: instrumented build -> $TREE/"
rm -rf "$PROF"; mkdir -p "$PROF"
cmake -S . -B "$TREE" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
      "-DCMAKE_CXX_FLAGS=-fprofile-generate=$PROF -fprofile-update=atomic" >/dev/null
cmake --build "$TREE" -j"$jobs" --target mtg mtg-analyze

echo ">>> pgo 2/3: training ($games games per deck, at each deck's mulligan-gen settings)"
for d in "$@"; do
  deck=$(ls "$d"/*.cod "$d"/*.txt 2>/dev/null | head -1 || true)
  [ -n "$deck" ] || { echo "build.sh pgo: no .cod/.txt decklist in $d" >&2; exit 2; }
  stem=$(basename "${deck%.*}")
  prof="$d/$stem.profile.json"
  read -r depth budget < <(python3 - "$d/$stem.value.json" <<'PY'
import json, sys
try:
    vp = json.load(open(sys.argv[1])).get("value_play", {})
except Exception:
    vp = {}
d = vp.get("mull_gen_depth") or vp.get("target_depth") or 5
b = vp.get("mull_gen_budget_ms") or vp.get("budget_ms") or 20
print(d, b)
PY
)
  args=("$deck" --cards-json src/cards/data/cards.json --games "$games" --seed 7001
        --depth "$depth" --budget-ms "$budget" --max-turns 8 --ignore-play-profile --threads "$jobs")
  [ -f "$prof" ] && args+=(--profile "$prof")
  echo "    $d  (d$depth/b$budget)"
  "$TREE/mtg" "${args[@]}" >/dev/null
done

echo ">>> pgo 3/3: profile-use + LTO build"
cmake -S . -B "$TREE" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
      "-DCMAKE_CXX_FLAGS=-fprofile-use=$PROF -fprofile-partial-training -Wno-missing-profile" >/dev/null
cmake --build "$TREE" -j"$jobs" --target mtg mtg-analyze

mkdir -p "$OUTDIR"
cp -f "$TREE/mtg" "$TREE/mtg-analyze" "$OUTDIR/"
git rev-parse HEAD:src > "$OUTDIR/SRC_TREE"
printf '%s\n' "$@" > "$OUTDIR/TRAINED_ON"
echo ">>> done: $OUTDIR/  (PGO+LTO, Release -O3; src tree $(cat "$OUTDIR/SRC_TREE"))"
