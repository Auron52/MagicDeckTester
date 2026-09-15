#!/usr/bin/env bash
# In-game callgrind (Ir, deterministic) for one config on build/Profile/mtg -- see
# docs/design/perf-pass-2026-09-11.md "Method". Usage: bash scripts/perf/cgg_run.sh <tag> <outdir>
set -u
tag=$1; out=$2; mkdir -p "$out"
MTG_MEM_BUDGET_MB=4096 MTG_BATCH_HEARTBEAT=0 valgrind --tool=callgrind --cache-sim=no --branch-sim=no \
  --toggle-collect='GameEngine::RunGame*' --callgrind-out-file="$out/callgrind.$tag.out" \
  build/Profile/mtg --batch "scripts/perf/cgg_$tag.json" --threads 1 > "$out/$tag.stdout" 2> "$out/$tag.stderr"
echo "$tag rc=$?"
callgrind_annotate --inclusive=no "$out/callgrind.$tag.out" 2>/dev/null | head -60 > "$out/$tag.flat.txt"
callgrind_annotate --inclusive=yes "$out/callgrind.$tag.out" 2>/dev/null | head -60 > "$out/$tag.incl.txt"
grep -E "^\s*[0-9,]+ .*PROGRAM TOTALS|totals:" "$out/$tag.flat.txt" | head -2
