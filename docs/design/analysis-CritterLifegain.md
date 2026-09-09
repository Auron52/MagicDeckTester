# Analysis ledger — CritterLifegain

Deck: `decks/CritterLifegain/CritterLifegain.cod` (60, mono-white lifegain; no sideboard).
Started 2026-09-08 on branch `phase-1-2-deck-analyzer` (base 67ef4c66).

Status: **ANALYZED** (2026-09-08) — all Stage-5 checks green; suite GT accepted for all three tiers
(smoke/regression/overnight); tie-break `--blocks 121` re-run + 5g mining are the remaining box work.

## Stage 1 — coverage (2026-09-08)

Implemented already (bracket notes reviewed, all faithful models, none a deferral):
- Soul Warden (`any_creature_enters_lifegain 1`), Plains, Orzhov Basilica (Karoo bounce), Unexpectedly Absent.

Missing (10): Soul's Attendant, Serra Ascendant, Ajani's Pridemate, Voice of the Blessed,
Daxos Blessed by the Sun, Ajani Strength of the Pride, Archangel of Thune, Heliod Sun-Crowned,
Auriok Champion, Ranger-Captain of Eos.

## Shared engine design decision (integrator, fixed before the fan-out)

The deck's engine is "whenever you gain life". Lifegain is currently applied at ~13 scattered
`.life +=` sites (Combat.cpp lifelink; SpellEffects.h enter-watchers, etb_self_lifegain,
cast_lifegain, gy-exile modes, drain_self_gain, charge_lifegain; LandPlay.cpp etb_lifegain) with no
central hook. Plan: ONE shared `GainLife(state, player, amount)` in SpellEffects.h (adds life, bumps
`life_gained_this_turn`, fires `FireLifegainWatchers` once per gain EVENT — CR 119.10: each source
is its own event, so 2x Soul Warden on one creature entering = 2 events; two lifelink attackers =
2 events; amount 0 = no event). Every existing site routes through it (byte-identical for decks
with no lifegain watcher). Watcher params: `lifegain_self_counters`,
`lifegain_each_own_creature_counters`, `lifegain_target_own_counter` (Heliod, a real choice).

## Cards done + tier (all 10 IMPLEMENTED 2026-09-08, coverage 14/14 full)

| card | tier | model |
|---|---|---|
| Soul's Attendant | 1 | `any_creature_enters_lifegain 1` ("you may" always taken -- dominated, disclosed) |
| Auriok Champion | 1 | `any_creature_enters_lifegain 1`; protection inert on all four DEBT axes (allowlisted in `scryfall_divergences.json`; NOT `protection_from_everything`, which would exclude it from Heliod's targets) |
| Ajani's Pridemate | 2 | NEW `lifegain_self_counters 1` (also the token def for Ajani's -2, by name) |
| Voice of the Blessed | 1 | `lifegain_self_counters 1`; counter-threshold flying/vigilance/indestructible inert (PROVISIONAL partial, disclosed) |
| Archangel of Thune | 2 | NEW `lifegain_each_own_creature_counters 1`; flying inert; lifelink live |
| Serra Ascendant | 2 | NEW `life_threshold_pump_life/_power/_tough` (30/+5/+5) in `ComputeLordBonus`; flying inert |
| Daxos, Blessed by the Sun | 2 | NEW `toughness_equals_devotion_color "W"` (CDA), `own_creature_enters_lifegain 1`, NEW `own_creature_dies_lifegain 1` (`FireCreatureDiesWatchers` from `OnCreatureDies`) |
| Heliod, Sun-Crowned | 3 | NEW `creature_requires_devotion 5` + `devotion_color` (`RefreshDevotionCreatures`, layer-4 type toggle on the permanent's Card copy), NEW `lifegain_target_own_counter` (ONE provider/default pick per event; human board-click), NEW `lifelink_grant_cost {1}{W}` = `PermAbilityMode::GrantLifelink` (repeatable, `Permanent::temp_lifelink` until-EOT) |
| Ranger-Captain of Eos | 2 | ETB single tutor (`tutor_to_hand` + `tutor_types` + `tutor_max_mv 1` + shuffle); sac = `sac_creature_outlet` + NEW `sac_outlet_self_only` (effect inert; emitted autonomously only while `SelfSacHasDeathPayoff`) |
| Ajani, Strength of the Pride | 2 | `loyalty_start 5`; NEW effects `lifegain_creatures_plus_walkers` (+1, ONE event), `pridemate_token` (-2, named token through the enter cascade), `exile_all_opponent_artifacts_creatures` (0: faithful, NOT enumerated autonomously -- value gate, human-reachable) |

Shared engine work (all param-gated, other decks byte-identical -- smoke 76/76 after the provider
scoping below): `GainLife`/`FireLifegainWatchers`/`AddPlusCounters` (SpellEffects.h), 12 lifegain
sites routed (+ `etb_life_floor` as a gain of the difference), Combat.cpp lifelink DEFERRED to after
the damage loop (one event per attacker, CR 510.2/119.10), `make_token` in `ApplyLoyaltyAbility` now
fires the universal enter cascade, Dominance.h boundary assertion for `temp_lifelink`, EvalCard terms
gated on the new params. `CritterLifegainProvider` (routes ABOVE goblin/anti -- Ranger-Captain alone
trips both) holds ONE hook: walker-aware `LegendKeepIndex` (`MTG_LEGEND_KEEP_LOYALTY`, default ON).
Tried in Generic first: fivecolour smoke d0 gi=15 moved 6->7 (kept fresh Bolas spent +3 on our own
permanent in main 2) -> NOT a clean win for FiveColour -> scoped to this deck's provider.

Tests: `test/unit/test_critter_lifegain.cpp` (10 cases: per-event semantics, own-entry counters,
Thune team/Heliod single target, combat deferral, two lifelink attackers, Daxos CDA + dies, Heliod
devotion flip, Serra threshold, GrantLifelink + counter merge); scenario
`test/scenarios/critter_thune_lifelink_after_damage.json`.

## Viewer classifications (2c-ter)

- Heliod counter target + lifelink-grant target: bucket A -- reuse `g_play_loyalty_chooser` (own-
  permanent board pick -> `target` decision with a `loyalty` prompt). Registered in DECISIONS.md.
- Ranger-Captain tutor: bucket A (`main_phase` tutor variants); sac: bucket A (`activate`, victim
  forced -- one candidate, no prompt).
- Ajani abilities: bucket A (loyalty sub-choice; `LoyaltyAbilityText` arms added). 0 is human-only.
- Everything else: no interactive choice (all 12 new params classified in `audit_viewer_decisions.py`;
  static audit: SELF-GUARD clean; advisory cross-check flags Heliod's 'target' because
  `modeled_tokens` keys only on `targeting`/`spectacle_cost` -- the MANIFEST maps it; noted).
- Known UX gap (disclose): Heliod's counter target fires a board prompt PER gain event (5-15/turn).

## Verification verdicts (Stage 5) -- commit fbe93635 + provider scoping

- 4a provider routing: `CritterLifegain` (intended -- Ranger-Captain alone trips goblin + anti).
- 5a mismatch: seed 2002 -- d3/b20 200 games: 0 `[nonconv]`; d5/b40 100 games (`MTG_FD_ORACLE=1`):
  0 `[fd-diverge]`, 0 `[nonconv]`. (NOTE: the skill's `MTG_FULL_DEPTH` flag no longer exists --
  the binary warns; `MTG_FD_ORACLE` alone is the oracle.)
- 5b multi-depth (seed 2002, first 100 games shared): avg d0 5.13 / d3 5.02 / d5 5.02; d3 never
  slower than d0 (11 faster), d5 never slower than d3. Distributions d0 T4-T8, d3 T4-T8, d5 T4-T7.
  Outlier: gi=0 unwon at every depth = a 5-Plains keep that drew five more Plains (cards 22-30) --
  three 1/1s from T4 leave the opponent at 2 on the T8 cap. Flood, not a play error.
- 5c budget: no starvation seen (d3 == d5 per game); suite budgets 10/20 (breaching-class cost).
- 5h viewer: static self-guard clean; runtime sweep (seed 4242, 12 games) PASS -- `bounce`,
  `discard`, `target` surfaced; `--verify-card "Heliod, Sun-Crowned"` VERIFIED (target).
- 5i discard: analyzer verdict DISCARD_INERT (no cleanup shed reached by either caller).
- 5c2 leaf tie-break: KEEP THE DEFAULT. 12,000 paired: 1 changed (helped); re-run at `--blocks 121`
  = 121,000 paired: 1 changed (helped), 0 worse. The lever essentially never fires for this deck
  (wins well inside the horizon) -- "unbindable at a large sample, the default is fine".
- 5g heuristic mining (300 games x seeds 2002/3003, d5 b3000, 2940 decisions): ORDER rules, all
  0-conflict, one shape -- enter-WATCHERS before PAYOFFS (Auriok Champion before Serra 11/0,
  before Pridemate 6/0, before Voice 4/0; Soul Warden / Soul's Attendant before Voice 6/0, 6/0,
  before Serra 5/0, 6/0; Pridemate before Serra 3/0). INCLUSION: every payoff/watcher negative
  (cast it); Serra +0.10 (help 44 / hurt 78) and Daxos +0.05 are situational -> left to the search;
  Auriok +0.01 neutral. LAND: Plains first (Basilica 1141 vs 7372). Encoded as
  `CritterLifegainProvider::CastOrderRank` (watchers 8, lifegain_self_counters payoffs 9) behind
  `MTG_CRITTER_WATCHER_ORDER` (default ON). With/without A/B, seed 2002: d0 400 games 7 faster /
  0 slower (5.1332 -> 5.1156); d3/b10 300 games 11 faster / 0 slower (4.9264 -> 4.8896). Clean win
  -> ADOPTED (no-drawback rule, USER 2026-09-03). Suite re-run after adoption: smoke 76 other keys
  byte-identical; critter overnight 201 faster / 4 slower (all four d0 greedy churn, 0 searched-depth
  slower); GT re-accepted for all three tiers. (The overnight audit's 65 'searched slower' were stale
  other-deck .wins left in test/logs/overnight/ by earlier full runs -- the classifier found 0 critter
  games among them.)
- 5d claude-play sweep (16 Opus agents, base seed 31337, gi 0-15, at fbe93635): 15/16 games
  identical win turn to the search; gi=5 Claude T5 vs search T6 -> ROOT-CAUSED as a search gap:
  loyalty actions are enumerated only from walkers already on the battlefield, so "cast Ajani,
  then -2 this turn" was unreachable autonomously (human play reaches it via the re-prompt).
  FIXED 2a6e8453: provider-gated cast-plan variants carrying `loyalty_ability` (target-free
  abilities), folded into `plan_signature` (#L<k>), applied after the walker enters + legend rule in
  both worlds. First attempt missed the executor half (showed as [fd-diverge] on every such line --
  the oracle caught it); final A/B on critter 300 games d3/b10 seed 2002: 29 faster / 0 slower
  (5.0234 -> 4.9264), 0 fd-diverge / 0 nonconv, gi=5 now T5. Generic stays OFF
  (`MTG_WALKER_CAST_ACTIVATE`): FiveColour's walkers are a follow-up A/B.
  Flags: 0 confirmed rules/state defects. Dismissed: Basilica bounce prompt after a same-plan
  cast (gi=10) = the legal tap-in-response shortcut. Cosmetic, FIXED 2a6e8453: board-prompt P/T
  labels without CDA/statics (Daxos "2/0", Serra without +5/+5; gi 0/7/13/15); Unexpectedly
  Absent's prompt inheriting the Swords wording (gi=9). Cosmetic, OPEN: Heliod's target prompt says
  "loyalty ability" (gi=14); no explicit pass entry when only UA/self-sac variants remain (-1
  passes; gi 2/6). FOLLOW-UP (pre-existing, Anti-Lifegain's card): UA's human target list omits
  own permanents although its note claims human play opens them (gi=9).
  Re-sweep of the four Ajani games (gi 5/11/13/15) under 2a6e8453: see below.
- Suite: added to `test/regression_cases.sh` (breaching shape, + `critter2hg` canary); GT to be
  accepted after the tie-break batch frees the box.

## Stage 6a -- encoded heuristics & assumptions disclosure (read from the code, 2026-09-08)

| assumption / heuristic | source | class | cost / why safe |
|---|---|---|---|
| Passive opponent (never blocks/attacks/casts/targets); its spawns enter on 8 of 10 game indices and DO fire the Wardens | engine | global | flying / vigilance / protection / indestructible inert; spawn pattern is a per-gi variance source for lifegain totals |
| Clairvoyant search, first-main only (`DeckUsesSecondMain` did not fire) | engine | global | Ranger-Captain's sac is taken pre-combat (forgoes that turn's 3 dmg) -- small disclosed under-rating |
| Suite settings d3/b10, d5/b20 (2x overnight); profile = card-scores only (min_lands 1 / max_lands 5, no keep table, no value leaf) | Stage 4 | global | gi=0 s2002 unwon = a 5-Plains keep that flooded (max_lands 5) |
| "you may gain 1 life" (Attendant, Champion) ALWAYS taken | cards.json | correctness shortcut | dominated (no lifegain hate; every gain = counters) |
| One trigger per life-gain EVENT (CR 119.10); lifelink gains applied AFTER the damage step (CR 510.2) | GainLife / Combat.cpp | rules | verified by 10 unit tests + scenario + 20 sweep games |
| Heliod counter target = ONE resolution pick (`DefaultLifegainCounterTarget`: highest-power own creature that can still attack this turn, else highest-power own creature; never a non-creature while a creature exists; tie lowest m_number) | SpellEffects.h (provider hook `LifegainCounterTarget` = default) | PRUNING (resolution heuristic) | could miss: spreading vs piling counters (identical for raw damage; differs only for Voice's inert thresholds); NOT openable by MTG_UNPRUNED (5-15 events/turn -> Cartesian); human play picks from the full legal set |
| Heliod lifelink-grant target = highest-power own attacker without lifelink (K axis searched, cap = #useful targets) | ApplyPermAbility / ModeSpec | PRUNING (resolution heuristic) | could miss: granting a smaller attacker (never better vs a non-blocker); human play picks any other creature |
| Ranger-Captain self-sac emitted only while a death payoff (Daxos) is live | TurnSolver enumeration (`SelfSacHasDeathPayoff`) | lossless dominated-action removal | with no payoff the sac is a strict loss; human play always offered; a Ranger-Captain cast this turn can sac in the same phase via breakpoint site 9 |
| Ranger-Captain tutor: all 3 legal MV<=1 names searched (tutor axis width 6); plan-less paths take the first library match; the FETCH IS CASTABLE THE SAME PHASE (2026-09-08 fix: the creature-ETB tutor now arms the acquisition re-solve in the rollout, as a tutor spell did; 0/7 -> 6/7 spare-mana fetches cast same-turn on 300 games d3) | Generic TutorCandidates + `MTG_ACQ_RESOLVE` | none (full) | fixture `critter_ranger_captain_fetch_same_turn` |
| Ajani 0 NOT enumerated autonomously | TurnSolver loyalty enumeration | value gate | strictly negative vs this opponent; human play sees it |
| Ajani cast + same-turn activation variants (target-free abilities) | `SearchesWalkerCastActivation` (CritterLifegain ON, Generic OFF) | WIDENING (search reach) | +29/-0 games on 300; other decks unchanged until measured |
| BREAKPOINT SITE 9 -- post-entry activation (2026-09-08, user directive): after the plan's own activations, if a permanent that entered this turn has an affordable, live activation (walker not yet activated; a PermAbility mode / blink / team pump / Pod whose cost fits the remaining pool and whose {T} source is untapped and not sick; a self-only outlet with a payoff live) the rest of the phase is re-decided through the SEARCHED breakpoint variants only (wave-0 fans out plans that cast such a permanent; `bp_choice` indexes the continuation list at the post-plan state; NO greedy fallback in either world -- the first build had one and it measured a real regression, see the review section) | `TurnSolver::PostEntryActivationPending` (`MTG_POST_ENTRY_BP`, default ON) | WIDENING (search reach) | engine-wide; fixtures `critter_walker_post_entry_activation` (cast variant pinned off) and `critter_heliod_post_entry_lifelink_grant` |
| Cast order: enter-watchers (rank 8) before lifegain_self_counters payoffs (rank 9) before the rest | `CritterLifegainProvider::CastOrderRank` (`MTG_CRITTER_WATCHER_ORDER`) | ordering heuristic (5g-mined, 0-conflict) | A/B +7/-0 (d0), +11/-0 (d3); only the canonical execution order moves -- plan choice is still searched |
| Legend rule keeps the walker with the most loyalty (tie: can still activate, then oldest) | `CritterLifegainProvider::LegendKeepIndex` (`MTG_LEGEND_KEEP_LOYALTY`) | correctness shortcut | rejected as a Generic default (FiveColour d0 gi=15 6->7) |
| EvalCard credits: Pridemate/Voice entry counters = #own enter-watchers; Thune team credit; Heliod flat +2 DMG and body discounted by devotion distance | TurnSolver EvalCard (param-gated) | greedy-d0 ordering only | rollout owns the real valuation; no other deck's eval moves |
| Voice's 4+/10+ counter keywords | MODELLED 2026-09-08 (`counter_threshold_flying_vigilance` 4 / `counter_threshold_indestructible` 10; `RefreshCounterThresholdKeywords` at the counter chokepoints toggles the permanent's keyword bits) | none | user: "it will look wrong in the viewer"; unit-tested incl. the drop-off when counters are annihilated |
| Serra/Thune flying, Auriok protection, Heliod indestructible + God-subtype/removed-from-combat halves | bracket notes (PROVISIONAL partials) | card-modeling collapse | each provably unobservable vs this opponent (see the notes) |
| Daxos: legend-rule deaths | FIXED 2026-09-08: `EnforceLegendRule` routes every doomed CREATURE through `OnCreatureDies` after the erase (unit-tested: second Heliod as a creature -> Daxos +1, one event) | none | engine-wide: any legend-rule creature death now fires LTB / dies watchers |
| Daxos: simultaneous-death look-back | bracket note | known engine gap | unreachable in this deck (no sweeper, no opponent removal, Daxos never at toughness 0) |
| Viewer: Heliod's counter target fires a board prompt PER gain event | 2c-ter | UX | faithful; a per-turn "apply to all" affordance is a follow-up; prompt now worded "this ability (<source>)" not "loyalty ability" (gi=14 cosmetic, fixed) |
| Viewer/claude-play: main-phase dump carries an explicit `pass` entry (index -1) | main.cpp WriteDecisionJson | UX | gi 2/6 cosmetic, fixed |
| Provider routing: `CritterLifegain` (intended -- Ranger-Captain alone trips goblin + anti) | 4a | routing | Generic for everything but the two hooks above |

## Claude-play sweep
- commit: `2a6e8453` (16 games at fbe93635 + 4 Ajani-game re-sweeps at 2a6e8453, all under the current play)
- seeds: 31337 games: 16 (gi 0-15) + re-sweep gi 5/11/13/15
- flags: 0 unresolved
- results: `logs/critter/sweep_results.jsonl`. 15/16 identical win turn; gi=5 (search T6 vs Claude T5)
  root-caused and FIXED (same-turn walker activation); re-sweep: 4/4 identical, walker path verified.
  Dismissed/cosmetic flags listed under Stage 5 above.

## Approved deferrals
(none yet — every proposed deferral is PROVISIONAL until the user signs off; user is asleep
2026-09-08, so all are provisional through this run)

## Deferred-item review (2026-09-08, user directive after the report)
User: "we absolutely need to fix things like Ranger-Captain ... Tutoring to hand should open a
breakpoint ... Walkers and other sources with activated abilities should also breakpoint in some
fashion ... skip sources we can't activate ... fix the viewer cosmetics ... fix the lack of
vigilance on Voice ... fix the Daxos + 2x Heliod case."
- Ranger-Captain: the ledger's "cannot be cast the same turn" was WRONG in cause and half-wrong in
  effect. Measured on 300 games (seed 7001, d3 b10): 7 fetches had spare mana, 0 cast same-turn;
  at d0 the executor's second pass cast 8 of 8. Cause: the deferred acquisition re-solve
  (`MTG_ACQ_RESOLVE`) was armed only in the tutor-SPELL branch of `ApplyPlanDirect`; the creature
  ETB path never armed it, so no committed line ever recorded the continuation. Fix = the same arm
  in the creature branch (cast path only; the Vial path has no executor classification). Re-measured:
  6 of 7. (The 7th: the plan spent the spare mana elsewhere.)
- Post-entry activation = BREAKPOINT SITE 9 (see the 6a row). General mechanism; supersedes
  nothing -- the cast-carried walker variant stays (it is scored inside the plan, which is stronger)
  and site 9 covers the rest (targeted abilities, other decks, mana-sink permanents, outlets).
  DESIGN LESSON (cost one smoke run): the first build gave the site the site-7 shape -- a greedy
  Solve continuation whenever no searched variant targeted the occurrence, in every rollout and in
  the executor (searched re-solve there). Smoke: searched slower=24 / faster=15, and the harness's
  classifier said the slowdowns PERSIST at 16x budget (fivecolour gi71/83/97/120/134/145, goblins
  gi55/141, melira gi14), every one isolating to `MTG_POST_ENTRY_BP=0`. Mechanism, read from
  fivecolour gi71: the greedy continuation +1'd a just-cast Jared for a Kavu where the scored line
  wanted the loyalty held; goblins: it sacrificed into a fresh outlet. A greedy continuation is a
  poor judge of a TRADE-OFF activation, and it also re-biased every future-turn rollout. Shipped
  shape = SEARCHED-ONLY: the occurrence is counted, wave 0 fans out the plans that cast such a
  permanent, the variants apply candidate k of the post-plan continuation list and are scored by
  their own rollout; a plan without a variant is exactly the plan as scored (zero cost, exact
  lockstep). Depth 0 has no wave and therefore no site 9 -- greedy stays greedy.
  SECOND LESSON (cost a regression run): the searched-only build still slowed Dragonstorm on all
  four regression keys (gi117 5->8 at d3, d5 AND unbounded budget). Cause: the committed-line
  executor replays INLINE sites (Apex of Power) from the recorded script without consuming a
  breakpoint index, while the rollout counts them -- so a site-9 occurrence after an inline site
  is index 1 in the rollout and index 0 in the executor; the wave scored a Lathliss-pump variant
  at bp_at 1 as a T5 win and the executor never applied it. Site 7 documents this very ordering
  constraint and survives only because Melira mixes no classes. Fix: site 9 opens only as the
  FIRST counted occurrence of an apply (`bp_seen == 0` / `bp_seen_exec == 0 && !bp_replayed`),
  in both worlds. Dragonstorm gi117/193/s3003-gi1 back to T5/T5/T6.
- Voice keywords, legend-rule deaths, both viewer cosmetics: fixed (rows above).
- STILL DEFERRED (unchanged, PROVISIONAL): Serra/Thune flying, Auriok protection, Heliod
  indestructible + God-subtype/removed-from-combat halves, Daxos simultaneous-death look-back,
  Ajani's 0 (value gate), Heliod counter target as one resolution pick, `MTG_WALKER_CAST_ACTIVATE`
  OFF for other decks (site 9 now reaches those walkers anyway), UA's human target list omitting
  own permanents (Anti-Lifegain's card), Heliod prompt once per gain event.
- Loyalty activations now WRITTEN TO THE GAME LOG (2026-09-09, user request): both executor sites
  (the cast-carried activation and the standalone ActivateLoyalty action) emit an ABILITY line
  `loyalty -2: create an Ajani's Pridemate (loyalty now 3)`, only when the activation actually
  fired (watching loyalty_activated_this_turn flip; a walker erased by its own cost logs
  `loyalty 0 -> graveyard`). LogAbility folds into the play digest, so the smoke + regression
  digests of the two walker decks (fivecolour, critter) moved with every mean identical and
  slower=0/faster=0 on both tiers; re-accepted.
- Scenario-harness lesson (cost an hour): a fixture whose `max_turns` ends the game on the turn
  under test makes the search DECLINE a free land drop and a free 1-drop cast -- nothing after the
  horizon can value them, so they tie with doing nothing. Assert on a later win turn instead.

## Deck-combination screen (2026-09-08, after the search fixes; user: "look at optimizations for the deck")
`scripts/deck_compare.py logs/critter/screen/screen1.json` -- 6 arms x 10,000 paired games, d3/b10
(spec override; the deck has no value_play), shared card-scores profile, no keep table on any arm
(the deck ships none -- symmetric), `pool_table: false`. Report `logs/deckcmp/CritterLifegain/screen.out`.

| arm | edit | avg | delta vs base 4.9394 | se | t | identical |
|---|---|---|---|---|---|---|
| rc2 | Ranger-Captain 1->2, Unexpectedly Absent 3->2 | 4.8838 | **-0.0556** | 0.0027 | -20.3 | 95.2% |
| cut_basilica | Orzhov Basilica 3->0, Plains 21->23 | 4.8880 | **-0.0514** | 0.0049 | -10.6 | 85.0% |
| serra4 | Serra Ascendant 2->4, Auriok Champion 4->2 | 4.9022 | -0.0372 | 0.0025 | -15.2 | 94.2% |
| lands23 | Plains 21->20 (23 lands), Serra Ascendant 2->3 | 4.9089 | -0.0305 | 0.0031 | -9.9 | 92.9% |
| daxos2 | Daxos 1->2, Auriok Champion 4->3 | 4.9301 | -0.0093 | 0.0012 | -7.9 | 98.6% |

rc2 vs cut_basilica head-to-head: -0.0042 +-0.0053 (t -0.8) -- a tie at the top. Held-out
confirmation of rc2 (`--confirm rc2`, seed 1410000, 10,000 paired games): **-0.0528 +-0.0026**
(shrinkage +0.0028 +-0.0038, t +0.7 -- reproduces); pooled point estimate **-0.0542 over
20,000 games**. NOT adopted -- deckbuilding is the user's call (and see the UA caveat below).

READ WITH THESE CAVEATS (the judgement the driver cannot make):
- **Unexpectedly Absent is INERT against the passive opponent** (it only ever tucks a spawn
  token that never attacks or blocks), so any arm that cuts a copy banks a free slot in the
  simulator that is NOT free in a real game. rc2's -0.056 is "a second Ranger-Captain instead
  of a dead card"; the real-game value of UA (answering a blocker / a bomb) is not measured here.
  The honest reading of rc2 is "a second Ranger-Captain is worth ~0.05t in goldfish tempo".
- Orzhov Basilica's cost (enters tapped, bounce) is fully modelled; its benefit (virtual card
  advantage against flood) is real but only matters when spells run out -- a goldfish with 24
  lands rarely does, so the screen overstates the cut a little. Still, 24 lands with a curve
  topping at 5 and three tapped lands is a lot for an aggro deck; 23 untapped lands is a
  defensible real-world change.
- serra4 / lands23 / daxos2 are count changes inside modelled cards: read at face value. Serra
  Ascendant's 30-life threshold is reached routinely here (12 enter-watchers), which is why the
  3rd and 4th copies pay; the 2nd Daxos (legendary) is worth little.
- The floor is unmeasured (`--floor` generates a table; this deck ships none, so the bracket
  route does not apply). With NO table on any arm the apparatus is symmetric by construction;
  the residual bias is the shared card-scores profile only.
- Combining the top two (rc2 + cut_basilica) is untested; they touch disjoint slots.

USER DECISION (2026-09-09): no deck modifications for now. **rc2 is NOT a useful comparison** --
Unexpectedly Absent is a genuinely powerful card in real games and a do-nothing card here, so any
arm that cuts it measures the wrong thing; do not re-propose it. The user may revisit
**cut_basilica, serra4 and similar count/land changes inside modelled cards** later. The list on
disk stays the original 60; value leaf / mulligan generation, if run, fit that list.

## Site-9 cost on this deck (2026-09-09, idle box, 32 threads, seed 5001, shipped binary e9aeea67)
`logs/critter/site9_cost/`. CPU = user seconds for the whole run; repeat runs of one arm differ ~2%.

| config | arm | CPU s | avg | games differing vs shipped |
|---|---|---|---|---|
| d3 b10, 1000 games | shipped | 207.4 (repeat 210.7) | 4.9750 | -- |
| | `MTG_POST_ENTRY_BP=0` | 189.9 | 4.9730 | 8 |
| | `MTG_POST_ENTRY_BP=0 MTG_ACQ_RESOLVE=0` | 186.3 | 4.9800 | 18 |
| d5 b20, 500 games | shipped | 191.1 | 4.9780 | -- |
| | `MTG_POST_ENTRY_BP=0` | 168.2 (repeat 165.4) | 4.9800 | 2 |
| | `MTG_POST_ENTRY_BP=0 MTG_ACQ_RESOLVE=0` | 168.3 | 4.9860 | 8 |

Reading: site 9 costs ~9% CPU at d3 and ~14% at d5 on Critter (the wave-0 variant fan-out is the
cost; a plan without a variant is free) and buys no win-turn here -- the cast-carried walker variant
already covers Ajani, so site 9's Critter work is Heliod grants and sac-outlet timing, which rarely
move a goldfish win turn. The tutor re-solve (`MTG_ACQ_RESOLVE` creature arm) is CPU-free and worth
~0.006t. Suite-wide, the regression tier's searched CPU moved +2.0% and smoke +2.8% vs the
pre-session full-tier logs (both inside pooled-run noise; Melira moved -35% and Minotaur +40% on
single keys in the same comparison, so per-key ms is not a signal at that grain).

## Search headroom + CPU profile (2026-09-09; user: "is there more headroom?")
Ladder `logs/critter/headroom/ladder.json` -- ONE pooled batch, seed 6001, paired numbering, no keep
table / value leaf (the deck ships neither), 500 games per searched arm (1000 at d0).

| arm | avg | digest | CPU s (sum) |
|---|---|---|---|
| d0 | 5.1580 | 73db14d1 | 0 |
| d3 b10 (suite) | 4.9080 | b58706b3 | 110 |
| d3 b40 | 4.9080 | a5b43310 | 194 |
| d5 b20 (suite) | 4.9080 | bbc443ea | 206 |
| d5 b80 | 4.9080 | a5b43310 | 423 |
| d5 b200 | 4.9080 | a5b43310 | 594 |
| d7 b100 | 4.9080 | a5b43310 | 450 |

**The search is converged on this deck.** Every searched arm has the same mean to four decimals and
the same win-turn distribution (T4 25.7% / T5 62.2% / T6 9.6% / T7 2.0% / T8 0.4%, 2 unwon); d3 b40,
d5 b80, d5 b200 and d7 b100 are BYTE-IDENTICAL play. d3 b10 and d5 b20 differ from that fixed point
only in budget churn that never moves an outcome. So there is NO win-turn headroom in depth or
budget: 10x the CPU buys nothing. What is left is (a) the mulligan (the deck has no keep table, so
the keep/bottom decisions are the heuristic's -- the 5-Plains flood keeps in the suite are this),
(b) modelling/heuristic gaps in the MOVE SET (a line the enumerator never offers cannot be searched
into), and (c) the goldfish ceiling of the 60 itself. (a) is the next stage by design and waits on
the user's references; (b) is what the claude-play sweep and hand-played references find.

CPU profile (`perf record -g`, build/Profile, d5 b20, 320 games, `logs/critter/perf/d5.flat.txt`):
FLAT. Top self-time: BuildSimKey 4.7% (memo key), CollectActions 3.5%, SolveUncached 3.2%,
operator new/delete + vector/GameState copies ~7% combined, LookupCached ~3.7% (three clones),
ResolveCombatDamage 1.7%, EffectiveSpellCost 1.7%, FireLifegainWatchers 0.8%. Nothing deck-specific
is hot; the lifegain watchers are under 1%. A micro-optimisation pass (allocation, state copies)
might buy 10-15% suite-wide -- and since the search is converged, faster search would buy Critter
no play quality at all. Not pursued.

Site-9 cost re-read against this: the ~9-14% it costs on Critter is real CPU for zero Critter
win-turn, which is consistent with the ladder (nothing at any budget moves this deck).

## Open questions surfaced (non-blocking)
(collected here and re-raised in the closing message)

<!-- verify_deck:begin (generated -- do not edit inside) -->
## Last verification (2026-09-08)

`verify_deck.py decks/CritterLifegain/CritterLifegain.cod --no-network --write-ledger` -> **PASS**

| Gate | Status | Blocking | Summary |
|---|---|---|---|
| coverage | PASS | yes | all 14 cards full (missing=0, partial=0) |
| card_costs | SKIP | yes | skipped (--no-network) |
| card_fields | PASS | yes | 371 cards match snapshot (cost/PT/types/keywords); 9 allowlisted divergence(s) |
| clause_ledger | SKIP | no | covered by coverage+bracket-notes+oracle-diff |
| viewer | PASS | yes | self-guard + surface sweep clean |
| viewer_wiring | PASS | yes | 2 type(s) wired (emitter + GUI): bounce, target |
| mismatch | PASS | yes | no nonconv/fd-diverge across seeds [7001, 7002] x 60 games (both arms completed) |
| play_invariants | PASS | yes | 8 game(s)/154 decisions: determinism+integrity+progress hold |
| claude_sweep | PASS | yes | Claude-play sweep recorded, 0 unresolved flags |

### Pending user sign-off (block the gate until fixed OR approved below)
_none_ -- every blocking gate is green or already signed off.

### Stage 6a disclosure (deferrals + not-yet-built checks)
- coverage deferral -- Soul Warden: any_creature_enters_lifegain 1 -- fires for every OTHER creature entering on EITHER side (including the deck's own gift tokens entering under the opponent). Our lifegain is not itself damage, but it is NOT inert either: it is a resource vs our own pain/shock/fetch life costs, and in a deck with a 'whenever you gain life' watcher (Ajani's Pridemate / Voice of the Blessed / Archangel of Thune / Heliod) each trigger is its OWN life-gain event (CR 119.10) routed through the shared GainLife hook -- so it converts straight into board presence.
- coverage deferral -- Soul's Attendant: any_creature_enters_lifegain 1 -- identical model to Soul Warden; fires for every OTHER creature entering on EITHER side (our casts, our tokens, the opponent's scheduled spawns), and never for itself. The 'you may' is ALWAYS TAKEN (same convention as Suture Priest): the passive opponent has no lifegain-hate, life_self MORE-dominates in the search prune, and every declined trigger would be a lost +1/+1 counter on Ajani's Pridemate / Voice of the Blessed and a lost Archangel of Thune pump -- so declining is provably dominated, not a searched branch. Each Attendant's gain is its OWN life-gain event (CR 119.10) routed through the shared GainLife hook: 4 Attendants + 4 Wardens on one creature entering = 8 separate gains = 8 lifegain triggers, not one gain of 8.
- coverage deferral -- Serra Ascendant: Lifelink is the parsed keyword, read by CreatureHasLifelink at Combat.cpp's damage site; its gain is its own life-gain event (CR 119.10), applied AFTER every attacker's damage (CR 510.2). The 30-life clause is a CONDITIONAL STATIC self-buff (life_threshold_pump_life 30 / _power 5 / _tough 5), evaluated inside ComputeLordBonus beside domain_self_pump -- which is why that function takes the whole GameState: the battlefield alone cannot see a life total. Continuously checked (CR 611.3), so it turns on and off with the life total; deliberately NOT temp_power_bonus, which is the until-EOT lane. Starting life is 20, so this needs +10: routinely reached here off 12 enter-watchers (Soul Warden / Soul's Attendant / Auriok Champion) firing on both sides' creatures including opponent spawns. PARTIAL: the granted FLYING is inert -- the passive opponent never blocks and no engine site reads Keyword::Flying in combat (the sole reader is haste_on_flying_enter, Dragon Tempest, not in this deck).
- coverage deferral -- Ajani's Pridemate: lifegain_self_counters 1 -- fires ONCE PER LIFE-GAIN EVENT for this permanent's controller (CR 119.10a), not per point of life: one Soul Warden trigger = 1 counter, two Soul Wardens on one creature entering = 2 events = 2 counters, an Archangel of Thune lifelink hit of 4 = 1 event = 1 counter, and a gain of 0 is no event at all (no counter). Routed through the shared GainLife(state, player, amount) hook so every lifegain site in the engine feeds it identically in executor and rollout. THE COUNTERS ARE THE CLOCK: our own life total is goldfish-inert (opponent life is the wincon), so this card is the deck's mechanism for converting inert lifegain into real damage. It sees its OWN entry's gains: when it enters with a Soul Warden out, the Warden's trigger resolves while the Pridemate is already on the battlefield, so it enters and immediately becomes a 3/3 (per Warden). This entry doubles as the token definition for Ajani, Strength of the Pride's -2 (the token is named 'Ajani's Pridemate' and has this exact ability), so the token's trigger is live via name lookup.
- coverage deferral -- Voice of the Blessed: lifegain_self_counters 1 -- the SAME param as Ajani's Pridemate and the Ajani, Strength of the Pride token (identical oracle sentence); fires ONCE PER LIFE-GAIN EVENT via the shared GainLife hook (CR 119.10), so 2 Soul Wardens on one creature entering = 2 counters, not 1. PARTIAL, PROVISIONAL -- the two counter-threshold keyword clauses are NOT modelled. WHY inert: flying is read nowhere in combat (the passive opponent never blocks, so evasion cannot change a damage total); vigilance is live at Combat.cpp's attack-tap but its only consumers are mana-dork attack heuristics and a domain_mana gate, and Voice taps for nothing in a deck with no convoke/crew; indestructible is read only at the lethal-damage SBA and two opponent-only sweepers, and NOTHING in this deck can destroy our creatures -- the opponent never attacks/blocks/casts, Unexpectedly Absent TUCKS (a zone change indestructible never stopped), Ranger-Captain of Eos sacrifices ITSELF (sacrifice ignores indestructible), and Ajani's 0 EXILES opponents' permanents only. Granting the keywords unconditionally was rejected as both unfaithful and behaviour-changing (it would flip the 'never hold a vigilant creature back' attack branch). Disclosed in 6a as an inert-collapse, not a silent drop.
- coverage deferral -- Daxos, Blessed by the Sun: Printed toughness '*' modelled as toughness_equals_devotion_color 'W' -- a CDA read live by DynamicBaseToughness (the Adeline / Voice-of-Resurgence-token pattern), NOT baked. DevotionTo (CR 700.5) counts white pips across the mana costs of every permanent you control INCLUDING Daxos itself, so its toughness is never below 2 while it is on the battlefield and the toughness<=0 SBA can never kill it; the passive goldfish opponent deals no damage and casts no removal, so toughness is otherwise unread -- faithful but outcome-inert, disclosed. Power 2 is printed and real. ONE ability with TWO trigger conditions (CR 603.1), each firing separately: enters -> own_creature_enters_lifegain 1 (shared enter-watcher cascade, own side only, 'another' via the entered_index skip); dies -> own_creature_dies_lifegain 1 (death-watcher fired from OnCreatureDies, which every death site funnels through). 'Another' is structural in both halves: every death site erases the dying permanent BEFORE calling OnCreatureDies, so a battlefield scan cannot see Daxos's own death. Each gain is its own life-gain EVENT (CR 119.10) routed through the shared GainLife hook, so Ajani's Pridemate / Voice of the Blessed / Archangel of Thune / Heliod each see one trigger per creature. KNOWN GAP, disclosed: simultaneous deaths -- the SBA erases every dead creature before firing any death trigger, so a Daxos dying alongside another creature under-triggers (CR 603.6d look-back); unreachable in this deck (no sweeper, no opponent removal, Daxos never at toughness 0). KNOWN GAP, disclosed: EnforceLegendRule graveyards a doomed legend without calling OnCreatureDies, so a second Heliod, Sun-Crowned dying to the legend rule while it is a creature does not trigger this (a 1-life miss on a 2-of x 2-of collision). Legendary is engine-enforced by EnforceLegendRule; irrelevant at 1 copy.
- coverage deferral -- Orzhov Basilica: Karoo bounce land: enters tapped, makes 2 mana ({W}{B}, modelled as wild like other duals), and on ETB returns one of your lands to hand (BounceKarooLand prefers a tapped land so no mana is lost this turn; the returned land must be replayed, the real tempo cost).
- coverage deferral -- Ajani, Strength of the Pride: Enters L5; one loyalty ability/turn (ActivateLoyalty plan variants), dies at loyalty<=0. +1 is the deck's most reliable repeatable lifegain EVENT source: it fires GainLife ONCE (CR 119.10 -- one source, one event), so the AMOUNT (creatures + planeswalkers, Ajani counting himself) does not scale the number of Pridemate/Voice/Archangel counters; it scales only the life TOTAL, which matters for Serra Ascendant's 30-life threshold and for this card's own 0. -2's token is created NAMED, so LookupCached resolves the real Ajani's Pridemate entry and its lifegain_self_counters trigger is live; the token also enters through the universal FireEtbWatchers cascade, so Soul Warden / Soul's Attendant / Auriok Champion / Daxos each fire on it (each is its OWN gain event -> a counter on every Pridemate and Voice incl. the new token, and Archangel of Thune's team pump). 0 NOT ENUMERATED for the search (PROVISIONAL, disclosed 6a): the exile half is inert -- the passive opponent's only permanents are spawn tokens that never attack or block, and exiling them does not stop future spawns from entering (which is lifegain FOR US) -- while exiling Ajani is pure cost, forfeiting every future +1 event and -2 body. Strictly negative vs this opponent, so it is a value gate, not a legality one: under HumanPlayActive() it is enumerated whenever rules-legal and resolves faithfully (below the threshold it does nothing), per the viewer's never-narrow-a-legal-choice rule (Oko/Bolas precedent). The 15-life threshold is measured against gamesetup::StartingLife(), not a literal 20. Legend rule engine-enforced (x3): the generic LegendKeepIndex keeps the walker with the most loyalty (tie: the one that can still activate, then oldest).
- coverage deferral -- Archangel of Thune: lifegain_each_own_creature_counters 1 -- fires once per LIFE-GAIN EVENT (CR 119.10) through the shared GainLife/FireLifegainWatchers hook, so 2x Soul Warden on one creature entering = 2 events = 2 counters on the whole team, and two lifelink attackers in one combat = 2 events. Counters land on EVERY creature we control including Thune itself and creatures that entered this turn. Flying is INERT: the single passive opponent never blocks (there is no blocker path in ResolveCombatDamage), so evasion can never change an outcome. Lifelink is MODELLED and NOT inert here -- its combat damage is a lifegain event that grows the team; per CR 510.2 combat damage is simultaneous, so counters from a combat lifelink gain do NOT add to that same combat's damage (Combat.cpp defers the gains until after every attacker's damage is assigned). Double strike is collapsed to one damage event engine-wide (inert here: no double strike in the deck).
- coverage deferral -- Heliod, Sun-Crowned: Devotion-gated creature-ness = creature_requires_devotion 5 + devotion_color "W", a LAYER-4 type-changing static evaluated by RefreshDevotionCreatures at the shared battlefield-membership chokepoints (enter cascade top, every death, the legend rule, exile/tuck, loyalty death, and both turn-start resyncs). It toggles CardType::Creature on the PERMANENT's own Card copy, so all existing IsCreature() sites become correct at once. DevotionTo counts Heliod's own {W} (CR 700.5) and hybrid pips both ways. Ordering is load-bearing: the refresh runs ABOVE FireEtbWatchers' IsCreature() gate, so a Heliod entering into devotion>=5 DOES fire Soul Warden and one entering below 5 does not (CR 603.6d + the printed ruling); when a later permanent lifts devotion to 5 Heliod flips on WITHOUT entering, so no enter watcher fires. Summoning sickness needs no new code -- entered_this_turn tracks control duration and clears at turn start (CR 302.6), so a Heliod that turns on turns after it landed may attack; is_animated is deliberately NOT reused because it would grant haste. Lifegain trigger = lifegain_target_own_counter, fired once per life-gaining EVENT by the shared GainLife hook (printed ruling: two lifelink attackers = two events; one 3-life gain = one event; 'for each' = one event; 2 Soul Wardens on one entrant = two events). Target is a RESOLUTION heuristic returning ONE pick (DefaultLifegainCounterTarget: the highest-power own creature that can still attack this turn, else the highest-power own creature; tie-break lowest m_number) rather than a searched branch: this deck produces 5-15 gain events per turn, so branching is a Cartesian explosion -- disclosed 6a; MTG_UNPRUNED does not open it. Human play picks off the board from every legal creature-or-enchantment, Heliod included -- legal (printed ruling: it can target itself) but dead value while it is not a creature. Activated ability = lifelink_grant_cost {1}{W}, PermAbilityMode::GrantLifelink: no {T}, no sacrifice, so repeatable within a turn and bounded only by mana (the Drain/ExileTop/IceCounter shape); it functions at devotion < 5 (printed ruling: a God's abilities work whether or not it is a creature). K capped by the count of OTHER own attack-eligible creatures lacking lifelink; the grant rides an until-EOT temp_lifelink flag on Permanent, cleared at both cleanup sites and folded into the sim key only when set; autonomous target = highest-power such attacker (human: board click over every other creature). Lifelink is NOT goldfish-inert here: each lifelink damage event is a life-gain event, which is a Heliod/Pridemate/Voice counter and an Archangel team pump. PARTIAL: Indestructible is wired (the lethal-damage SBA and both destroy effects read it) but structurally inert vs a passive opponent that never blocks, attacks, or casts removal. PARTIAL: the 'loses the creature type God' and 'removed from combat if it stops being a creature' halves of the type-changing static are unmodelled -- nothing reads the God subtype, and devotion can only fall if one of our own permanents leaves, which this opponent cannot cause.
- coverage deferral -- Auriok Champion: Protection from black and from red is INERT on all four DEBT axes here: Damage -- nothing on either side deals damage (passive opponent never attacks/casts; its spawns are vanilla bodies; the deck has no burn); Enchant/Equip -- the deck runs zero Auras and zero Equipment, and colored protection would not stop a colourless Equipment anyway (unlike Progenitus' protection_from_everything, which DOES block one, CR 702.6b); Block -- the opponent never blocks; Target -- the opponent never targets, and every targeting source in this deck (Heliod, Sun-Crowned's counter trigger; Unexpectedly Absent) is WHITE, so this creature remains a legal Heliod target. Deliberately NOT given protection_from_everything: that would wrongly exclude it from Heliod's targets. Orzhov Basilica producing {B} is irrelevant -- mana is not a source. Trigger: any_creature_enters_lifegain 1, the same model as Soul Warden -- fires for every OTHER creature entering on EITHER side, including the passive opponent's spawn tokens. The 'you may' is ALWAYS TAKEN (strictly beneficial: nothing here punishes life gain and every point feeds Pridemate/Voice/Archangel/Heliod/Serra Ascendant), a disclosed auto-decision, not a surfaced choice -- same treatment as Suture Priest. Each copy is its own trigger: 4 Champions = 4 SEPARATE 1-life events per creature entering (CR 119.10).
- coverage deferral -- Ranger-Captain of Eos: ETB SINGLE-tutor to HAND (Ranger of Eos's params minus etb_tutor_hand_count, which selects the multi path at >1): tutor_to_hand + tutor_types Creature + tutor_max_mv 1 + tutor_shuffle_after. Legal pool in this deck is exactly Soul Warden / Soul's Attendant / Serra Ascendant (every other creature is MV>=2); WHICH one is a searched main_phase plan variant (tutor axis, width 6 >= 3 candidates, so all three are scored; plan-less paths take the first library match, disclosed). 'You may' is ALWAYS taken when a legal target exists -- declining is legal but never right with three castable {W} bodies; the only theoretical cost is the post-search shuffle, disclosed (same treatment as Ranger of Eos). The fetched card lands in HAND and, per the engine-wide plan model, cannot be cast in the same turn (disclosed engine limitation). Clause 2: the effect ('opponents can't cast noncreature spells') is INERT -- the passive goldfish opponent never casts a spell of any kind -- but the SACRIFICE is real and is modelled: sac_creature_outlet + sac_outlet_self_only (the cost is 'Sacrifice this creature', so the source is the ONLY legal victim), with no payload params, so the activation's whole effect is the death event routed through OnCreatureDies. This is the deck's only way to kill its own creature, which matters solely because Daxos, Blessed by the Sun turns it into a lifegain event (-> Pridemate / Voice / Archangel of Thune / Heliod counters). Emitted to the search only when such a payoff is live (SelfSacHasDeathPayoff); with none on board the sac is strictly dominated (loses a 3/3, changes nothing else) so omitting it is lossless. Human play always sees it. Sac timing: the engine plays first main only, so the search sacs pre-combat and forgoes that turn's 3 damage -- a small disclosed under-rating (the Goblins DeferSacOutletPreCombat gap).
- coverage deferral -- Unexpectedly Absent: Targeting::NonlandPermanent (approved full scope 2026-08-13). Opponent target: every spawn is a TOKEN, so it ceases on leaving the battlefield (CR 111.7) -- the erase is faithful, X irrelevant there. Own target: a real Library::insert at min(X, size) (the Stoneforge-reset line). Pruned search: X=0 only (higher X provably never better for either use) and opponent-creature targets only (self-tuck approved-lean per user); MTG_UNPRUNED / human play opens the full X range and every nonland permanent incl. own side.
- card_costs SKIPPED (--no-network) -- Scryfall cost/cmc reality-diff not run
- allowlisted divergence -- Galerider Sliver [keywords]: Keyword-lord: 'Sliver creatures you control have flying' grants flying to your Slivers INCLUDING itself, so the card functionally has flying (modeled 
- allowlisted divergence -- Striking Sliver [keywords]: Keyword-lord: grants first strike to your Slivers incl. itself (modeled self-innate). First strike is inert in goldfishing (no blockers). See oracle b
- allowlisted divergence -- Cloudshredder Sliver [keywords]: Keyword-lord: grants flying+haste to your Slivers incl. itself. Flying self-innate + inert in goldfishing; haste additionally granted to other Slivers
- allowlisted divergence -- Haytham Kenway [keywords]: 'Protection from Assassins' is a real keyword but inert in goldfishing (no Assassins in play); the protection-to-other-Knights is an anthem grant, not
- allowlisted divergence -- Goblin Piledriver [keywords]: 'Protection from blue' is a real keyword but inert in goldfishing (the passive opponent has no blue sources or blockers to target); the attack-trigger
- allowlisted divergence -- Progenitus [keywords]: 'Protection from everything' is a real keyword but inert in goldfishing (the passive opponent never targets, blocks, or damages); the graveyard shuffl
- allowlisted divergence -- Bloom Tender [keywords]: Scryfall lists 'vivid' in keywords -- a data quirk (no rules-meaningful innate keyword on this card); the each-color-among-permanents mana ability is 
- allowlisted divergence -- Glorybringer [keywords]: 'Exert' is a real keyword but its use is OPTIONAL and provably worthless here: exerting costs the next untap step (so Glorybringer cannot attack the f
- allowlisted divergence -- Auriok Champion [keywords]: 'Protection from black and from red' is a real keyword but inert in goldfishing on all four DEBT axes (the passive opponent never damages, targets, bl
- oracle_text advisory -- Light Up the Stage: oracle_text diverges (similarity 0.69); scryfall='Spectacle {R} (You may cast this spell for its spectacle cost rather than its mana cost if an opponent lost li
- oracle_text advisory -- Crystalline Sliver: oracle_text diverges (similarity 0.61); scryfall="All Slivers have shroud. (They can't be the targets of spells or abilities.)"
- oracle_text advisory -- Galerider Sliver: oracle_text diverges (similarity 0.41); scryfall='Sliver creatures you control have flying.'
- oracle_text advisory -- Striking Sliver: oracle_text diverges (similarity 0.56); scryfall='Sliver creatures you control have first strike. (They deal combat damage before creatures without first strike
- oracle_text advisory -- Cloudshredder Sliver: oracle_text diverges (similarity 0.48); scryfall='Sliver creatures you control have flying and haste.'
- oracle_text advisory -- Hibernation Sliver: oracle_text diverges (similarity 0.49); scryfall='All Slivers have "Pay 2 life: Return this permanent to its owner\'s hand."'
- oracle_text advisory -- Cavern of Souls: oracle_text diverges (similarity 0.75); scryfall="As this land enters, choose a creature type.
{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Unclaimed Territory: oracle_text diverges (similarity 0.75); scryfall='As this land enters, choose a creature type.
{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Secluded Courtyard: oracle_text diverges (similarity 0.44); scryfall='As this land enters, choose a creature type.
{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Mutavault: oracle_text diverges (similarity 0.56); scryfall="{T}: Add {C}.
{1}: This land becomes a 2/2 creature with all creature types until end of turn. It's still a l
- oracle_text advisory -- Aether Vial: oracle_text diverges (similarity 0.71); scryfall='At the beginning of your upkeep, you may put a charge counter on this artifact.
{T}: You may put a creature c
- oracle_text advisory -- Reliquary Tower: oracle_text diverges (similarity 0.44); scryfall='You have no maximum hand size.
{T}: Add {C}.'
- oracle_text advisory -- Dwarven Hold: oracle_text diverges (similarity 0.23); scryfall='This land enters tapped.
You may choose not to untap this land during your untap step.
At the beginning of y
- oracle_text advisory -- Mercadian Bazaar: oracle_text diverges (similarity 0.26); scryfall='This land enters tapped.
{T}: Put a storage counter on this land.
{T}, Remove any number of storage counters
- oracle_text advisory -- Temple of Epiphany: oracle_text diverges (similarity 0.60); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Thundering Falls: oracle_text diverges (similarity 0.63); scryfall='({T}: Add {U} or {R}.)
This land enters tapped.
When this land enters, surveil 1. (Look at the top card of y
- oracle_text advisory -- Land's Edge: oracle_text diverges (similarity 0.51); scryfall='Discard a card: If the discarded card was a land card, this enchantment deals 2 damage to target player or pla
- oracle_text advisory -- Throes of Chaos: oracle_text diverges (similarity 0.06); scryfall='Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland card tha
- oracle_text advisory -- Tournament Grounds: oracle_text diverges (similarity 0.37); scryfall='{T}: Add {C}.
{T}: Add {R}, {W}, or {B}. Spend this mana only to cast a Knight or Equipment spell.'
- oracle_text advisory -- Dauntless Bodyguard: oracle_text diverges (similarity 0.55); scryfall='As this creature enters, choose another creature you control.
Sacrifice this creature: The chosen creature ga
- oracle_text advisory -- Venerable Knight: oracle_text diverges (similarity 0.52); scryfall='When this creature dies, put a +1/+1 counter on target Knight you control.'
- oracle_text advisory -- Worthy Knight: oracle_text diverges (similarity 0.45); scryfall='Whenever you cast a Knight spell, create a 1/1 white Human creature token.'
- oracle_text advisory -- Acclaimed Contender: oracle_text diverges (similarity 0.77); scryfall='When this creature enters, if you control another Knight, look at the top five cards of your library. You may 
- oracle_text advisory -- Knight Exemplar: oracle_text diverges (similarity 0.41); scryfall='First strike (This creature deals combat damage before creatures without first strike.)
Other Knight creature
- oracle_text advisory -- Marshal of Zhalfir: oracle_text diverges (similarity 0.49); scryfall='Other Knights you control get +1/+1.
{W}{U}, {T}: Tap another target creature.'
- oracle_text advisory -- Haytham Kenway: oracle_text diverges (similarity 0.53); scryfall='Protection from Assassins
Other Knights you control get +2/+2 and have protection from Assassins.
When Hayth
- oracle_text advisory -- Adeline, Resplendent Cathar: oracle_text diverges (similarity 0.76); scryfall="Vigilance
Adeline's power is equal to the number of creatures you control.
Whenever you attack, for each opp
- oracle_text advisory -- Windswept Heath: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Plains card, put it onto the battlef
- oracle_text advisory -- Marsh Flats: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Swamp card, put it onto the battlefi
- oracle_text advisory -- Bloodstained Mire: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Mountain card, put it onto the battle
- oracle_text advisory -- Wooded Foothills: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Mountain or Forest card, put it onto the battl
- oracle_text advisory -- Grove of the Burnwillows: oracle_text diverges (similarity 0.20); scryfall='{T}: Add {C}.
{T}: Add {R} or {G}. Each opponent gains 1 life.'
- oracle_text advisory -- Ignoble Hierarch: oracle_text diverges (similarity 0.56); scryfall='Exalted (Whenever a creature you control attacks alone, that creature gets +1/+1 until end of turn.)
{T}: Add
- oracle_text advisory -- Skyshroud Cutter: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 5 life."
- oracle_text advisory -- Plague Drone: oracle_text diverges (similarity 0.70); scryfall='Flying
Rot Fly — If an opponent would gain life, that player loses that much life instead.'
- oracle_text advisory -- Aria of Flame: oracle_text diverges (similarity 0.78); scryfall='When this enchantment enters, each opponent gains 10 life.
Whenever you cast an instant or sorcery spell, put
- oracle_text advisory -- Fiery Justice: oracle_text diverges (similarity 0.54); scryfall='Fiery Justice deals 5 damage divided as you choose among any number of targets. Target opponent gains 5 life.'
- oracle_text advisory -- Swords to Plowshares: oracle_text diverges (similarity 0.44); scryfall='Exile target creature. Its controller gains life equal to its power.'
- oracle_text advisory -- Invigorate: oracle_text diverges (similarity 0.52); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have an opponent gain 3 life.
Target
- oracle_text advisory -- Reverent Silence: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 6 life.

- oracle_text advisory -- Idyllic Tutor: oracle_text diverges (similarity 0.43); scryfall='Search your library for an enchantment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Enlightened Tutor: oracle_text diverges (similarity 0.55); scryfall='Search your library for an artifact or enchantment card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Forbidden Orchard: oracle_text diverges (similarity 0.22); scryfall='{T}: Add one mana of any color.
Whenever you tap this land for mana, target opponent creates a 1/1 colorless 
- oracle_text advisory -- Reflecting Pool: oracle_text diverges (similarity 0.26); scryfall='{T}: Add one mana of any type that a land you control could produce.'
- oracle_text advisory -- Izzet Signet: oracle_text diverges (similarity 0.11); scryfall='{1}, {T}: Add {U}{R}.'
- oracle_text advisory -- Ponder: oracle_text diverges (similarity 0.32); scryfall='Look at the top three cards of your library, then put them back in any order. You may shuffle.
Draw a card.'
- oracle_text advisory -- Preordain: oracle_text diverges (similarity 0.27); scryfall='Scry 2, then draw a card. (To scry 2, look at the top two cards of your library, then put any number of them o
- oracle_text advisory -- Expressive Iteration: oracle_text diverges (similarity 0.45); scryfall='Look at the top three cards of your library. Put one of them into your hand, put one of them on the bottom of 
- oracle_text advisory -- Crackle with Power: oracle_text diverges (similarity 0.17); scryfall='Crackle with Power deals five times X damage to each of up to X targets.'
- oracle_text advisory -- Remand: oracle_text diverges (similarity 0.58); scryfall="Counter target spell. If that spell is countered this way, put it into its owner's hand instead of into that p
- oracle_text advisory -- Memory Lapse: oracle_text diverges (similarity 0.69); scryfall="Counter target spell. If that spell is countered this way, put it on top of its owner's library instead of int
- oracle_text advisory -- Distorting Wake: oracle_text diverges (similarity 0.34); scryfall="Return X target nonland permanents to their owners' hands."
- oracle_text advisory -- Icy Blast: oracle_text diverges (similarity 0.64); scryfall="Tap X target creatures.
Ferocious — If you control a creature with power 4 or greater, those creatures don't 
- oracle_text advisory -- Hinata, Dawn-Crowned: oracle_text diverges (similarity 0.31); scryfall='Flying, trample
Spells you cast cost {1} less to cast for each target.
Spells your opponents cast cost {1} m
- oracle_text advisory -- Izzet Boilerworks: oracle_text diverges (similarity 0.42); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {U}{
- oracle_text advisory -- Orzhov Basilica: oracle_text diverges (similarity 0.42); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {W}{
- oracle_text advisory -- Soulfire Eruption: oracle_text diverges (similarity 0.28); scryfall="Choose any number of target creatures, planeswalkers, and/or players. For each of them, exile the top card of 
- oracle_text advisory -- Magma Opus: oracle_text diverges (similarity 0.46); scryfall='Magma Opus deals 4 damage divided as you choose among any number of targets. Tap two target permanents. Create
- oracle_text advisory -- Reality Spasm: oracle_text diverges (similarity 0.20); scryfall='Choose one —
• Tap X target permanents.
• Untap X target permanents.'
- oracle_text advisory -- Ornithopter of Paradise: oracle_text diverges (similarity 0.13); scryfall='Flying
{T}: Add one mana of any color.'
- oracle_text advisory -- Gamble: oracle_text diverges (similarity 0.22); scryfall='Search your library for a card, put that card into your hand, discard a card at random, then shuffle.'
- oracle_text advisory -- Irencrag Feat: oracle_text diverges (similarity 0.10); scryfall='Add seven {R}. You can cast only one more spell this turn.'
- oracle_text advisory -- Pyretic Ritual: oracle_text diverges (similarity 0.05); scryfall='Add {R}{R}{R}.'
- oracle_text advisory -- Seething Song: oracle_text diverges (similarity 0.08); scryfall='Add {R}{R}{R}{R}{R}.'
- oracle_text advisory -- Desperate Ritual: oracle_text diverges (similarity 0.18); scryfall="Add {R}{R}{R}.
Splice onto Arcane {1}{R} (As you cast an Arcane spell, you may reveal this card from your han
- oracle_text advisory -- Dragonlord Kolaghan: oracle_text diverges (similarity 0.53); scryfall='Flying, haste
Other creatures you control have haste.
Whenever an opponent casts a creature or planeswalker 
- oracle_text advisory -- Karrthus, Tyrant of Jund: oracle_text diverges (similarity 0.37); scryfall='Flying, haste
When Karrthus enters, gain control of all Dragons, then untap all Dragons.
Other Dragon creatu
- oracle_text advisory -- Ruby Medallion: oracle_text diverges (similarity 0.17); scryfall='Red spells you cast cost {1} less to cast.'
- oracle_text advisory -- Lotus Bloom: oracle_text diverges (similarity 0.25); scryfall='Suspend 3—{0} (Rather than cast this card from your hand, pay {0} and exile it with three time counters on it.
- oracle_text advisory -- Rite of Flame: oracle_text diverges (similarity 0.21); scryfall='Add {R}{R}, then add {R} for each card named Rite of Flame in each graveyard.'
- oracle_text advisory -- Scourge of Valkas: oracle_text diverges (similarity 0.32); scryfall='Flying
Whenever this creature or another Dragon you control enters, it deals X damage to any target, where X 
- oracle_text advisory -- Lathliss, Dragon Queen: oracle_text diverges (similarity 0.31); scryfall='Flying
Whenever another nontoken Dragon you control enters, create a 5/5 red Dragon creature token with flyin
- oracle_text advisory -- Utvara Hellkite: oracle_text diverges (similarity 0.24); scryfall='Flying
Whenever a Dragon you control attacks, create a 6/6 red Dragon creature token with flying.'
- oracle_text advisory -- Dragonstorm: oracle_text diverges (similarity 0.16); scryfall='Search your library for a Dragon permanent card, put it onto the battlefield, then shuffle.
Storm (When you c
- oracle_text advisory -- Apex of Power: oracle_text diverges (similarity 0.15); scryfall='Exile the top seven cards of your library. Until end of turn, you may cast spells from among them.
If this sp
- oracle_text advisory -- Slippery Bogle: oracle_text diverges (similarity 0.41); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Gladecover Scout: oracle_text diverges (similarity 0.76); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Kor Spiritdancer: oracle_text diverges (similarity 0.53); scryfall='This creature gets +2/+2 for each Aura attached to it.
Whenever you cast an Aura spell, you may draw a card.'
- oracle_text advisory -- Light-Paws, Emperor's Voice: oracle_text diverges (similarity 0.74); scryfall='Whenever an Aura you control enters, if you cast it, you may search your library for an Aura card with mana va
- oracle_text advisory -- Ethereal Armor: oracle_text diverges (similarity 0.60); scryfall='Enchant creature
Enchanted creature gets +1/+1 for each enchantment you control and has first strike.'
- oracle_text advisory -- Rancor: oracle_text diverges (similarity 0.68); scryfall="Enchant creature
Enchanted creature gets +2/+0 and has trample.
When this Aura is put into a graveyard from 
- oracle_text advisory -- Daybreak Coronet: oracle_text diverges (similarity 0.58); scryfall='Enchant creature with another Aura attached to it
Enchanted creature gets +3/+3 and has first strike, vigilan
- oracle_text advisory -- Armadillo Cloak: oracle_text diverges (similarity 0.77); scryfall='Enchant creature
Enchanted creature gets +2/+2 and has trample.
Whenever enchanted creature deals damage, yo
- oracle_text advisory -- Spirit Mantle: oracle_text diverges (similarity 0.66); scryfall='Enchant creature
Enchanted creature gets +1/+1 and has protection from creatures.'
- oracle_text advisory -- Spider Umbra: oracle_text diverges (similarity 0.40); scryfall='Enchant creature
Enchanted creature gets +1/+1 and has reach. (It can block creatures with flying.)
Umbra ar
- oracle_text advisory -- Ancestral Mask: oracle_text diverges (similarity 0.59); scryfall='Enchant creature
Enchanted creature gets +2/+2 for each other enchantment on the battlefield.'
- oracle_text advisory -- Alpha Authority: oracle_text diverges (similarity 0.54); scryfall="Enchant creature
Enchanted creature has hexproof and can't be blocked by more than one creature."
- oracle_text advisory -- Gryff's Boon: oracle_text diverges (similarity 0.75); scryfall='Enchant creature
Enchanted creature gets +1/+0 and has flying.
{3}{W}: Return this card from your graveyard 
- oracle_text advisory -- Audacity: oracle_text diverges (similarity 0.59); scryfall="Enchant creature
Enchanted creature gets +2/+0 and has trample. (It can deal excess combat damage to the play
- oracle_text advisory -- All That Glitters: oracle_text diverges (similarity 0.57); scryfall='Enchant creature
Enchanted creature gets +1/+1 for each artifact and/or enchantment you control.'
- oracle_text advisory -- Spirit Link: oracle_text diverges (similarity 0.47); scryfall='Enchant creature (Target a creature as you cast this. This card enters attached to that creature.)
Whenever e
- oracle_text advisory -- Lion Umbra: oracle_text diverges (similarity 0.77); scryfall='Enchant modified creature (Equipment, Auras its controller controls, and counters are modifications.)
Enchant
- oracle_text advisory -- Brushland: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{T}: Add {G} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Branchloft Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Darkbore Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {B}.'
- oracle_text advisory -- Goblin King: oracle_text diverges (similarity 0.31); scryfall='Other Goblins get +1/+1 and have mountainwalk.'
- oracle_text advisory -- Goblin Chieftain: oracle_text diverges (similarity 0.41); scryfall='Haste (This creature can attack and {T} as soon as it comes under your control.)
Other Goblin creatures you c
- oracle_text advisory -- Goblin Warchief: oracle_text diverges (similarity 0.52); scryfall='Goblin spells you cast cost {1} less to cast.
Goblins you control have haste.'
- oracle_text advisory -- Goblin Piledriver: oracle_text diverges (similarity 0.43); scryfall="Protection from blue (This creature can't be blocked, targeted, dealt damage, or enchanted by anything blue.)\
- oracle_text advisory -- Goblin Matron: oracle_text diverges (similarity 0.66); scryfall='When this creature enters, you may search your library for a Goblin card, reveal that card, put it into your h
- oracle_text advisory -- Mogg War Marshal: oracle_text diverges (similarity 0.56); scryfall='Echo {1}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Siege-Gang Commander: oracle_text diverges (similarity 0.58); scryfall='When this creature enters, create three 1/1 red Goblin creature tokens.
{1}{R}, Sacrifice a Goblin: This crea
- oracle_text advisory -- Skirk Prospector: oracle_text diverges (similarity 0.31); scryfall='Sacrifice a Goblin: Add {R}.'
- oracle_text advisory -- Krenko, Mob Boss: oracle_text diverges (similarity 0.43); scryfall='{T}: Create X 1/1 red Goblin creature tokens, where X is the number of Goblins you control.'
- oracle_text advisory -- Pashalik Mons: oracle_text diverges (similarity 0.52); scryfall='Whenever Pashalik Mons or another Goblin you control dies, Pashalik Mons deals 1 damage to any target.
{3}{R}
- oracle_text advisory -- Rundvelt Hordemaster: oracle_text diverges (similarity 0.36); scryfall="Other Goblins you control get +1/+1.
Whenever this creature or another Goblin you control dies, exile the top
- oracle_text advisory -- Goblin Lackey: oracle_text diverges (similarity 0.56); scryfall='Whenever this creature deals damage to a player, you may put a Goblin permanent card from your hand onto the b
- oracle_text advisory -- Muxus, Goblin Grandee: oracle_text diverges (similarity 0.08); scryfall='When Muxus enters, reveal the top six cards of your library. Put all Goblin creature cards with mana value 5 o
- oracle_text advisory -- Goblin Chainwhirler: oracle_text diverges (similarity 0.40); scryfall='First strike
When this creature enters, it deals 1 damage to each opponent and each creature and planeswalker
- oracle_text advisory -- Twinshot Sniper: oracle_text diverges (similarity 0.50); scryfall='Reach
When this creature enters, it deals 2 damage to any target.
Channel — {1}{R}, Discard this card: It de
- oracle_text advisory -- Stingscourger: oracle_text diverges (similarity 0.71); scryfall="Echo {3}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Three Tree City: oracle_text diverges (similarity 0.47); scryfall='As Three Tree City enters, choose a creature type.
{T}: Add {C}.
{2}, {T}: Choose a color. Add an amount of 
- oracle_text advisory -- Hunted Phantasm: oracle_text diverges (similarity 0.34); scryfall="This creature can't be blocked.
When this creature enters, target opponent creates five 1/1 red Goblin creatu
- oracle_text advisory -- Suture Priest: oracle_text diverges (similarity 0.49); scryfall='Whenever another creature you control enters, you may gain 1 life.
Whenever a creature an opponent controls e
- oracle_text advisory -- Massacre Wurm: oracle_text diverges (similarity 0.38); scryfall='When this creature enters, creatures your opponents control get -2/-2 until end of turn.
Whenever a creature 
- oracle_text advisory -- Soul Warden: oracle_text diverges (similarity 0.15); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- Essence Warden: oracle_text diverges (similarity 0.24); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- City of Brass: oracle_text diverges (similarity 0.45); scryfall='Whenever this land becomes tapped, it deals 1 damage to you.
{T}: Add one mana of any color.'
- oracle_text advisory -- Defense of the Heart: oracle_text diverges (similarity 0.43); scryfall='At the beginning of your upkeep, if an opponent controls three or more creatures, sacrifice this enchantment, 
- oracle_text advisory -- Sylvan Scrying: oracle_text diverges (similarity 0.47); scryfall='Search your library for a land card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Crop Rotation: oracle_text diverges (similarity 0.42); scryfall='As an additional cost to cast this spell, sacrifice a land.
Search your library for a land card, put that car
- oracle_text advisory -- Varchild's War-Riders: oracle_text diverges (similarity 0.58); scryfall='Cumulative upkeep—Have an opponent create a 1/1 red Survivor creature token. (At the beginning of your upkeep,
- oracle_text advisory -- Azorius Chancery: oracle_text diverges (similarity 0.42); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {W}{
- oracle_text advisory -- Tree of Tales: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Misty Rainforest: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Island card, put it onto the battlef
- oracle_text advisory -- Verdant Catacombs: oracle_text diverges (similarity 0.28); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Forest card, put it onto the battlefi
- oracle_text advisory -- Scalding Tarn: oracle_text diverges (similarity 0.29); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for an Island or Mountain card, put it onto the batt
- oracle_text advisory -- Cosmic Spider-Man: oracle_text diverges (similarity 0.47); scryfall='Flying, first strike, trample, lifelink, haste
At the beginning of combat on your turn, other Spiders you con
- oracle_text advisory -- Mana Cannons: oracle_text diverges (similarity 0.44); scryfall='Whenever you cast a multicolored spell, this enchantment deals X damage to any target, where X is the number o
- oracle_text advisory -- Ancient Cornucopia: oracle_text diverges (similarity 0.43); scryfall="Whenever you cast a spell that's one or more colors, you may gain 1 life for each of that spell's colors. Do t
- oracle_text advisory -- Two-Headed Hellkite: oracle_text diverges (similarity 0.26); scryfall='Flying, menace, haste
Whenever this creature attacks, draw two cards.'
- oracle_text advisory -- Progenitus: oracle_text diverges (similarity 0.28); scryfall="Protection from everything
If Progenitus would be put into a graveyard from anywhere, reveal Progenitus and s
- oracle_text advisory -- Faeburrow Elder: oracle_text diverges (similarity 0.36); scryfall='Vigilance
This creature gets +1/+1 for each color among permanents you control.
{T}: For each color among pe
- oracle_text advisory -- Bloom Tender: oracle_text diverges (similarity 0.52); scryfall='Vivid — {T}: For each color among permanents you control, add one mana of that color.'
- oracle_text advisory -- Deathrite Shaman: oracle_text diverges (similarity 0.46); scryfall='{T}: Exile target land card from a graveyard. Add one mana of any color. (Activate only as an instant.)
{B}, 
- oracle_text advisory -- Lightning Greaves: oracle_text diverges (similarity 0.24); scryfall="Equipped creature has haste and shroud. (It can't be the target of spells or abilities.)
Equip {0}"
- oracle_text advisory -- Maelstrom Archangel: oracle_text diverges (similarity 0.31); scryfall='Flying
Whenever this creature deals combat damage to a player, you may cast a spell from your hand without pa
- oracle_text advisory -- Jared Carthalion: oracle_text diverges (similarity 0.60); scryfall="+1: Create a 3/3 Kavu creature token with trample that's all colors.
−3: Choose up to two target creatures. F
- oracle_text advisory -- Nicol Bolas, Planeswalker: oracle_text diverges (similarity 0.21); scryfall="+3: Destroy target noncreature permanent.
−2: Gain control of target creature.
−9: Nicol Bolas deals 7 damag
- oracle_text advisory -- Oko, Thief of Crowns: oracle_text diverges (similarity 0.42); scryfall='+2: Create a Food token. (It\'s an artifact with "{2}, {T}, Sacrifice this token: You gain 3 life.")
+1: Targ
- oracle_text advisory -- Garth One-Eye: oracle_text diverges (similarity 0.39); scryfall="{T}: Choose a card name that hasn't been chosen from among Disenchant, Braingeyser, Terror, Shivan Dragon, Reg
- oracle_text advisory -- Black Lotus: oracle_text diverges (similarity 0.36); scryfall='{T}, Sacrifice this artifact: Add three mana of any one color.'
- oracle_text advisory -- Braingeyser: oracle_text diverges (similarity 0.23); scryfall='Target player draws X cards.'
- oracle_text advisory -- Terror: oracle_text diverges (similarity 0.32); scryfall="Destroy target nonartifact, nonblack creature. It can't be regenerated."
- oracle_text advisory -- Shivan Dragon: oracle_text diverges (similarity 0.32); scryfall='Flying
{R}: This creature gets +1/+0 until end of turn.'
- oracle_text advisory -- Regrowth: oracle_text diverges (similarity 0.46); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Unite the Coalition: oracle_text diverges (similarity 0.46); scryfall="Choose five. You may choose the same mode more than once.
• Target permanent phases out.
• Target player dra
- oracle_text advisory -- Disenchant: oracle_text diverges (similarity 0.21); scryfall='Destroy target artifact or enchantment.'
- oracle_text advisory -- Mirrorwing Dragon: oracle_text diverges (similarity 0.42); scryfall='Flying
Whenever a player casts an instant or sorcery spell that targets only this creature, that player copie
- oracle_text advisory -- Zada, Hedron Grinder: oracle_text diverges (similarity 0.57); scryfall='Whenever you cast an instant or sorcery spell that targets only Zada, copy that spell for each other creature 
- oracle_text advisory -- Goblin Instigator: oracle_text diverges (similarity 0.32); scryfall='When this creature enters, create a 1/1 red Goblin creature token.'
- oracle_text advisory -- Fists of Flame: oracle_text diverges (similarity 0.36); scryfall="Draw a card. Until end of turn, target creature gains trample and gets +1/+0 for each card you've drawn this t
- oracle_text advisory -- Luxurious Libation: oracle_text diverges (similarity 0.24); scryfall='Target creature gets +X/+X until end of turn. Create a 1/1 green and white Citizen creature token.'
- oracle_text advisory -- Fortifying Draught: oracle_text diverges (similarity 0.33); scryfall='You gain 2 life. Target creature gets +X/+X until end of turn, where X is the amount of life you gained this t
- oracle_text advisory -- Gold Rush: oracle_text diverges (similarity 0.36); scryfall='Create a Treasure token. Until end of turn, up to one target creature gets +2/+2 for each Treasure you control
- oracle_text advisory -- Ancestral Anger: oracle_text diverges (similarity 0.52); scryfall='Target creature gains trample and gets +X/+0 until end of turn, where X is 1 plus the number of cards named An
- oracle_text advisory -- Oracle's Restoration: oracle_text diverges (similarity 0.10); scryfall='Target creature you control gets +1/+1 until end of turn. You draw a card and gain 1 life.'
- oracle_text advisory -- Expedite: oracle_text diverges (similarity 0.29); scryfall='Target creature gains haste until end of turn.
Draw a card.'
- oracle_text advisory -- Impolite Entrance: oracle_text diverges (similarity 0.18); scryfall='Target creature gains trample and haste until end of turn.
Draw a card.'
- oracle_text advisory -- Scale the Heights: oracle_text diverges (similarity 0.49); scryfall='Put a +1/+1 counter on up to one target creature. You gain 2 life. You may play an additional land this turn.\
- oracle_text advisory -- Twinflame: oracle_text diverges (similarity 0.42); scryfall="Strive — This spell costs {2}{R} more to cast for each target beyond the first.
Choose any number of target c
- oracle_text advisory -- Gruul Turf: oracle_text diverges (similarity 0.43); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {R}{
- oracle_text advisory -- Kazandu Refuge: oracle_text diverges (similarity 0.50); scryfall='This land enters tapped.
When this land enters, you gain 1 life.
{T}: Add {R} or {G}.'
- oracle_text advisory -- Rootbound Crag: oracle_text diverges (similarity 0.43); scryfall='This land enters tapped unless you control a Mountain or a Forest.
{T}: Add {R} or {G}.'
- oracle_text advisory -- Colossus Hammer: oracle_text diverges (similarity 0.25); scryfall='Equipped creature gets +10/+10 and loses flying.
Equip {8} ({8}: Attach to target creature you control. Equip
- oracle_text advisory -- Loxodon Warhammer: oracle_text diverges (similarity 0.36); scryfall='Equipped creature gets +3/+0 and has trample and lifelink.
Equip {3}'
- oracle_text advisory -- Shadowspear: oracle_text diverges (similarity 0.54); scryfall='Equipped creature gets +1/+1 and has trample and lifelink.
{1}: Permanents your opponents control lose hexpro
- oracle_text advisory -- Grafted Wargear: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +3/+2.
Whenever this Equipment becomes unattached from a permanent, sacrifice that per
- oracle_text advisory -- O-Naginata: oracle_text diverges (similarity 0.49); scryfall='This Equipment can be attached only to a creature with power 3 or greater.
Equipped creature gets +3/+0 and h
- oracle_text advisory -- Umezawa's Jitte: oracle_text diverges (similarity 0.47); scryfall="Whenever equipped creature deals combat damage, put two charge counters on Umezawa's Jitte.
Remove a charge c
- oracle_text advisory -- Kor Duelist: oracle_text diverges (similarity 0.49); scryfall='As long as this creature is equipped, it has double strike. (It deals both first-strike and regular combat dam
- oracle_text advisory -- Puresteel Paladin: oracle_text diverges (similarity 0.34); scryfall='Whenever an Equipment you control enters, you may draw a card.
Metalcraft — Equipment you control have equip 
- oracle_text advisory -- Balan, Wandering Knight: oracle_text diverges (similarity 0.37); scryfall='First strike
Balan has double strike as long as two or more Equipment are attached to it.
{1}{W}: Attach all
- oracle_text advisory -- Armored Skyhunter: oracle_text diverges (similarity 0.49); scryfall='Flying
Whenever this creature attacks, look at the top six cards of your library. You may put an Aura or Equi
- oracle_text advisory -- Kemba, Kha Regent: oracle_text diverges (similarity 0.34); scryfall='At the beginning of your upkeep, create a 2/2 white Cat creature token for each Equipment attached to Kemba.'
- oracle_text advisory -- Stoneforge Mystic: oracle_text diverges (similarity 0.44); scryfall='When this creature enters, you may search your library for an Equipment card, reveal it, put it into your hand
- oracle_text advisory -- Unexpectedly Absent: oracle_text diverges (similarity 0.28); scryfall="Put target nonland permanent into its owner's library just beneath the top X cards of that library."
- oracle_text advisory -- Boros Garrison: oracle_text diverges (similarity 0.34); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {R}{
- oracle_text advisory -- Elvish Archdruid: oracle_text diverges (similarity 0.38); scryfall='Other Elf creatures you control get +1/+1.
{T}: Add {G} for each Elf you control.'
- oracle_text advisory -- Priest of Titania: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {G} for each Elf on the battlefield.'
- oracle_text advisory -- Arbor Elf: oracle_text diverges (similarity 0.12); scryfall='{T}: Untap target Forest.'
- oracle_text advisory -- Wirewood Lodge: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.
{G}, {T}: Untap target Elf.'
- oracle_text advisory -- Worldly Tutor: oracle_text diverges (similarity 0.39); scryfall='Search your library for a creature card, reveal it, then shuffle and put the card on top.'
- oracle_text advisory -- Mirri's Guile: oracle_text diverges (similarity 0.44); scryfall='At the beginning of your upkeep, you may look at the top three cards of your library, then put them back in an
- oracle_text advisory -- Call of the Wild: oracle_text diverges (similarity 0.52); scryfall="{2}{G}{G}: Reveal the top card of your library. If it's a creature card, put it onto the battlefield. Otherwis
- oracle_text advisory -- Hornet Queen: oracle_text diverges (similarity 0.41); scryfall='Flying, deathtouch
When this creature enters, create four 1/1 green Insect creature tokens with flying and de
- oracle_text advisory -- Terastodon: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, you may destroy up to three target noncreature permanents. For each permanent put i
- oracle_text advisory -- Elderscale Wurm: oracle_text diverges (similarity 0.53); scryfall='Trample
When this creature enters, if your life total is less than 7, your life total becomes 7.
As long as 
- oracle_text advisory -- Craterhoof Behemoth: oracle_text diverges (similarity 0.44); scryfall='Haste
When this creature enters, creatures you control gain trample and get +X/+X until end of turn, where X 
- oracle_text advisory -- Worldspine Wurm: oracle_text diverges (similarity 0.39); scryfall="Trample
When this creature dies, create three 5/5 green Wurm creature tokens with trample.
When Worldspine W
- oracle_text advisory -- Vaultborn Tyrant: oracle_text diverges (similarity 0.47); scryfall="Trample
Whenever this creature or another creature you control with power 4 or greater enters, you gain 3 lif
- oracle_text advisory -- Natural Order: oracle_text diverges (similarity 0.38); scryfall='As an additional cost to cast this spell, sacrifice a green creature.
Search your library for a green creatur
- oracle_text advisory -- Turntimber Symbiosis: oracle_text diverges (similarity 0.45); scryfall='Look at the top seven cards of your library. You may put a creature card from among them onto the battlefield.
- oracle_text advisory -- Boros Reckoner: oracle_text diverges (similarity 0.24); scryfall='Whenever this creature is dealt damage, it deals that much damage to any target.
{R/W}: This creature gains f
- oracle_text advisory -- Burning-Fist Minotaur: oracle_text diverges (similarity 0.17); scryfall='First strike
{1}{R}, Discard a card: This creature gets +2/+0 until end of turn.'
- oracle_text advisory -- Deathbellow Raider: oracle_text diverges (similarity 0.16); scryfall='This creature attacks each combat if able.
{2}{B}: Regenerate this creature.'
- oracle_text advisory -- Fanatic of Mogis: oracle_text diverges (similarity 0.39); scryfall='When this creature enters, it deals damage to each opponent equal to your devotion to red. (Each {R} in the ma
- oracle_text advisory -- Gnarled Scarhide: oracle_text diverges (similarity 0.34); scryfall="Bestow {3}{B} (If you cast this card for its bestow cost, it's an Aura spell with enchant creature. It becomes
- oracle_text advisory -- Kragma Warcaller: oracle_text diverges (similarity 0.33); scryfall='Minotaur creatures you control have haste.
Whenever a Minotaur you control attacks, it gets +2/+0 until end o
- oracle_text advisory -- Neheb, the Worthy: oracle_text diverges (similarity 0.35); scryfall='First strike
Other Minotaurs you control have first strike.
As long as you have one or fewer cards in hand, 
- oracle_text advisory -- Rageblood Shaman: oracle_text diverges (similarity 0.35); scryfall='Trample
Other Minotaur creatures you control get +1/+1 and have trample.'
- oracle_text advisory -- Ragemonger: oracle_text diverges (similarity 0.45); scryfall='Minotaur spells you cast cost {B}{R} less to cast. This effect reduces only the amount of colored mana you pay
- oracle_text advisory -- Rakdos Carnarium: oracle_text diverges (similarity 0.36); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {B}{
- oracle_text advisory -- Sethron, Hurloon General: oracle_text diverges (similarity 0.34); scryfall='Whenever Sethron or another nontoken Minotaur you control enters, create a 2/3 red Minotaur creature token.
{
- oracle_text advisory -- Slaughter-Priest of Mogis: oracle_text diverges (similarity 0.28); scryfall='Whenever you sacrifice a permanent, this creature gets +2/+0 until end of turn.
{2}, Sacrifice another creatu
- oracle_text advisory -- Atsushi, the Blazing Sky: oracle_text diverges (similarity 0.43); scryfall='Flying, trample
When Atsushi dies, choose one —
• Exile the top two cards of your library. Until the end of 
- oracle_text advisory -- Inferno of the Star Mounts: oracle_text diverges (similarity 0.35); scryfall="This spell can't be countered.
Flying, haste
{R}: Inferno of the Star Mounts gets +1/+0 until end of turn. W
- oracle_text advisory -- Dragon Tempest: oracle_text diverges (similarity 0.34); scryfall='Whenever a creature you control with flying enters, it gains haste until end of turn.
Whenever a Dragon you c
- oracle_text advisory -- Urza's Incubator: oracle_text diverges (similarity 0.15); scryfall='As this artifact enters, choose a creature type.
Creature spells of the chosen type cost {2} less to cast.'
- oracle_text advisory -- Mind Stone: oracle_text diverges (similarity 0.25); scryfall='{T}: Add {C}.
{1}, {T}, Sacrifice this artifact: Draw a card.'
- oracle_text advisory -- Fire Diamond: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.
{T}: Add {R}.'
- oracle_text advisory -- Dragonspeaker Shaman: oracle_text diverges (similarity 0.15); scryfall='Dragon spells you cast cost {2} less to cast.'
- oracle_text advisory -- Glorybringer: oracle_text diverges (similarity 0.46); scryfall="Flying, haste
You may exert this creature as it attacks. When you do, it deals 4 damage to target non-Dragon 
- oracle_text advisory -- Haven of the Spirit Dragon: oracle_text diverges (similarity 0.32); scryfall='{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana only to cast a Dragon creature spell.
{2}, {T}
- oracle_text advisory -- Nest Invader: oracle_text diverges (similarity 0.27); scryfall='When this creature enters, create a 0/1 colorless Eldrazi Spawn creature token. It has "Sacrifice this token: 
- oracle_text advisory -- Young Pyromancer: oracle_text diverges (similarity 0.28); scryfall='Whenever you cast an instant or sorcery spell, create a 1/1 red Elemental creature token.'
- oracle_text advisory -- Undercellar Myconid: oracle_text diverges (similarity 0.39); scryfall='Whenever this creature enters or dies, create a 1/1 green Saproling creature token.
{T}: Add one mana of any 
- oracle_text advisory -- Frontline Heroism: oracle_text diverges (similarity 0.39); scryfall='When this enchantment enters, create a 1/1 red Soldier creature token with haste.
Whenever you cast a spell t
- oracle_text advisory -- Adarkar Wastes: oracle_text diverges (similarity 0.29); scryfall='{T}: Add {C}.
{T}: Add {W} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Caves of Koilos: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.
{T}: Add {W} or {B}. This land deals 1 damage to you.'
- oracle_text advisory -- Yavimaya Coast: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.
{T}: Add {G} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Llanowar Wastes: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.
{T}: Add {B} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Conservatory: oracle_text diverges (similarity 0.64); scryfall='This land enters tapped.
{T}: Add {G} or {W}.
{4}, {T}: Investigate. (Create a Clue token. It\'s an artifact
- oracle_text advisory -- Shivan Gorge: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{2}{R}, {T}: Shivan Gorge deals 1 damage to each opponent.'
- oracle_text advisory -- Mariposa Military Base: oracle_text diverges (similarity 0.26); scryfall='You may have this land enter tapped. If you do, you get two rad counters.
{T}: Add {C}.
{5}, {T}: Draw a car
- oracle_text advisory -- Eldrazi Displacer: oracle_text diverges (similarity 0.47); scryfall="Devoid (This card has no color.)
{2}{C}: Exile another target creature, then return it to the battlefield tap
- oracle_text advisory -- Emiel the Blessed: oracle_text diverges (similarity 0.48); scryfall="{3}: Exile another target creature you control, then return it to the battlefield under its owner's control.

- oracle_text advisory -- Cloud of Faeries: oracle_text diverges (similarity 0.25); scryfall='Flying
When this creature enters, untap up to two lands.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Peregrine Drake: oracle_text diverges (similarity 0.22); scryfall='Flying
When this creature enters, untap up to five lands.'
- oracle_text advisory -- Wild Growth: oracle_text diverges (similarity 0.50); scryfall='Enchant land
Whenever enchanted land is tapped for mana, its controller adds an additional {G}.'
- oracle_text advisory -- Overgrowth: oracle_text diverges (similarity 0.61); scryfall='Enchant land
Whenever enchanted land is tapped for mana, its controller adds an additional {G}{G}.'
- oracle_text advisory -- Fertile Ground: oracle_text diverges (similarity 0.49); scryfall='Enchant land
Whenever enchanted land is tapped for mana, its controller adds an additional one mana of any co
- oracle_text advisory -- Trace of Abundance: oracle_text diverges (similarity 0.34); scryfall="Enchant land
Enchanted land has shroud. (It can't be the target of spells or abilities.)
Whenever enchanted 
- oracle_text advisory -- Training Grounds: oracle_text diverges (similarity 0.49); scryfall="Activated abilities of creatures you control cost {2} less to activate. This effect can't reduce the mana in t
- oracle_text advisory -- Eladamri's Call: oracle_text diverges (similarity 0.61); scryfall='Search your library for a creature card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Stroke of Genius: oracle_text diverges (similarity 0.22); scryfall='Target player draws X cards.'
- oracle_text advisory -- Vexing Shusher: oracle_text diverges (similarity 0.06); scryfall="This spell can't be countered.
{R/G}: Target spell can't be countered."
- oracle_text advisory -- Essence Depleter: oracle_text diverges (similarity 0.13); scryfall='Devoid (This card has no color.)
{1}{C}: Target opponent loses 1 life and you gain 1 life. ({C} represents co
- oracle_text advisory -- Dimensional Infiltrator: oracle_text diverges (similarity 0.14); scryfall="Devoid (This card has no color.)
Flash
Flying
{1}{C}: Target opponent exiles the top card of their library.
- oracle_text advisory -- Living Wish: oracle_text diverges (similarity 0.09); scryfall='You may reveal a creature or land card you own from outside the game and put it into your hand. Exile Living W
- oracle_text advisory -- Aether Hub: oracle_text diverges (similarity 0.08); scryfall='When this land enters, you get {E} (an energy counter).
{T}: Add {C}.
{T}, Pay {E}: Add one mana of any colo
- oracle_text advisory -- Maelstrom Wanderer: oracle_text diverges (similarity 0.34); scryfall='Creatures you control have haste.
Cascade, cascade (When you cast this spell, exile cards from the top of you
- oracle_text advisory -- Annoyed Altisaur: oracle_text diverges (similarity 0.50); scryfall='Reach, trample
Cascade (When you cast this spell, exile cards from the top of your library until you exile a 
- oracle_text advisory -- Sakashima's Protege: oracle_text diverges (similarity 0.28); scryfall='Flash
Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Boarding Party: oracle_text diverges (similarity 0.62); scryfall='Haste
Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Breaching Dragonstorm: oracle_text diverges (similarity 0.26); scryfall="When this enchantment enters, exile cards from the top of your library until you exile a nonland card. You may
- oracle_text advisory -- Call Forth the Tempest: oracle_text diverges (similarity 0.40); scryfall="Cascade, cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland
- oracle_text advisory -- Creative Technique: oracle_text diverges (similarity 0.33); scryfall='Demonstrate (When you cast this spell, you may copy it. If you do, choose an opponent to also copy it.)
Shuff
- oracle_text advisory -- Dwarven Ruins: oracle_text diverges (similarity 0.09); scryfall='This land enters tapped.
{T}: Add {R}.
{T}, Sacrifice this land: Add {R}{R}.'
- oracle_text advisory -- Svyelunite Temple: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
{T}: Add {U}.
{T}, Sacrifice this land: Add {U}{U}.'
- oracle_text advisory -- Melira, Sylvok Outcast: oracle_text diverges (similarity 0.34); scryfall="You can't get poison counters.
Creatures you control can't have -1/-1 counters put on them.
Creatures your o
- oracle_text advisory -- Vizier of Remedies: oracle_text diverges (similarity 0.55); scryfall='If one or more -1/-1 counters would be put on a creature you control, that many -1/-1 counters minus one are p
- oracle_text advisory -- Kitchen Finks: oracle_text diverges (similarity 0.44); scryfall="When this creature enters, you gain 2 life.
Persist (When this creature dies, if it had no -1/-1 counters on 
- oracle_text advisory -- Murderous Redcap: oracle_text diverges (similarity 0.51); scryfall="When this creature enters, it deals damage equal to its power to any target.
Persist (When this creature dies
- oracle_text advisory -- Carrion Feeder: oracle_text diverges (similarity 0.28); scryfall="This creature can't block.
Sacrifice a creature: Put a +1/+1 counter on this creature."
- oracle_text advisory -- Bloodthrone Vampire: oracle_text diverges (similarity 0.26); scryfall='Sacrifice a creature: This creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Recruiter of the Guard: oracle_text diverges (similarity 0.35); scryfall='When this creature enters, you may search your library for a creature card with toughness 2 or less, reveal it
- oracle_text advisory -- Ranger of Eos: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, you may search your library for up to two creature cards with mana value 1 or less,
- oracle_text advisory -- Severance Priest: oracle_text diverges (similarity 0.40); scryfall="Deathtouch
When this creature enters, target opponent reveals their hand. You may choose a nonland card from 
- oracle_text advisory -- Birthing Pod: oracle_text diverges (similarity 0.25); scryfall="({G/P} can be paid with either {G} or 2 life.)
{1}{G/P}, {T}, Sacrifice a creature: Search your library for a
- oracle_text advisory -- Chord of Calling: oracle_text diverges (similarity 0.25); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1} 
- oracle_text advisory -- Reveillark: oracle_text diverges (similarity 0.22); scryfall="Flying
When this creature leaves the battlefield, return up to two target creature cards with power 2 or less
- oracle_text advisory -- Felidar Guardian: oracle_text diverges (similarity 0.18); scryfall="When this creature enters, you may exile another target permanent you control, then return that card to the ba
- oracle_text advisory -- Voice of Resurgence: oracle_text diverges (similarity 0.32); scryfall='Whenever an opponent casts a spell during your turn and when this creature dies, create a green and white Elem
- oracle_text advisory -- Scavenging Ooze: oracle_text diverges (similarity 0.23); scryfall='{G}: Exile target card from a graveyard. If it was a creature card, put a +1/+1 counter on this creature and y
- oracle_text advisory -- Ravenous Chupacabra: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, destroy target creature an opponent controls.'
- oracle_text advisory -- Reclamation Sage: oracle_text diverges (similarity 0.16); scryfall='When this creature enters, you may destroy target artifact or enchantment.'
- oracle_text advisory -- Celes, Rune Knight: oracle_text diverges (similarity 0.25); scryfall='When Celes enters, discard any number of cards, then draw that many cards plus one.
Whenever one or more othe
- oracle_text advisory -- Drifting Meadow: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.
{T}: Add {W}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Polluted Mire: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.
{T}: Add {B}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Smoldering Crater: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.
{T}: Add {R}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Blasted Landscape: oracle_text diverges (similarity 0.30); scryfall='{T}: Add {C}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fetid Pools: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {U} or {B}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Irrigated Farmland: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {W} or {U}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Canyon Slough: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {R}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Sheltered Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {G}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Scattered Groves: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {G} or {W}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Glittering Massif: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {W}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Festering Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {G}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Capital City: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.
{1}, {T}: Add one mana of any color.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Forsake the Worldly: oracle_text diverges (similarity 0.22); scryfall='Exile target artifact or enchantment.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fluctuator: oracle_text diverges (similarity 0.17); scryfall='Cycling abilities you activate cost {2} less to activate.'
- oracle_text advisory -- Drannith Stinger: oracle_text diverges (similarity 0.26); scryfall='Whenever you cycle another card, this creature deals 1 damage to each opponent.
Cycling {1} ({1}, Discard thi
- oracle_text advisory -- Hollow One: oracle_text diverges (similarity 0.20); scryfall="This spell costs {2} less to cast for each card you've cycled or discarded this turn.
Cycling {2} ({2}, Disca
- oracle_text advisory -- Unearth: oracle_text diverges (similarity 0.24); scryfall='Return target creature card with mana value 3 or less from your graveyard to the battlefield.
Cycling {2} ({2
- oracle_text advisory -- Scrying Sheets: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{1}{S}, {T}: Look at the top card of your library. If that card is snow, you may reveal it and 
- oracle_text advisory -- Skred: oracle_text diverges (similarity 0.23); scryfall='Skred deals damage to target creature equal to the number of snow permanents you control.'
- oracle_text advisory -- Coldsteel Heart: oracle_text diverges (similarity 0.25); scryfall='This artifact enters tapped.
As this artifact enters, choose a color.
{T}: Add one mana of the chosen color.
- oracle_text advisory -- Abominable Treefolk: oracle_text diverges (similarity 0.35); scryfall="Trample
Abominable Treefolk's power and toughness are each equal to the number of snow permanents you control
- oracle_text advisory -- Arcum's Astrolabe: oracle_text diverges (similarity 0.25); scryfall='({S} can be paid with one mana from a snow source.)
When this artifact enters, draw a card.
{1}, {T}: Add on
- oracle_text advisory -- Frost Augur: oracle_text diverges (similarity 0.39); scryfall="{S}, {T}: Look at the top card of your library. If it's a snow card, you may reveal it and put it into your ha
- oracle_text advisory -- Ice-Fang Coatl: oracle_text diverges (similarity 0.21); scryfall='Flash
Flying
When this creature enters, draw a card.
This creature has deathtouch as long as you control at
- oracle_text advisory -- Kaldring, the Rimestaff: oracle_text diverges (similarity 0.15); scryfall='{T}: You may play target snow permanent card from your graveyard this turn. If you do, it enters tapped.'
- oracle_text advisory -- Marit Lage's Slumber: oracle_text diverges (similarity 0.35); scryfall="Whenever Marit Lage's Slumber or another snow permanent you control enters, scry 1.
At the beginning of your 
- oracle_text advisory -- Rimefeather Owl: oracle_text diverges (similarity 0.26); scryfall="Flying
Rimefeather Owl's power and toughness are each equal to the number of snow permanents on the battlefie
- oracle_text advisory -- Rimescale Dragon: oracle_text diverges (similarity 0.29); scryfall="Flying
{2}{S}: Tap target creature and put an ice counter on it. ({S} can be paid with one mana from a snow s
- oracle_text advisory -- Soul's Attendant: oracle_text diverges (similarity 0.12); scryfall='Whenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Auriok Champion: oracle_text diverges (similarity 0.12); scryfall='Protection from black and from red
Whenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Serra Ascendant: oracle_text diverges (similarity 0.21); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)
As long as you have 30 or mo
- oracle_text advisory -- Ajani's Pridemate: oracle_text diverges (similarity 0.09); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Voice of the Blessed: oracle_text diverges (similarity 0.24); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.
As long as this creature has four or more +1/+1
- oracle_text advisory -- Daxos, Blessed by the Sun: oracle_text diverges (similarity 0.19); scryfall="Daxos's toughness is equal to your devotion to white. (Each {W} in the mana costs of permanents you control co
- oracle_text advisory -- Archangel of Thune: oracle_text diverges (similarity 0.24); scryfall='Flying
Lifelink (Damage dealt by this creature also causes you to gain that much life.)
Whenever you gain li
- oracle_text advisory -- Heliod, Sun-Crowned: oracle_text diverges (similarity 0.14); scryfall="Indestructible
As long as your devotion to white is less than five, Heliod isn't a creature.
Whenever you ga
- oracle_text advisory -- Ranger-Captain of Eos: oracle_text diverges (similarity 0.18); scryfall="When this creature enters, you may search your library for a creature card with mana value 1 or less, reveal i
- oracle_text advisory -- Ajani, Strength of the Pride: oracle_text diverges (similarity 0.31); scryfall='+1: You gain life equal to the number of creatures you control plus the number of planeswalkers you control.

- clause_ledger: no dedicated per-clause artifact. Its function -- every oracle clause modeled/inert/deferred -- is covered by coverage(partial hard-stop) + bracket-note deferrals + viewer oracle cross-check + audit_card_fields oracle-diff. A dedicated ledger is deferred (high per-card cost, marginal added rigor).
- claude_sweep recorded at commit 2a6e8453 (HEAD fabf51bffb97); re-run if play changed since (play_invariants + smoke digests track play live).

<!-- verify_deck:end -->
