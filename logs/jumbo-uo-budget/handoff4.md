# Handoff 4: Jumbo unlock-order lever CPU work, written for an agent (Claude or Codex) with no context

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/agent-a1cd888d17514cedb`, branch `wip/jumbo-uo-budget`.
Push ONLY this wip branch; never push `phase-1-2-deck-analyzer` (the parent session lands it). Scratch
`S=/workspaces/MagicDeckTester/build/scratch/claude_jumbo_uo_budget`. Read `handoff3.md` (same dir) first: it has
the background, the USER rulings verbatim, and every measurement before this handoff. Main design doc:
`docs/design/jumbo-apply-drop-order-2026-11-18.md` (a section "Exact cuts, 2026-11-21/22" is still owed).

## Box rules (USER)
ONE big engine process at a time under `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock` with
`MTG_RSS_CAP_GB=40`; small work through `scripts/box_lane.sh --slots N --cap-gb G -- <cmd>` with N <= 2. Build only
with `./build.sh`, ~200 s after a src edit (9p clock lag). No `timeout`, no `pgrep -f` / `pkill -f` waiters (wait on a
PID with `kill -0` or on a DONE file). Rebase, never merge. Never touch `references/` except to commit.

## State (2026-11-22 ~14:45 UTC)
- xm3 result (see handoff3 "xm3 RESULT"): normalising eval / equiv_ord / cont_pins brings back no hits. Some other
  Action field differs on every action-mismatched hit (Jumbo UOT act_diff 36,968, all `other_after_norm`).
  Bruna's 78 UOT diffs are all hand_index.
- Added a census of EVERY Action member (`uotx::CensusAll`, X-macro `UOTX_FIELDS` in `src/ai/TurnSolver.cpp`,
  diagnostic only, printed as `[uot-xmemo] site=... all-field diffs:` at exit).
- Running: lane job xm4 (`$S/xm4/chain.sh`: build, then Jumbo 400 `$S/prof/on.json` with both sites + VERIFY).
  DONE marker `$S/xm4/DONE`; read `grep uot-xmemo $S/xm4/jver/batch.err`.

## Next steps
1. Read xm4's all-field line and name the differing field(s). Prove the unlock-order tree / SPS walk never reads
   them (or key on them), normalise them in `uotx::WalkSame`, and confirm the hits return to about 73,780 with
   instructions near 338G.
2. Make the winner DEFAULT ON (`EnvOn("MTG_UOT_XMEMO", true)`), rewrite its comment as a shipped exact cut, and drop
   the SPS site if it does not pay.
3. Run the identity slice under the box lock (`logs/jumbo-uo-budget/mkid.py out.json 300 2000 733000`: every suite deck
   300 games at play settings, Jumbo 2000). Run one process with the memo on and one with `MTG_UOT_XMEMO=0`, in
   sequence, with nproc threads and `MTG_PGO=0`. Each game's played= line must match once ms/cpu are stripped.
4. Run `bash test/regression.sh --both` under the lock. No GT change is expected. Then run `python3 test/check_gt_logs.py`.
5. Write the doc section, commit, and push wip/jumbo-uo-budget.
