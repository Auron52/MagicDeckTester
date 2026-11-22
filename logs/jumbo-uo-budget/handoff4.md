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

## State (2026-11-22 15:40 UTC)
- xm4 (`$S/xm4`): the field census found the unidentified field. It is **tutor_target**, in 36,886 of 36,968 Jumbo
  UOT diffs (GSZ / Finale / Tooth and Nail variants of the same subset). Within the walk cluster the field is read only
  for the Fireblast, Archive Trap and Walk-the-Aeons-buyback pins (SeqWalkFixedOrder) and in TutoredUntapperHostId.
  Every other reader is plan-level; the function map is in this session's log, so grep `tutor_target` to check it.
  Folded under `spsx::TutorWalkSame`.
- xm5 (`$S/xm5`, d7366d723): VERIFY 0 mismatches on both sites and both decks. Play is byte-identical on every arm.
  Hits: UOT 73,698, SPS 179,064. Instructions (Jumbo, 400 games, 2 threads, `perf stat instructions:u`):
  | arm | lever on | lever off |
  |---|---|---|
  | no memo | 348.97G | 303.45G |
  | UOT site | 338.41G | -- |
  | SPS site | **329.63G** | **293.86G** |
  | both sites | 330.53G | -- |

  Bruna: no memo 837.75G, UOT site 836.74G, both sites 837.50G. Most of Bruna's SPS act-diffs are hand_index, which
  stays strict because the walk reads it.
- **a27e75565**: shipped as `MTG_SPS_XMEMO` DEFAULT ON (`SpsCrossEnumMemo`, namespace `spsx`). This is the SPS site
  alone. The UOT-site prototype and `MTG_UOT_XMEMO` / `MTG_XMEMO_NORM` were removed. Diagnostics print only under
  `MTG_SPS_XMEMO_VERIFY=1`. Memory is bounded at 32 MB of stored Actions per thread.
- Running: detached chain `$S/id1/chain.sh` (setsid; log `$S/id1/chain.log`). It sleeps 200 s, builds in the lane
  (`$S/id1/build.log`) and copies the binary to `$S/mtg.id1`. Then, under the box lock (`$S/id1/big.sh`, nproc threads,
  MTG_PGO=0, RSS 40), it runs:
  - identity slice `on` (memo on) and `off` (MTG_SPS_XMEMO=0) over `$S/id1/id.json`: 60 suite decks x 300 games plus
    Jumbo 2000, gi 733000+;
  - `ver` (VERIFY) over `$S/id1/ver.json`: 100 games per deck, Jumbo 400, gi 736000+;
  - `bash test/regression.sh --both` (log `$S/id1/regr.log`, marker `$S/id1/REGR_DONE`);
  - `test/check_gt_logs.py` (`$S/id1/checkgt.log`).

  Per-arm markers: `$S/id1/<arm>/DONE`. Final marker: `$S/id1/DONE` (or "buildfail").

## UPDATE 2026-11-22 23:00 UTC -- the identity slice is NOT byte-identical
- The box lock never admitted big.sh's slice (5.5 h of queueing), so a reduced slice ran in the lane:
  `$S/id1/idl.json` (100 games per deck, Jumbo 1000, gi 733000+), arms `lon` (memo on) and `loff` (MTG_SPS_XMEMO=0).
  59 decks are identical. **Extra Turns** has 4 jobs where units differ and 1 where the digest differs (gi 733038 and
  733048 play different lines with the same win turn). **Hinata** has 2 jobs where only units differ.
  `big.sh` was cut down to regression `--both` plus check_gt; it started 22:20 UTC under the lock, built at a27e75565
  with the memo ON. `lver` (VERIFY over `verl.json`) is still running in the lane.
- Repro: `$S/et1/m.json` (Extra Turns gi 733038, 1 game). ON gives 824a46aa and OFF gives faaf957a; it diverges at
  trace turn index 58 (Horn of Greed + land vs land + Nexus of Fate). **VERIFY gives 824a46aa with 0 hits**, so the
  verdicts are not the cause. A SIDE EFFECT of the memo wrapper on a miss changes play.
  Suspect: stored Action copies keep shared_ptr members alive (cont_pins / rec_payment_traits / rec_line_hold /
  breakpoint_casts). That changes allocation addresses, which would matter to any ADDRESS-keyed cache elsewhere
  (paymemo::Ctx keys plan contexts by address, for one). It cannot be index carry, which is off by default.
- Bisect running: a TEMP uncommitted flag `MTG_SPSX_EXP` (1 = no key, 2 = key only, 3 = store with the shared_ptr
  members cleared), binary `$S/mtg.exp`, outputs `$S/et1/e{0,1,2,3}/g/*.wins`, marker `$S/et1/EXPDONE`. Remove the
  flag after the bisect.

## Next steps
1. When `$S/id1/on/DONE` and `off/DONE` exist, diff the arms with
   `sed 's/ ms=[0-9]*//;s/ cpu=[0-9]*//' batch.out | grep played= | sort` on each. The result must be identical
   (digest per 25-game job). Then `grep sps-xmemo $S/id1/ver/batch.err` must show VERIFY_MISMATCH=0.
2. regression `--both` must show no GT change; nothing to accept. check_gt_logs rc=0.
3. Add the doc section "Exact cuts, 2026-11-21/22" to `docs/design/jumbo-apply-drop-order-2026-11-18.md`. It should
   cover: the memo revert (0 hits), replay refuted, the perf split, the census, the xmemo numbers above, the
   tutor_target finding, and the shipped cut. Then commit and push wip/jumbo-uo-budget.
4. Report to the coordinator. The price variants all fail the bar (USER decision, not settled). The lever's overhead
   on Jumbo falls from +15.0% to about +8.6% vs today's off (329.6 vs 303.5). Off itself also gets 3.2% cheaper, so
   on-with-memo is +12.2% over off-with-memo.
