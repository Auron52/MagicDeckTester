#!/usr/bin/env bash
# Final: full unit suite + byte-identity (lever off vs base) + lever-on digests equal to round r6's.
U=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/logs/userorder
NEW=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/build/Release/mtg
bash "$U/unit.sh" > "$U/unit_final.log" 2>&1
grep -E "test cases|Status" "$U/unit_final.log"
mkdir -p "$U/ident_final" "$U/ab_final"
cp "$U/ident_base/manifest.json" "$U/ident_final/manifest.json"
bash "$U/run_batch.sh" "$NEW" "$U/ident_final" MTG_DRAGONSTORM_USER_ORDER=0 > /dev/null
grep -o '^[^:]*: played=[0-9]* avg=[0-9.]* digest=[0-9a-f]*' "$U/ident_final/batch.out" | sort > "$U/ident_final/digests.txt"
if cmp -s "$U/ident_base/digests.txt" "$U/ident_final/digests.txt"; then echo "IDENT lever=0 vs base: IDENTICAL ($(wc -l < "$U/ident_base/digests.txt") jobs)"; else echo "IDENT: DIFFER"; fi
cp "$U/ab_r6/manifest.json" "$U/ab_final/manifest.json"
bash "$U/run_batch.sh" "$NEW" "$U/ab_final" > /dev/null
grep -o '^[^:]*: played=[0-9]* avg=[0-9.]* digest=[0-9a-f]*' "$U/ab_r6/batch.out" | sort > "$U/ab_r6/digests.txt"
grep -o '^[^:]*: played=[0-9]* avg=[0-9.]* digest=[0-9a-f]*' "$U/ab_final/batch.out" | sort > "$U/ab_final/digests.txt"
if cmp -s "$U/ab_r6/digests.txt" "$U/ab_final/digests.txt"; then echo "A/B digests final == r6: IDENTICAL"; else echo "A/B digests: DIFFER"; fi
grep -E "^d[03]_(on|off)" "$U/ab_final/batch.out" | awk '{print $1, $4}' | head -0
