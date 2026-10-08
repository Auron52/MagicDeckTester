#!/usr/bin/env bash
# Profile-guided + link-time-optimised build of mtg / mtg-analyze -> build/PGO/.
#
# Invoked as `./build.sh pgo` (trained on the WHOLE regression suite -- the default, and the binary
# every long run uses) or `./build.sh pgo <deck-dir>...` (trained on just those decks). Never needed
# directly. Linux/macOS GCC or Clang only.
#
# PGO+LTO IS THE ENGINE FOR EVERY LONG RUN (user, 2026-10-08: "we should be using PGO+LTO for everything
# except maybe quick development cycles. It would also be good for screening changes."). The one place
# that decides is test/lib/harness.sh: harness_bin / harness_analyze_bin return build/PGO whenever it is
# FRESH (its SRC_TREE stamp == HEAD:src and src/ is clean), and harness_pgo_ensure builds it first for
# the long runners (mullgen.sh, valueleaf.sh, deck_compare.py, the overnight tier). A dirty or not yet
# rebuilt tree -- a development cycle -- falls back to build/Release with no build delay. MTG_PGO=0
# pins Release everywhere (do that on BOTH arms of any wall-clock comparison).
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
#   2. TRAIN in ONE pooled --batch (scripts/pgo_train_manifest.py): with no deck named, every SMOKE case
#      at ~1/10 of its games in the suite's own job shape, plus each deck at its MULLIGAN-GEN settings;
#      with decks named, PGO_GAMES games of each at its gen settings;
#   3. reconfigure with -fprofile-use -fprofile-partial-training + LTO, rebuild, install the two
#      binaries into build/PGO/ and stamp build/PGO/SRC_TREE with `git rev-parse HEAD:src`.
# Code the training never executed is still optimised normally (-fprofile-partial-training), so a
# deck that was not in the training set runs correctly -- just without the PGO gain.
#
# The stamp is what makes the binary safe to use: harness.sh trusts a build/PGO binary only while its
# SRC_TREE matches the current src tree, so a stale PGO binary can never run on an engine other than
# the checked-out one.
#
# Env: PGO_GAMES (named decks: games per deck, default 48), PGO_FRACTION (suite: share of each smoke
# case's games, default 0.1), PGO_THREADS (default: nproc).
set -euo pipefail
cd "$(dirname "$0")/.."

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

echo ">>> pgo 1/3: instrumented build -> $TREE/"
rm -rf "$PROF"; mkdir -p "$PROF"
cmake -S . -B "$TREE" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
      "-DCMAKE_CXX_FLAGS=-fprofile-generate=$PROF -fprofile-update=atomic" >/dev/null
cmake --build "$TREE" -j"$jobs" --target mtg mtg-analyze

echo ">>> pgo 2/3: training, one pooled batch (${*:-the whole regression suite})"
MAN="$TREE/train.manifest.json"
python3 scripts/pgo_train_manifest.py "$MAN" "$@"
"$TREE/mtg" --batch "$MAN" --threads "$jobs" > "$TREE/train.log" 2> "$TREE/train.err" || {
  echo "build.sh pgo: the training batch failed -- see $TREE/train.err" >&2
  tail -5 "$TREE/train.err" >&2
  exit 1
}

echo ">>> pgo 3/3: profile-use + LTO build"
cmake -S . -B "$TREE" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
      "-DCMAKE_CXX_FLAGS=-fprofile-use=$PROF -fprofile-partial-training -Wno-missing-profile" >/dev/null
cmake --build "$TREE" -j"$jobs" --target mtg mtg-analyze

# Install ATOMICALLY (copy beside, then rename): a long run may be executing the previous build/PGO
# binary right now, and a rename leaves its inode untouched. The stamp is removed first and written
# LAST, so a reader can never pair a new stamp with an old binary or the reverse.
mkdir -p "$OUTDIR"
rm -f "$OUTDIR/SRC_TREE"
for b in mtg mtg-analyze; do cp -f "$TREE/$b" "$OUTDIR/.$b.new"; mv -f "$OUTDIR/.$b.new" "$OUTDIR/$b"; done
if [ "$#" -gt 0 ]; then printf '%s\n' "$@" > "$OUTDIR/TRAINED_ON"; else echo suite > "$OUTDIR/TRAINED_ON"; fi
git rev-parse HEAD:src > "$OUTDIR/SRC_TREE"
echo ">>> done: $OUTDIR/  (PGO+LTO, Release -O3, trained on $(tr '\n' ' ' < "$OUTDIR/TRAINED_ON"); src tree $(cat "$OUTDIR/SRC_TREE"))"
