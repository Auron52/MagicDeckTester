#!/usr/bin/env bash
# run_audit.sh <bin> <tag> -- the USER-order audit (d0 300 / d3b10 200 / ship 200, seeds 7000001+) on <bin>
U=/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/logs/userorder
bin="$1"; tag="$2"
mkdir -p "$U/audit_$tag"
cp "$U/audit_before/manifest.json" "$U/audit_$tag/manifest.json"
bash "$U/run_batch.sh" "$bin" "$U/audit_$tag" MTG_USER_ORDER_TRACE=1 > /dev/null
python3 "$U/audit.py" "$U/audit_$tag/batch.err" > "$U/audit_$tag/audit.txt"
grep -E "^setting|Dragonstorm not last|Irencrag|unpaid|leftover|spin" "$U/audit_$tag/audit.txt" | cut -c1-140
