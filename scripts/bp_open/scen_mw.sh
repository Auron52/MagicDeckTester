#!/usr/bin/env bash
# The two Mirrorwing demoted-wave fixtures, lever on and =0, on a pinned binary. Usage: scen_mw.sh <bin-tag>
set -u
S=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9469f45bff622cfb/logs/bp_open/scen.sh
T=$1
for f in mirrorwing_bp_demoted_anger_before_oracle mirrorwing_bp_demoted_fists_before_goldrush; do
  echo "== $f (on)";  bash "$S" "$T" "test/scenarios/$f.json"
  echo "== $f (MTG_BP_NEW_ONLY_DEMOTE=0)"; bash "$S" "$T" "test/scenarios/$f.json" MTG_BP_NEW_ONLY_DEMOTE=0
  echo "== $f (MTG_BP_NEW_ONLY=0)"; bash "$S" "$T" "test/scenarios/$f.json" MTG_BP_NEW_ONLY=0
done
