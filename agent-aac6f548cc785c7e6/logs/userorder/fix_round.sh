#!/usr/bin/env bash
# fix_round.sh <tag> <LEVER> <prev-bin> <seed_base> [deck keys...]
#  1. byte-identity: prev binary (all defaults) vs new binary with LEVER=0, the ident manifest
#  2. paired sanity A/B for LEVER on the listed decks (default ds)
U=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/logs/userorder
NEW_SRC=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/build/Release/mtg
tag="$1"; lever="$2"; prev="$3"; base="$4"; shift 4
mkdir -p "$U/fx_$tag/prev" "$U/fx_$tag/off" "$U/fx_$tag/ab"
NEW="$U/fx_$tag/mtg"; cp "$NEW_SRC" "$NEW"
cp "${IDENT:-$U/ident_base}/manifest.json" "$U/fx_$tag/prev/manifest.json"
cp "${IDENT:-$U/ident_base}/manifest.json" "$U/fx_$tag/off/manifest.json"
bash "$U/run_batch.sh" "$prev" "$U/fx_$tag/prev" > /dev/null
bash "$U/run_batch.sh" "$NEW" "$U/fx_$tag/off" "$lever=0" > /dev/null
grep -o '^[^:]*: played=[0-9]* avg=[0-9.]* digest=[0-9a-f]*' "$U/fx_$tag/prev/batch.out" | sort > "$U/fx_$tag/prev/digests.txt"
grep -o '^[^:]*: played=[0-9]* avg=[0-9.]* digest=[0-9a-f]*' "$U/fx_$tag/off/batch.out" | sort > "$U/fx_$tag/off/digests.txt"
if cmp -s "$U/fx_$tag/prev/digests.txt" "$U/fx_$tag/off/digests.txt"; then
  echo "IDENT $lever=0 vs prev: IDENTICAL ($(wc -l < "$U/fx_$tag/prev/digests.txt") jobs)" | tee "$U/fx_$tag/ident.txt"
else
  { echo "IDENT $lever=0 vs prev: DIFFER"; diff "$U/fx_$tag/prev/digests.txt" "$U/fx_$tag/off/digests.txt" | head; } | tee "$U/fx_$tag/ident.txt"
fi
python3 "$U/make_ab2.py" "$U/fx_$tag/ab" "$lever" "$base" "$@"
bash "$U/run_batch.sh" "$NEW" "$U/fx_$tag/ab" > /dev/null
python3 "$U/make_ab2.py" --analyse "$U/fx_$tag/ab/batch.err" | tee "$U/fx_$tag/ab.txt"
