#!/usr/bin/env bash
# One round after a code change: after-audit, byte-identity (lever off vs base), sanity A/B -- sequential,
# each a single pooled 4-thread batch.
U=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/logs/userorder
NEW=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/build/Release/mtg
tag="$1"
mkdir -p "$U/audit_$tag" "$U/ident_$tag" "$U/ab_$tag"
cp "$U/audit_before/manifest.json" "$U/audit_$tag/manifest.json"
bash "$U/run_batch.sh" "$NEW" "$U/audit_$tag" MTG_USER_ORDER_TRACE=1
python3 "$U/audit.py" "$U/audit_$tag/batch.err" > "$U/audit_$tag/audit.txt"
cp "$U/ident_base/manifest.json" "$U/ident_$tag/manifest.json"
bash "$U/run_batch.sh" "$NEW" "$U/ident_$tag" MTG_DRAGONSTORM_USER_ORDER=0
grep -o '^[^:]*: played=[0-9]* avg=[0-9.]* digest=[0-9a-f]*' "$U/ident_$tag/batch.out" | sort > "$U/ident_$tag/digests.txt"
if cmp -s "$U/ident_base/digests.txt" "$U/ident_$tag/digests.txt"; then
  echo "IDENTICAL: $(wc -l < "$U/ident_base/digests.txt") jobs" | tee "$U/ident_$tag/verdict.txt"
else
  { echo "DIFFER"; diff "$U/ident_base/digests.txt" "$U/ident_$tag/digests.txt" | head -20; } | tee "$U/ident_$tag/verdict.txt"
fi
python3 "$U/make_ab.py" "$U/ab_$tag"
bash "$U/run_batch.sh" "$NEW" "$U/ab_$tag"
python3 "$U/make_ab.py" --analyse "$U/ab_$tag/batch.err" | tee "$U/ab_$tag/ab.txt"
