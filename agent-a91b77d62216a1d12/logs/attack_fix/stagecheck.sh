#!/usr/bin/env bash
# stagecheck.sh -- -fsyntax-only every changed TU of each intermediate commit tree (stages A..G; H is
# the final tree, fully built). Each stage = a copy of the worktree's src/ + test/unit/ with that
# stage's files overlaid. Real compile flags (from compile_commands.json), lowprio, one TU at a time.
set -u
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a91b77d62216a1d12
S=/workspaces/MagicDeckTester/build/scratch/agent-a91b77d62216a1d12
ST=$S/stages
B=$W/build
out=$S/stagecheck.log
: > "$out"
for st in A B C D E F G; do
  R=$S/stagesrc/$st
  mkdir -p "$R/test"
  cp -r "$W/src" "$R/"
  cp -r "$W/test/unit" "$R/test/"
  rm -f "$R/test/unit/test_attack_projection.cpp"
  (cd "$ST/$st" && find . -type f) | while read -r f; do cp "$ST/$st/$f" "$R/$f"; done
  for tu in src/ai/TurnSolver.cpp src/ai/DecisionProviders.cpp src/ai/LandPlay.cpp test/unit/test_bruna_sweep.cpp test/unit/test_attack_projection.cpp; do
    [ -f "$R/$tu" ] || continue
    bash /workspaces/MagicDeckTester/build/scratch/lowprio.sh /usr/bin/c++ -DMTG_HAVE_ZLIB '-DCMAKE_INTDIR="Release"' \
      -I"$R/src" -I"$B/generated" -I"$B/_deps/pugixml-src/src" -I"$B/_deps/nlohmann_json-src/include" \
      -I"$B/_deps/doctest-src" -std=gnu++20 -fsyntax-only "$R/$tu" >> "$out" 2>&1
    echo "stage $st $tu exit=$?" >> "$out"
  done
done
echo ALLDONE >> "$out"
