#!/usr/bin/env bash
# The final pooled run: the NEW binary's manifest (held-out + control + identity-new), then the 3d64d9a6
# binary's identity side, then the analysis. Both through vbatch.sh (lowprio, 6 threads).
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
R=$WT/logs/rs_crackle
S=/workspaces/MagicDeckTester/build/scratch/agent-a97fc601a3891aba8
python3 "$R/make_final.py" new "$S/fin_new"
python3 "$R/make_final.py" old "$S/fin_old"
bash "$R/vbatch.sh" "$S/mtg_final" "$S/fin_new" 6
bash "$R/vbatch.sh" "$S/mtg_3d64d9a6" "$S/fin_old" 6
python3 "$R/make_final.py" analyse "$S/fin_new" "$S/fin_old" > "$S/fin_analyse.txt" 2>&1
cp "$S/fin_analyse.txt" "$R/fin_analyse.txt"
echo done
