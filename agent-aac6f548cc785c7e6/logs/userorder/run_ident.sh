#!/usr/bin/env bash
# Byte-identity: the starting commit's binary vs this tree with MTG_DRAGONSTORM_USER_ORDER=0, same manifest.
U=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/logs/userorder
NEW=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/build/Release/mtg
bash "$U/run_batch.sh" "$U/mtg.base" "$U/ident_base"
bash "$U/run_batch.sh" "$NEW" "$U/ident_off" MTG_DRAGONSTORM_USER_ORDER=0
grep -o '^[^:]*: played=[0-9]* avg=[0-9.]* digest=[0-9a-f]*' "$U/ident_base/batch.out" | sort > "$U/ident_base/digests.txt"
grep -o '^[^:]*: played=[0-9]* avg=[0-9.]* digest=[0-9a-f]*' "$U/ident_off/batch.out" | sort > "$U/ident_off/digests.txt"
if cmp -s "$U/ident_base/digests.txt" "$U/ident_off/digests.txt"; then
  echo "IDENTICAL: $(wc -l < "$U/ident_base/digests.txt") jobs"
else
  echo "DIFFER"; diff "$U/ident_base/digests.txt" "$U/ident_off/digests.txt" | head -20
fi
