# LandfallElementals: the plan-fan explosion (2026-10-23)

**Status:** DIAGNOSED; one sound fold landed (look-top FLIP fold, `MTG_LOOKTOP_FLIP_FOLD`, default ON);
the remaining explosion is OPEN and blocks the deck's mulligan profile, its Stage 5c2 tie-break check
and its unbudgeted backstop rows on a 23.5 GB box. The bounding fix is a design decision (below).

## 1. Symptom

On a 23.5 GB / 32-core box (RSS cap 20 GB), every pooled LandfallElementals run beyond the small suite
rows aborts at the RSS cap:

* the mulligan generation-setting screen (`mullgen_finalize.py --write`): its d5 b20 hand scorer at 32,
  12 and 6 threads; keep-table discovery at 32 threads (fixed by the pool-allocator retention cap,
  upstream 88a979f07 -- with the pool off discovery peaks at 3.5 GB, K=21);
* `leaf_tiebreak_check.py` (ordinary play WITH mulligans at d5 b20) at 24 and at 10 threads, after
  ~1,100-1,200 of 24,000 games both times;
* unbudgeted (budget 0) games: every d3 block of 10 games abandons 1-4 at the 2M-unit ceiling, and some
  run 17-19 MINUTES without reaching it.

## 2. The two single-game repros (play settings: omit depth/budget -> d5 b20, profile attached)

```
mtg decks/LandfallElementals/LandfallElementals.cod --profile decks/LandfallElementals/LandfallElementals.profile.json \
    --games 1 --seed 1100011 --game-index 11      # MEMORY: 18.9 GB in 23 s, dies at the default cap
                                                  #   (8.4 GB / finishes T5 at d93492f1f~1)
    --games 1 --seed 1000664 --game-index 664     # TIME: 9 min 23 s single-threaded at 611 MB, wins T6
                                                  #   (6:00 with the FLIP fold)
```

Both were in flight when the tie-break batches died. The memory one is ~1 game in 1,100 of ordinary
play; a keep-table generation is ~14M rollouts, so it would meet thousands.

## 3. What the explosion is

ONE main-phase enumeration (`EnumeratePlansWithLand` -> `AppendSubdecisionAxes`) inside a rollout's
per-turn lookahead (FSLineWin -> SimulateToEnd -> RolloutPreCombatSolve -> SolveWithLookahead), turn 6,
three Risen Reefs on the battlefield. gdb at 6 GB: `all` = 1,028,168 plans, `extra` = 440,049 and
growing in the land-search pin fan. `MTG_AXIS_TRACE` stage counts for that list:

| stage | plans |
|---|---|
| base + tutor variants (126k) + land_slot twins (105k) | 200,064 |
| + land-search activation position twins / pre-cast subsets (`land_search_act_slot`, 142k subsets) | 605,538 |
| + land-search pins (which basic each crack fetches) | 1,028,168 |
| + Risen Reef look-top pins: ~605k BASES x 3-4 looks per entrant, ~15-25 one-hot variants each | ~10M (never reached) |

The axes compose multiplicatively because the later fans take the earlier fans' variants as BASES: the
act-slot twins take tutor variants and land_slot twins; the look-top fan takes position twins and tutor
variants (both deliberately, 2026-10-19 loop-1 enumeration fix d93492f1f: "a land-search position twin
IS a base here", because a crack before a reader changes what the reader sees). `MTG_ENUM_HIWATER_KB`
is SILENT for this game -- it reports only a finished enumeration; use `MTG_AXIS_TRACE=<min plans>`.

**Most of it is duplicate work.** `MTG_ENUM_DUPCENSUS` on a turn-6 list of a non-monster game (s70060
gi60): 806,356 plans, 88,030 distinct post-apply states (11%). Per-axis redundancy after the FLIP fold
(variants landing on the same (family, state)): land-search pins 77%, look-top 53%, land_slot 18%,
act-slot 11%.

## 4. Landed: the look-top FLIP fold (sound identity fold)

A look-top LAND ordinal was fanned over the explicit {0 battlefield, 1 hand}; one of the two always equals
the provider's default, so in the additive fan one variant was always the base plan again. It now fans
{-1 default, kLookTopLandFlip}. Same outcome set at every look whatever the default is there. Measured:
the s70060 list 806,356 -> 567,926 plans with the identical distinct-state set (digest equal); 400 games
(seeds 70000-70199) at d3 b10 and d5 b20: 400/400 identical win turns, peak RSS 4.5 -> 1.1 GB (d3) and
4.3 -> 3.1 GB (d5); the time twin 7:24 -> 6:00. It does NOT save the monster (still > 6 GB at ~8 s).

## 5. Options for the remaining explosion (USER decision; option 5 landed default ON 2026-10-23, the rest not implemented)

Constraints that bind every option: no greedy pick inside the search window; a budget may re-price work
but never make a line unfindable at large budget; a lossy narrowing needs a held-out A/B and the USER.

1. **Budget-staged look-top answers (largest lever).** Do not pre-fan a one-hot look-top variant for every
   possible look on every base; admit them in a later WAVE (or resolve them through the breakpoint
   machinery, which already handles "the card a look sees"), so a small budget spends on the base plans
   first and a large budget reaches the rest. Sanctioned shape ("waves are the sanctioned way to get a
   lossy lever's win legitimately"). Caveat: at UNLIMITED budget everything is still admitted, so the
   unbudgeted backstop and lazily-materialized lists need the streaming form, not just the ordering.
2. **Size look-top land ordinals by the lands that can actually be seen** (lands left in the library; the
   top window when nothing reorders the library first). Sound in principle, but needs an exhaustive guard
   over every effect that puts cards into / reorders the library -- an incomplete guard makes lines
   unfindable. Would remove most of the remaining 53% look-top redundancy.
3. **Size land-search pins per ordinal** by the sources that can consume that ordinal in resolution
   order (today every ordinal gets the union of every source's value list). Sound if a failed crack
   shifting later ordinals is handled (size each ordinal by the union over itself and LATER sources).
   Targets the 77% land-search redundancy. **MEASURED NULL (2026-10-23):** implemented as a suffix union
   per ordinal (`MTG_LSEARCH_PIN_ORDINAL`, not shipped; patch kept in the agent worktree at
   `logs/fold/proposal3_per_ordinal_sizing.patch`) it removes NOTHING on either repro (567,926 plans
   on/off, identical state set; the monster's stage counts unchanged): Terramorphic / Myriad each offer one
   pick per available basic type, so every source's list is already the same width. The 77% comes from
   resolution-time CLAMPS (a later crack sees fewer picks after an earlier one took the only Island) --
   not knowable at enumeration. It also had an open soundness hole (a FAILED own pre-cast crack consumes
   no ordinal, so a continuation's queued crack shifts onto it).
4. **Bound the act-slot twins x tutor x land_slot cross product.** With a Reef out every Elemental entrant
   is a library-top reader, so no identity fold collapses the positions; it needs staging (as in 1) or a
   measured narrowing -- the USER's call.
5. **A rollout-only narrowing** (the monster sits in the PLAYOUT's per-turn lookahead, beyond the horizon,
   where a greedy policy is permitted): skip the reader/position fans inside RolloutPreCombatSolve. A
   playout-policy change -> held-out A/B; does not help a monster board at a SEARCHED node.
   **MEASURED 2026-10-23 (experiment, NOT shipped; patch in the appendix):** with the reader position
   twins and the look-top fan skipped inside RolloutPreCombatSolve (and the enum memo bypassed there):
   the monster s1100011 gi11 FINISHES -- 2.3 s, 1.17 GB peak, T5 (was > 18.9 GB, dead); the time twin
   s1000664 gi664 27.6 s / 232 MB, T6 (was 6:00 with the FLIP fold, 9:23 without). Paired A/B on seeds
   70000-70199 (`--seed 70000+gi`), 200 games per cell: d3 b10 avg 4.550 -> 4.560 (2 faster / 4 slower,
   net +2 turns), d5 b20 4.515 -> 4.505 (6 faster / 4 slower, net -2); units -20% / -17%; batch peak
   RSS (12 threads) 4.7 -> 2.1 GB. Second sample, paired against the ACCEPTED suite GT (the six elementals
   smoke + regression searched rows, 675 games; the flag-off arm of the patched binary reproduced GT 675/675):
   13 faster / 9 slower, net -5 turns. Pooled: 1,075 games, net -5 turns. Quality NEUTRAL-to-better so far, so a held-out A/B at
   scale (and the d8 b0 check of each slower game) is the next step IF the USER takes this route. Caveat:
   the breakpoint cache can still carry a narrowed continuation list out of the playout (the experiment
   bypasses only the enum memo).
   **HELD-OUT A/B (2026-10-23, 3,000 paired games, `logs/lfe/opt5_ab/`):** fresh seeds 80000+gi (d3 b10) and
   90000+gi (d5 b20), 1,500 games each, the patched binary with the flag 0 vs 1 (one pooled batch per arm,
   12 cores each): d3 22 faster / 11 slower, net **-11** turns (avg 4.5433 -> 4.5360), units -23%; d5 32
   faster / 16 slower, net **-17** (4.4980 -> 4.4867), units -18%, wall -27%. No game unfinished in either
   arm. **d8 b0 check of all 27 slower games (ON binary):** 26 reach the OFF arm's turn or better; d5 gi1019
   (OFF T4 at d5 b20) is T5 at d8 b0 with the flag OFF as well as ON (13.7 s each, identical), so its T4 is
   a budgeted-search accident the flag does not cause. Pooled over all three samples: 4,075 games, net -33.
   **AUDIT BEFORE LANDING found the experiment unsound in three ways**, so the A/B above measured a patch
   that was NOT shipped: (a) RolloutPreCombatSolve is also reached from the SolveWithLookahead root's own
   rollouts at `sub_depth > 0`, which are INSIDE the window (greedywindow::Frame(depth), `searched =
   !in_rollout || depth > 0`), so it narrowed in-window enumerations in that mode (the full-depth
   no-win FALLBACK and non-full-depth play); (b) narrowed lists leaked through caches that did not key on
   the flag (breakpoint enum cache bpcache::t_plans, the searched-m2 memo, the m2 enum-memo host, the leaf
   TT when it is game-persistent); (c) only the pre-combat solve was narrowed, so the same rollout turn's
   pre-plan apply / combat / m2 re-derived continuation lists UN-narrowed -- a `bp_choice` index chosen
   against a narrowed list could be replayed against a different one.
   **SHIPPED SHAPE (rescoped):** `RolloutNarrowScope` arms the flag around the full-depth search's TAIL
   ROLLOUT only (the two `SimulateToEnd(..., EffectiveFdLeafDepth(), ...)` leaf sites -- "tail estimate
   beyond the horizon"), for the WHOLE rollout; the SolveWithLookahead root's rollouts are never armed.
   Every reachable cache folds the flag into its key (m2 enum memo, searched-m2 memo, rollout-m1 memo,
   breakpoint enum key -- which bpcanon shares -- and the leaf TT); the m1 enum host still bypasses its
   memo. Not covered: `MTG_BP_NSKIP_GLOBAL` (opt-in, unbudgeted-only). Repros unchanged: monster T5 2.4 s
   1.17 GB, time twin T6 30.7 s 1.55 GB (so the explosion lived in the tail rollouts).
   **Fresh held-out A/B of the shipped shape (`logs/lfe/opt5b_ab/`, seeds 100000+gi d3 b10 / 110000+gi d5
   b20, 1,500 games each):** d3 25 faster / 19 slower, net **-5**; d5 37 faster / 21 slower, net **-16**.
   The flag-OFF arm hit the 10 GB RSS cap at d5 gi~1366 (the explosion itself) and was completed by a
   second OFF batch at a 20 GB cap over every game it had not streamed.

## 6. Instruments added on the way (all inert unless set)

`MTG_EQUIV_ONLY=<probe>:<card>` (one discovery rollout), `MTG_ENUM_HIWATER_KB` axis breakdown,
`MTG_AXIS_TRACE=<min plans>` (stage counts while the list grows), `MTG_ENUM_DUPCENSUS=<min plans>`
(distinct post-apply states + per-axis inert variants; prints and exits).
Keep-gen RSS-cap aborts now list the rollouts IN FLIGHT as SLOW-ROLLOUT lines (5136dadca):
`grep 'in flight' gen.log` feeds `MTG_KEEP_REPLAY_LIST` unchanged.

## 7. The keep-gen RSS-cap loop (2026-10-23 PM) and the canon-front fans fold

**Symptom.** The fresh mulligan generation at ffeb8f10a (d1 b1, K=21, 32 threads, 20 GB cap) aborted at the cap
20 times in 4 h, each relaunch 4-75 min long, the journal gaining ~1.4 MB per 10-min cycle at the end. Baseline
RSS after a resume is 8-9 GB (the cell tables); the rest was 4-11 Risen Reef rollouts in flight at 400-990 s each
(healthy rollouts take < 1 s), never journaled, so every resume restarted them. Replayed alone on one thread one
of them swung between 0.7 and 2.2 GB and was still running after an hour.

**Layer 1: the canon derived the whole pin product to read one entry.** With three or four Reefs out every
reveal is an UNBRANCHED breakpoint, and `BpUnbranchedCanon` -> `BpCanonFront` takes the front() of the
continuation list there. The front-only derivation (which skips the put-back / Ponder clones) only armed when a
put-back / reorder card was in hand, so this deck built the full list, land-search pins and look-top pins
included, every time: on the replayed rollout 620 derivations over 300 ms (t6 main 1, enum depth 1, outside any
playout, up to 381,711 plans) took 516 s of ~600 s. **Fold (`MTG_BP_CANON_FRONT_FANS`, default ON, =0 restores):**
the front-only derivation also skips the land-search pin and look-top pin fans, and arms whenever the deck has a
look-top watcher. Same argument as the put-back clones: each variant is a copy of a base differing only in its
pin list, appended after every base, so no verdict separates it from its base and the stable ranking keeps it
behind; derivations charge no units. Verified: smoke 149/149 byte-identical; 1,200 held-out games (seeds
300000/310000/320000 + gi, d1 b1 / d3 b10 / d5 b20) identical digests AND units; `MTG_BP_CANON_FRONT_VERIFY=1`
194,466 front-only derivations, 0 mismatches. Replays of the 11 stuck rollouts: 438 s -> 16-21 s and 990 s -> 173 s
for the two that finish under a 1.7 GB per-process cap.

**Layer 2 (OPEN, the USER's call -- section 5 option 1 / 4):** the other nine still exceed 1.7 GB, now in the
OUTER turn-6 enumeration at the rollout's own searched decision: 160-300k plans before the look-top fan (about
100k tutor variants and 100k land_slot twins over 7-cast plans), which the fan then multiplies. Option 5 does not
reach it (that node is in the window at d1). The enumeration is uncharged, so at d1 b1 the decision builds
millions of plans to score a few hundred.

**Measured since (2026-10-23 PM), all uncommitted levers, patch `logs/lfe/reef_land_levers.patch`:**
* A keep-gen gate serialising giant enumerations across workers (play-identical) CANNOT work: alone on one thread
  the 19 heavy rollouts peak at 1.3-9.8 GB (8), 11.9-19.9 GB (7) and over 18 GB (4), against ~11 GB of headroom
  over the generation's ~9 GB baseline (cell tables).
* `MTG_LFE_REEF_LAND_DEFAULT` (the Reef land answer is the provider default only; Harbinger ORDER entries still
  fan) fixes it -- all 19 finish together at 11.4 GB -- at +13 turns over 15,000 held-out paired games (three
  samples, d1/d3/d5) with 8 of 21 slower games not recovering at d8 b0. One root cause is local (a Terramorphic
  to hand and cracked the same turn; two reveals sharing one drop), the other is not: the pruned search steers
  away from early Reef lines because their future flips are gone (d5 s380967 gi967).
* `MTG_LFE_REEF_LAND_DROP_USED` (prune only where every look resolves with the drop used) is ~neutral (+1 over
  4,500 games, d1 digest identical) but does not fix memory: the heavy plans do not play their land first.

**Option 2 scope (play-identical; not started).** Hold the look-top variants lazily like the put-back clones
(`PbLazyLayout` / `CandSeq`): generalise the layout to several blocks (`Locate`/`Synth`, `PbLazyMaterialize`,
`PbLazyOrderFs`/`RotateFs`, per-block `fan_bp_base`, `PbLazyListBytes`), emit one prototype per base plus a pin
table, and teach `PutBounceFold::BuildInPlan` (and `Build`/`BuildMasked`, the combat-pin share) to skip the block
rather than materialise it -- on this deck `deck_has_land_search` forces `bp_variants_here`, so BuildInPlan
materialises every lazy pre-combat list today and nothing would be saved. Hosts: FSLineWin and SolveWithLookahead
only; continuation lists and the m2 host stay full. ~300-500 lines; one decision ~1 GB instead of ~9.8 GB.

## Appendix: the option-5 experiment patch (against 9711d6231; `MTG_ROLLOUT_FAN_NARROW=1` arms it)

```diff
diff --git a/src/ai/TurnSolver.cpp b/src/ai/TurnSolver.cpp
index b4fd48d05..adf4ea818 100644
--- a/src/ai/TurnSolver.cpp
+++ b/src/ai/TurnSolver.cpp
@@ -72335,6 +72335,11 @@ static void AxisTrace(const char* stage, const GameState& state, const std::vect
 
 // MTG_LOOKTOP_FLIP_FOLD (default ON, =0 restores the {0,1} land fan): see the look-top pin axis below.
 static bool LookTopFlipFoldOn() { static const bool on = EnvOn("MTG_LOOKTOP_FLIP_FOLD", true); return on; }
+// EXPERIMENT (MTG_ROLLOUT_FAN_NARROW, default OFF, uncommitted measurement for docs/design/lfe-plan-fan-explosion.md
+// option 5): inside the PLAYOUT's per-turn lookahead (RolloutPreCombatSolve, beyond the horizon) skip the
+// library-top-reader position twins and the look-top pin fan. A playout-policy change -- lossy, measured only.
+static thread_local bool t_rollout_narrow = false;
+static bool RolloutFanNarrowOn() { static const bool on = EnvOn("MTG_ROLLOUT_FAN_NARROW"); return on; }
 static void AppendSubdecisionAxes(const GameState& state, bool is_pre_combat,
                                   std::vector<TurnSolver::Plan>& all, bool idle_pass = false,
                                   PbLazyLayout* pb_lazy = nullptr)
@@ -75016,7 +75021,7 @@ static void AppendSubdecisionAxes(const GameState& state, bool is_pre_combat,
         const bool acts_at_end = ResolveProvider(state).LandEntrySpotsRule(state)
                               && !HumanPlayActive() && !DecisionUnpruned(UnprunedGate::LandPick);
         AxisTrace("pre-actslot", state, all);
-        if (!cont_axes_only && !acts_at_end)
+        if (!cont_axes_only && !acts_at_end && !t_rollout_narrow)
         {
             std::vector<TurnSolver::Plan> twins;
             for (const TurnSolver::Plan& p : all)
@@ -75687,7 +75692,7 @@ static void AppendSubdecisionAxes(const GameState& state, bool is_pre_combat,
     // "which order" interact and the tutor axis's additivity argument no longer holds for the order;
     // the cross product is bounded by the tutor width. Human play never fans: the human answers the
     // resolution-time `dig` prompts. Gated on the deck constant (one bool test for every other deck).
-    if (!HumanPlayActive() && state.deck_has_look_top_watcher)
+    if (!HumanPlayActive() && state.deck_has_look_top_watcher && !t_rollout_narrow)
     {
         constexpr std::size_t kLookTopProductCap = 16;
         // A base plan, or a pure tutor-target variant (`tutor_variant`), and nothing else: the other
@@ -78196,7 +78201,8 @@ static std::vector<TurnSolver::Plan> EnumeratePlansWithLand(const GameState& sta
                       // frames that do (2026-10-20: a cached outer list served inside the walk handed it
                       // its own route plan back).
                       && !AlchemistComboRouteInProgress()
-                      && (g_cs_solver_nest > 0 || g_fsline_nest > 0);
+                      && (g_cs_solver_nest > 0 || g_fsline_nest > 0)
+                      && !t_rollout_narrow;
     if (!memo_ok)
     {
         if (enummemo::StatsOn() && enummemo::Enabled() && !HumanPlayActive())
@@ -94507,6 +94513,8 @@ static TurnSolver::Plan RolloutPreCombatSolve(const GameState& state, int depth,
                                               SearchBudget* budget, bool second_main,
                                               TranspositionTable* tt)
 {
+    struct NarrowScope { bool prev; NarrowScope() : prev(t_rollout_narrow) { if (RolloutFanNarrowOn()) { t_rollout_narrow = true; } }
+                         ~NarrowScope() { t_rollout_narrow = prev; } } _narrow;
     const bool finite_budget = (budget != nullptr && !budget->Unlimited());
     if (depth <= 0 || !m1memo::On() || HumanPlayActive() || g_cantrip_order_site != nullptr
         || (finite_budget && budget->Overrun()))
```
