#!/usr/bin/env bash
# Write the rebased A/B's tables into docs/design/bp-open-data/ (vs off, vs nodemote = F1-F4, vs on = all five).
set -u
W=/workspaces/MagicDeckTester/.claude/worktrees/agent-a9469f45bff622cfb
D=/workspaces/MagicDeckTester/build/scratch/bp_open/ab_r2
O=$W/docs/design/bp-open-data
python3 "$W/scripts/bp_open/make_ab.py" analyse "$D" --base nodemote > "$O/ab_r2_vs_nodemote.txt"
python3 "$W/scripts/bp_open/make_ab.py" analyse "$D" --base on > "$O/ab_r2_vs_on.txt"
python3 "$W/scripts/bp_open/make_ab.py" analyse "$D" --base off > "$O/ab_r2_vs_off.txt"
B=/workspaces/MagicDeckTester/build/scratch/bp_open
{ for a in off nodemote on; do echo "== arm $a: r2 (16dfb7e4 + levers) vs v5 (77e094a2 + levers), same seeds"; python3 "$W/scripts/bp_open/cmp_bases.py" "$a" "$B/ab_r2" "$B/ab" "$B/fu"; done; } > "$O/rebase_vs_v5.txt"
echo "--- onnl vs on (NEW_LAST's effect with F5 on)"; grep " onnl " "$O/ab_r2_vs_on.txt"
echo "--- nl14 vs nodemote (NEW_LAST's effect with F1-F4)"; grep " nl14 " "$O/ab_r2_vs_nodemote.txt"
