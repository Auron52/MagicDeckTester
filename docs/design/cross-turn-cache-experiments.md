# Cross-turn cache experiments (checkpoint, started 2026-11-10)

USER 2026-11-10: *"let's start experimenting on ideas to utilize this cross-turn caching more effectively."*
Background and the 2026-11-10 re-measurement of the shipped game line cache: `cross-decision-tree-reuse.md`.

Worktree `.claude/worktrees/xturn`, branch `xturn-cache` (local), based on `927c83502` (src `de17f5f35`).
Scratch: `build/scratch/xturn/<exp>/` (primary checkout's build volume).

## Experiments

| id | lever (per-job, default OFF) | idea | status |
|---|---|---|---|
| E1 | `MTG_ID_HINT_GAME` | keep the previous decision's `MTG_ID_ORDER_HINT` map; a node with no hint in its own decision reads it. Fills subtrees the game line cache answered in a warm-up pass (nothing walked, nothing recorded) and passes an escalation skip never ran. Ordering only: win turns cannot change below the root. | **measured**: units -0.3% budgeted / -0.7% backstop, win turns identical in all 244 jobs (45 tie-line changes), base arm byte-identical to stock. prev-decision hints: +14% hinted nodes (35k on 253k). Small, sound. |
| E2 | `MTG_INWIN_CACHE` (+ `_VERIFY`, `_MB`) | DEPTH-FREE in-window proofs: a completed node's win at w <= T+depth-1, and its no-win bound clipped to T+depth-1, hold at any remaining depth. Read across ID passes AND across decisions. Exact if the in-window menu does not depend on remaining depth -- verify mode checks that. | verify v1 FOUND A HOLE (below), fixed; verify v2: 12,960 hits re-searched, **0 MATERIAL**. A/B: units -0.5% budgeted / -0.4% backstop, win turns identical everywhere, 2 of 160 budgeted jobs changed line; best MidrangeBurn b0 -16.7%, EDF b0 -9.0%, Alchemist -3.5%. Small, sound. |

## E2 findings

* **The no-win half needed a tighter read rule.** First verify: FiveColour 2HG, turn 5, depth 1, cutoff 6 -- a
  depth-2 node's "no win by 6" against a depth-1 node whose ROLLOUT won on turn 6. A proof covers the SEARCH MENU's
  lines only, and the rollout plays lines a provider prunes from the menu. So a no-win answers only a query whose
  cutoff lies inside the query's own window: `cutoff <= min(bound, T+depth-1)`.
* **Win hits are ZERO** (76,500 stores, 0 hits): an interior in-window win becomes the root's verified win, which
  ends the decision, and the next decision replays the committed line. Only no-win proofs recur (17,987 hits).

## E3 probe: reusing rollout CONTINUATIONS (`MTG_ROLLOUT_SUFFIX_PROBE`, read-only)

Only a rollout's START key reaches the TT; the states it passes at later turn boundaries are thrown away. When the
window grows by a turn, a rollout start X becomes an interior node and X's greedy child -- that rollout's first
boundary -- becomes a new leaf. The probe files every boundary key with the rollout's (result, cutoff) and counts
later outermost rollouts that start on one. 244 searched regression jobs, 1.72M rollouts, 85.0M rollout units of
313.4M total:

| match kind | matches | usable for the query's cutoff | agree with fresh | units of usable |
|---|---|---|---|---|
| start key, same decision | 76 | 13 | 13 | ~0 (the TT already answers these) |
| start key, earlier decision | 42,190 | 37,725 | 37,686 (99.90%) | 2.69M |
| suffix key, same decision | **0** | 0 | 0 | 0 |
| suffix key, earlier decision | 75,663 | 69,156 | 69,123 (99.95%) | 3.13M |

* Ceiling of a game-persistent start+suffix rollout cache: **5.8M units = 6.8% of rollout units, 1.9% of all
  units.** Not exact: ~0.05-0.1% of reused values differ from a fresh rollout (path-dependent rollouts -- cause not
  yet found), so it would be a play change needing an A/B, not a sound collapse.
* **Zero same-decision suffix overlap** is the surprise: within a decision no rollout starts where an earlier one
  passed. Consistent with most decisions running one pass at the target depth (single-depth / escalation skip),
  so there is no "next pass" inside a decision for a continuation to serve. Not yet verified directly.
* Probe is read-only: digests equal the stock run except Extra Turns d5 s2002, which is the pool-nondeterministic
  job (a THIRD distinct digest in four runs).

## E3 disagreement census (rebased on origin `33a08dc74`, src `3e2535d5`)

Rerun with a split by cutoff: 68 disagreements on ~107k usable matches, ALL from an earlier decision, all at rollout
depth 1 / horizon 8. 51 at the SAME cutoff (49 stored wins), 17 at a different one. So cutoff dependence (the
kill-only last turn) is the minority; the same start state at the same cutoff plays differently in a later decision.

## Found on the way: the plan caches serve budget-state-dependent entries (2026-11-10)

Extra Turns d5 s2068 gi66 replayed alone: default plan pool 57,526 units, `MTG_PLAN_CACHE_KB=1` 56,198, same digest.
Every turn's fs root decision differs by exactly 249 units. `MTG_BP_ENUM_VERIFY=3`: **389 bp hits re-derived,
CONTENT-DIFF 118** (e.g. a 2-plan Time Warp list served where a fresh derivation returns 0), context diff
`dec_ovr+ fresh_axis-` (the decision budget had overrun at the hit, not at the store). 118 = the gap in charging
computations between the two pools (175 vs 57). Mechanism: `cachecharge` mode 1 treats a computation that charged no
unit as state-free, but a derivation can READ `Overrun()`/`Exhausted()` without charging -- "no" at store, "yes" at
the hit. Served lists then drive extra downstream search, and how often they are served depends on what the shared
pool holds, i.e. on other threads in a pool: the suspected cause of the Extra Turns s2002 run-to-run digests.

Fix (`MTG_CACHE_READ_GUARD`, default ON, worktree xturn): `budgetbind::t_reads` counts predicate evaluations;
`Cost::budget_read`; a budget-reading computation that saw a bind is unstorable; a budget-reading entry is served
(and the priced-ledger shadow's `WouldServe` agrees, flag `kBudgetRead`) only while the decision and greedy budgets
read no cut. Hypothesis to test: it also removes the E3 same-cutoff disagreements.

**The real read was `decisionwork::PricedExceeded()`**, not Overrun/Exhausted: `MTG_BP_DERIVE_PRICED_STOP` (default ON
since 11-04) interrupts a derivation at its next subset once the decision's priced ceiling trips (fresh = 0 plans),
while a hit served the full list, so the doomed pass kept searching until its next rollback poll (gi66: 13.7M refused
subset visits, so the "never trips in play" note on that flag is stale). `t_reads` now lives in DecisionWorkMeter.h
and an ARMED `PricedExceeded()` counts; hit tests use `*Quiet()` variants. Result on gi66: units default = 1 KB pool =
4 GB pool = **56,198** (was 57,526 / 56,198 / 57,526), `MTG_BP_ENUM_VERIFY=3` **CONTENT-DIFF 0** on 217 hits (was
118/389). Binary `build/scratch/xturn/mtg-rg2.bin`.

**Chain result (13:10-13:54 UTC, `rg/`):** guard ON at the default pool vs `MTG_PLAN_CACHE_KB=1`, 244 regression
jobs: **digests 244/244 identical, units 243/244 identical**. The one residual is `hinata_regression_d5_s3003` gi27
(unwon, wt=-1): 510,771 vs 510,772 units, same digest -- a second, much smaller residency dependence, OPEN (single-game
replays at both pool sizes in `build/scratch/xturn/hin27/`). The `off` arm (guard-off A/B) was skipped: the guard is a
defect fix, and the regression GT accept shows how play moves.

**LANDED 2026-11-10** (commit "search: MTG_CACHE_READ_GUARD ..." + its "gt: accept" commit, on origin phase-1-2-deck-analyzer): guard ON, E1 + E2 default ON,
E3 probe kept default OFF, flip-fresh diagnostic stripped, MTG_BP_DERIVE_PRICED_STOP comment corrected. Unit tests
2699/2699. Smoke 156/183, regression 245/291 -- every move avg-identical except one churn game each (hinata smoke d3
gi46, elementals regression d5 s2002 gi121; both recover at higher budget). GT accepted.

Residency check of the landed binary (regression manifest at `MTG_PLAN_CACHE_KB=1`, `build/scratch/xturn/inv/`):
digests 291/291 identical. Units: hinata d5 s3003 +1 (OPEN, below) and four Extra Turns jobs -- which turned out to be
the PROBE, not the plan pool: `MTG_ROLLOUT_SUFFIX_PROBE=1` moves Extra Turns d3 s2002 961,979 -> 961,601 units at
EITHER pool size (same digest). Without the probe, Extra Turns is pool-invariant with E1/E2 on or off. So the probe is
not unit-neutral (cause not found; its comment now says so), and its earlier "Extra Turns d5 s2002 digest differs"
was likely the probe plus the plan-cache hole together.

**E3 after the guard:** same-cutoff disagreements are NOT removed (DIS[samecut][win] 46 win / 2 no-win, was 49/2;
same_cutoff usable 93,307, agree 93,259). They are a separate rollout path dependence -- still to root-cause before
E3 can become a real cache.

**OPEN: the 1-unit Hinata residual.** hinata_regression_d5_s3003 gi27 (seed 3030, unwon, wt=-1): 510,771 units at
the default pool vs 510,772 at 1 KB, single-threaded, same digest, with E1/E2 on or off. `MTG_BP_ENUM_VERIFY=3`: 342,601
hits, CONTENT-DIFF 0, so not the bp list. Charge-accumulator residuals are already unstorable (cachecharge Done). Next:
bisect by cache (canon front / enum memo / shadow ledger) with each one's own verify switch.

## 2026-11-11 session (USER: "Go ahead and try them all out")

Worktree `.claude/worktrees/xturn`, branch `xturn-cache` rebased on origin `d4bfb2071`. Scratch `build/scratch/xturn/`
(`x4/` chain, `hin27/` gi27 repro, `pr/` pristine check). Pristine origin build: `.claude/worktrees/xturn-pristine`.

* **Probe NOT unit-neutral -- FIXED.** Cause: `BuildSimKeyBase` bills `workctr::kSimKey` to the PRICED meter the priced
  decision ceiling reads (`MTG_DECISION_PRICED_X`, default 100), so the probe's extra boundary keys moved the ceiling.
  The probe now mutes the meter (and restores the combat-pin read witness) around its own keys. Regression tier with
  the probe on vs off: **295/295 digests and units identical**.
* **E4 (`MTG_ID_HINT_ANYDEPTH`, hint from any recorded depth) -- REJECTED, code removed.** Regression A/B: win turns
  identical in all 295 jobs, 15 changed line, units +0.01% overall (Extra Turns +2.2%, nothing better than -0.5%).
* **Item "carry FSLineCache past-cutoff / provisional entries across decisions" -- nothing sound left.** The game line
  cache already carries a past-cutoff win as a no-win refutation under its cutoff (the only sound reading: a win past
  the cutoff is a line the node met, not its best). Provisional entries are budget-spent dead ends of ONE pass
  (`ProvisionalLive`, keyed on `g_prov_epoch`): serving them elsewhere is exactly the budget-state dependence
  `MTG_CACHE_READ_GUARD` removes.
* **E3 disagreements, root cause so far.** Probe now records whether the FRESH rollout was clean and each side's
  leaf/beam/m2/labeller context. Regression: 48 same-cutoff disagreements in 95,293 usable; **24 have an unclean fresh
  side** (budget artifact). The 24 clean ones are NivMizzetCombo (21) and Shadowborn (13 incl. d3); contexts equal on
  both sides; unchanged with E1, E2, both, the game line cache, or the mana cache off. Same-decision matches agree
  13/13. So: within-decision tie-path dependence of the rollout's inner depth-1 solves (sound memos returning one of
  several tied lines). E3 as a cache is therefore a play change, not an identity -- built as `MTG_ROLLOUT_GAME_CACHE`
  (default OFF, heurarm slot) for an A/B.
* **The 1-unit Hinata residual -- FIXED (`MTG_MUTE_NO_STORE`, default ON).** It went through the PRICED ceiling
  (with `MTG_DECISION_PRICED_X=0` both pools gave 1,786,203 units). Mechanism: where the plan pool cannot hold an
  entry, the priced ledger (pledger) mutes the recompute so its work counts as the unlimited cache's free hit -- but
  the muted recompute still STORED into the game-persistent payment-refine memo (ManaOracle.cpp `t_refine_memo`),
  whose later hits skip `RefinePaymentImpl` and with it priced payment work. So the priced meter, the priced ceiling
  and the units followed residency. Found by tracing the priced meter at every unit charge (`MTG_PRICED_TRACE_UNITS`,
  `[pt]`/`[pe]` lines) to one bp derivation that cost 241,859 ns at the default pool and 176,013 ns at 1 KB, then
  `MTG_PAY_PROBE_MEMO=0 MTG_PAY_REFINE_MEMO=0` (both pools 510,270). Rule now in core/WorkCounters.h: a muted
  recompute stands in for a hit that never ran, so it stores nothing (`workctr::StoresAllowed()`): refine memo and
  mana cache (incl. tier-2 promotion). gi27 now 510,771 at both pools; regression tier: 0 digest and 0 unit changes at
  the default pool (it only matters where a pool is starved).
* **Gods d3/d5 s2002 residency -- FIXED (provider enumeration context).** `GodsProvider::LoamTargetSearchCandidates`
  narrows Life from the Loam's target fan using the SEARCH ROOT (Magus of the Bazaar there, its graveyard land
  count), the probe mode and DecisionUnpruned -- none in any key. Breakpoint lists live for the game, so a list
  narrowed under one root (299 target sets) was served under another (fresh: 1 set): `MTG_BP_ENUM_VERIFY=3`
  CONTENT-DIFF, the state swap re-derived 1 on the stored state too (a global input), mana cache / Loam-probe fold
  ruled out. Fix: `DecisionProvider::EnumerationContextKey` (default 0), folded by BuildBreakpointKey when nonzero;
  Gods returns the exact branch inputs, only while a Loam-class card is in hand. gi39: both pools 33,587 units, same
  digest, verify CONTENT-DIFF 0 of 9,199. (Also kept: a fold of "inside a Loam probe", a smaller real hole.)
  The mana-clean agent had parked this as a determinism defect (docs/design/mana-clean-pass-checkpoint.md).
* **E3 (`MTG_ROLLOUT_GAME_CACHE`) A/B.** Regression seeds (295 jobs, pooled): 32 jobs changed line, 12 moved avg
  win turn -- 10 faster, 2 slower, net -24 game-turns (NivMizzetCombo, Shadowborn, Gods, Bruna, Hinata, Cyclone);
  units +0.56%, thread-ms +0.47%; 206k hits. HELD-OUT seeds 4004/5005 (324 jobs, pooled): 16 changed line, net -5.4
  game-turns (Niv -4, Niv 2HG -1, Hinata 2HG -1); the one "+1" is a budget-0 backstop row where E3 FINISHED a game the
  base abandoned at its 2M-unit ceiling (3 played vs 2) -- no slower game. Units +0.7%, thread-ms -3.4% (contended).
  Default OFF pending the USER's call.
* **E3 ADOPTED, default ON (USER 2026-11-11: "Okay, let's try it out").** Unit tests needed per-arm
  `ClearPerGameCaches()` (test_leaf_kill_only, test_brainstorm): each arm is a fresh game, and E3 otherwise answered a
  later arm from an earlier one. Tiers vs the fixes-only GT: smoke 11 faster / 3 slower, regression 26 faster / 4
  slower; 6 of the 7 slower recover at 4x/16x budget; shadowborn d5 s3003 gi135 (T4->T7) persists, but E3 OFF also plays
  T7 at 16x and at d8 b0 (same digest) -- the T4 was a truncated-search line. Units on the regression tier:
  same-play budgeted +0.13%, budget-0 rows -1.7%, changed-play +1.9%. Residency with E3 on: the whole regression
  manifest at `MTG_PLAN_CACHE_KB=1` equals the default pool on digest and units.
  **Gods is the one deck net slower:** regression seeds +3 game-turns (d3 +2/+2, d5 -1), held-out 4004/5005 0 over 200,
  11011..14014 +1 over 800 (d3 -1/+2, d5 identical play). And Gods d5 plays IDENTICALLY with E3 yet spends +3..+11%
  units (+6.5% over the 800 games).
* **Gods unit cost ROOT-CAUSED; the gate-price fix (`MTG_RGC_GATE_PRICE`) was REJECTED and deleted.** `MTG_ROLLOUT_STATS` on Gods d5 h13013
  (identical play both arms): id_pass aborted 2 -> 4, wasted units 15,982 -> 241,558 (0.7% -> 9.6%), rollout calls
  71k -> 89k -- the whole +258k. The iterative-deepening start gates project the next pass as c_prev x growth, and an E3
  hit is free, so a pass E3 answered looks cheap; the gate starts a deeper pass whose rollouts are mostly NEW states,
  it overruns and is rolled back. Fix: each E3 entry records what its rollout (or, for a boundary key, its suffix) cost;
  a served hit adds that to `rgc::t_served`, and both start gates add the pass's served delta to c_prev. Gate-only:
  charging, the budget and the priced meter are unchanged. Held-out 4004/5005, all decks, 3 arms pooled (228 jobs):
  E3 on -6.0 game-turns / units +0.83%; E3 + gate price -6.0 game-turns (same decks: Niv -4, Niv2HG -1, Hinata2HG -1)
  / units -0.16%; gate price moves 4 job digests vs E3 alone; Gods units +1.8% -> -3.0%, Gods2HG +0.7% -> -3.5%.
  **Rejected on the tiers (x9, vs the E3-on GT):** smoke 9 slower / 3 faster, regression 12 slower / 2 faster, net
  +23 game-turns -- Shadowborn +13 (regression; 44 games changed, 10 slower, 0 faster), +5 smoke, Shadowborn2HG +3;
  Gods itself -2 smoke / +1 regression. The held-out A/B had 3 games per Shadowborn job, so it could not see this.
  Likely mechanism: on Shadowborn the deeper pass hits E3 again, so pricing the served units as if they would be paid
  again blocks passes that would have finished; on Gods the deeper pass reaches new states. A sound version must tell
  those apart (e.g. probe the next pass's hit rate) rather than price every hit. Patch kept at
  `logs/xturn/rgc-gate-price-rejected.patch` (gitignored). E3 ships without it; Gods stays the one deck net slower
  (+~4 game-turns over ~1,300 games, all churn) and Gods d5 spends ~+6.5% units -- surfaced to the USER.
* **`MTG_MUTE_NO_STORE` wall cost:** same binary, Alchemist d5 s2002, two concurrent 12-thread arms: 1,240 s (ON) vs
  1,180 s (OFF) thread time, +5%, identical digest and units. It is the price of units not following the plan pool.
  **DEFERRED (USER 2026-11-11: "Let's measure it in more detail when other runs are finished."):** that is one job
  with concurrent arms, so it is a ballpark only. Owed, once the box is free (no generation, no tier run):
  1. Wall A/B `MTG_MUTE_NO_STORE=1` vs `=0`, same binary, `MTG_PGO=0` on both, ONE pooled batch with the arms
     interleaved job-by-job, across several decks (the heavy plan-pool overflowers -- Alchemist d5, Hinata d5, Gods d5,
     Shadowborn d5 -- plus a few light decks as controls). Digest and units must be identical per job; report thread
     time per deck, and count muted recomputes and refused stores (add a counter) so the cost is attributed.
  2. Candidate cheaper fix to measure in the same batch as a third arm: let a muted recompute STORE again, but tag each
     entry it creates with the priced work it cost; the first hit on a tagged entry charges that cost to the priced
     meter and clears the tag. That reproduces the unlimited-pool trace (there the entry would not exist, the lookup
     would miss, do the work and store it) while keeping the wall saving. Precondition to verify: recomputing a key
     costs the same priced work wherever it happens. Gate: the KB=1 residency check (digest + units, every job).
  3. **Gods with E3 (USER 2026-11-11 asked whether it is seed-specific):** over ~3,200 Gods games so far the net is
     ~+4 game-turns and every classified slower game recovers at 4x/16x, so no evidence yet of a real quality loss; the
     unit cost (+6.5% at d5) is the aborted-pass mechanism above. Owed when the box is free: a paired held-out Gods /
     Gods2HG sample, E3 off vs on, d3 and d5, a few thousand games, one pooled batch -- plus `MTG_ROLLOUT_STATS` waste at
     d3. If the loss is real, build a gate price that only charges hits the deeper pass will NOT see again (e.g. probe the
     next pass's hit rate), and re-test it on Shadowborn, which the blanket version cost +13.
* **Origin GT is stale for 4 regression jobs** (gods d3 s2002, fivecolour d3/d5 s2002, landfall d5 s3003; avg equal,
  digest differs): pristine origin `d4bfb2071` reproduces the new digests exactly, so it came with the Gods merge
  `f90930c9f`, not with this work.

* **Final verification (x11 on origin `78e623dac`; after Hinata2 finished, re-run as x12 on origin `cd0368564`: 2,832
  unit tests, smoke 3 slower / 13 faster net -11, regression 4 slower / 33 faster net -27 -- the same slower games;
  GT accepted on x12 and pushed):** x11 detail: build, 2,770 unit tests,
  smoke 3 slower / 12 faster (net -10 game-turns), regression 4 slower / 33 faster (net -27); smoke + regression GT
  accepted, check_gt_logs clean. Two of the slower games are T8 -> loss (shadowborn2hg smoke d3 gi9, shadowborn smoke
  d5 gi85), both churn at 4x / 16x; shadowborn regression d5 s3003 gi135 4 -> 7 persists (single game; deck net -8);
  the rest churn. Per deck only Gods is net slower (+3, all churn) -- the strict per-deck bar is the USER's call.
  Residency (KB=1 vs default pool): 300 / 300 completed jobs identical on digest + units; the two KB=1 tail jobs
  (hydras d5 s3003 gi118, alchemist d5 s2002) were stopped after 2 h at 2 / 24 workers, as in every earlier pass.
* **Deferred measurements, DONE 2026-11-11 (build/scratch/xturn/def; one pooled 54-job batch, quiet box, Release):**
  `MTG_MUTE_NO_STORE` is now a per-job arm lever (heurarm MUTE_NO_STORE) and the refused stores are counted per site
  (`[plan-ledger] refused_stores ...` under MTG_PLAN_LEDGER_STATS).
  1. **MUTE_NO_STORE wall: NO measurable cost.** 12 jobs x 2 reps per arm, interleaved: 24 / 24 pairs identical on
     digest and units (and equal to the x11 GT run); thread ms =1 / =0 = 0.985 overall -- Alchemist 0.986, Hinata
     0.993, Shadowborn 1.027, Gods 0.919, Gods2HG 0.936; the light controls swing 0.96-1.05 between reps, so every
     figure is inside noise. The batch refused only 19,123 stores (all refine-memo; mana tier-1 / tier-2 and the
     rollout game cache 0) against 2.64M muted recomputes. The earlier ~5% was the two concurrent processes, not the
     change. **The tagged-store arm (item 2) is therefore not worth building** -- there is no wall to win back.
  2. **Gods E3 held-out: no quality loss.** 3,900 new paired games per arm, E3 on vs off: d3 seed 50000 x1500 5.5327
     vs 5.5340 (-2 game-turns), d5 seed 53000 x1500 5.3780 vs 5.3807 (-4), Gods2HG d5 seed 56000 x600 5.3783 vs
     5.3783 (0), d3 seed 59000 x300 5.4633 vs 5.4533 (+3): net -3 (with the earlier ~3,200 games, ~+1 over ~7,100 --
     neutral). Units +9.0% / +5.0% / +4.3% / +11.6%. The d3 extra is NOT aborted passes: MTG_ROLLOUT_STATS on the 300-
     game pair shows id_pass aborted 13 vs 13 and waste_units 419k vs 420k equal, but 4,992 -> 5,062 pass starts and
     387k -> 479k rollout calls -- the budget the hits free is spent on more completed search (the doctrine's "search
     further"), at no quality gain on Gods. The d5 aborted-pass waste (above) is the only true waste. A gate price
     scoped to hits the deeper pass will not see again stays an OPTION, not owed: there is no loss to repair.

## Method

One pooled `mtg --batch` per experiment, both arms as per-job `flags` in the same manifest (paired seeds,
one tail). Read: units and avg win turn per deck (budgeted rows), units + digest identity on the budget-0
backstop rows, verify-mode MATERIAL count for E2. Thread-ms only as a secondary signal (per-job thread wall is
contention-confounded -- see the re-measurement section).

## Next steps

1. E1 result -> table here.
2. E2: verify run (MATERIAL must be 0) on the backstop rows + a budgeted sample, then the pooled A/B.
3. Candidates not built yet: E3 carry the per-decision FSLineCache's past-cutoff / provisional entries? (check
   soundness first); E4 hint fallback to any recorded depth for the state.
