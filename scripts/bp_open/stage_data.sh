#!/usr/bin/env bash
# Copy the A/B's inputs (this worktree's card data + the decks it plays, minus the *.raw.json.gz the engine never
# reads in play) onto the build volume ONCE, so a batch does not re-read 10 MB keep tables over 9p per job
# (build/scratch/boxlock/RULES.txt item 9). Usage: stage_data.sh  -> build/scratch/bp_open/data/
set -u
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9469f45bff622cfb
D=/workspaces/MagicDeckTester/build/scratch/bp_open/data
mkdir -p "$D/src/cards/data" "$D/decks"
cp -f "$W/src/cards/data/cards.json" "$D/src/cards/data/cards.json"
for deck in "Hinata2" "Creature Giving" "Fluctuator" "Melira Pod" "Mirrorwing Dragon" "Prevent Damage" "Pirates" \
            "treasure_hunt" "Bruna" "Auras" "CritterLifegain" "Dragonstorm" "Goblins"; do
  mkdir -p "$D/decks/$deck"
  for f in "$W/decks/$deck"/*; do
    [ -f "$f" ] || continue
    case "$f" in *.raw.json.gz) continue ;; esac
    cp -f "$f" "$D/decks/$deck/"
  done
done
git -C "$W" rev-parse HEAD > "$D/SOURCE_HEAD"
du -sh "$D"
