#!/usr/bin/env bash
# Byte-identity: both hold levers =0 (env path) on the new binary vs 2cec6ec4 (mtg_fix1), Hinata; and the
# shipped defaults vs 2cec6ec4 on three decks with no Crackle (the new hook is false there).
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
R=$WT/logs/rs_crackle
bash "$R/run_batch.sh" "$R/mtg_fix1"  "$R/hold_ident_base" 2 &
bash "$R/run_batch.sh" "$R/mtg_hold1" "$R/hold_ident_new"  2 MTG_HINATA_HOLD_LETHAL_BURN=0 MTG_HINATA_HOLD_NEEDS_HINATA=0 &
wait
python3 "$R/make_hold.py" ident "$R/hold_ident_base" "$R/hold_ident_new"
mkdir -p "$R/other_hold"
cp "$R/other_fix/manifest.json" "$R/other_hold/manifest.json"
bash "$R/run_batch.sh" "$R/mtg_hold1" "$R/other_hold" 2
grep "played=" "$R/other_hold/batch.out" | sort > "$R/other_hold/d.txt"
paste -d'|' "$R/other_fix/d.txt" "$R/other_hold/d.txt" | awk -F'|' '{split($1,a," "); split($2,b," "); print a[1], a[4], b[4], (a[4]==b[4]?"SAME":"DIFF")}'
