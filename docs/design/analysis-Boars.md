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
