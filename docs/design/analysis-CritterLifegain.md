# Analysis ledger — CritterLifegain

Deck: `decks/CritterLifegain/CritterLifegain.cod` (60, mono-white lifegain; no sideboard).
Started 2026-09-08 on branch `phase-1-2-deck-analyzer` (base 67ef4c66).

Status: **IN FLIGHT** — Stage 1 done; Stage 2 research fan-out COMPLETE (10 Opus drafts saved under
`logs/critter/drafts/*.md`, one per card); serial integration in progress.

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

## Verification verdicts (Stage 5)
(pending)

## Approved deferrals
(none yet — every proposed deferral is PROVISIONAL until the user signs off; user is asleep
2026-09-08, so all are provisional through this run)

## Open questions surfaced (non-blocking)
(collected here and re-raised in the closing message)
