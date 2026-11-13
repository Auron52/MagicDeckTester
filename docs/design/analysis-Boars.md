# Analysis ledger — Boars

Per-deck ledger for `decks/Boars/Boars.cod`, per the `analyze-deck` skill. Updated continuously.

**Run started:** 2026-11-13. Worktree `.claude/worktrees/boars`, branch `boars-analysis`, base
commit `d81e83fae` (shared branch `phase-1-2-deck-analyzer`). Subagents never push the shared branch.

## Decklist (60 main / 11 side, sideboard unreachable: no wish)

| n | card | role | status at Stage 1 |
|---|---|---|---|
| 4 | Birds of Paradise | 1-drop dork | implemented |
| 3 | Ignoble Hierarch | 1-drop dork (exalted) | implemented |
| 3 | Zhur-Taa Druid | 2-drop dork + ping | **MISSING** |
| 4 | Somberwald Sage | 3-drop creature-only ramp (3 of one colour) | implemented |
| 4 | Flinthoof Boar | 2-drop Boar, +1/+1 w/ Mountain, {R}: haste | **MISSING** |
| 4 | Herald of Ilharg | 4-drop Boar, cast-creature counters + MV>=5 burn | **MISSING** |
| 3 | Raggadragga, Goreguts Boss | 4-drop, dork anthem, dork untap on attack, 7-mana cast trigger | **MISSING** |
| 4 | Ilharg, the Raze-Boar | 5-drop, attack: put creature from hand tapped+attacking | **MISSING** |
| 4 | End-Raze Forerunners | 8-drop, ETB team +2/+2 vigilance trample | **MISSING** |
| 4 | Decimator of the Provinces | 10 / Emerge {6}{G}{G}{G}, cast: team +2/+2 trample | **MISSING** |
| 2 | Herald's Horn | Boar cost reduction + upkeep top-card dig | **MISSING** |
| 1 | Lightning Greaves | haste | implemented |
| 4 | Stomping Ground | shock | implemented |
| 4 | Wooded Foothills | battlefield fetchland | implemented |
| 3 | Windswept Heath | fetchland (OLD in-hand model) | implemented — **GAP, see below** |
| 3 | Gruul Turf | Karoo | implemented |
| 1 | Rootbound Crag | checkland | implemented |
| 4 | Forest, 1 Mountain | basics | implemented |

## Stage 1 — Coverage (DONE)

* missing (8): Ilharg, End-Raze Forerunners, Decimator of the Provinces, Flinthoof Boar, Zhur-Taa
  Druid, Herald's Horn, Raggadragga, Herald of Ilharg.
* Bracket notes on the 11 implemented cards re-read: all are faithful-model descriptions, no
  Tier 1-3 simplification — EXCEPT:
* **Windswept Heath** still uses the pre-2026-11-05 immediate-on-play fetch (`basic_land` with a
  false WBRG `produces`), while Wooded Foothills in the same deck is the real battlefield fetchland
  (`land_search_activation_*`). `fetch_hold_review`: Heath `hold_modelled: false`.
  **Decision (agent default, SURFACED to USER, PROVISIONAL):** migrate Heath to the battlefield
  model (copy the Foothills/Arid Mesa param shape, fetch Forest/Plains). Moves play for
  Anti-Lifegain, Creature Giving, Five Colour (they run Heath) -> smoke/regression GT re-accept with
  slower games classified.

## Stage 2 — research drafts

(per card: tier, cards.json entry, C++ changes, viewer bucket, deferrals)

Research fan-out launched 2026-11-13 (5 Opus read-only agents); drafts land in
`logs/boars-research/{ilharg,decimator,raggadragga,herald-zhurtaa,endraze-flinthoof-horn}.md`
(gitignored scratch; conclusions are copied here).

### Research verdicts (all drafts in, 2026-11-13)

| card | tier | key design | viewer |
|---|---|---|---|
| Zhur-Taa Druid | 2 | NEW `tap_mana_damage_each_opponent` (Grove of the Burnwillows pattern, sign flipped) at every mana-tap site; FIX the mana-payment cache that assumes only Grove-type lands move opp life | none |
| Herald of Ilharg | 2 | cast-creature trigger in `FireOnCastTriggers`: +2 counters, then if MV>=5 damage = counters to each opponent | none |
| Flinthoof Boar | 2 | FIX `self_pump_if_land_subtype` (only live inside Kumena's block) in `ComputeLordBonus`; NEW `self_haste_cost` (ActivatePump mode) — NOT `team_pump_grants_haste` (Minotaur routing signature) | plan action |
| Herald's Horn | 2/3 | Urza's Incubator shape (type choice + reduction); upkeep look-at-top "may take" | NEW `creature_type` decision; take/leave reuses `dig` |
| End-Raze Forerunners | 3 | ETB other-team +2/+2 beside Craterhoof in `FireOwnEtbTriggersBody`; NEW `Permanent::temp_vigilance` (NOT inert: vigilant Zhur-Taa / dorks keep mana post-combat) | none |
| Raggadragga | 3 | anthem via shared `CreatureHasManaAbility` predicate (has, not can-use); Jorn-style attack untap after damage + `NoteUntapEffect`; cast trigger needs NEW `mana_spent` arg on `FireOnCastTriggers`; target via Ulamog's `cast_trigger_target_choice` (creatures) | `target` (A) |
| Decimator | 3 | emerge as `Action::emerge` + `CastRiders`, victim via the Diabolic Intent sac-creature route; cost via `EffectiveSpellCostWithBase` (Horn applies, victim MV off generic only); NEW `cast_trigger_team_pump_*` + `TriggerKind::CastTeamPump` | `sacrifice` board click BEFORE payment |
| Ilharg | 3 | `attack_put_creature_from_hand` / `attack_put_return_at_end_step` / `dies_to_library_position:3`; shared `FireAttackPutFromHand` in both combats; searched pin `Plan::attack_put_choice` | reuses `lackey_put` (generic wording); death-clause yes/no reuses `dig` |

Engine bugs exposed (fix as part of this work): EXALTED "attacks alone" is computed after puts/tokens
join the attacker list (both combat paths; use the declared count); attack-trigger helpers that run
after the put fire for PUT creatures (they were not declared attackers); legend-rule keep choice is
surfaced in the viewer only for Chandra (4 Ilharg + 3 Raggadragga here).

### Agent defaults taken (PROVISIONAL, surfaced to USER, not blocking)

* D1 Windswept Heath -> battlefield fetchland (moves Anti-Lifegain, Creature Giving, Five Colour).
* D2 Trample (End-Raze grant, Decimator keyword+grant, Raggadragga grant, Ilharg/Herald keyword):
  inert — engine has no blocker path. Bracket-noted, PROVISIONAL deferral.
* D3 Ilharg exile half of the death clause: unreachable in goldfish (no exile effect) — PROVISIONAL
  deferral. The DIES half IS implemented (legend rule, emerge sacrifice reach it).
* D4 Herald's Horn type: provider one-option prune to Boar (PROVISIONAL; human play offers every
  type via the new `creature_type` decision). Upkeep "may take": fold to TAKE only where provably
  dominant (no forced cleanup discard), search the rest (option A).
* D5 Zhur-Taa end-of-turn free ping of any untapped Druid: a sound dominance fold (nothing left to
  spend the mana or the untapped body on). No 2HG hold-back-to-ping provider rule (left to search /
  the standing attack exemption) — surfaced.
* D6 Second main: Raggadragga/End-Raze untap+vigilance via Jorn's per-turn `UntapSecondMainLive`
  route (not the deck-wide `DeckUsesSecondMain`); the Ilharg put-body-as-emerge-victim main-2 line
  as a per-turn live condition (put body on battlefield AND emerge card castable).
* D7 No prune of a second legendary copy as Ilharg put candidate (lossy until measured).
* D8 Cast order (e.g. Herald of Ilharg after dorks, before other creatures): USER-owned — not
  encoded now; a per-deck order will be drafted for USER review later.

### Integration plan (serial, one agent per change set, local commits on `boars-analysis`)

1. I1: Windswept Heath, Flinthoof Boar, Herald's Horn.
2. I2: Zhur-Taa Druid, Herald of Ilharg, `FireOnCastTriggers(mana_spent)` plumbing.
3. I3: End-Raze Forerunners (`temp_vigilance`), Raggadragga.
4. I4: Decimator (emerge).
5. I5: Ilharg (+ exalted / put-attacker / legend-rule viewer fixes).
Then: build, coverage re-check, cost + field audits, provider (BoarsProvider), Stage 4 profile, smoke.

## Open USER questions (surfaced, not blocking)

1. D1-D8 above (each taken provisionally).
2. I3b risk 3: Raggadragga's cast-trigger target is ONE shared per-turn rank, so two 7+-mana casts in a turn cannot
   pick independent targets (a line unfindable at any budget). Recommendation: build a per-cast rank in I4. See "I3b done".

## I1 done (2026-11-13) -- Windswept Heath, Flinthoof Boar, Herald's Horn

Commit: `3641c75fd` on `boars-analysis` (local only, not pushed). Unit tests 2943/2943 pass; audit_card_costs rc 0;
audit_card_fields offline rc 0 (the --update refresh hit Scryfall 429s for already-snapshotted cards; the two new
cards were added). No smoke/regression run (the parent pools one).

**Landed**
* **Windswept Heath** -> the Wooded Foothills / Arid Mesa battlefield fetchland shape (`produces []`,
  `land_search_activation_*`, Forest/Plains); oracle verbatim from Scryfall; `uncracked-fetchland.md` and
  `LandSearch.h` updated. `analyze_deck.py` hold detection keys on `land_search_activation_cost`, so it now reads
  `hold_modelled: true`. Intentionally moves play for Anti-Lifegain, Creature Giving, Five Colour (D1); their
  provider routing is unchanged (provider_audit: AntiLifegain / CreatureGiving / FiveColour, before and after).
* **Flinthoof Boar**: `self_pump_if_land_subtype` now works on its own in `ComputeLordBonus` (Kumena's path is the
  identical test sequence; unit test pins both). NEW `self_haste_cost` -> `ActivatePump` **mode 6** (modes 1-5 were
  taken; the draft's "mode 5" is AccomplishedAlchemist's loop-funded pump). One shared liveness predicate
  `SelfHasteActivationLive` (could attack only WITH the grant) read by the enumerator (pre-combat only, K=1), the
  new site-9 key `kActSelfHaste` (a Flinthoof cast this main can be hasted in the same main) and both pay sites
  (checked before paying, so a Greaves-equipped body never pays {R}). Added to the two `gives_haste` reach lambdas.
  Labels in main.cpp (`Flinthoof Boar #<n>: gains haste until end of turn`) and the executor log.
* **Herald's Horn**: the Urza's Incubator shape (`chooses_creature_type`, amount 1, creature-only) + NEW
  `upkeep_look_top_chosen_type_to_hand` -> shared `PerformUpkeepHornReveal` at `GameEngine::UpkeepTail` and the
  rollout upkeep (after `PerformUpkeepReorder`, before echo / the draw). D4 option A:
  * FOLD to take (`HornTakeFoldHolds`) when hand + 1 + 1 + later Horn triggers <= 7 (or no max hand size) AND every
    card that could act this turn (battlefield, hand, graveyard, staged, the top 2+later library cards) is
    `hand_gain_audited` -- a NEW CardDatabase whitelist of raw param keys that cannot add to hand mid-turn (the
    Overlord-fold pattern; unknown params fail closed). Audited for the I1 card pool only.
  * otherwise SEARCHED: `GameState::scripted_upkeep_horn` (bit k = leave the k-th residual) <- `Plan::upkeep_horn_choice`,
    a new BoundaryFan digit (width 2^residuals, learned per crossing like Klothys), threaded through Record /
    RecordOnState / the single-main tail (`boundary_horn`) / `BoundaryChoice`, the plan keys, the dominance fold
    (sizeof(GameState) 1408 -> 1416, assert updated with its note), the boundary key (value-gated in the tutor slot)
    and the executor's crossing node `ChooseRealCrossingHorn` (AIEngine::DecideCrossingFetch). Unpinned residuals
    take (beyond the horizon) and are counted in `g_horn_unpinned`.
  * human: the shared `dig` chooser "(upkeep reveal)".
* **NEW `creature_type` decision** (all four sites): `CreatureTypeChooser` / `g_play_creature_type_chooser` in
  GameLogger.h (nulled in both RevealLogPause scopes + ClearClaudePlayChoosers), call site in `FireEtbWatchers`
  (menu = `DeckCreatureSubtypeMenu`, shares one tally with `DominantCreatureSubtypeId`), `WriteCreatureTypeDecisionJson`
  + chooser in RunClaudePlay, `creatureTypePanelHtml` in index.html. It also covers Urza's Incubator / Metallic Mimic /
  Adaptive Automaton (they used to resolve silently). DECISIONS.md rows added (creature_type, Horn's dig reuse,
  Flinthoof's main_phase activation); `audit_viewer_decisions.py`: `chooses_creature_type` +
  `chosen_type_added_to_self` moved from the inert list to MANIFEST -> `creature_type`,
  `upkeep_look_top_chosen_type_to_hand` -> `dig`, `self_haste_cost` -> BOARD_ACTIVATIONS `activate`.
* **BoarsProvider** (DeckProvider-derived, the Morph precedent: Boars previously rode AntiLifegain via the fetchlands'
  `anti` signature). Keeps only the exalted-aware attack hold (3x Ignoble Hierarch); `ChosenCreatureSubtypeId` names
  Boar for the Horn (D4 PROVISIONAL prune; the generic pick would also be Boar on this list). Certificate NotAssessed
  (what a bound would have to price is in the string). Routed above `anti`.

**Deviations from the drafts**
* Windswept Heath carries a NEW `hand_fixing_produces` W/B/R/G (the uncracked-fetchland.md "split the field" step):
  Creature Giving's USER-settled cleanup-discard cover (`land_bits`) read Heath's old fake `produces`, and its unit
  test (worked example C1) broke when `produces` went empty. The cover now reads the hint when `produces` is empty,
  so that USER rule is unchanged; every other reader sees the real (empty) `produces` and moves (D1).
* `test_pirates_provider` routing table: Boars -> "Boars" added (53 folders).
* ActivatePump mode **6**, not 5 (5 is taken).
* The take fold's "max possible hand gain" is not a hand-written per-deck bound: it is the `hand_gain_audited`
  whitelist (fails closed). Consequence: once I2-I5 add cards, each new card is UNAUDITED until its integrator adds its
  keys to `kHandAuditedParams` (CardDatabase.cpp) -- until then the fold does not fire while that card is in a zone the
  fold looks at, and the decision is searched (correct, only wider). **Ilharg's end-step return IS a hand gain** (I5
  must not whitelist `attack_put_return_at_end_step`; it needs a bounded term instead: +1 per Ilharg that can attack).
* BoarsProvider signature is `self_haste_cost` ONLY today (the only Boars-only param in the I1 pool; Herald's Horn is
  a colourless tribal staple and deliberately excluded). **OWED by I2-I5:** OR in each new Boars-only param.
* Human play keeps the generic `act=Flinthoof Boar` verb; two summoning-sick copies are told apart by the label's
  `#<n>`. Unverified in the GUI (Stage 5h owes it).

**Open questions (surfaced, not blocking)**
* D4 (USER): Boar prune for the Horn type; option A take fold + searched residual (implemented).
* Is the provider move AntiLifegain -> BoarsProvider OK (no GT for Boars yet, so nothing to rebaseline)?
* The `creature_type` prompt now also fires for Urza's Incubator (Dragons), Metallic Mimic / Adaptive Automaton
  (Pirates, Kithkin) in the viewer -- a viewer-only change; autonomous play is untouched.

## I2a done (2026-11-13) -- Zhur-Taa Druid

Commit: `5739ea267` on `boars-analysis` (local only, not pushed). Unit tests 2949/2949
pass (6 new cases, `test/unit/test_boars_zhurtaa.cpp`). audit_card_costs rc 0 (611/611); audit_card_fields --update hit
Scryfall 429s for other cards (rc 1; the Druid was snapshotted), offline rc 0. No smoke/regression run (the parent pools one).
viewer audit `--no-sweep` on Boars: the new param classifies; it still FAILS on I1's `self_haste_cost` (in BOARD_ACTIVATIONS,
which the self-guard does not read) -- an I1 leftover for the parent.

**Landed**
* `Zhur-Taa Druid` (cards.json, oracle verbatim from Scryfall + bracket): `mana_dork`, `produces [G]`, NEW
  `tap_mana_damage_each_opponent` 1.
* ONE helper `ApplyTapManaPing` (SpellEffects.h, beside OpponentGainsLife) at every tap-for-mana choke point:
  `TapSourceIntoFloat` (greedy payer + human pre-tap), the backtracker's `activate()` (undone by its existing
  opp_life_snap / oll_snap), and the mana cache's HIT replay (per tap). Raw life loss + `opponent_lost_life_this_turn`
  x OpponentHeads() (2HG: 2 to the team). It deliberately bypasses the armed dmgev model and
  RecordNoncombatDamageToPlayer (a mid-payment branch could not undo their queues); no deck pairs them (disclosed).
* **Mana-cache sign hazard FIXED:** the store took the whole opponent-life diff as `drip_life` and replayed it via
  OpponentGainsLife, so a cached Druid tap made the opponent GAIN 1. Now the store counts the solve's newly-tapped
  ping dorks and records `drip_life = |diff + pings|`; the ping replays per tap. Unit test: hit gives 19, not 21; a
  Druid+Grove {R}{G} solve agrees store vs hit. `MTG_MANA_CACHE_VERIFY=1` on Boars (40 games d3 b10): 466,440 checks,
  0 mismatches.
* Side-effect-free fast paths now exclude the Druid like the drip: `PayRollback::SwapEligible`, the ManaPayment seed
  source, `RitualTapAheadIntoFloat`, `EtbUntapTapAheadIntoFloat` (the other `tap_opponent_lifegain` predicates are
  land-only or armed-only).
* D5 END-STEP FREE PING `PingDorkEndSweep`: first thing in our end step, lockstep in `GameEngine::EndStep` and
  `SimulateEndAndStartNextTurn` (also human play: a "no decision" DECISIONS.md row). Taps every untapped Druid that
  CanTapNow. A lethal sweep returns before the rollout's turn counter advances (the opponent_decked precedent), so the
  win is credited to THIS turn in both worlds (the executor's PlayOutFrom reads CheckWinCondition before advancing).
  Unit-tested (lethal: turn stays 4).
* POST-COMBAT LETHAL FINISH `PingDorkFinish` right after `CounterPingFinish` in both combats (autonomous only): taps
  the ready Druids when their pings alone cover the remaining life.
* No once-per-turn assumption (a re-untapped Druid pings again; tested). BoarsProvider signature ORs in the new param;
  the param is in `kHandAuditedParams` (Horn take-fold whitelist); `audit_viewer_decisions.py` classifies it.

**Byte-identity** (I1 binary vs I2a, same cwd, `--games 20 --seed 9101 --depth 3 --budget-ms 10
--ignore-play-profile --threads 32`, `[win]` lines): Anti-Lifegain (Grove of the Burnwillows) IDENTICAL 20/20,
Angels IDENTICAL 20/20. (The primary checkout's binary predates I1, which intentionally moves Anti-Lifegain via
Windswept Heath, so the baseline was the I1 build.) Boars itself, 40 games: 1 faster / 0 slower vs I1.

**Deviations / not done**
* Draft item E (this-turn projection credit for the ping in the `wins` projection) NOT implemented: the projection is
  only the beyond-horizon Solve's; an exact per-Druid attack-vs-ping split needs AttackTapLoss internals. OWED if a
  measurement shows the playout misjudging Druid turns.
* Draft item F (2HG: Druid contested on the dork attack/hold axis) left to the provider / USER (Q2 of the draft).

**Open questions (surfaced, not blocking)**
* Draft Q3: the post-combat finish skips human play (the CounterPingFinish precedent); the end-step sweep runs there.
* Draft Q2 (2HG attack vs ping) as above.

## I2b done (2026-11-13) -- Herald of Ilharg, mana-spent plumbing, viewer manifest

Commit: `103bb7592` on `boars-analysis` (local only, not pushed). Unit tests 2954/2954 pass (`test/unit/test_boars_herald.cpp`).
audit_card_costs rc 0 (612/612); audit_card_fields --update hit Scryfall 429s (rc 1; Herald was fetched), offline rc 0.
`audit_viewer_decisions.py decks/Boars/Boars.cod --no-sweep` rc 0. Byte-identity vs the I2a binary (`--games 20 --seed 9101
--depth 3 --budget-ms 10 --ignore-play-profile`): Angels, Burn (Eidolon), Five Colour (Mana Cannons) IDENTICAL 20/20.

**Landed**
* **Herald of Ilharg**: `creature_cast_trigger_plus_counters` 2 + `creature_cast_counter_damage_min_mv` 5 in the shared
  `FireOnCastTriggers` (own Heralds only; PutPlusCounters, then printed MV >= 5 -> TotalCountersOn damage x OpponentHeads via
  DealPermanentAbilityDamage). Never sees its own cast; puts are not casts. Trample inert (D2).
* **Projection** `MTG_ATTACK_HERALD_TRIGGER` (default ON, heurarm slot `ATTACK_HERALD_TRIGGER`): both consider() sites add the
  ping plus a pre-combat ATTACKING Herald's growth, walked in PlanCastOrder. A Herald cast in the plan joins for later casts; its
  own growth is not credited (needs haste). `xw_nowin` stands down while a Herald is live. Inert for every deck without one.
* **`FireOnCastTriggers(state, def, int mana_spent)`**: REQUIRED at every site. Executor `cast_mana_spent` = `x_effective`
  minus creature-convoke taps, 0 on spend_free / alt lifegain. Rollout twin off `ec` (0 on free_cast / alt_cost). Retrace = its
  effective cost. Free casts (PushFreeCast, ripple, suspend, Invigorate redirect) = 0. Garth = printed + X ("you still pay its
  costs"). Madness = `o.reduced`. Remand / Remand chain = their EffectiveSpellCost. Nothing reads it yet (Raggadragga, I3).
  Test probe: `g_cast_trigger_mana_spent_probe`. Tested: Herald hard cast = 4, with a Horn = 3, in both worlds.
  **For I4 (emerge):** pass the emerge cost actually paid (after Horn) and sacrifice BEFORE the call.
* BoarsProvider signature ORs in `creature_cast_trigger_plus_counters`. Both Herald params are in `kHandAuditedParams`.
* Viewer: `self_haste_cost` -> MANIFEST `main_phase` (the I1 leftover). Both Herald params go in INERT_PARAMS (no decision),
  plus a DECISIONS.md no-decision row.

**Deviations / not done**
* No `[fd-diverge]` assert comparing the executor and rollout mana_spent (the draft's optional item). Lockstep is by
  construction, and the unit test checks both worlds.
* The Ilharg-put "no trigger" test is owed to I5 (Ilharg is not in cards.json yet).

**Open questions (surfaced, not blocking)**
* INERT_PARAMS rows for the two Herald params need the USER's OK (viewer paperwork).
* D8: Herald's cast-order rank is USER-owned. The draft suggests Herald goes after the dorks and before the other creatures.
  Not encoded.

## I3a done (2026-11-13) -- End-Raze Forerunners, `temp_vigilance`, combat-kept-mana main 2

Commit: `93f3154b0` on `boars-analysis` (local only, not pushed); snapshot + ledger in the commit after it. Unit tests 2960/2960 pass
(`test/unit/test_boars_endraze.cpp`, 6 cases). Byte-identity vs the I2b binary (`--games 20 --seed 9101 --depth 3
--budget-ms 10 --ignore-play-profile`, `[win]` lines): Stompy Surprise (Craterhoof), Angels (vigilance + Giada m2),
Snow (Jorn, the predicate the new clause extends) IDENTICAL 20/20. Boars 40 games at d3 b10 run clean (End-Raze cast on
23 turns; main 2 opened on 6 of them). audit_card_costs rc 0 (613/613). audit_card_fields --update hit Scryfall 429s
(207 cards incl. End-Raze); End-Raze's snapshot entry was written from the already-fetched Scryfall JSON
(`build/scratch/boars/scry/EndRazeForerunners.json`, the same fields do_update writes); offline rc 0.
`audit_viewer_decisions.py decks/Boars/Boars.cod --no-sweep` rc 0.

**Landed**
* **End-Raze Forerunners** (cards.json, cost/oracle verbatim from Scryfall + bracket; `template custom`; keywords
  Vigilance/Trample/Haste): `etb_other_creatures_pump_power` 2 / `_tough` 2 / `etb_other_creatures_grant_vigilance`.
  ETB in the shared `FireOwnEtbTriggersBody` right after Craterhoof (both worlds, every enter route): every OTHER own
  creature / animated permanent at resolution gets +2/+2 (temp bonuses) and `temp_vigilance`; play event `etb`. Also in
  the mass-put `late_own_etb` pass (simultaneous entrants are pumped). Printed vigilance/haste were already live keywords.
* **NEW `Permanent::temp_vigilance`** (bool in existing padding; sizeof(Permanent) still 328): read by
  `CreatureHasVigilance` (both attack-tap sites); cleared at BOTH cleanup sites; mirrored at every `temp_double_strike`
  site -- sim key bit 1<<26, `AtCleanBoundary` refusal (boundary assertion), ManaPayment lockstep checker, the provider
  whole-state signature, the LandfallTargetFold refusal + label, StealAttackBodyKey clear+mix, the permanent string key
  (`|vig`, gated), the fold-census refusal, the dedupe labels, the dominance equality, the packed key (bit 41), the TT
  fold, and the `--scenario` loader. All set-gated -> byte-identical elsewhere.
* **Dork attack/hold providers see the grant** (`|| p.temp_vigilance` beside the printed-keyword reads at the four
  DecisionProviders sites), so a granted dork is attacked with freely and not contested as if attacking tapped it.
* **D6 second main, per-turn**: `UntapSecondMainLive` gained a second clause, `CombatKeptManaCreature` -- an own
  creature with `produces` non-empty, `temp_vigilance`, untapped post-combat. `StampDeckTraits` stamps
  `deck_has_attack_untap` for a mainboard card with `etb_other_creatures_grant_vigilance` (name historical; comment
  updated). Every site already reading the predicate (executor + all search post-combat sites) follows. **For I3b
  (Raggadragga): add the attack-untap clause in the same predicate and stamp the same constant.**
* Attack projection (`MTG_ATTACK_TEAM_PUMP_ETB` walk): End-Raze's +2/+2 to the OTHER creatures beside Craterhoof's.
* BoarsProvider signature ORs in `etb_other_creatures_grant_vigilance`; all three params in `kHandAuditedParams` (Horn
  take-fold whitelist). Viewer: three INERT_PARAMS rows + a DECISIONS.md no-decision row.

**Deviations / not done**
* The m2 clause does not check that the dork ATTACKED (attack counts are tracked only for extra-combat decks). A granted
  untapped dork that stayed home also opens main 2 -- sound (opening main 2 only adds lines), slightly wider.
* Printed-vigilance mana creatures are NOT in the clause (would move Faeburrow-style decks); D2 trample inert.
* **Owed to I5 (Ilharg), not implemented:** Ilharg may put End-Raze onto the battlefield tapped and attacking after
  attackers are declared. The put helper must (a) apply the CR 508.1f declaration tap to every already-declared
  non-vigilant attacker BEFORE the put (otherwise the mid-combat `temp_vigilance` grant would wrongly leave Ilharg and
  every declared attacker untapped, since this engine defers the attack tap to `ResolveCombatDamage`), (b) fire
  `FireEtbWatchers` + `FireOwnEtbTriggers` on the put body so End-Raze's +2/+2 lands before the damage loop reads power,
  in both combats, and (c) use the declared count for exalted.

**Open questions (surfaced, not blocking)**
* D6 for End-Raze: main 2 opens on the turns a granted mana creature is untapped post-combat (agent default per D6).
* D2: End-Raze's trample (printed and granted) inert, PROVISIONAL.


## I3b done (2026-11-13) -- Raggadragga, Goreguts Boss

Commit: `db1c83d1b` on `boars-analysis` (local only, not pushed). Unit tests 2966/2966 pass (I3c fixed one compile error in the new file: `AvailableManaPool(exec)` -> `AvailableManaPool(exec, nullptr)`; no engine bug found) (`test/unit/test_boars_raggadragga.cpp`,
6 cases). Byte-identity vs the I3a binary (`--games 20 --seed 9101 --depth 3 --budget-ms 10 --ignore-play-profile`,
`[win]` lines): Unpredictable Cyclone, Snow, Stompy Surprise all IDENTICAL, 20/20 games each (sorted `[win]` lines; the unsorted compare reported DIFFER only from the parallel completion order -- `i3b_ident.sh` now sorts). Boars 40 games at d3 b10: rc 0, 40/40 wins, avg 5.525 turns (21 x T5, 17 x T6, 2 x T7), no assert. Audits: audit_card_costs rc 0 (614/614 match Scryfall); audit_card_fields offline rc 0 (the oracle-divergence lines are advisory -- bracket notes, as for every implemented card); audit_viewer_decisions `--no-sweep` rc 0, oracle cross-check clean, expected decisions bounce / creature_type / dig / land_entry / target / tutor_etb. The Scryfall snapshot entry was written
from the already-downloaded JSON (`build/scratch/boars/scry/RaggadraggaGoregutsBoss.json`, the fields do_update writes),
without a `--update` round trip, to avoid the 429s I1-I3a hit.

**Landed**
* **cards.json** (cost/oracle verbatim from Scryfall + bracket; `template custom`, Legendary Human Boar 4/4): params
  `lord_affects_mana_ability_creatures` + `power_bonus`/`tough_bonus` 2, `attack_untap_mana_ability_attackers`,
  `cast_trigger_untap_pump_target_min_spent` 7 / `_power` 7 / `_tough` 7 / `_grants_trample` (inert, D2 PROVISIONAL).
* **Anthem**: ONE predicate `CreatureHasManaAbility(state, self, card)` (SpellEffects.h, beside ManaDefOf): HAS the
  ability, never can-use (mana dork / `produces` / creature mana rock / sac-for-mana / Alchemist / animated mana land /
  a live Brightcap grant, the grant walk gated on `deck_has_mana_grant`). `IsLordPermanent` + a `process_lord` branch
  in `ComputeLordBonus` -- every P/T reader in both worlds. Sick Birds and tapped Sage are 2/3 and 2/5.
* **Draft risk 5 (EffectivePower misses the anthem)**: the attack-decision power reader (`AttackPowerOf`) already adds
  lords. The dork hold tower (`HoldManaSourceForCollapsedMain`, which tests `EffectivePower() > 0`) and the three
  other dork attack/hold sites I3a touched now also exempt `AttackUntapsAttacker` (Raggadragga out + a mana ability):
  attacking costs that dork no mana, the vigilance-equivalent SOUND exemption.
* **Attack untap** (Jorn shape, both combats): `AttackUntapManaAttackers(state, ctrl, atk_idx, declared_n)` at the Jorn
  capture point takes only the first `declared_n` slots (DECLARED attackers; a put/token is not, CR 508.4 -- Ilharg's
  put is excluded by construction), as m_numbers; `UntapManaAttackers` right after `ResolveCombatDamage` /
  `CounterPingFinish`, before `PingDorkFinish` (an untapped Druid can then ping), with `NoteUntapEffect`.
* **Main 2 (D6)**: `UntapSecondMainLive` third clause -- a Raggadragga we control AND an own untapped creature with a
  mana ability that can tap now (pure-board proxy, the Jorn/I3a precedent; a dork held home only widens main 2). Deck
  stamp `deck_has_attack_untap` ORs the param. No GameState field for this clause.
* **Cast trigger**: in the shared `FireOnCastTriggers`, `mana_spent >= 7` (the I2b argument) collects one trigger per
  own Raggadragga; resolved after the loop's tokens/clues via `ChooseCastTriggerTargets(..., untap_pump=true)` +
  NEW `ResolveUntapPumpTarget` (re-found by m_number, CR 608.2b; untap with `NoteUntapEffect`; +7/+7 temp; trample
  recorded only). Order vs Herald's counters / other cast triggers is a sound identity (pumps and counters commute).
* **Target = searched** (Ulamog's machinery): `CastTriggerTargetClasses(..., untap_pump)` = creatures only, shroud
  excluded, opponent creatures folded away in SEARCH when an own creature is legal (sound vs the passive opponent;
  human play sees the full set). **Risk 4**: the fold key (LandfallTargetFoldKey) already mixes `tapped` and
  `entered_this_turn` and refuses temp bonuses -- unit-tested (tapped / sick / ready Birds are three classes). NEW
  provider hook `DecisionProvider::UntapPumpTargetOrder` (ORDER only, a pure board function so both worlds agree): own
  tapped mana creatures that can tap now (largest yield first), own untapped attack-capable creatures (highest attack
  power), the rest of ours, the opponent's. Axis: `RaggaUntapPumpAxisWidth` -- a plan with a Raggadragga out or cast AND
  a cast whose spend can reach 7 (`CastSpendMayReach`: MV >= 7, an {X}, or a kicker) gets ranks up to the creature
  count (board creatures either side + creature cards the plan casts; an upper bound because paying can SPLIT a class
  by tapping one copy, so board classes are not a bound). No cap. Stamp `deck_has_cast_target_trigger` ORs the param.
* **Risk 1 (mid-phase refund)**: `ResolveUntapPumpTarget` bumps NEW `GameState::cast_untap_refunds` (uint8, monotonic,
  bool padding, sizeof unchanged; Dominance.h classification: read only as a delta -> nothing to fold) when it untaps
  a TAPPED own mana creature that can tap now. `CollectActivationKeys` emits NEW `kActUntapRefund` (219) on the
  Raggadragga with the counter in bits 40+, so breakpoint site 9 (shared by both worlds) sees a new key and re-plans the
  rest of the phase; `PlanOpensBreakpoint` marks such plans for the site-9 wave. Unit-tested: the rollout apply of
  End-Raze off a Sage untaps the Sage and `PostEntryActivationPending` is true, false again with the counter reset.
* BoarsProvider signature ORs all three Raggadragga params; all eight params in `kHandAuditedParams` (Horn take-fold).
  Viewer: `cast_trigger_untap_pump_target_min_spent` -> MANIFEST `target` (+ the trailing scan); the anthem / attack
  untap / amounts / trample in INERT_PARAMS; two DECISIONS.md rows (the `target` reuse and a no-decision row).

**Deviations / owed**
* **Risk 3 OWED (per-turn shared rank)**: the rank is still ONE whole-turn policy; two qualifying casts in one turn
  share it, so "a different target for each" is findable only where the shared rank happens to name both. A per-cast
  pin needs an ordinal that is lockstep across the executor's continuation re-installs and the rollout's inherited pin
  -- a new per-turn state field plus mixed-radix decoding in both pin consumers. Reachability in this list: two of
  End-Raze (8, or 7 with a Horn) / Decimator (emerge 9 - victim MV) in one turn needs >= 14 mana in a turn -- rare, but
  real with a Sage refund. Raised for the USER; recorded in code (`RaggaUntapPumpAxisWidth` comment).
* **Risk 2 hook for I4**: the cast-trigger axis still skips any plan that carries another axis (`sac_pins`, ...).
  I4's emerge must NOT carry its victim on `sac_pins` through that skip (every emerge plan would silently take rank 0 --
  a greedy pick inside the window), or must cross the two axes. Comment at `RaggaUntapPumpAxisWidth`.
* `CastSpendMayReach` covers MV / X / kicker only; a Raggadragga deck with buyback / entwine / replicate spells must
  extend it (comment). The width bound counts creature CARDS the plan casts, not tokens a mid-plan trigger makes (none
  in this pool).
* The m2 clause does not check that the dork attacked (as I3a); sound, wider.
* Site 9 stands down behind an earlier counted breakpoint in the same apply (its standing rule), so a refund after an
  earlier breakpoint is re-planned only by main 2 / the next decision.

**Open questions (surfaced, not blocking)**
* **Risk 3 -- RESOLVED in I4c (`07e71e938`, below): built per trigger instance, per the agent default.** Original question:
  Raggadragga's "untap target creature, +7/+7" target is searched on the same whole-turn rank policy as the
  Ulamogs' cast triggers: one rank per plan, applied to EVERY qualifying cast that turn. With two 7+-mana casts in one
  turn (two of End-Raze / Decimator, >= 14 mana, reachable with a Sage refund), the search can only find target pairs
  that the one rank names for both casts -- e.g. "untap the Sage, then pump the Boar" may be unfindable at ANY budget.
  That breaks the completeness-at-large-budget invariant (a line made unfindable rather than re-priced), even though
  it is rare in this list. **Recommendation: build it**, folded into I4 (Decimator emerge touches the same axis and
  must already resolve the `sac_pins` skip, risk 2): a per-turn cast-trigger ordinal in GameState, advanced in
  lockstep by the executor's continuation re-installs and the rollout's inherited pin, with mixed-radix decoding of
  the rank in both pin consumers. Until the USER answers, the agent default is to proceed with I4 and build it there.
* D2: Raggadragga's trample grant inert, PROVISIONAL. D6: main 2 opens on a Raggadragga turn with an untapped mana
  creature post-combat (pure-board proxy).

## I4 done (2026-11-13) -- Decimator of the Provinces

Commits: `ccf358f45` (WIP, code) + `2e2356e2e` (tested; audit fixes) on `boars-analysis` (local only, not pushed).
Unit tests 2975/2975 pass (`test/unit/test_boars_decimator.cpp`, 9 cases, passed as first compiled; no engine bug
found). Byte-identity vs the I3b binary (`--games 20 --seed 9101 --depth 3 --budget-ms 10 --ignore-play-profile`,
sorted `[win]` lines): Shadowborn Apostle, Landfall Elementals, Melira Pod all IDENTICAL, 20/20 each. Boars 40 games at
d3 b10: rc 0, 40/40, **avg 5.325** (1 x T4, 26 x T5, 12 x T6, 1 x T7; was 5.525 before emerge), no assert, wall ~1 min
(no slowdown visible). Game logs: 17 Decimator casts in 40 games, ALL emerge (paid {G}{G}{G} .. {5}{G}{G}{G}).
Audits: audit_card_costs rc 0 (615/615); audit_card_fields rc 0 after adding `emerge` to MODELED_ELSEWHERE_KEYWORDS
(structural via `emerge_cost`, the kicker/morph shape) and writing the Decimator snapshot entry from
`build/scratch/boars/scry/DecimatoroftheProvinces.json` (no `--update` round trip); audit_viewer_decisions
`--no-sweep` rc 0, oracle cross-check clean, expected decisions now include `sacrifice`.

**Landed** (detail in `logs/boars-research/I4-handoff.md`)
* cards.json: `emerge_cost {6}{G}{G}{G}`, `cast_trigger_team_pump_power/_tough` 2, `_trample` (inert, D2); keywords
  Trample, Haste; bracket note.
* Emerge = an alternative-cost cast variant PER VICTIM CLASS (board readiness reps + hand victims), emitted beside the
  hard cast by the sac-any-creature post-pass, searched (no victim pick outside the branch). Bill =
  `EmergeCastCost(def, state, victim_mv)`: generic reduced by the victim's MV, floored at 0, then Herald's Horn
  stacks. The victim taps for this very cast, then is sacrificed after the card leaves hand and BEFORE
  `FireOnCastTriggers(mana_spent = bill)`, in both worlds. A same-plan hand victim is cast first via
  `IsDependentAttachSpell` (structural dependency, not a rank change).
* Risk 2 (I3b): the victim rides per-cast `soulfire_own_targets`, NOT `sac_pins`, so emerge plans are crossed with the
  Raggadragga cast-trigger target axis rather than silently taking rank 0.
* Team pump: `TriggerKind::CastTeamPump` -- +2/+2 (trample recorded only) to creatures we control at RESOLUTION, i.e.
  never Decimator itself (still on the stack; CR 608.2h/611.2c locks the affected set), both worlds.
* Plan signatures carry `#EM`/victim; BoarsProvider signature ORs the new params; Horn take-fold whitelist; viewer
  `emerge_cost` -> MANIFEST `sacrifice` (human chooser asked before the bill), main-phase emerge mode sub, DECISIONS.md
  rows.

**Owed / deviations**
* **This-turn win estimate ignores Decimator's team pump** (`ProjectPlanWalk` / the kWalkPump flag, TurnSolver
  ~14033): an underestimate (conservative); `xw_nowin` only matters for X spells (none in Boars). Fix: add
  `cast_trigger_team_pump_power` to kWalkPump + a pump step before the body enters.
* Castability probes (`HandCardLegalToCast` etc.) still price printed {10} (ordering/gating heuristics only; the
  post-pass emits emerge regardless, unit-tested).
* `CastSpendMayReach` (Ragga axis width) uses printed MV 10 -> widens the axis needlessly for a cheap emerge (sound).
* Chandra nested-cast serializer does not carry `emerge` (no pool combines them); `EmergeVictimMv` ignores face-down
  MV 0 (no morph in Boars).
* Ilharg as emerge victim / D6 main-2 put-body condition: I5.
* Viewer GUI walk of the emerge mode + pre-payment board click NOT done (Stage 5h).
* Risk 3 (per-cast Raggadragga target rank) NOT built here: brief ruling "do NOT do the Raggadragga per-cast target
  change (next integrator)". Still an OPEN USER question.

## I4c done (2026-11-13) -- Raggadragga target per trigger instance (risk 3 RESOLVED)

Commit `07e71e938` on `boars-analysis` (local only, not pushed). Unit tests 2977/2977 pass (two new cases in
`test/unit/test_boars_raggadragga.cpp`, one I3b subcase moved to the new rank encoding). Byte-identity vs the I4
binary (`build/scratch/boars/mtg.i4`, `--games 20 --seed 9101 --depth 3 --budget-ms 10 --ignore-play-profile`, sorted
`[win]` lines, `build/scratch/boars/i4c_ident.sh`): **Extra Turns** and **Unpredictable Cyclone** (the Ulamog
cast-trigger-target users), Stompy Surprise, Angels all IDENTICAL, 20/20 each. Boars 40 games d3 b10: rc 0, 40/40,
**avg 5.325** (1 x T4, 26 x T5, 12 x T6, 1 x T7) -- the sorted `[win]` lines are identical to I4's (no game in this
sample had two qualifying casts in one turn), wall 18 s vs 17 s (box shared with the unit suite). Audits:
audit_card_costs rc 0 (615/615); audit_card_fields rc 0 (advisory oracle divergences only); audit_viewer_decisions
`--no-sweep` rc 0, oracle cross-check clean.

**Design**
* The Raggadragga rank is a per-INSTANCE tuple packed in `Plan::cast_trigger_target_choice` (widened int16 -> int32,
  sizeof(Plan) unchanged at 656): 0 = every instance takes the provider's front; else low 4 bits = digit width B, then
  one B-bit digit per trigger instance (`UntapPumpRankPack` / `UntapPumpRankDigit`, SpellEffects.h; 27 payload bits).
  Only the untap-pump consumer decodes; the Ulamogs read the raw rank as before.
* Instance index = `GameState::cast_untap_pump_ordinal` (NEW uint8 in bool padding, sizeof unchanged, monotonic, bumped
  once per untap-pump trigger in the shared `FireOnCastTriggers`) minus the ordinal saved when the pin was installed.
  All three installs pass it (ApplyPlanDirect's `_sctt`, AIEngine's committed-plan install and the breakpoint
  re-solve adopt), so a re-solve's own pin starts at its digit 0 and an inheriting continuation reads the outer pin's
  remaining digits -- lockstep in both worlds. Dominance.h: read only relative to an install, nothing to fold.
* `AppendRaggaInstanceVariants` (TurnSolver.cpp): every tuple over [0, w)^m except the one the base plan already plays
  (all-zero on a fresh turn, else the live pin's remaining digits clamped to w-1 -- StatePinVariantRanks per digit);
  w = I3b's per-trigger option upper bound, m = qualifying casts in the plan x Raggadraggas we control. It also fans
  breakpoint CONTINUATION plans (cont_axes_only, Raggadragga only), so a second qualifying cast that a site-9 refund
  makes affordable is searched in the rollout rather than inheriting the outer plan's digit. Lossless: an overshoot
  digit clamps at resolution; tuples that land on the same state fold in the post-apply state dedup; identical targets
  are one class already (CastTriggerTargetFoldKey). No heuristic narrowing.
* Viewer: the `target` decision is asked once per trigger instance (it already was per call; DECISIONS.md row says so).

**Tests**: (1) two End-Raze casts off one board reach all six ordered target outcomes (incl. untap the Sage, then pump
the Boar), the ordinal advances per trigger, and a later install starts at its own digit 0; (2) the SEARCH
(SolveWithLookahead d2) and the EXECUTOR (AIEngine d2 TakeTurn) both untap two DIFFERENT tapped Apex Altisaurs with two
Elderscale Wurm casts for a 34-damage kill (opponent at 30); under the shared rank, rank 0 names the Altisaur class and
then the untapped Altisaur again (17 at most).

**Owed / limits**
* A plan casting BOTH an Ulamog and a qualifying spell with Raggadragga out keeps the shared flat rank, which the
  untap-pump consumer then decodes as a tuple (no deck runs both; comment at the axis).
* Storage bound: 27 payload bits (6 instances at w <= 16) -- 42+ mana of qualifying casts in one turn.
* Variant count is w^m for a plan with m qualifying casts (no cap, per the completeness rule); in Boars m >= 2 needs
  >= 14 mana in one turn, so the cost is rare. Not measured beyond the 40-game run.


## I5a done (2026-11-13) -- Ilharg, the Raze-Boar (attack put + end-step return)

Commit `5be6f80bf` on `boars-analysis` (local only, not pushed). Unit tests **2987/2987** (10 cases in
`test/unit/test_boars_ilharg.cpp`). Byte-identity vs the I4c binary (`build/scratch/boars/mtg.i4c`, `--games 20 --seed
9101 --depth 3 --budget-ms 10 --ignore-play-profile`, sorted `[win]` lines, `build/scratch/boars/i5a_ident.sh`):
**Knights** (Adeline / Hero tokens), **Goblins** (Lackey `lackey_put`), **Anti-Lifegain** all IDENTICAL, 20/20 each.
Boars 40 games d3 b10: rc 0, 40/40, **avg 5.225** (1 x T4, 30 x T5, 8 x T6, 1 x T7), no fd-diverge; wall 13.1 s vs
20.6 s for the I4c binary on the same command (I4c's binary reads 5.275 today: it sees Ilharg as a vanilla 6/6).
Audits: audit_card_costs rc 0 (616/616); audit_card_fields rc 0 after adding the Ilharg snapshot from
`build/scratch/boars/scry/IlhargtheRazeBoar.json` (advisory oracle divergence only); audit_viewer_decisions `--no-sweep`
rc 0, oracle cross-check clean.

**Landed**
* cards.json Ilharg (verbatim oracle + bracket notes; dies clause PENDING I5b, exile half D3, trample D2), params
  `attack_put_creature_from_hand` / `attack_put_return_at_end_step` (loader throws on return without put), Horn audit.
* `FireAttackPutFromHand` (SpellEffects.h), ONE body called by both combats right after FireMilitiaPrideTokens: sources
  from the DECLARED prefix only; CR 508.1f declaration tap first; put body tapped through FireEtbWatchers /
  FireOwnEtbTriggers + legend rule, NOT cast; atk_idx rebuilt by number. `Permanent::return_at_end_step` +
  `PerformEndStepDelayedReturns` (both end steps; an emerged-away body is not returned, CR 603.7c), folded at every
  destroy_at_end_if_attacked site.
* Searched pin `Plan::attack_put_choice` -> `GameState::scripted_attack_put` (-1 unpinned, -2 decline, >0 card number),
  mirrored at every militia_pride_pay_choice site; sizeof(Plan) 656 -> 664.
* Projection addend `ApplyAttackPutProjection` (put body + End-Raze ETB pump priced), Herald's Horn take-fold `+1` per
  returning card (conservative), D6 main-2 clause (return body on bf + emerge card in hand), viewer `lackey_put` with
  `"trigger":"attack"`, BoarsProvider signature.

**Completeness fix (this integrator).** The WIP axis skipped the variant equal to the provider's `AttackPutDefault`,
relying on the unpinned base plan to make that put. That was lossy: the base resolves the default at COMBAT time, and
the default's formula reads the battlefield (own creature count for End-Raze's pump; a legend already in play), which
this plan's own casts change -- so the plan-time front could differ from the realised one and the skipped put was
unreachable. The axis now emits DECLINE plus an explicit pin for EVERY candidate name, the default's included. The only
fold left is the inherited state pin's NAME (the base carries that pin to combat unchanged: provably the same put).
Test: "the put axis emits an EXPLICIT variant for every name, the default's included".

**Where AttackPutDefault is consulted** (none of it is an in-window pick): beyond the horizon (the playout -- the
permitted greedy), by the unpinned base plan at combat (one EXTRA line beside the explicit pins, never a stand-in for
one), on a pin miss (`g_attack_put_pin_miss`, the pinned card left hand), and as the human chooser's pre-selection.
**USER QUESTION (agent default):** the playout policy is agent-chosen -- the highest power counting End-Raze's ETB pump
(`power + etb_other_creatures_pump_power * own creatures`), a legend we already control last, then higher mana value,
then lower card number. It orders the playout only; inside the window every option is branched.

**Deviations / residuals**
* A creature card that enters the hand AFTER plan time (a mid-main draw) is reachable only through the unpinned base
  (the default), because breakpoint continuations do not re-fan combat pins (cont_axes_only). No card in Boars adds a
  card to hand mid-main (Herald's Horn takes in upkeep, before the plan); a deck that does would need a rank pin.
* The unpinned base usually duplicates its default's explicit variant (a small redundancy, kept because it is the only
  route to a mid-main draw; candidate for a post-combat fold).
* Exalted is still dropped when more than one creature attacks (lockstep with the current engine) -- I5b.
* No dedicated real-resolution node: the put is a combat pin like Militia's Pride, chosen before combat.
* Pre-existing per-attacker attack-trigger helpers took put creatures as SOURCES (wrong: CR 508.4) -- fixed in I5b item 2.

## I5b items 1-2 done (2026-11-13) -- declared-attacker rules fixes the Ilharg put exposed

* **Item 1 `b8f9c7ba5`:** exalted's "attacks alone" counts the DECLARED attackers (CR 506.5 / 702.83) in both combats
  and the put projection. 9 decks identical vs I5a; Boars 40/40 avg 5.225 byte-identical.
* **Item 2 `b268c794c`:** a creature put onto the battlefield attacking (Ilharg's put, Hero/Adeline tokens, Militia's
  Pride) was never declared (CR 508.4), so every per-attacker "whenever this / a <type> creature attacks" helper now
  takes its SOURCES from the declared prefix of the attacker list only, in both combats: self pumps (incl. Kragma's
  matching pump), battle cry (recipients still everyone), annihilator, exile-defender-top, attack draws, Goblin Guide,
  Jorn's untap, Bogbeast's team pump, Hero's creature-scoped tokens, Bruna's gather, dig-attach, Omnitool's dig,
  Surtland's fling, and ResolveCombatDamage's Leeching Sliver life loss / Utvara tokens. Raubahn's attach and the
  fling rebuild the list by number, so the declared count is recounted after them. Player-level "whenever you
  attack" (Adeline) is unchanged; every attacker still deals combat damage. 4 unit cases; mtg-test 2994/2994.
* **Identity vs item 1 (20 g d3 b10 seed 9101, sorted wins):** Knights, White Knights, Kithkin, Angels, Soldiers,
  Goblins, Bruna, Minotaur, Selesnya Lifegain, Snow, Giants, Kitty Equipment, Colossus Hammer, Five Colour, Extra
  Turns, Dragons, Slivers, Pirates, Jumbo Cactuar, Burn, Hydras, Vampires, Fungus, Stompy Surprise: all IDENTICAL.
  **Boars 40 g: 40/40 avg 5.225**, byte-identical. **No moved decks, no GT to re-accept** for items 1-2: no suite
  deck's undeclared body is itself a source of one of these triggers.
* Residual (projection only): `ApplyAttackPutProjection` makes the put body an ordinary attacker, so the projection
  could credit a put body's own attack trigger. No Boars put target has one that the projection prices.
* Items 3 (Ilharg dies clause) and 4 (legend-rule keep choice) remain: `logs/boars-research/I5b-handoff.md`.
