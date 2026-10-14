#!/usr/bin/env bash
# Concatenate several batch dirs' outputs into one dir for make_ab.py analyse. Usage: combine.sh <out> <dir>...
set -u
OUT=$1; shift
mkdir -p "$OUT"
: > "$OUT/batch.out"; : > "$OUT/batch.err"
for D in "$@"; do
  cat "$D/batch.out" >> "$OUT/batch.out"
  cat "$D/batch.err" >> "$OUT/batch.err"
done
cp "$1/seeds.json" "$OUT/seeds.json"
cp "$1/manifest.json" "$OUT/manifest.json"
echo "combined $# dirs into $OUT"
