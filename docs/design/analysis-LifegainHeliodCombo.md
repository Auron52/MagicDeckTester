# Analysis ledger — LifegainHeliodCombo

Deck: `decks/LifegainHeliodCombo/LifegainHeliodCombo.cod` (60, mono-white lifegain + the Heliod,
Sun-Crowned / Walking Ballista combo; sideboard unreachable — no wish).
Started 2026-10-27 on branch `phase-1-2-deck-analyzer` (base 81f6850c6).

## User direction (2026-10-27)

- *"Even though it is similar to CritterLifegain we should use a new provider for this even if it is a
  derived or something of that nature."* → `LifegainHeliodComboProvider`. Today the deck routes to
  `CritterLifegainProvider` (Pridemate / Voice / Heliod set the critter signature), so per the
  analyze-deck 4a rule ("derive from what the deck ACTUALLY ROUTES TO today") it derives from
  `CritterLifegainProvider`.
- *"A big difference is the Heliod + Ballista combo in this list."*
- *"But the deck often wins without the combo. It is not that reliant on it."*
- *"(but when the combo is the right line we should take it nonetheless)"*
  → The combo must be modelled faithfully and recognised as a kill so the SEARCH takes it whenever
  it is the best line; nothing may force the deck toward assembling it (no combo-first ordering).

## Stage 1 — coverage (2026-10-27)

Already implemented (CritterLifegain onboarding, bracket notes reviewed — faithful models):
Soul Warden, Soul's Attendant, Heliod Sun-Crowned, Voice of the Blessed, Auriok Champion,
Ajani Strength of the Pride, Serra Ascendant, Plains, Ranger-Captain of Eos, Ajani's Pridemate,
Unexpectedly Absent.

Missing (4): **Walking Ballista, Abiding Grace, Martyr of Sands, Emeria, the Sky Ruin.**

Interactions new to this list (not exercised by CritterLifegain):
- Heliod lifelink grant on Walking Ballista → each ping is a life-gain event → Heliod counter back
  on Ballista (infinite damage with >= 2 counters on Ballista).
- Ranger-Captain of Eos (tutor MV <= 1) can fetch Walking Ballista (MV 0 in library) and
  Martyr of Sands.
- Abiding Grace returns MV-1 creatures (Martyr, Soul Warden, Soul's Attendant, Serra Ascendant).
- Emeria recursion at 7+ Plains (22 Plains in the list).

## Stage 2 — cards

Research fan-out (2026-10-27): four Opus agents, read-only, one per card; integration serial
(Martyr by the orchestrator, then Ballista -> Abiding Grace -> Emeria, one writer at a time).

| card | tier | model | viewer bucket | status |
|---|---|---|---|---|
| Martyr of Sands | 2 | self-only sac outlet (Brindle Boar shape), `sac_creature_cost {1}`, `sac_outlet_lifegain 3` x white cards in hand (`sac_outlet_lifegain_per_revealed_color "W"`, `SacOutletLifegainAmount`); X = max is a sound dominance (free reveal, one gain event, every life reader monotone); new deck stamp `deck_reads_life_total` (Serra / Ajani 0 read the TOTAL) keeps it on the menu without a gain watcher | A: activation via `main_phase`; X via `x_value` (new reveal kind: emitter + GUI branch) | IMPLEMENTED; scenario `lhc_martyr_serra_threshold` PASS |
| Walking Ballista | 3 | X-creature cast branch (0..max searched; X keyed into both plan signatures), as-enters X counters at both enter sites, `{4}` counter (PayCounterSelf), counter ping (CounterPing, LKI lifelink -> one GainLife), post-combat lethal finish (CounterPingFinish, loop only when it needs no mana), Heliod loop as an execution-verified PingLoop action with a Heliod-target pin, the 'bump a 1-counter Ballista' clause in DefaultLifegainCounterTarget (proven-kill, gated on the deck stamp `deck_has_counter_ping`), doomed-0/0 queue (Ballista entrants) + target-list exclusions, site-9 keys, ping-reach projection, COMBO OFF gate | A: X plan variants, `target` for the ping, COMBO OFF plan | IMPLEMENTED; 10 `lhc_ballista_*` scenarios PASS; smoke play-changed=0; sanity d3 b10 50g avg 5.00 (loop decided 1/50, post-combat finish 5/50; X=0 x4, 1 x13, 2 x2) |
| Abiding Grace | 2 | end-step modal trigger (`PerformEndStepAbidingGrace`, lockstep), targets snapshotted at the top of the end step; mode+target SEARCHED (`Plan::grace_choice` packed 4-bit rank per trigger, kill switch `MTG_GRACE_AXIS`); shared reanimate helpers (`GyReanimate*`, `ResolveGyReanimate`); folds B1 (return dominates gain with an unfiltered enter-lifegain watcher out) + B4 (identical-behaviour cards) in `LifegainGraceFolds` behind `MTG_GRACE_RETURN_DOMINATES` (to be wired into the provider) | B: new `gy_return_one` (alt "Gain 1 life") | IMPLEMENTED; 4 `lhc_grace_*` scenarios PASS; smoke play-changed=0; 300g cost on/off 4.8 s vs 5.0 s, avg identical 4.8033 |
| Emeria, the Sky Ruin | 3 | enters tapped; upkeep intervening-if (7+ Plains) reanimate any creature card, SEARCHED next-upkeep axis (`Plan::upkeep_gy_reanimate_choice`, Search-for-Tomorrow shape); reuses Grace's helpers | B: `gy_return_one` ("Return nothing") | integrating |

Integration notes:
- Martyr of Sands is MV 1 (`{W}`) — a legal Abiding Grace target (the Emeria draft said MV 2; wrong).
- Walking Ballista is MV 0 off the stack (CR 202.3e): a Ranger-Captain fetch, NOT an Abiding Grace target.
- Ranger-Captain of Eos's bracket note lists a stale tutor pool; in this list it is Soul Warden / Soul's
  Attendant / Serra Ascendant / Martyr of Sands / Walking Ballista (5 names, within the width-6 axis).
- Martyr eval (`a.eval`) follows the Brindle Boar convention (life read -> DMG units, 3X). It can be up to
  ~15 DMG; check the d0-vs-search divergence (5i) for an over-eager rollout sac.

## Provider

| item | state |
|---|---|
| class | `LifegainHeliodComboProvider : CritterLifegainProvider` (src/ai/DecisionProviders.h) |
| signature | `critter && lhc_piece` -- the critter watcher signature AND one of Ballista's `remove_counter_ping_damage`, Grace's `endstep_modal_reanimate_mv`, Martyr's `sac_outlet_lifegain_per_revealed_color`, Emeria's `upkeep_gy_reanimate`; routed just above critter. Unit test row added (test_pirates_provider.cpp). |
| certificate | NotAssessed (combat is not the only route: Ballista pings + the Heliod loop) |
| own hooks | GraceOptions -> LifegainGraceFolds (MTG_GRACE_RETURN_DOMINATES) |

## Open questions for the user (PROVISIONAL defaults taken; never blocking)

1. Cast order: the provider derives from `CritterLifegainProvider`, so it inherits that deck's USER-settled
   total cast order and discard buckets (Ballista / Martyr / Abiding Grace land in its trailing slots).
   Default: inherit, report the measured order; the user decides this deck's order.
2. Heliod's lifegain-counter target is an existing single-pick engine heuristic (highest-power attacker;
   CritterLifegain disclosure). Added: a proven-kill clause (bump a 1-counter Ballista into a live loop).
   Open: should the general target choice (grow Ballista's reach vs grow an attacker) be searched?
3. Martyr of Sands: instant-speed / opponent's-turn activation reduced to our main phase (PROVISIONAL
   deferral, argued inert). Attack-then-sacrifice not available (no post-combat main; same as Ranger-Captain).
4. Walking Ballista autonomous pings: face only; non-lethal non-lifelink pings not emitted (deferral
   dominance); own-creature pings (Abiding Grace re-entry fodder) human-only (PROVISIONAL).

## Stage 4 — baseline profile (2026-10-27)

`analyze_deck.py --no-rebuild`: profile written (same shape as CritterLifegain's baseline; threshold
-1e18, static keep). Card scores: Voice +0.41, Pridemate +0.36, Ballista +0.25, Heliod +0.24, Serra +0.22,
Attendant +0.18, Warden +0.16, Ranger-Captain +0.08, Martyr -0.06, Champion -0.06, Ajani -0.09, Emeria -0.15,
Abiding Grace -0.29, Unexpectedly Absent -0.32. Cost diagnostic: NO_COST_INTERACTIONS. Discard:
DISCARD_UNLABELLED (0 real sheds in play; the rollout sheds ~2000x / 400 games, mostly under 4 lands) --
the skill says BOUND it (MTG_SHED_WORST pair) before assuming inert.

## Suite (4-bis / 5j)

Key `lhc` (+ `lhc2hg` smoke d5 50). Rows: smoke d0 1000 / d3 50 b10 / d5 175 b20; regression d0 1000 /
d3 100 b10 / d5 2x500 b20; overnight ENTRY set d0 4x2000 / d3 125 b10 / d5 4x300 b20; unbudgeted backstop
d3+d5 x20 on 30001 (regression) and 30101 (overnight) -- screened: every game finished, slowest block 3.5 s
wall for 20 games. Cost gate: 341.5 ms/game (worst searched case lhc_regression_d5_s2002) = **0.16x
Hinata** -> PASS. GT not accepted yet (waiting on the review fixes).

## Stage 5 verdicts

- **5a** (2026-10-27, pre-review-fix binary): MTG_FLAG_NONCONV d3 b10 and MTG_FD_ORACLE (FULL_DEPTH) d5 b20,
  300 games x seeds 92001/93001: **0 [nonconv], 0 [fd-diverge]**.
- **5b** (seed 94001, 400 games): d0 5.2050 / d3 b10 4.8125 / d5 b20 4.8075 / d5 b200 4.8025. Monotone;
  d3->d5 0 slower, d5 b20->b200 0 slower; d0->d3 1 slower (gi 78, d0 greedy churn) / 136 faster. One game
  unwon at every depth (gi 367): a flood (2 Soul Warden + Auriok Champion + 2 Abiding Grace vs 8 Plains,
  opponent at 2 on T8) -- legitimate.
- **5c2** tie-break: 12000 paired, 6 changed (all better, -6); `--blocks 24`: 24000 paired, 12 changed, 12 better / 0 worse, halves -6/-6. Still below the script's 20-change bar (binding 0.05%); default ON kept. Owed: `--blocks 48` on the final (post-fix) binary.
- **2d review** (independent Opus reviewer, 11 findings): see the fix log below.

## Approved deferrals

(none)

## 2d review fix log (2026-10-27)

Independent Opus review of the full change set: 11 findings. Fixed serially by the area's integrator.

| # | finding | fix |
|---|---|---|
| 1 | `expect_history_*` silently unchecked on autonomous scenario paths (4 lhc fixtures asserted nothing) | harness exits 2 on an unevaluable history assertion; the 4 fixtures pinned to binding numbers (Martyr/Serra: active life 36) |
| 2 | Martyr reveal-before-cast unfindable at any budget (trailing activation) | in progress |
| 3a | `CounterPingOffered` engine narrowing (incl. own-creature targets = Grace/Emeria fodder) | engine keeps only sound folds (`CollectCounterPingOffers`: face always; a creature only at its exact kill count and only if its death pays -- SelfSacHasDeathPayoff incl. Grace/Emeria); the deck-pool rule (no non-lifelink main-phase face ping) moved to `LifegainHeliodComboProvider::CounterPingCounts`, `MTG_LHC_PING_PRUNE` (default ON); own `UnprunedGate::CounterPing` (gate masks widened to 64-bit) |
| 3b | `PingLoopBumpTarget` heuristic in the engine default | moved to the provider's `LifegainCounterTarget`, `MTG_LHC_BUMP_TARGET` (default ON), our turn only; never-worse argument per phase in the hook comment (PROVISIONAL) |
| 4 | Grace/Emeria axes: duplicate rollouts the dedup can't fold | `EnumerateReanimateCodes` walks real resolutions and emits one code per distinct ordered outcome (reviewer board: 9 -> 7; the 3 order pairs kept on purpose -- trigger order is observable here: a Warden returned first sees the second entry; Heliod/Thune counters need the body in play) |
| 5 | Grace width ignored tutor-to-hand (gain mode unreachable after Ranger-Captain -> Martyr -> sac) | shared `ReanimateHeadroomNames` upper bound for both axes |
| 6 | Emeria 6-Plains fixture passed with the intervening-if broken | rebuilt as a pair (`..._seven_plains_front_wins` / `..._six_plains_no_trigger`, Ranger-Captain only in gy, axis off) |
| 7 | Ballista X=0 fixture didn't exercise the doomed-entrant skip | rebuilt with Heliod + Soul Warden + Archangel of Thune (Thune's each-creature counter is what would save the 0/0) |
| 8 | ping damage bypassed shared damage hooks; kill helper missed token-to-graveyard | routed through `dmgev::DealDamageEvent` / `OnCreatureDealtDamageToOpponent`; death helper matches `CheckStateBasedActions` |
| 9 | Emeria targets snapshotted after other upkeep triggers (CR 603.3d) | `SnapshotUpkeepGyReanimate` at the top of upkeep, both worlds |
| 10 | `g_pending_zero_toughness` never cleared | cleared at game setup |
| 11 | ping loop could take the {4} counter for free | true cost paid at activation in both worlds (`PingLoopCostNow`) |

Kill-switch sanity (seed 94001, 400 g, d5 b20; single seed, not a held-out A/B):
ping prune / bump target on/on 4.8075 (5.0 s) · off/on 4.8125 (6.4 s) · on/off 4.8300 (5.1 s) · off/off 4.8400 (6.3 s).
Loop fired 12 (bump on) vs 6-7 (bump off); post-combat finish 44-47; Ballista cast 138x.

## RESUME STATE (PARKED 2026-10-27 ~12:15 UTC -- USER: "park this and run the pipeline overnight")

Committed + pushed: the feature commit (all 4 cards + provider + 26 lhc_* scenarios) and this ledger.
Suite rows are PARKED on `origin/wip/lhc-suite-rows` (820fb1769: `test/regression_cases.sh` lhc/lhc2hg in all 3
tiers + backstop rows, and `test/suite_cost.json`). They are NOT on the main branch because they land WITH the
GT accept. To resume: `git cherry-pick 820fb1769` (resolve against any new rows), then the tier runs below.

Done before parking:
- 5d claude-play sweep: COMPLETE (see `## Claude-play sweep`).
- 5a on the final binary (build/scratch/lhc/5a2/, 300 games x seeds 92001/93001, d3 b10): 0 nonconv, 0 fd-diverge.
- 5c2 `leaf_tiebreak_check.py --blocks 48`: 48,000 paired games, net -12 turns, 14 better / 2 worse (half A -11 0/11, half B -1 2/3); only 16 changed (0.033%), below the 20-change bar -> NO SIGN at this sample. The tie-break barely binds here; the default (ON) stands. On resume either re-run at `--blocks 96` on the final binary or record it as unbindable.

Sweep flags so far (verify + resolve before recording `## Claude-play sweep`):
1. CONFIRMED (seed 95005, pre-existing, affects CritterLifegain too): legend rule enforced AFTER the enter-watcher
   cascade (EffectHandler.cpp ~285, rollout same ordering) -> a redundant 2nd Heliod sees every enter-lifegain event
   -> double counters (4 events -> 6 counters). CR 704.3/704.5j: SBA before triggers go on the stack. Fix moves
   CritterLifegain GT; needs a held-out A/B on critter + lhc. Legend-rule keep choice is not surfaced to the human
   (engine keeps the original; dominant here) -- uncertain.
2. CONFIRMED (seeds 95004/95010/95013/95016/95017/95019): no opponent-loss check after the pre-combat main
   (src/core/GameEngine.cpp turn body ~233 -- only own_death_live; CheckWinCondition runs after CombatPhase), so a
   main-phase kill still plays combat and asks extra Heliod target prompts. Win turn unaffected; final life totals and
   the human decision count are wrong. Fix: SBA opponent-loss check right after main 1 (check GT digests).
4. CONFIRMED by code reading (seeds 95011 + 95008, pre-existing): DefaultLifegainCounterTarget
   (SpellEffects.h ~6550) ranks by Permanent::EffectivePower() (base + temp + counters), which omits
   ComputeLordBonus's conditional statics -- Serra Ascendant's +5/+5 at 30 life -- so it picks a 5/5 Heliod over a
   9/9 Serra. Heuristic inaccuracy (affects CritterLifegain too); fix = rank by the lord-adjusted power. Moves GT.
3. Decision JSON hand entry shows Walking Ballista cost "{X}" (cards.json "{X}{X}") -- display only; fix the emitter.

Remaining steps after the sweep:
- Resolve flags 1-3 (1 = engine fix + A/B, report to user; 2/3 = small fixes) and record `## Claude-play sweep`.
- `python3 scripts/audit_card_fields.py --update` (network) for the 4 new cards, commit the snapshot.
- `scripts/audit_card_costs.py` (2d-bis), `audit_viewer_decisions.py ... --verify-card` for Grace / Emeria /
  Martyr X (5h), MTG_SHED_WORST discard bound (5i), optionally `mine_heuristics.sh` (5g).
- Run each tier for `--deck=lhc,lhc2hg`, inspect, `--accept --deck=lhc,lhc2hg`, `test/check_gt_logs.py`, commit
  rows + GT + suite_cost.json; then full `verify_deck.py` (with sweep) and `--write-ledger`.
- Push; watch CI (Windows) in the background.
- Closing report to the user: Stage 6 + 6a disclosure + the open questions above.

## Claude-play sweep
- commit: `0d7dbaf52`
- seeds: 95001-95020 (one game per seed, Opus agents, --reveal 6, --firebreathe-prompt) games: 20
- result: claude_win == ai_win in 20/20 (T4 x8, T5 x10, T6 x1, ...); Heliod+Ballista loop used in 7 (95004, 95010,
  95013, 95016, 95017, 95018 post-combat, 95019); decision types seen: mulligan, bottom, main_phase, target,
  x_value (Martyr reveal), gy_return_one (Abiding Grace). Ranger-Captain offered all 5 MV<=1 targets incl. Ballista.
- flags: 4 unresolved
  - legend rule after the enter-watcher cascade -> duplicate Heliod double counters (pre-existing, CR 704.3)
  - no opponent-loss check after main 1 (combat + extra prompts after a main-phase kill)
  - DefaultLifegainCounterTarget ranks by EffectivePower, omitting Serra's conditional +5/+5 (pre-existing)
  - decision JSON shows Walking Ballista cost "{X}" instead of "{X}{X}" (display)
