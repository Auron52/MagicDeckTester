#!/bin/bash
# usage: one.sh <bin> <outdir> <seed> <gi> <depth> <budget> [ENV=V ...]  -- one game, logs
bin=$1; out=$2; seed=$3; gi=$4; d=$5; b=$6; shift 6
mkdir -p "$out"; cd /workspaces/MagicDeckTester/.claude/worktrees/maze-f5r
/usr/bin/time -f "wall %e user %U maxrss %M" env MTG_RSS_CAP_GB=20 MTG_PGO=0 "$@" "$bin" "decks/Maze's End/Maze's End.cod" --profile "decks/Maze's End/Maze's End.profile.json" --ignore-play-profile --seed $seed --game-index $gi --games 1 --depth $d --budget-ms $b --threads 1 --log-dir "$out" > "$out/out.txt" 2> "$out/err.txt"
echo "exit $?" > "$out/DONE"
