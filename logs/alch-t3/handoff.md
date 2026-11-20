# alch-t3 handoff (CONDENSED by agent 7, 2026-11-20 ~18:00 UTC)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/alch-t3`, branch `alch-t3-1119`. History of agents 1-6 lives in
git (`git log -p logs/alch-t3/handoff.md`) and, condensed, in `docs/design/aa-staff-in-hand-loop-cast-draw.md`
(section "The holes behind T4 at every budget"). Scratch: `/workspaces/MagicDeckTester/build/scratch/alch-t3/`.

## USER rulings (verbatim, binding)
- "I would like to know why we can't find it at any budget." / "we should improve the heuristic and the search itself." / "the heuristic is our answer to this working at a lower budget, but the search should be capable at a high budget."
- On the Crab Umbra host: "Why do we need branching here? Just have a heuristic specific to the deck and we are done. Obviously in this deck Accomplished Alchemist is the target."
- On BurstPendingActColour: "Yes, we need to fix this."
- CLAUDE.md: NO GREEDY PICK INSIDE THE SEARCH WINDOW; a budget may re-price work but may not make a line unfindable;
  adoption bar = every slower game recovers at `--depth 8 --budget-ms 0`, every deck's aggregate net <= 0 on a large
  held-out sample ("+1 is not neutral. 0 is neutral.").
- Brief (parent): subagents NEVER push the shared branch; push ONLY `backup/2026-11-20/alch-t3`. Commit named paths;
  messages end "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>". Heavy jobs under
  `flock /workspaces/MagicDeckTester/build/scratch/BOX.lock`; builds only via ./build.sh; no timeouts; waiters on PID /
  DONE markers. 250K-context handoff rule.

## Commits (REBASED onto origin/phase-1-2-deck-analyzer at ~17:45 UTC; nothing on the shared branch)
- `95e3b4842` (agent 7, UNBUILT) site-9-after-deferred replay segment fix + scenario (below).
- `fa34fc204` MTG_DEV_SEQ_HEXIT_TAINT (rebase resolution: upstream's `g_devseq_hard_n` sites kept, `DvsNotePassLatent`
  added to every latent record incl. upstream's new canon-drop record ~TurnSolver.cpp:74512 and the gamelc `gt >= 1` sites).
- `c4bf31cc5` tap-last, `7ca408e56` host rule / s9d / burst, `20deaeab0`, `fe5ebbaf9` float, `da449177a` loop-cast-draw.
- HeuristicArm.h conflicts resolved by union (upstream slots first, ours appended).
- Pre-rebase backup pushed: `origin/backup/2026-11-20/alch-t3-prerebase` (= old cc656a83f).
- `docs/design/aa-staff-in-hand-loop-cast-draw.md`: new section appended (UNCOMMITTED until this handoff commit).

## Lever set (all default ON) and ab4 (pre-rebase, d5 b20, 61001/63001 x1000, 200 chunks)
| lever | ab4 delta | t | wall |
|---|---|---|---|
| MTG_AURA_LATE_BIND_RANK (+ provider MTG_AA_UMBRA_HOST) | -0.0665 | -8.89 | -25.1% |
| MTG_SITE9_AFTER_DEFERRED | -0.0010 | -0.63 | -3.2% |
| MTG_BURST_ACT_COLOUR_PENDING | 0.0000 | - | -0.3% |
| prior (loop-cast-draw + float) | -0.0075 | -2.83 | |
| MTG_AA_ALCH_TAP_LAST | -0.0100 | -3.64 | -5.1% |
| MTG_DEV_SEQ_HEXIT_TAINT | -0.0010 | -1.42 | |
| all | -0.0880 | -10.99 | -31.7% |
Acceptance (57b56d62c build): seed 12345681 g0 keep 7, heur OFF, d8 b0 **T3**; d5 b20 heur OFF T3, heur ON T3.

## ROOT CAUSE of the 3 AA games slower at d8 b0 (agent 7) -- an executor/rollout LOCKSTEP break, not a search loss
Games: smoke d3 gi12 (seed 1013) 6->7, reg d5_s3003 gi40 (3043) 5->6, gi87 (3090) 4->5. In all three the T1 search
COMMITS a line whose winning turn holds MTG_SITE9_AFTER_DEFERRED continuations, and the replay does not realise it
(MTG_FD_TRACE: gi87 `[fd-pred] turn=4 opp_life=-10`, realised opp 4; traces in build/scratch/alch-t3/a7/{t87,f87,g40,g12}*).
- Node: deferred continuation of a draw breakpoint (gi87 T4: Stroke X=1 draws Staff, site-3 continuation casts it), then
  two s9d continuations [Staff untap-Alchemist loop, Ezuri pump] and [Staff loop, Ezuri, Ezuri].
- The rollout applies each continuation's activations at its end; all records go into ONE sink list. `replay_recorded`
  (AIEngine.cpp) pooled every activation into one trailing pass after all casts; the provider activation sort ran both
  Staff loops before all pumps -> Alchemist left tapped, Ezuri attacked alone, T5. gi40/gi12: a Stroke recorded after a
  segment's activations was cast before them. gi12 needs tap-last too only because tap-last makes that line the pick.
- FIX (95e3b4842): `Action::rec_s9d_seg` (shared_ptr<const PlanTraits>, TurnSolver.h) stamped by the rollout's s9d loop
  (TurnSolver.cpp, s9d block in the deferred loop) on each segment's top-level records; `replay_recorded` closes a segment
  at the boundary (cast-less section opened as the rollout did, section + line closed, activations dispatched under the
  segment's traits via `rec_flush_acts`). No-commit twin (resolve_draw_breakpoint): passes > 0 carry pending activations
  from pass-0's `_cont_traits` (`s9d_outer_traits`). Unstamped records unchanged (only s9d lines can move).
- Scenario `test/scenarios/aa_s9d_replay_segments_staff_ezuri_t4.json` (gi87 T4 board, Sprawl GREEN): pre-fix binary
  (copy at build/scratch/alch-t3/a7/mtg.old) T5 with the identical committed line; s9d=0 T5. Expect T4 after the fix.
  Its _comment's "Alchemist + Ezuri attack" is an inference -- check the realised T4 log once built.
- NOT deleted: the fix is a lockstep correction; decide deletion only if chain7 shows the 3 games still slower.

## Running jobs (both mine; queued behind p18j-int chain3 AB2 batch (~75/260 jobs at 17:45) and provreg build_units)
- **chain7**: `flock BOX.lock bash build/scratch/alch-t3/a7/chain7.sh`, flock PID in `a7/chain7.pid` (1785453).
  Does: ./build.sh (rebased tree) -> units -> verify batch `a7/verify_manifest.json` (32 non-AA movers at tier settings
  `__new`, 3 AA games d8b0 `__d8b0_new` vs `__d8b0_s9d0`, 4 AA tier games) -> acceptance runs (acc_all / play_off /
  play_heur) -> all `aa_*` scenarios -> gate: each of gi12/gi40/gi87 new <= s9d0 -> held-out A/B via multi_ab.py
  (on = all 7 levers 1 incl. MTG_AA_LOOP_CAST_DRAW, off = all 0; 13 decks, 37 searched cells incl. d5 profile cells,
  500 games/arm/cell, chunk 10, seed-off 910000, nproc threads) -> `a7/ab7/` (manifest, batch.stdout/stderr) and
  report `a7/chain7/ab7_report.txt`. Status `a7/chain7/status.txt`; **DONE marker `a7/chain7/DONE`** = OK /
  BAR_FAILED / BUILD_FAILED. Check the `[batch] heartbeat` in `a7/ab7/batch.stderr` within 10 min of "A/B start".
  Estimated A/B ~1.5 h on a free box.
- **tiers7**: `a7/tiers7.sh` (PID 1785886, unlocked wait): when chain7/DONE == OK takes BOX.lock and runs
  `env -C <wt> bash test/regression.sh --both` -> `a7/tiers7/both.txt`, DONE marker `a7/tiers7/DONE`
  (SKIPPED if chain7 not OK).
- Time window: the run touches 13:30-21:00 UTC; no CronCreate tool was available to agent 7 -- parent should set the
  2-hourly session-only timer.

## multi_ab.py (task 2 done)
`build/scratch/alch-t3/multi_ab.py`: d5 cells without a depth key now INCLUDED (skip only explicit depth<=0 /
budget_ms<=0); cell key includes starting_life/opponent_heads (2HG separate); `--on/--off` lever sets; exact deck dir
names; streams batch output to files; `--report <dir>` re-prints per-cell and per-deck tables.

## Remaining, in order
1. When chain7/DONE lands: read status.txt (units, verify, scenarios incl. the new one, acceptance T3) and
   ab7_report.txt. If BAR_FAILED: root-cause further or DELETE MTG_SITE9_AFTER_DEFERRED and its code (no default-off flag).
2. A/B table per moving deck (delta, t, better/worse, wall) -> every deck net <= 0, else root-cause that deck.
3. tiers7: per-game audit of every SLOWER searched game at d8 b0 (audit6b manifest shape; tier-setting job specs in
   test/logs/<tier>/manifest.json, game seed = case seed + gi). Accept GT in the worktree only when all recover
   (`env -C <wt> bash test/regression.sh --both --accept` promotes tiers7's run; do not re-run), then
   `python3 test/check_gt_logs.py`.
4. Fill "Measured" in docs/design/devseq-hexit-taint.md (chain6/iwv/ab4 numbers in this file + ab7/tiers7); finish the
   aa doc section; commit named paths; push ONLY backup/2026-11-20/alch-t3.

## Open USER questions (with numbers)
- MTG_AA_ALCH_TAP_LAST adoption (provider tap ORDER): ab4 -0.0100 t=-3.64 (22/4 chunks), wall -5.1%; acceptance needs
  it (T4 without). Parent default: ADOPT.
- MTG_SITE9_AFTER_DEFERRED: ab4 -0.0010 t=-0.63, wall -3.2%; 11 non-AA tier games faster, 1 Snow slower (recovers d8 b0);
  not needed for seed 12345681. Keep if ab7 + the d8b0 audit are clean after the replay fix.
- MTG_BURST_ACT_COLOUR_PENDING: ab4 0.0000; kept per USER ("Yes, we need to fix this.").
- MTG_DEV_SEQ_HEXIT_TAINT: soundness fix; ab4 -0.0010 t=-1.42; verify sample inwin DISAGREE 3 MATERIAL 0.

## Key measurements (pre-rebase)
- chain6: units 3723/3723, combo_off 51/51; acc_all T3, acc_notaint T4, nos9d/noburst T3, notlast/norank/nofloat T4.
- tiers6 (57b56d62c): 349 pass / 19 FAIL vs stale GT; AA cells all faster (smoke d5 4.94->4.84, reg d5_s3003
  5.0400->4.9371 ...). Non-AA movers all s9d (attr6/out.txt).

## UPDATE 19:04 UTC (agent 7, after handoff): chain7 PASSED the gate; A/B RUNNING (started 19:03:37)
- Rebased build rc=0; units 3784/3784; acceptance acc_all / play_off / play_heur all **T3**; all 26 `aa_*` scenarios
  PASS incl. the new `aa_s9d_replay_segments_staff_ezuri_t4` (T4).
- d8 b0: gi12 6 (=s9d0 6, was 7), gi40 5 (=5, was 6), gi87 4 (=4, was 5) -> **all three recover** with the fix.
- AA tier settings: gi12 6 (old GT 6), gi40 5 (5), gi87 4 (4), gi143 **7** (old 6; recovered 6 at d8 b0 in audit6).
- 32 non-AA movers, tier settings, rebased binary (`chain7/verify.out` vs `attr6/out.txt`): none slower than s9d0;
  still faster: goblins2hg gi17, goblins gi3, martyr d3/d5 gi5, mazesend gi166/gi179/smoke gi55, melira gi110/gi126;
  newly faster mazesend smoke gi127 5->4; no longer faster cyclone2hg gi10 (6), cyclone d5_s3003 gi125 (5);
  Snow smoke gi81 now 6 (was the one slower game). Rebase confounds the comparison (upstream changes).
- NEXT AGENT: check the `[batch] heartbeat` in `a7/ab7/batch.stderr` (first one due ~19:13 UTC; none yet at 19:04),
  then wait on `a7/chain7/DONE`; tiers7 follows automatically (`a7/tiers7/DONE`).
