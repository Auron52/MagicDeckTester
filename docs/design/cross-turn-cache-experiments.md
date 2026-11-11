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
* **Origin GT is stale for 4 regression jobs** (gods d3 s2002, fivecolour d3/d5 s2002, landfall d5 s3003; avg equal,
  digest differs): pristine origin `d4bfb2071` reproduces the new digests exactly, so it came with the Gods merge
  `f90930c9f`, not with this work.

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
