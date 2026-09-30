# No greedy pick inside the search window (USER HARD RULE, 2026-09-30)

## The rule

The user's words, 2026-09-30:

> "That sounds like a greedy pick within the search window. We need to make sure all of them are purged."
> "I don't care whether it is main 1 or 2."
> "They should be off in all situations and the code that calls that way should be deleted."
> "I don't want to risk this coming back ever again, since it has been a thorn in my side for over a month."
> "Rollouts are the exception because they are out of the search window."
> "Only heuristics in the provider are allowed to interfere with the search and there just to prune options."
> "To be clear heuristics that return 1 option (such as fetch targets) are okay ... As long as they are in the provider and sufficiently tested."
> "This far outranks fixing up Prevent Damage in priority ... As it is potentially a big flaw in our implementation."
> "At the end of the day we need to move to have greedy fully dropped."
> "Most of the things you listed should be provider heuristics? That isn't an issue. They are intended to prune. Discard in particular always defines a provider heuristic, though it should be able to return more than one option if we want it to."
> "Essentially the engine itself should be free of heuristics, whereas the provider can have a number of them that restrict the search."

**The architecture, in one line:** the ENGINE (TurnSolver, AIEngine, SpellEffects, ManaPayment) is
heuristic-free; the PROVIDERS hold the heuristics, and those may only restrict the search. The test
for any heuristic is where it lives.

What the rule means in practice:

1. **No greedy pick at any node with search depth remaining.** The greedy picker is
   `TurnSolver::Solve()`. The rule applies equally to main 1, main 2, breakpoint continuations and
   playout turns that still have depth left.
2. **Heuristics live only in the providers, and they may only prune.** A provider may remove options
   or order them. A provider prune down to a single option is allowed if it is tested: returning one
   fetch target is the standing default (`heuristic-returns-one-option`). What is forbidden is a
   **host** taking a provider's top candidate out of several because the host did not branch. That
   is a greedy pick.
3. **Code is deleted, not put behind a lever.** A default-off lever that gates a missing searched
   capability is the same defect.
4. **The only permitted greedy:**
   - the playout policy **beyond** the horizon (remaining depth <= 0);
   - the depth-0 runner, which is an engine with no search at all;
   - the standing exemptions for attacks and mana payment.

   Phase 2 (below) removes the horizon greedy too.

## Why it kept coming back

- **The instrument had a blind spot.** The greedy-site instrument (`greedysite`, `MTG_M2_YIELD_STATS`)
  stated that site 90 was "the ONE greedy Solve() reached from inside the search". It never counted
  `SolveSecondMainInSearch(in_rollout=true)`. That site ran greedy `Solve()` in the playout's second
  main at **any** depth, while the same playout turn's main 1 was searched (`SolveWithLookahead` at
  `turn_depth`). So a month of "audit clean" readings missed a live in-window greedy pick.
- **It was filed as tuning.** An earlier scope ruling (2026-09-02) classed in-playout greedy at
  depth > 0 as "quality tuning, not a violation". A per-deck opt-in hook (`SearchesRolloutSecondMain`,
  default greedy) then kept it alive. Anti-Lifegain measured greedy as better there and kept it, even
  though the user had said on 2026-08-23: "We shouldn't have any greedy within the searched window."
- **How it surfaced.** The Prevent Damage all-casts-in-main-2 experiment
  (`analysis-Prevent Damage.md`) lost +0.37 turns/game. The cause: moving every cast to main 2 moved
  every projected future turn from searched to greedy.

## The gate (so it cannot come back)

- **Compile time.** `TurnSolver::Solve()` and `SolveUncached()` take a required
  `TurnSolver::GreedyPermit`, so no call compiles without one.
- **A closed list of sites.** A permit names a `GreedySite` from a closed enum: `HorizonLeaf` and
  `D0Runner`. A `static_assert` pins the enum's size, so adding a site means editing the assert,
  which sits next to the rule. There is deliberately no "diagnostic" site.
- **Run time.** The permit constructor **aborts the process** if remaining depth is > 0. The check is
  in every build, not an `assert` that vanishes under NDEBUG. A regression dies in the first smoke
  run.

## Deleted in this change

| Site | What it did | Now |
|---|---|---|
| `SolveSecondMainInSearch` (in_rollout, depth > 0) | greedy playout main 2 unless the provider opted in | searched whenever depth > 0, greedy only beyond the horizon |
| `DecisionProvider::SearchesRolloutSecondMain` and its overrides (AntiLifegain `MTG_AL_SSM_ROLLOUT`, FiveColour `MTG_5C_SSM`, KittyEquipment, PreventDamage) | per-deck greedy/searched choice | deleted |
| AIEngine refuted-follow (`MTG_REFUTED_FOLLOW`, `MTG_REFUTED_LAND`) | stopped searching after a "proven" no-win and played `Solve()` for the rest of the game (default OFF since 2026-09-17) | deleted; the no-committed-line fallback is always the searched lookahead |
| AIEngine breakpoint re-solve (two sites), `m_in_rollout` escape | greedy re-solve inside London-bottoming playouts at depth > 0 | searched whenever the engine has depth |
| AIEngine divergence log (`MTG_DIVERGENCE_LOG`) | ran a greedy `Solve()` beside each searched decision (diagnostic) | deleted (no diagnostic exemption) |

## Still to do (in-window heuristic substitutes)

The four main-2 levers this section used to list are DONE: `MTG_M2_AXES` (step 2a), `MTG_M2_BPVARS`
(2b) and `MTG_MAIN2_DROP` (2c) were deleted with their behaviour made unconditional; `MTG_M2_FIXPOINT`
was measured (2d, 2.7x CPU on Goblins) and reverted pending a sound dedupe against the bp node's
children. What remains is tracked per row in the audit table below and in the step log.

**Gate gap:** `GreedyPermit` guards `Solve()` only. Resolution-time greedy loops (the replicate
max-loop, Land's Edge fire count, ...) have no depth context, so they cannot abort on their own; each
is closed by pinning its choice in the plan so the unpinned path is reachable only beyond the horizon.
A runtime check for that ("an unpinned resolution choice inside the window") is still to build.

### Engine-side audit (2026-09-30)

This is a read-only audit applying the location test, where engine means violation and provider means
fine. Line numbers are approximate. Items are ordered by likely play impact.

| id | Where it lives | Class | Compliant fix | Decks |
|---|---|---|---|---|
| A1+A2 | SCOPE (corrected 2026-09-30): breakpoints ARE normally branched -- wave 0 (top-W ranks per breakpoint), deferred waves (deeper ranks) and the in-tree bp node (full continuation list). The hole is narrower: `BpNodeWaveDropAt` strips site 3 from the wave masks wherever the node is ENABLED, including states where it does not HOST (`MTG_BP_WAVEDROP_HOSTED` off), so there neither mechanism branches and the resolver takes `ncands.front()`; plus no explicit "cast nothing more" arm (`MTG_BP_EMPTY_ARM` off) | host fails to branch | make hosted-only wave-drop unconditional; EMPTY arm wherever no node hosts; delete the levers | creature_giving, antilife, critter, melira, hinata, kitty, fivecolour, goblins |
| A3 | `Plan::bp_choice/bp_at`: one deviation per plan | representation gap | per-breakpoint choice path in the Plan | Hinata2, Dragonstorm, Cyclone, Auras, treasure_hunt |
| A4/C6 | axes apply to base plans only; continuation lists (`g_bp_enum_depth>0`) get none; pins are single ints, so only the first instance is branched | representation gap / host | per-instance pins; axes on continuation lists | every tutor/scry/ponder/discard/dig deck |
| E | `MTG_M2_AXES`, `MTG_MAIN2_DROP`, `MTG_M2_BPVARS`, `MTG_M2_FIXPOINT` (+`M2FixpointOptIn` hook), all off | host / representation gap | all on unconditionally, levers deleted. Urgent: playout main 2 is now searched, so this gap is hit on every lookahead turn | every second-main deck |
| C2 | `ManaPayment.cpp` `AnimateLandsShared` / `ActivateTapTokensShared` spend spare mana; exert veto hard-coded (`MTG_GREEDY_EXERT_TOKEN`) | engine | enumerate `AnimateLand` / `TapForTokenPay` autonomously (drop the HumanPlayActive gate); exert veto becomes a provider hook; delete the sinks | slivers_vial, WhiteKnights |
| F pain/drip | `TapPainSourcesIfUseful` / `TapDripLandsIfUseful` (keep lands for main 2) | engine (spending, not payment) | end-of-main-1 sweep axis, pruned by the provider | Prevent Damage, Anti-Lifegain |
| D5 replicate | greedy max replicate at resolution; fan only for human/unpruned | host / engine | autonomous `replicate_count` variants (provider prunes) | slivers_vial |
| C1 | `AIEngine::ActivateLandsEdge` executor-only two-arm trial at depth>0 (out-of-band probe) | host | un-gate the `DiscardToLandsEdge(N)` fan; delete the trial | treasure_hunt |
| D5 Terastodon | `ProjectEtbDestroyK` / victim class chosen at resolution (`kEtbKxHeuristic`) | engine | provider hook returning K candidates; host fans out | StompySurprise |
| engine-resident prunes | UACast gate; Jitte non-combat modes; spore pop-all ladder; magnet strive K>0; `SubsetHasUnbackedEtbGift` / `AltPayload` | engine (prunes allowed, wrong location) | move each into a provider hook whose default is today's rule; no play change | Angels, Critter, Kitty, Knights, WhiteKnights, Fungus, Mirrorwing, AntiLifegain |
| F AL Swords | `TryPumpThenSwordsRedirect` auto-fires in resolution | engine | cast variant plus provider prune | Anti-Lifegain (+Angels, Kitty, WhiteKnights) |
| C4 | Ponder order (`MTG_PONDER_ORDER` off) | host | on, lever deleted | Hinata2, Cyclone |
| D7 | dig loop gated to main 1 | representation gap | allow dig in main 2 | treasure_hunt, Auras, Fluctuator |
| B3 Varchild | cumulative upkeep always paid (engine) | engine | provider `PayCumulativeUpkeep` hook | Creature Giving |
| new-1 | Emiel ETB {G/W} counter paid whenever affordable in search/autonomous play (`SpellEffects.h` optional-ETB-counter loop); only the blink-loop combo declines it mid-loop, and human play defaults to no | engine | provider hook, default DECLINE (USER 2026-09-30: "should almost never be paid"; one-option provider heuristic, unit-tested), then A/B on EDF | EldraziDisplacerFlicker |
| new-2/3 | `SacExpendabilityRank` / `CanonicalSacVictim`; `DevourRankOrder` victims | engine | provider victim-candidates hook plus host fan | Goblins, Fungus, Creature Giving, Melira; Fungus |
| new-4 | `ChooseNonCleanupDiscardIndex` takes `rank.front()` (Burning-Fist, Neheb) | host | fan over the provider's list | Minotaur |
| new-5 | `FirebreatheActivations` host takes `fb.front()` (executor and apply) | host | fan the provider's counts | firebreathing decks |
| new-6 | shock lands always pay 2 life | engine | shock-or-tapped axis, provider prunes | FiveColour, treasure_hunt, Cyclone |
| new-7 | hosts take the first entry of `BounceLandCandidates`, `ReviveCandidates` (+ engine MV fallback), `OwnPumpTargetCandidates` | host | fan, or provider prunes to one | Hinata2, Melira, Anti-Lifegain |
| new-8/9/10 | Turntimber "any number = all"; Sakashima same-plan entrant highest power; Mirri's Guile order | representation gap / engine | low impact | various |

**Relocation only (no behaviour change):** the default bodies of `WantVialCharge`,
`ShouldConsiderDig`, `LandsEdgeHeuristicFireCount`, `CanAutoFireAltPayload` and
`HeuristicTopDisposition` sit in engine headers but are reached only through provider hooks. Move them
into `DecisionProviders.cpp`.

**Dead code to delete:** the `s_searched_vial` executor trial.

**Fine as provider prunes:** WantVialCharge (as a hook), PayEchoToKeep, SacTutorPutList, the ETB/put
picks, the cascade-family "may", Apex "R", UtC split, TreasureTrickCast, ShouldCastDrawEngine, EDF
blink targets, EdfAutoGoOff, cast order, the main-phase split (sound only once E is fixed),
alt-payload auto-fire, DigChain rules, and cast-scry via ScryKeepOnTop (extend the scry axis to cast
sources: Snow).

**Smoke on phase 1 (the deletions above):**
- unit tests: 344/344;
- gate: no aborts;
- movers: 10 of 101 cells (burn, antilife, hinata, goblins, giants), 4 games faster, 2 slower, 25
  changed play at the same win turn. No cell average got worse: antilife 4.192→4.188, hinata2hg
  5.653→5.627.
- NOT accepted yet; every changed game needs a verdict first.

## Testing

This is a major engine change, so it goes through quality and performance testing before adoption:

- **Quality:** smoke, then regression, then overnight, on all tiers. Every moved game gets a verdict
  (`rebaseline-others-work-needs-per-difference-verdict`) before any GT accept. The expected movers
  are the decks that use a second main, because their playouts change from greedy to searched.
- **Performance:** paired per-deck cost in units and paired CPU. The box is contended, so absolute
  wall time is not a valid measure. Anti-Lifegain measured the searched playout main 2 at +12 turns
  per 3,000 games at d3, mostly from budget dilution. The remedy for that is provider pruning or
  budget, never a greedy revert.

## Cost and churn ledger (USER, 2026-09-30: track every cost, mitigate AFTER the purge)

The user's bar: quality at least stays even, and every performance cost is recorded here so it can be
mitigated once the purge is complete. The only mechanism by which a purge step has made quality
WORSE is **budget churn**: a new scored option (axis variant, arm, wave) thins a fixed per-decision
budget so a near-tied line flips. Churn is therefore a cost to be MITIGATED, not an excuse (see the
standing rule: budget churn must be minimized, not excused) -- it goes in the same ledger.

| Step | CPU / wall cost (paired unless noted) | Churn games (slower, recover at 4x/16x) | Mitigation candidates |
|---|---|---|---|
| 2a m2 axes | wall 1.03x (contended) | 1 (hinata d3/2hg gi11) | -- |
| 2b m2 bp variants | wall 0.98x | 2 (hinata d5 gi12/18) | -- |
| 2c m2 land drop | wall 1.09x; several d5 cells ~1.5x contended | 6 | land-drop dedupe vs no-drop line; re-time d5 clean |
| 2d fixpoint (REVERTED) | Goblins d5 CPU 2.7x | -- | sound dedupe against bp-node children before re-adopting |
| 3a wave stand-down | wall 0.97x | 6 | -- |
| 3b EMPTY arm | wall 0.99x | 4 smoke + regression-tier share | order EMPTY last / prune when provably equal to base |
| 4b Emiel axis | EDF CPU 0.97x | 0 | -- |
| 5a ponder ORDER | Hinata d3 wall 1.19-1.26x (contended) | 3 | dedupe orders that leave the same top card |
| 6-8 | byte-identical | 0 | -- |
| 9 sinks + sweep axis | smoke wall 0.97x contended; **PD d5 CPU 1.14x vs step 8** | PD 5 (all recover at 4x) | 9c/9d below -> **PD CPU back to 1.01x** |
| 9c sweep "no spare nonland" prune | PD CPU 1.11x vs step 8 (830 vs 746 s); byte-identical to step 9 | same 5 | fires on only 3.6% of variants (428k -> 413k / 30 games) |
| **9d axis-variant post-apply dedup skip** | **PD CPU 1.01x vs step 8 (756 s)**; Hinata d3 probe wall 0.98x | PD vs step 8: 0 faster / 2 slower (s90001 gi27, gi46 -- both already churn at 4x) | -- (98% of PD sweep variants were exact dups: 459,756 / 467,654 skipped) |
| 9e delete 9c prune | PD 1.03x vs step 8 (767 s) | outstanding: PD s90001 gi27, gi46 (+2t vs step 8, recover at 4x) | churn phase |
| 10/10b replicate fan | slivers d3 paired CPU 1.02x (smoke wall 1.11-1.19x contended) | 0 | -- |
| 10c dedup key completes | slivers d5+2hg paired 0.93x vs step 9 | 0 (goblins real regressions fixed, not churn) | -- |
| 11/11b Land's Edge axis | TH smoke wall 0.80-0.82x vs 10c (the deleted executor trial ran two rollouts per fire decision) | th2hg gi18 | -- |
| 12-14b provider hooks, m2 dig | byte-identical (12, 13); 14b smoke wall 1.04x contended, 2 games changed | 0 | -- |
| **Checkpoint (step 7 vs 3753dcf3)** | CPU 1.13x raw; d3 1.21x, d5 1.32x vs a d0 control that itself read 1.32x -> no clear net cost; **Hinata 1.7-2.4x wall: needs clean paired timing** | 58 of 64 slower games | -- |

**d8 b0 runtime check (2026-09-30).** The 66 purge-slower games at `--depth 8 --budget-ms 0`, purge
binary vs committed tree: quality never worse (2 better: hinata d3 s2002 gi143 T7 -> T6, hinata2hg d5
s2002 gi21 T6 -> T5), but **3.72x total runtime** (843 s vs 227 s), concentrated in Hinata (up to 24.5x,
hinata d5 s2002 gi21 0.5 s -> 12.5 s) and fungus d5 (5.4x). Bisected (hinata gi21, d8 b0): **step 3a**
(0.37 s -> 4.33 s; later steps add little). Mechanism (`MTG_BP_WAVE_PROBE`): before 3a the waves never
fired (nodes=0 -- the non-hosted breakpoints were the unbranched `cands.front()` hole 3a closed); after
it, 45 wave nodes score 12,538 continuation entries with max rank 706, and **84% are post-apply state
duplicates** (dupstate 10,541, self 8,897): same-card copies / orderings in a cantrip deck's huge
continuation lists, each paid for with a full prefix apply before the dedup sees it. Not a bug; the top
performance item: a SOUND pre-apply dedup of continuation candidates (NB the plan-signature dedupe was
measured UNSOUND in an earlier arc -- only a signature that provably implies state identity may skip).

Mitigation phase (after the purge, before Phase 2), in order:
1. Clean paired CPU timing per deck (alternate base/new binary; d0 as the contention control).
2. Exact-duplicate prunes for every new axis (a variant whose pin cannot change the plan's outcome
   must not be scored) -- measured UNBUDGETED so a prune is proven lossless before adoption.
3. Churn: re-run every recorded churn game at 1x after (2); for what remains, sweep the budget
   share of the new options until the churn curve flattens, never by removing the option.

## Deferred relocations (engine-resident PRUNES, no play change -- need an interface decision)

These are option-RESTRICTING rules (allowed as heuristics) that still live in TurnSolver.cpp. They are
not greedy picks, so they do not violate the no-greedy rule's substance, only its location test. Each
is blocked on the same thing: the provider header (`DecisionProvider.h`) is deliberately Action-free
(`PlanContext.h`), and these rules read `Action` lists or TurnSolver-internal helpers.

* **Spore pop-all ladder** (`CollectActions`, the fungus spore-count narrowing to `{max_k}` or
  `{1, max_k}`; levers `MTG_SPORE_POP_ALL` / `MTG_SPORE_HOLD_*`). Proposed hook
  `SporeActivationCounts(state, src, max_k)`; the block uses `DoublerShift` and a mana/land projection
  that would move with it. Also: it is gated on `UnprunedGate::BlinkTarget`, which looks like a
  copy-paste -- give it its own gate when moving it.
* **`SubsetHasUnbackedEtbGift` / `SubsetHasUnbackedAltPayload`** (subset-validity prunes, called from
  both `SolveUncached` and `EnumeratePlans`). A hook needs either an Action-free subset summary
  (PlanTraits-style) or a second provider interface that may see Actions.
* **Engine-header default BODIES reached only via provider hooks** (`WantVialCharge`,
  `ShouldConsiderDig`, `LandsEdgeHeuristicFireCount`, `CanAutoFireAltPayload`): pure code moves into
  `DecisionProviders.cpp`, no engine code calls them directly (surveyed 2026-09-30). Hygiene only.
* **`ResolveSoloTargetTrick`'s strive-extra order** (highest printed power, `SpellEffects.h`): a
  resolution pick in the shared resolver (lockstep by construction); hook candidate `StriveExtraOrder`.

## Deferred: slivers d0 regression (USER 2026-09-30: "d0 is not crucial, but we should take a look at some point")

Slivers d0 (the greedy RUNNER, no search) is +10 turns / 1000 games vs the committed tree (regression
tier s2002; smoke s1001 +13). Root cause, measured: step 9 deleted the greedy post-cast mana sinks
(AnimateLandsShared / ActivateTapTokensShared). The search now spends that mana through Mutavault /
Sliver Hive / token-tap plan ACTIONS, but the greedy runner has no equivalent -- hiding those actions
from it (step 16) changed nothing (-1t), so the loss is the runner no longer sinking spare mana at all,
not how it weighs the new actions. Every searched depth is unaffected. Options when picked up:
(a) Phase 2 (replace the d0 runner with a searched decision) removes the question; (b) a
provider-owned post-cast sink for the greedy runner ONLY (outside every window -- GreedyPermit
territory), measured against step 8's d0. Not a quality issue for any searched cell.

## Phase 2: greedy fully dropped

The user's stated end state is that the horizon playout's greedy policy (site 90 and the playout's
main 2 at depth <= 0) goes too. It needs a non-greedy estimator beyond the horizon. The candidates are
the per-deck value leaf (already an O(1) evaluator on the decks that have one) or a searched shallow
playout. Design and measure it once phase 1 is in.

## Step log (one change per step, smoke-diffed against the previous step's snapshot)

Snapshots: `logs/snapshots/purge-step<N>` (gitignored); per-game diff `logs/purge/stepdiff.py`.

| Step | Change | Smoke vs previous step | Verdicts |
|---|---|---|---|
| 0 | phase 1 (playout m2 searched, refuted-follow/divergence deleted, GreedyPermit gate) | vs GT: 10/101 cells, 4 faster / 2 slower | hinata d3 gi22 churn (recovers 4x); gi110 variance (T2 Ponder choice differs -> draws diverge T6) |
| 2a | `MTG_M2_AXES` deleted, m2 axes unconditional | 35 games changed, 3 faster / 6 slower (+5 turns, all Hinata); contended wall 1.03x | hinata d3/2hg gi11 CHURN (both T7 at 4x and 16x); hinata d5 gi19/40/55/60 variance (draws diverge T1-T4 after different cantrip/mulligan choices) |
| 2b | `MTG_M2_BPVARS` deleted, memoized m2 host always appends wave-0 breakpoint variants | 18 changed, 8 faster / 2 slower (-6 turns); wall 0.98x | hinata d5 gi12, gi18: variance/churn (different T1-T2 cantrip lines; both recover to T5 at 4x) |
| 2c | `MTG_MAIN2_DROP` deleted, m2 land drop always offered (with its defer-drop tie reorder) | 1344 changed, 38 faster / 10 slower (-30 turns); wall 1.09x (several d5 cells ~1.5x contended) | all 10 slower = churn or variance: AL d3 gi94/194, 5C d3 gi97, hinata d3 gi18, d5 gi25/39 recover at 4x; 5C d3 gi25, hinata d5 gi22 persist but draws diverge T1-T2; 5C d5 gi21 variance (T1 fetch differs), recovers at 8x |
| 2d | fixpoint mode 2 unconditional -- **REVERTED, re-evaluate after A1/A2** | 16 changed, 2 faster / 1 slower; Goblins d5 CPU **2.7x** (4.0 s -> 10.9 s user, paired, repeated) | goblins2hg d3 gi7 variance (T2 cast order changes the Muxus reveal, draws diverge T3). Cost is the mode-2 RE-SOLVE, not the kill-scan: a new-cards-in-hand gate at all three sites cut nothing (tutored cards stay in hand). Rationale for reverting: the fixpoint is a SECOND search pass over a post-draw decision the breakpoint machinery already branches (continuation lists 3.1 -> 9.2 entries, bp-node children 3.6k -> 19.7k), so its absence is not a greedy pick. Its only unique coverage is a continuation that was NOT branched -- exactly what A1/A2 fixes. Re-measure (Hinata opt-in on vs off) once A1/A2 lands; if it still earns, it becomes unconditional with a sound dedupe against the node's children. |
| 3a | `MTG_BP_WAVEDROP_HOSTED` deleted; the node's site-3 wave stand-down applies only where the node really hosts | 51 changed, 10 faster / 7 slower (-4 turns); wall 0.97x | CG d5 gi3, hinata d3 gi84/112, d5 gi19/34/53 recover at 4x (gi19 goes to T5); hinata2hg gi35 persists but draws diverge T3 = variance. (First run of this step was VOID: the edit landed mid-build, a stale object mixed two HeuristicArm.h enum layouts -- never edit sources while a build runs.) |
| 3b | `MTG_BP_EMPTY_ARM` deleted; "done with this phase" is always a scored arm at every breakpoint index | 24 changed, 0 faster / 4 slower (+5 turns); wall 0.99x | auras d3 gi128, kitty d3 gi238, th2hg d3 gi35, th d5 gi47: ALL recover at 4x and 16x = churn (the extra arm dilutes a 10-20 ms budget). 0/4 direction flagged: re-check on the regression tier before accepting; remedy is pruning/budget, never the lever |
| 4 | Emiel's optional {G/W} counter: engine no longer decides; provider hook `PaysOptionalEtbCounter` (EDF declines, USER). Not in any regression tier -> paired EDF A/B, 2x200 games d3 b20 | vs 3b: 12 changed, 0 faster / 2 slower | s71001 gi76, s72001 gi124: REAL (same draws, persists 4x and 16x): the counters were the combat damage (T5 hit 8 vs 4). A bare decline is a one-option prune that deletes a line the search needs -> step 4b |
| 4b | + searched axis `Plan::etbcounter_choice` (variant opposite the provider default on every base plan that can trigger; pin `ScriptedEtbCounter` in ApplyPlanDirect AND the executor) | EDF vs 3b: averages identical (4.290 / 4.245), 13 changed, 0 faster / 0 slower; CPU 0.97x | both lost games recovered; the search now decides per plan |
| 5a | `MTG_PONDER_ORDER` deleted, ORDER axis unconditional (which looked-at card is drawn now was an unbranched resolution pick) -- smoke isolates 5a (no smoke deck plays Emiel) | 31 changed (all Hinata), 1 faster / 3 slower (+2 turns); Hinata d3 wall 1.19-1.26x contended | hinata d3 gi22 (4x), gi102 (16x), d5 gi61 (4x): all recover = churn |
| 6 | no-drop path appends sub-decision axes in BOTH mains (was m2-only; the m1 re-solve after the land drop was bare) | 0 changed | INERT, and PROVEN inert rather than assumed: the new `MTG_ROLLOUT_STATS` counter `nodrop_axes` reads m1 enums=0 on auras/hinata/th/kitty d3 (m2: 696,926 enums, 301,490 axis variants). m1 re-solves after the drop reach the enumerator through the breakpoint-continuation route, not this one. Kept as insurance (a zero-cost path) |
| 7 | Varchild cumulative upkeep -> provider hook `PaysCumulativeUpkeep` (default pay; decline sacrifices via SacrificePermanentAt), USER: design for 1v1 | 0 changed (expected: default pays) | byte-identical; unit test covers both arms |
| 8 | commit-the-line replay per breakpoint segment (`Action::rec_bp_ord`) -- the lockstep fix above | smoke 0 changed (byte-identical, as designed: acts only on multi-segment recordings) | th s2002 gi208 T5 -> T4 on the regression case |
| 9 | C2: greedy post-cast sinks (`AnimateLandsShared` / `ActivateTapTokensShared`) DELETED, 16 call sites; Mutavault animate + tap-token abilities enumerated as plan actions for autonomous play, executor twins added; Basri's exert pruned by provider hook `OffersExertTokenActivation` (default false = the user's 2026-09-27 ruling). PLUS the end-of-main-1 SWEEP axis (`Plan::sweep_choice`, pin `ScriptedSweep` in apply + executor) | only slivers moved: d3 69 changed 1 faster / 0 slower, d5 35 changed 0/0, 2hg 12 changed 0 / 1; **d0 270 changed, 4 faster / 18 slower (+14 turns / 500)** | searched depths neutral-to-better. d0 is the greedy runner (no search): the sink actions change WHICH spells greedy Solve picks (gi183 T3: Galerider+Plated instead of Muscle+Galerider) -- greedy-runner quality, the Phase-2 target; no greedy sink re-added. Sweep axis FIRES (`plan_axes sweep_variants=45,627` over the AL cells) and moves 0 AL games -> the search agrees with the keep-for-main-2 rule on every AL smoke turn; PD measured separately |
| 9c | sweep axis: skip the variant when no nonland card is left in hand after the plan (the rule would sweep anyway) | PD d5 2x150 vs step 8: 3 faster / 5 slower (+4t), byte-identical to step 9; CPU 1.11x | fires on 3.6% of variants only -- the real duplication is the rule's OTHER no-op paths (damage not useful, nothing castable within the untapped count) |
| 9d | `PlanIsAxisVariant`: the post-apply state dedup, which already SKIPPED duplicate breakpoint variants, now skips every sub-decision axis variant (sweep, Emiel counter, ponder/scry/tutor/dig/discard/lackey) whose state an earlier sibling reached. Exact (state identity -- the same key the bp-variant skip already stakes on); ordinary plans still only record. Counter `axis_dup_skips` | PD vs 9c: 3 faster / 3 slower (-2t); vs step 8: 7 changed, 0 faster / 2 slower (+2t, gi27/46 = the step-9 churn games); **PD CPU 1.14x -> 1.01x**. Smoke: see step 10 (the 9d/9e smoke runs were VOID -- regression.sh was invoked without `bash` and failed, the diff compared step 9 with itself). The skip FIRES on smoke decks (AL+Hinata d3 40-game probe: 25,845 skips, identical digests) | the step-9 PD cost was 98% duplicate rollouts; mitigated exactly |
| 9e | 9c prune DELETED (redundant with 9d, and unsound across mid-plan draws) | PD vs 9d: 1 changed (same turn), CPU 1.01x (767 vs 756 s); vs step 8 still gi27/46 +2t; smoke: see step 10 | -- |
| 10 | D5 replicate: the count fan (k = 0..kmax, each priced into the cast's own bill) is enumerated in AUTONOMOUS play, not just human play; `MTG_REPLICATE_DIM` deleted; the dead `ReplicateCounts` hook becomes a provider PRUNE (empty = full fan). Resolution's greedy max is reached only by an unpinned cast | smoke vs step 9 (covers 9d+9e+10): slivers d3/d5/2hg 59 changed, 0 faster / 0 slower; **slivers d0 +36 turns (45 slower)** -- the greedy runner was handed the fan and picks among the variants by the ordering hint | `MTG_REPLICATE_TRACE`: every executed replicate cast now carries a searched pin (was -1) -> step 10b for d0 |
| 10b | the greedy picker (`Solve`/`SolveUncached`, GreedyPermit-checked to sit OUTSIDE every window: d0 runner, horizon leaf) is not offered the replicate fan (`g_greedy_solve_nest`); it keeps resolution's line-hold max. Human play keeps the fan | smoke vs step 9: slivers d0 byte-identical again; slivers d3/d5 = step 10; rest: goblins d3/d5 gi44, 2hg gi37/40 slower, hinata d5 1 faster (+3 turns) -- from 9d, verdicts below | slivers d3 60-game paired CPU 1.02x |
| 10c | KEY HOLE fixed: `BuildDedupKey` now folds every pending `scripted_*` state pin (Lackey put, fling, Tectonic mode/keep, cleanup discard, Vial charge, saga target), value-gated so a pin-free key is byte-identical. 9d let every axis variant skip on this key; a Lackey variant (pin consumed in combat, after the apply) collided with its base plan and was skipped -- goblins d3/d5 gi44 and 2hg gi37 lost a turn at 1x/4x/16x with IDENTICAL draws (real, bisected to 9d). Latent before 9d: only bp variants skipped, and they never carry these pins | smoke vs step 9 (9d..10c cumulative): **1 faster / 0 slower (-1 turn)**; all four goblins games back to T4; only slivers (logged replicate pins, same turns), hinata, melira changed | paired CPU slivers d5 + 2hg: step 9 12.51/11.38 s vs 10c 11.24/10.94 s = **0.93x** |
| 11 | C1 Land's Edge: fire count becomes plan axis `Plan::le_fire_choice` (pin consumed at the SAME end-of-main fire point in ApplyPlanDirect and, via `AIEngine::m_le_fire_pin`, in the executor -- a pin, not an appended action, so lands drawn by breakpoints/digs are in hand). The executor-only heuristic-vs-fire-all rollout trial DELETED with `MTG_LE_RELINE` / `MTG_LE_TRIAL` / `MTG_LE_TRIAL_NEWTURN`. First cut fanned every k = 0..L | vs 10c: TH only, 3 faster / 3 slower (-1t); th2hg gi15 REAL (T5 -> T6 at 1x/4x/16x, same draws): with 8-9 lands in hand the fan was 10+ variants per base plan per level and the T3 search stopped at depth 1 (`MTG_FD_TRACE`: searched_depth=1 verified=0 vs old depth 3 verified T5) | th2hg gi18, th d5 gi71 churn (recover 4x). TH s3304 gi301 (the case the trial existed for) still T4 |
| 11b | fan PRUNED by new provider hook `LandsEdgeFireCandidates` (default {hold all, fire all} -- the trial's two arms; the base plan keeps the provider's count) | vs 10c: 3 faster / 1 slower (**-4 turns**), TH wall 0.80-0.82x; gi15 back to T5, gi301 T4 | th2hg gi18 churn (T7 at 1x, draws diverge; T6 at 4x/16x) |
| 12 | D5 Terastodon: destroy-K on an unpinned entry (autonomous cast + every PUT) routed through new provider hook `EtbDestroyK` (default = the USER's 2026-08-20 projection `ProjectEtbDestroyK`, a one-option provider heuristic); `MTG_TERA_K` lever DELETED (default-on shape kept). New unit test `test_terastodon_k.cpp` (K=0 vs K=2 arms must differ); fixture `stompy_terastodon_k` still T7. Victim ORDER (`EtbDestroyVictimClass`, shared emission/resolution) left for the relocation batch | smoke vs 11b byte-identical (by design) | -- |
| 13 | F AL Swords: the pump-then-Swords redirect decision routed through new provider hook `RedirectPumpOntoRemovalTarget` (default true = the documented weak dominance); the engine keeps only the cast's rules. Unit test `test_swords_redirect.cpp` (default vs declining provider, arms must differ) | smoke vs 12 byte-identical (by design) | -- |
| 14 | D7: ApplyPlanDirect's dig loop (cycle / sac-to-draw lands) runs in BOTH mains (was main 1 only -- a main-2 dig line was inexpressible); executor's reactive dig widened to both mains too | vs 13: 7 changed, 0 faster / 2 slower (+2t); fluctuator d5 gi68 churn (T5 both at 4x); **fluctuator d3 gi87 REAL** (T4 -> T5 at 1x/4x/16x, same draws) | game-log diff: the executor cycled Canyon Slough in T3 MAIN 2 -- the committed line had no main-2 phase (search modelled m2 as nothing), so m2 ran the UNCOMMITTED path and the widened heuristic reactive dig realised a cycle the search never saw, spending a Drannith Stinger ping the T4 kill needed |
| 14b | executor's heuristic reactive dig back to MAIN 1 ONLY; the apply's dig loop stays in both mains. A searched main-2 dig is recorded in its plan and replayed on the committed path, so the executor never needs to dig heuristically in m2 | vs 13: 2 changed (fivecolour, same turn), **0 faster / 0 slower**; gi87 back to T4 | -- |
| **Regression checkpoint 2** (14b vs step 7, full tier) | 854 changed, 15 faster / 26 slower (+14t). Slivers d0 +10 (greedy runner, step 9 sinks). **Slivers d3/d5 +8, 0 faster**: s2002 gi155/169 and s3003 gi115 REAL (same draws, persist 16x), bisected to step 10; gi226/354 churn. TH 7 faster / 5 slower (-1t); fivecolour, antilife, melira, hinata faster/neutral | -- | see 10d |
| 10d | replicate fan also keeps the UNPINNED cast ("as many copies as spare mana allows", realised by resolution's line-hold loop) as a SEARCHED option. A pinned k is priced statically into the cast's bill, which cannot see Thrumming Hivepool's Sliver-affinity discount as the token copies enter, so Hivepool + Striking x2 read as unaffordable | gi155/169/115 back to T4; slivers regression cells vs step 7: searched depths 0 faster / 2 slower (gi226, gi354 = churn), d0 +10 unchanged | the static-vs-sequential affordability class (docs: enumeration-feasibility-via-executor) |
| 15 | new-2/3: `SacExpendabilityRank` becomes provider hook `DecisionProvider::SacExpendabilityRank` (default = the engine rule, now `DefaultSacExpendabilityRank`); every consumer -- CanonicalSacVictim (the sac-outlet action's victim + the multi-sac burst), DevourRankOrder, sac-pay fodder rank, Pod victim emission order -- asks the provider. Unit test `test_sac_rank_hook.cpp` (default token-first vs tokens-last provider) | smoke vs 14b (built with 10d): only slivers moved (replicate logs, same turns); goblins/melira/fungus byte-identical | -- |
| 16 (REJECTED, reverted) | hide the Mutavault / token-tap actions from the greedy picker (as 10b did for the replicate fan) | smoke vs 15: only slivers d0 moved, 17 faster / 14 slower (-1t); vs step 8 d0 still +13 | the d0 loss is the greedy RUNNER losing the deleted post-cast sinks, not its weighing of the new actions -- nothing to recover short of Phase 2 (replace the d0 runner). Kept one path |
| 17 | dead code: the Aether Vial out-of-band two-arm rollout probe (`MTG_SEARCHED_VIAL`, default off since 2026-08-30) and its `ChargeRemainingVialsHeuristic` helper DELETED. The charge stays the provider's `WantVialCharge`; `MTG_VIAL_AXIS` (the opt-in SEARCHED fan) is kept -- USER 2026-08-30 shipped the heuristic with the fan opt-in, and it is a search option, not a greedy substitute | smoke vs 15 byte-identical | -- |
| 18 | relocations (resolution picks): `HeuristicTopDisposition` -- the autonomous scry / surveil / reorder pick, called DIRECTLY by the engine at 4 play-path sites (ChooseTopDisposition, TopDispositionCandidates' candidate 0, ReorderCandidatesNarrow, ReorderTopNoShuffle) plus the claude-play default -- now routes through new hook `TopDispositionPick` (default = the old body, `DefaultTopDisposition`); Terastodon victim ORDER through new hook `EtbDestroyVictimClass` (all 3 sites: resolution loop, emission K cap, K projection); the revive MV-desc fallback moved into `ReviveCandidates`' base default and DELETED from the engine. Unit tests `test_provider_relocations.cpp` (3 two-arm tests) | smoke vs 17 byte-identical | -- |
| 19 | relocations (enumeration prunes): the USER-doctrine prunes that sat in CollectActions -- Unexpectedly Absent never cast autonomously (both the opponent-target and the self-target form) and Jitte's non-combat modes -- become provider hooks `OffersTuckRemovalCast` / `OffersJitteNonCombatModes` (default false = the doctrine). LOCKSTEP HOLE fixed: the executor's safe-alt auto-fire lacked the rollout's `MTG_UNPRUNE=altpayload` suppression (inert at default). Unit tests `test_provider_prunes.cpp` (default vs overriding provider on EnumerateMainPlans). The magnet strive K>0 prune is a measured DOMINANCE fold (strictly fewer tokens for strictly more mana vs the passive opponent) -- lossless, stays | smoke vs 18 byte-identical | -- |
| **Regression checkpoint 3** (every slower game, with verdict, is listed for revisit in `docs/design/no-greedy-worse-cases.md`; ALL 66 recover at the definitive `--depth 8 --budget-ms 0` test) (step 19 = commit cd2908d9 vs GT = committed tree 3753dcf3, full tier) | searched depths **100 faster / 66 slower, net -33 turns**; d0 5 faster / 13 slower, +10 (all slivers d0). Biggest gains: hinata d3 s2002 -17, hinata/hinata2hg d3 -6 each, fivecolour/burn -4 per cell, giants -2 per cell. Of the 66 slower: 62 churn (recover at 4x/16x, `classify_turn_later.sh`), 3 variance (hinata d3 s2002 gi57/77/143: draws diverge at T3-T6 vs the committed-tree binary), 1 d3-only (th s2002 gi276: T7 at d3 for every budget to 64x, but T6 at d5 and d7 on both binaries -- recovers with DEPTH). USER bar 2026-09-30: per-deck slivers of churn can wait unless a case does not recover with more budget AND depth -- none remains. Per-deck searched net (games): th +5/1600, antilife +4/1100, fungus +4/600, melira +3/230, goblins +2/1100, slivers +2/1400, auras +1/2000 (all churn) vs hinata -19, burn -12, fivecolour -10, giants -6, creature_giving -2, hinata2hg -2, fivecolour2hg -2, kitty -1; the rest 0 -- tracked for the mitigation phase | -- | -- |

### Audit items re-examined and found COMPLIANT (no code change) -- 2026-09-30

* **new-4 non-cleanup discard** (Burning-Fist cost, Neheb trigger): the pick is the provider's own
  authored discard ranking -- a provider heuristic, which the user ruled is the intended home for
  discard. Its entire reach is BOUNDED: a best-vs-worst bracket (`MTG_NONCLEANUP_SHED_WORST`,
  28,800 paired games, docs/design/per-deck-discard-analysis-phase.md) measured +0.00025 t/game at d3
  and +0.00042 at d5, both t < 0.3. The search already owns whether and how often to activate.
* **new-5 firebreathing count**: no provider overrides `FirebreatheActivations`, so every deck gets
  the base provider's single option ("as many as the pool affords") -- a one-option provider rule.
  The hook already returns a list; a deck that needs to hold mana for main 2 overrides it with
  several counts and the host must then fan (the `fb.front()` read becomes a violation only then).
* **B3 Varchild cumulative upkeep**: paying is WEAKLY DOMINANT against the passive opponent (the
  gifted Survivors only feed our drains; the 3/4 body is kept) -- a dominance auto-yes like Goliath's
  cost-free "may", not a heuristic.
* **new-7 pump / revive / bounce targets**: the provider's ranked list, front taken -- provider prunes
  (user: "they are intended to prune"). Hygiene only: `PerformReturnFromGraveyardToBattlefield`'s
  engine MV fallback belongs in the base provider's `ReviveCandidates` default (byte-identical move,
  batched with the other engine-resident relocations).
* **new-6 shock lands**: ALREADY provider-owned -- every autonomous decision routes through
  `DecisionProvider::LandEntersUntapped(state, def, heur)`, which receives the engine default as its
  input and may overrule it. The default (pay while life > cost) is weakly dominant in a goldfish:
  paying only ever ADDS mana, and life is a resource none of the six shock decks (Anti-Lifegain,
  Creature Giving, FiveColour, Minotaur, Cyclone, treasure_hunt) is pressed on. Prevent Damage, where
  life matters, runs none. A deck whose life becomes load-bearing overrides the hook.

### Regression checkpoint (step 7 vs committed tree 3753dcf3, full regression tier)

140 cells, 3679 games changed, **96 faster / 64 slower, net -31 turns**. CPU: 6839 s vs 6065 s user
(1.13x), but the d0 cells -- zero changed games, identical code path -- ran 1.32x slower in the second
run, i.e. the box was more contended then; against that control d3 is 1.21x and d5 1.32x, so no
clear net cost. Hinata cells (1.7-2.4x wall) stand above the control: timed separately below.

Slower games: 58/64 churn (recover at 4x/16x). 3 Hinata persist but draws diverge T3-T6 = variance.
2 treasure_hunt persist with IDENTICAL draws -- bisected to step 3b (EMPTY arm):

* **th s2002 gi208 -- REAL LOCKSTEP BUG, FIXED (step 8).** The commit-the-line replay replayed the
  whole top-level `breakpoint_actions` list at the FIRST main-level breakpoint. Correct only when
  every record came from it. With EMPTY at breakpoint 0 the search's apply recorded breakpoint 1's
  continuation (Frostboil + third Hunt, the T4 kill); the executor ran it after the first Hunt, the
  base plan's second Hunt could not pay, T4 -> T5. Latent before the EMPTY arm (any empty first
  continuation). Fix: `Action::rec_bp_ord` stamped at the apply's main-level sink pop, replayed per
  segment at the executor's matching trigger; end-of-main catch-all replays unreached segments.
  Byte-identical when every record is ordinal 0. gi208 back to T4.
* **th s2002 gi276 -- NON-MONOTONE SEARCH, OPEN.** No divergence (the search itself scores its T2
  choice T7). Persists at 64x budget, and with each of 15 default-on prunes/memos off, and with
  `MTG_FS_HORIZON_EXIT=0`. The T6 kill lies beyond the T2 horizon inside the (now searched)
  playouts; an extra scored option there changes which near-tied line the playout realises. The
  known class in docs/design/draw-divergence-diagnosis.md ("the search is NON-MONOTONE"). The EMPTY
  arm stays (the rule requires it); the non-monotonicity is the bug to fix.
