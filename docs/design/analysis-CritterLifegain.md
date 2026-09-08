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
- 5c2 leaf tie-break: RUNNING (`logs/critter/tiebreak.log`).
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
| Ranger-Captain self-sac emitted only while a death payoff (Daxos) is live | TurnSolver enumeration (`SelfSacHasDeathPayoff`) | lossless dominated-action removal | with no payoff the sac is a strict loss; human play always offered |
| Ranger-Captain tutor: all 3 legal MV<=1 names searched (tutor axis width 6); plan-less paths take the first library match | Generic TutorCandidates | none (full) | disclosed plan-less fallback |
| Ajani 0 NOT enumerated autonomously | TurnSolver loyalty enumeration | value gate | strictly negative vs this opponent; human play sees it |
| Ajani cast + same-turn activation variants (target-free abilities) | `SearchesWalkerCastActivation` (CritterLifegain ON, Generic OFF) | WIDENING (search reach) | +29/-0 games on 300; other decks unchanged until measured |
| Legend rule keeps the walker with the most loyalty (tie: can still activate, then oldest) | `CritterLifegainProvider::LegendKeepIndex` (`MTG_LEGEND_KEEP_LOYALTY`) | correctness shortcut | rejected as a Generic default (FiveColour d0 gi=15 6->7) |
| EvalCard credits: Pridemate/Voice entry counters = #own enter-watchers; Thune team credit; Heliod flat +2 DMG and body discounted by devotion distance | TurnSolver EvalCard (param-gated) | greedy-d0 ordering only | rollout owns the real valuation; no other deck's eval moves |
| Voice's 4+/10+ counter keywords, Serra/Thune flying, Auriok protection, Heliod indestructible + God-subtype/removed-from-combat halves | bracket notes (PROVISIONAL partials) | card-modeling collapse | each provably unobservable vs this opponent (see the notes) |
| Daxos: simultaneous-death look-back and legend-rule deaths not firing dies-triggers | bracket note | known engine gap | unreachable / a 1-life miss on a 2-of x 2-of collision |
| Viewer: Heliod's counter target fires a board prompt PER gain event | 2c-ter | UX | faithful; a per-turn "apply to all" affordance is a follow-up |
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

## Open questions surfaced (non-blocking)
(collected here and re-raised in the closing message)
