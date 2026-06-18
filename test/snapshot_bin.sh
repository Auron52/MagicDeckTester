#!/usr/bin/env bash
# Snapshot the built binary into a labeled, provenance-stamped artifact under
# logs/snapshots/, so it can be run later without rebuilding and is traceable to the
# exact version it was built from.
#
#   test/snapshot_bin.sh <label>     ->  logs/snapshots/<label>  (+ <label>.meta)
#
# The .meta records the git short hash, clean/dirty state, build date, and source path.
# Use for A/B testing: snapshot each arm (e.g. baseline vs a change, or two commits)
# and run the COPIES. Benefits over rebuild-per-arm:
#   - no ETXTBSY / mid-run binary swaps (the source binary is free to be rebuilt),
#   - both arms persist, so a run can be re-examined or re-run later,
#   - every result is traceable to an exact (hash, dirty) version via the .meta.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 1

label=${1:?usage: test/snapshot_bin.sh <label>}
BIN=./build/Release/mtg.exe
[ -f "$BIN" ] || BIN=./build/Release/mtg
[ -f "$BIN" ] || { echo "ERROR: no binary at $BIN; build first (cmake --build build --config Release)" >&2; exit 1; }

OUT=logs/snapshots
mkdir -p "$OUT"
cp -f "$BIN" "$OUT/$label"

hash=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
state=$(git diff --quiet HEAD 2>/dev/null && echo clean || echo dirty)

# Reproducibility for UNCOMMITTED changes: a bare "dirty" flag can't be reconstructed,
# so when dirty, save the full working-tree diff (staged + unstaged vs HEAD) next to the
# binary and stamp its sha. The exact source is then recoverable as: checkout git_hash,
# then `git apply <label>.diff`. Untracked source files are NOT in the diff, so list them
# as a warning -- if any exist, the snapshot may not be fully reconstructable.
diff_sha="-"
if [ "$state" = "dirty" ]; then
  git diff HEAD > "$OUT/$label.diff" 2>/dev/null || true
  diff_sha=$(sha1sum "$OUT/$label.diff" 2>/dev/null | cut -c1-12)
fi
untracked=$(git ls-files --others --exclude-standard -- '*.cpp' '*.h' 2>/dev/null | tr '\n' ' ')

{
  echo "label=$label"
  echo "git_hash=$hash"
  echo "git_state=$state"
  echo "diff_sha=$diff_sha"
  [ "$state" = "dirty" ] && echo "diff=$label.diff"
  [ -n "$untracked" ] && echo "untracked_sources=$untracked"
  echo "built=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "source=$BIN"
} > "$OUT/$label.meta"

echo "snapshot: $OUT/$label  ($hash, $state${diff_sha:+, diff:$diff_sha})"
[ -n "$untracked" ] && echo "  WARNING: untracked source files present, not captured by the diff: $untracked" >&2
