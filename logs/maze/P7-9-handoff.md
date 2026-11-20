# P7-9 handoff (Maze's End P7c + P7d + census split + (O) + gi961 fix), 2026-11-20 ~20:30 UTC

Worktree `.claude/worktrees/maze-p7`, branch **`wip/maze-p7`**. Code tip **3ca5e9f1e** (pushed). Base is
origin/phase-1-2-deck-analyzer 8fbbc5023 (batch 1). This handoff commit sits on top of it. Scratch:
`build/scratch/maze/p7g/` (tools in `tools/`). **Binary for the tip: `p7g/mtg.p7t`** (== build/Release/mtg at 3ca5e9f1e).

## Done
- Everything in `P7-8-handoff.md` (P7c (a)(b)(c), the P7d fold, the soundness fix, the census split 78ecc9d6f,
  (O) 539bd4f6c).
- **Snow gi961 ROOT-CAUSED and FIXED: a650abea4 + 3ca5e9f1e** (`MTG_LASTPLY_PROBE_MEASURE`, default ON, heurarm
  slot LASTPLY_PROBE_MEASURE). Snow's shape is a leafless probe ladder plus one FIT rollout pass. While
  `g_probe_recording` is set, the probe is a MEASUREMENT: FIT prices its rollout pass as tree(d) + R x leaves(d)
  from `g_probe_cost` / `g_probe_leaves`. A folded depth-1 node answers before its depth-0 children, so the
  recorded leaves at d3 on T1 fell 184 -> 0 (tree 390 -> 111). FIT then admitted a d3 rollout pass priced at
  111 units. On T2 and T3 that pass ran to the 25x overrun ceiling (225k units). On T3 the overrun broke every
  remaining plan's breakpoint-node loop (`budget->Overrun()` at the top of the k loop) and returned instantly
  from every tail (FSLineWin's entry overrun check). That is what "plans skip their breakpoint continuations"
  was. The anytime rescue committed T7, where base's affordable d1 FIT pass finds Coldsteel Heart -> T6.
  b100/b400 recover because the mispriced pass then fits. The proofs were sound. The fold changed the PRICE of
  a pass that can never fold. Fix: the fold does not fire while g_probe_recording (same reason and gate as
  MTG_PROBE_INCLUSIVE_CUT). Trace tool: `MTG_P16_TRACE=1 MTG_P12_FIT_TRACE=1` (`p7g/g961t/`, `p7g/g961u/`).
  Fixed build on gi961: fold on T6, P16 pass trace byte-identical to fold off; lever =0 reproduces T7.
- Units on 3ca5e9f1e: PASS (`p7g/units6.log`). **Windows CI on 3ca5e9f1e (run 44772616463): SUCCESS.**
- chain5 (the old tiers on the pre-fix binary) was KILLED by P7-8 at smoke start, because src changed. Tiers were
  re-run as chain6 on mtg.p7t.

## Tier results on 3ca5e9f1e (VPC_SKIP=1, MTG_PGO=0): `p7g/smoke6.log`, `p7g/reg6.log`; runs in test/logs/{smoke,regression}
- **Smoke:** 233 pass, 2 FAIL, only Maze's End: `mazesend_smoke_d5_s1001` 5.6742 -> 5.6591 (better),
  `mazesend2hg_smoke_d5_s1001` 5.3333 unchanged (digest only). Audit: searched slower=0, faster=2,
  play-changed=5.
- **Regression:** 373 pass, 2 FAIL, only Maze's End: `mazesend_regression_d5_s2002` 5.5598 -> 5.5543 (better),
  `mazesend_regression_d5_s3003` 5.5867 -> 5.5933 (one game worse). Audit: searched slower=1, faster=1,
  play-changed=15. No other deck moved (EDF/Snow keys all PASS).
- **The one slower game:** `mazesend_regression_d5_s3003 gi14`, 6 -> 7 (repro
  `--seed 3017 --game-index 14 --budget-ms 20`; reproduces T7 on mtg.p7t). It is NOT from the fold, the
  certificate or (O): fold0 / cert0 / ord0 all give 7, and the pre-fix mtg.p7r gives 7. It is from **P7c (a),
  `MTG_MAZE_CASTDOM_WINDOWS`**. With win0 the game is T6; pp0 and ka0 give 7. Attribution runs: `p7g/d8r/att_*.out`.
  NOTE: an earlier d8 b0 run WITHOUT `--game-index 14` (`p7g/d8r/gi14.out`, T7) is NOT the suite game. Ignore it.

## RUNNING (started by P7-8)
- **d8 b0 recovery of gi14**: PID 2959538 (`mtg.p7t ... --seed 3017 --game-index 14 --depth 8 --budget-ms 0
  --ignore-play-profile --threads 1`), output `p7g/d8r/gi14b.out`. It is done when that file has a line `rc=`.
  Read its `avg (turns)`. The bar is <= 6.
- **VPC subset**: heavy_run PID 2957079 (`MTG_BIN=p7g/mtg.p7t python3 test/viewer_protocol_check.py --strict --only
  Maze_s_End --threads 32`), output `p7g/vpc6.log`. It is done when its last line is `rc=`.
- Waiter task **b1lgq0l0l** (P7-8's session) waits on both of the above and prints their tails. It is not needed
  after this handoff; P7-9 should arm its own waiter on the two `rc=` lines.

## Remaining for batch 2, in order
1. **gi14 d8 b0 verdict.** If T6: the slowdown is budget churn of P7c (a) and the bar is met. If T7: P7c (a)'s
   window rule loses a line, so root-cause it (MazesEndProvider::CastDominanceKeep window conditions) before
   accepting. The fix, or the question, goes to the coordinator.
2. **Accept GT, scoped to Maze's End only** (smoke and regression both moved only mazesend keys):
   `bash test/regression.sh --smoke --deck=mazesend,mazesend2hg --accept-with-regressions="<one line>"`, then
   the same with no mode flag for regression (`--deck=mazesend,mazesend2hg`). The test/logs runs are from mtg.p7t.
   Do not re-run first. Diff `test/regression_gt.txt` (expect only mazesend keys + the note line), then run
   `python3 test/check_gt_logs.py`. Commit the GT.
3. Report the VPC line as "VPC: full corpus held; subset strict (Maze_s_End) = <vpc6 result>".
4. Push wip/maze-p7 and watch CI (`gh run watch <id> --interval 60 --exit-status`). Then READY FOR BATCH 2.
5. Optional / follow-up, NOT done: `EchoEdgeFoldHere` (TurnSolver.cpp ~82248) also fires on
   `FsNextLeafConstant` with no `g_probe_recording` exclusion, so it may misprice FIT the same way. Not ours;
   flag it to the coordinator. (O) is still sound but inert (see measurements); a mana-aware reshuffle bound is
   the next step if more Summit proofs are wanted.

## Measurements
- **ab11** (fixed build, `p7g/ab11/`, compared by `tools/cmp11.py` against ab10's fold-off arm; the fold-off arm is
  untouched by the fix). Every cell has the same digest on every game, units 1.000x, net 0:
  EDF d5b20 1000/1000, d3b10 1000/1000, 2HG d5b20 300/300, d3b0 40/40, d5b0 40/40; Snow d5b20 1000/1000,
  d3b10 1000/1000 (incl. gi961), d3b0 6/6, d5b0 6/6. Maze's End b0 d5 20/20 identical to ab10 'ord'. Maze's End
  d5b20 50/50 identical with the lever on and off (the fix removes no fold from Maze's End's play). So **EDF and
  Snow: net 0 at every cell, std and 2HG** (the fold is now fully inert on both; all their leafless passes are
  recording probes).
- ab10 (pre-fix): EDF all net 0 (PASS); Snow d3b10 net +1 (gi961). Superseded by ab11.
- **cen11** (EDF + Snow census, 1,601 games: d5b20 / d3b10 + Snow gi961; pre-fix binary): **UNSOUND=0**; PROOF
  19,433 nodes (3.8%), units 549,278 (3.2%), won=0; OTHER 492,049 nodes, won 1,737.
- **(O) library-order Summit bound** (539bd4f6c): sound (UNSOUND=0) but inert. `[maze-order] calls=55628 proof=0`;
  with ~8-10 entry sources every Maze's End fetch is a reshuffle, so the reachable tops cover the whole library.
- Summit split on 30042 gi1 d5 b0: CODE-5 (Manabond only in the library, count open) 64.5% of nodes / 67.0% of
  units; CODE-1/-4/-6/-7 = 0 ("too many sources" binds at every Summit decline).
- Maze's End fold identity (ab10): b0 d5 20/20 and d8 10/10 identical digests cert0 vs shipped; units d5 -2.7%,
  d8 -8.6%.

## USER rulings (verbatim)
- 2026-11-20 (Guild Summit / 30042): "Don't we already run clairvoyantly or are you talking about in a heuristic?"
  Coordinator reading: the search already plays the known library order, so a bound reading the same order is a
  SOUND fold relative to the search's information. Build it default ON, and fall back where the node's library is
  hidden or shuffled.
- "I suppose P7's prune premise is fine." (dominance-fold premise SIGNED OFF)
- "3-8 We should address this at some point or we won't be able to do the value-leaf. I'll leave the timing to you.
  3-9 This means we probably need to work more on performance to start which fits with 3-8. We'll need more effective
  prunes to be applied. Higher budget is not out of the realm of consideration, but only after we have exhausted other
  avenues."
- "It would be nice if there was no extra branching given how easy the mana is in this list."
- "What I mean by pruning fetches is about the set of targets. Not whether to use it or not." / "Though, if there is
  idle mana we should use it." / "And that is fine, because not fetching would be dominated, but this would be
  assuming we can replay it."
- Coordinator: a sound fold is default ON; only an unprovable/heuristic version goes to the USER. Single-game 1-thread
  probes must NOT hold build/heavy.lock. VPC full-corpus HOLD (Galadriel s10 / F5R, owned elsewhere): tiers run with
  VPC_SKIP=1, then the Maze_s_End strict gate separately (`--only Maze_s_End,<moved decks>`).
- Bar (CLAUDE.md): every slower game must recover at --depth 8 --budget-ms 0; per-deck net <= 0, std and 2HG
  co-equal ("+1 is not neutral. 0 is neutral.").
- Box rules: heavy work via `bash /workspaces/MagicDeckTester/scripts/heavy_run.sh <one stage>`; MTG_BUILD_JOBS=12;
  `MTG_RSS_CAP_GB=20`; waiters on PID/markers (never pgrep -f); push wip/* only; rebase never merge; no timeouts;
  per-job manifest flags must be heurarm slots.
