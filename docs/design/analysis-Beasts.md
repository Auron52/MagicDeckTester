# Analysis ledger — Beasts

Started 2026-11-21. Worktree `.claude/worktrees/beasts`, branch `beasts-analysis`, based on
`origin/phase-1-2-deck-analyzer` @ c9a2e1ff6. Deck file `decks/Beasts/Beasts.cod` (copied from the
primary checkout's untracked `decks/Beasts/`, original left in place). Nothing pushed yet.

## Checkpoint (update after every milestone)

* **Last completed stage:** Stage 1 coverage; Stage 2 research drafts 13/13 (ALL) in
  `logs/beasts/drafts/*.md` (Thragtusk, Contested Cliffs, Packleader, Cragplate Baloth, Krosan
  Warchief, Questing Beast, Elder Gargaroth, Garruk's Harbinger, Kaheera(+Cavern recheck), Migloz,
  Nykthos).
* **Baseline build:** done, rc 0 (Release, worktree base c9a2e1ff6).
* **Batch A integrated:** A1 `375a50399`, A2 `53e68dcd3` (handoff `logs/beasts/integrator-A-handoff.md`;
  14/14 batch-A unit tests pass, cost audit rc 0). Includes the Herald dup-legend upside in
  `DuplicateCastHasUpside`. A tail still owed: full unit suite, `audit_card_fields.py --update`,
  smoke byte-identity (no accept).
* **Batch A tail DONE (integrator B, 2026-11-21):** full unit suite (2 batch-A census failures fixed:
  kicker count 5 with Cragplate Baloth; decks/Beasts pinned to its INTERIM Boars route in the Pirates
  routing table -- batch J re-pins it), `audit_card_fields.py --update` + offline rc 0, snapshot committed,
  `audit_viewer_decisions.py --no-sweep` clean.
* **Batch B integrated (Contested Cliffs):** NEW PermAbilityMode::TapFight + shared ResolveFight (deathtouch
  honoured -> Questing Beast's bracket is true; immediate SbaKillDeadCreatures pass fires Thragtusk's LTB and
  the Packleader draw), both targets searched, emitted in main 1 and any main 2, NOT in DeckUsesSecondMain.
  6 new unit tests. See the integrator B commits on this branch.
* **Open gap (batch B, surfaced):** a Packleader draw off a fight-death token in main 1 lands in the
  trailing pass, so that card cannot be cast the same main (no breakpoint re-solve for a trailing-activation
  draw). Covered by the per-turn second-main gate planned for the provider batch.
* **Smoke byte-identity (batches A + B, at 27e1f4892, no accept):** 235/235 PASS, per-game audit 0 configs changed, play-changed 0 (searched and d0). Log `build/scratch/beasts_smokeB.log` (build volume).
* **Batch C integrated (Elder Gargaroth, integrator C 2026-11-21; see the batch C commit on this branch):** NEW
  `attack_choose_one` + `attack_choose_token_*` / `attack_choose_lifegain` / `attack_choose_draw` (NOT
  `attack_trigger_modal`). Shared `ApplyAttackChooseOneTriggers` (SpellEffects.h) called right after
  `ApplyAttackModalTriggers` in GameEngine::CombatPhase AND the rollout's SimulateOneCombat (lockstep), per
  DECLARED copy. Searched pin `Plan::attack_choose_mode_choice` -> `GameState::scripted_attack_choose_mode` (int16,
  2 bits/ordinal, spent after the loop, cleared at both turn starts). Axis on the Tectonic pattern in
  AppendSubdecisionAxes (after Thunderkin), product cap 27, skipped under HumanPlayActive and on a dead post-combat
  list. Life fold = root `DecisionProvider::AttackChooseOneModes` (token, draw, life; life dropped when the NEW deck
  stamp `deck_life_inert` && !own_death_live; `MTG_UNPRUNE=attackchooselife` restores it). Folded into
  Dominance::Build, BuildDedupKey combat pins (new LAST slot), plan-cache scalar key (value-gated), BpCandFingerprint,
  solvememo, PlanDomCensus, RouteSig, every axis skip list. sizeof(GameState) 1712 -> 1720, Plan 784 -> 792 (asserts updated).
  Viewer: attack_mode widened to 3 options (main.cpp clamp 0..2 for an attack_choose_one source + Gargaroth JSON
  branch; index.html default reads any listed index); DECISIONS.md row; audit manifest. 8 new unit tests, all pass;
  full unit suite 3843/3843. Viewer audit `--no-sweep` clean. Smoke NOT run (next batch runs one pass for C + D).
* **Open gap (batch C, surfaced):** a card drawn by Gargaroth's draw mode (or Packleader off its token) arrives in
  combat; the deck has no searched second main (NOT added to DeckUsesSecondMain, USER unsettled), so it waits a
  turn. Covered by the per-turn second-main gate planned for the provider batch (J).
* **Batch C integrated:** `2f1375982` (Elder Gargaroth; 3843/3843 unit tests; smoke for C ran with D below: 235/235 byte-identical).
* **Batch D integrated (Garruk's Harbinger, integrator D2 2026-11-21): `afa007b00`** (replaces the WIP commit
  7227280c1, which a 9p mount loss cut off). NEW `combat_damage_dig_per_damage` / `combat_damage_dig_types`; shared
  `FireCombatDamageDig` in BOTH damage-step shapes of the combat core (count = damage actually dealt, Creature/Garruk
  filter, rest to the bottom via `BottomInRandomOrder`, no dig once the opponent is dead). Searched RANK pin
  `Plan::combat_dig_choice` -> `GameState::scripted_combat_dig` (4 bits/ordinal, 3 ordinals, cap 27; a rank, not a
  card number -- DESIGN DEPARTURE from the draft, because the window is the top N after this turn's draws). Axis
  `harbingeraxis` width = min(15, matching library cards, power + 2); no decline variant (dominated; the viewer offers
  it). NOT in DeckUsesSecondMain. Sizes unchanged (1720 / 792, padding). Viewer: `dig` reuse, DECISIONS.md row,
  audit mapping. Keyword divergence (Scryfall Hexproof / 'Hexproof from') allowlisted. 7 new unit tests; full unit
  suite 3850/3850; audit_card_costs rc 0, audit_card_fields offline rc 0 (`--update` hit 5 HTTP 429s, old snapshot
  entries kept), audit_viewer_decisions `--no-sweep` clean.
* **Smoke byte-identity (batches C + D, at afa007b00 + ledger, no accept):** 235/235 PASS, per-game audit 0 configs
  changed, play-changed 0 (searched and d0). Log `build/scratch/beasts_smokeD.log` (build volume).
* **Owed to batch J (BeastsProvider), from D:** (1) the dig does NOT record into `combat_hand`; the per-turn
  second-main gate must add that record and its state fold together; (2) the root `EtbDigCandidates` does not merge
  same-name copies, so duplicate names give wasted (identical-state) variants -- add a same-name fold in the provider;
  (3) Adaptive Omnitool's combat dig can now reuse this pin (`docs/design/omnitool-combat-dig-greedy.md`).
* **Running:** none. NEXT = batch E (Migloz).
* **Orchestrator decisions taken (PROVISIONAL, surfaced to USER):** implement Kaheera companion
  (Tier 3) + make the coverage scan treat a met companion as a reachable sideboard; deathtouch is
  honoured in the shared Cliffs fight helper (not deferred); deck-wide searched second main;
  Gargaroth life mode = provider dominance fold; provisional deferrals listed per card in the
  table below.
* **Next:** (1) save the last two drafts; (2) serial integration — ONE integrator agent at a time
  (cards.json + shared C++ can't take concurrent edits), grouped by subsystem, each building,
  adding unit/scenario tests, running `scripts/audit_card_costs.py` + `audit_card_fields.py
  --update`, committing locally; suggested batches: A) Tier-1/2 data cards (Packleader, Warchief,
  Questing Beast, Thragtusk, Baloth, Kaheera lord+vigilance); B) Contested Cliffs fight (+
  deathtouch) ; C) Elder Gargaroth modal axis + viewer; D) Garruk's Harbinger dig pin; E) Migloz
  oil + pump; F) Nykthos devotion mana; G) Bloated Contaminator toxic/proliferate; H) Woodland
  Bellower; I) Kaheera companion; J) BeastsProvider (route above `boars`, own signature,
  Certificate, dup-legend upside params). Then Stage 3 coverage → Stage 4 profile → 4-bis suite
  rows (3x Hinata gate) → Stage 5 → Stage 6 report.
* Brief every integrator with: the 250K handoff rule, no shared-branch push, no `cd X &&`,
  ONE heavy job at a time, read the draft file + this ledger.

## Deck

60 cards, G/r Beast tribal ramp: 23 lands (incl. 2 Contested Cliffs, Nykthos, 2 Cavern of Souls,
1 Gruul Turf, 7 fetches), 8 land Auras (Utopia Sprawl, Wild Growth), 2 Urza's Incubator, 4 Krosan
Warchief, 4 Herald of Ilharg, payoffs (Elder Gargaroth, Thragtusk, Questing Beast, Craterhoof,
Garruk, Packleader, …). Sideboard: 1 Kaheera (companion residue — see open questions).

**Every creature is a Beast** (Scryfall-verified 2026-11-21; Bloated Contaminator and Migloz are
Phyrexian Beasts, Kaheera a Cat Beast, Herald a Boar Beast). So Cavern of Souls / Urza's Incubator
(chosen type Beast), Krosan Warchief, Kaheera's lord and Contested Cliffs' "target Beast" cover the
whole creature base. (An earlier draft of this ledger wrongly called Contaminator a Rat.)

## Stage 1 — coverage (2026-11-21)

Missing (13): Contested Cliffs, Thragtusk, Krosan Warchief, Elder Gargaroth, Questing Beast,
Woodland Bellower, Cragplate Baloth, Garruk's Packleader, Garruk's Harbinger, Migloz Maze Crusher,
Nykthos Shrine to Nyx, Bloated Contaminator, Kaheera the Orphanguard.

Already implemented (13): Forest, Mountain, Gruul Turf, Stomping Ground, Wooded Foothills,
Windswept Heath, Garruk Primal Hunter, Craterhoof Behemoth, Herald of Ilharg, Urza's Incubator,
Utopia Sprawl, Wild Growth, Cavern of Souls.

Bracket notes to re-check for THIS deck:
* **Cavern of Souls** — "ETB creature-type choice simplified to 'any creature' — exact for a
  tribal list whose creatures share the named type". Exact here (all creatures are Beasts).
* Fetch-hold review (Wooded Foothills, Windswept Heath): hold is modelled; the deck has
  landfall-free fetch use, so hold mostly matters for shuffle/Garruk's Harbinger dig timing. To
  review at Stage 5.

## Stage 2 — cards

| card | tier | status | notes |
|---|---|---|---|
| Thragtusk | 2 | integrated 53e68dcd3 | new ltb_self_creates_tokens on FireLeavesBattlefieldTriggers |
| Contested Cliffs | 3 | integrated (batch B, see Checkpoint) | NEW PermAbilityMode::TapFight (Kessig shape), shared ResolveFight (deathtouch + immediate SBA death pass); both targets searched (own class x opp class); emitted in main 1 and any main 2; NOT added to DeckUsesSecondMain (USER: second main unsettled) |
| Cragplate Baloth | 2 | integrated 53e68dcd3 | new kicked_enters_with_plus_counters (pre-entry kicked branch) |
| Kaheera companion (sideboard) | 3 | planned | companion {3} put-into-hand, once/game, sorcery speed |
| Krosan Warchief | 1 | integrated 375a50399 | reduces_spell_subtype Beast; regenerate PROVISIONAL deferral (dominated) |
| Questing Beast | 1 | integrated 375a50399 | deathtouch via fight helper; D2-D4 PROVISIONAL (no blocks / no prevention / no PWs); dup-legend prune risk |
| Elder Gargaroth | 3 | integrated (batch C, see Checkpoint) | NEW attack_choose_one 3-mode trigger, searched per-ordinal axis (Tectonic-style, cap 27), shared executor+rollout helper; life mode = root provider fold on deck_life_inert (USER approved); viewer attack_mode widened to 3; 'or blocks' PROVISIONAL deferral; NOT in DeckUsesSecondMain (combat draw waits a turn until the per-turn gate) |
| Garruk's Harbinger | 3 | integrated afa007b00 (batch D, see Checkpoint) | NEW combat_damage_dig_per_damage + FireCombatDamageDig (both damage-step shapes, shared executor+rollout; count = damage dealt), shared PerformLookTakeDig (Creature/Garruk, random bottom); searched RANK pin per ordinal (cap 27, width min(15, library matches, power+2)); no decline variant (dominated, viewer offers it); NOT in DeckUsesSecondMain (dug card waits a turn until the per-turn gate); hexproof-from-black + PW PROVISIONAL |
| Kaheera | 2 | lord+vigilance integrated 53e68dcd3; companion pending (batch I) | lord_effect multi-subtype + NEW grants_vigilance; companion: implement (see Q1) |
| Migloz | 3 | drafted | oil_counters int; searched pump (ActivatePump mode 4); Steelbane destroy w/ oil + own-target provider prune; vig/menace PROVISIONAL |
| Cavern of Souls (recheck) | - | no change | exact for this list |
| Nykthos | 2 | drafted | Three Tree City scaled-land shape + NEW mana_per_devotion (per-colour yield, cache key!) |
| Bloated Contaminator | 2 (+3) | drafted | NEW toxic + combat_damage_self_proliferate (closed-table dominant set, viewer dragon multi-pick); poison in sim key + no-win bounds; 2HG 15 poison |
| Woodland Bellower | 2 | drafted | Natural Order put route wired into creature ETB + NEW tutor_nonlegendary + optional decline (no shuffle); fail-to-find + trigger order PROVISIONAL |
| Garruk's Packleader | 1 | integrated 375a50399 | own_creature_enters_draw + creature_enters_min_power 3; "may" PROVISIONAL |

Drafts: `logs/beasts/drafts/*.md` (gitignored, host drive).

## Viewer classifications (2c-ter)

* Contested Cliffs (batch B): bucket A. ONE unpinned main-phase entry under human play; both fight targets (our Beast, then theirs) are `target` board clicks at activation via `g_play_loyalty_chooser` (Voracious precedent). `audit_viewer_decisions.py`: `tap_fight_cost` -> target / activate, `tap_fight_requires_subtype` inert. Registry: tools/play/DECISIONS.md "Targeted tap fight".

* Elder Gargaroth (batch C): bucket A (reuse, widened). `attack_mode` with THREE options (0 token, 1 life, 2 draw), asked per declared copy at declare-attackers from `ApplyAttackChooseOneTriggers` over the full legal menu (the search's life fold never narrows the human). `audit_viewer_decisions.py`: `attack_choose_one` -> attack_mode, payload params inert. Registry: tools/play/DECISIONS.md attack_mode Gargaroth row.

## Verification verdicts (Stage 5)

(pending)

## USER rulings (2026-11-21, verbatim, on the open questions)

1. Companion: *"Yes, Kaheera is a companion in the deck."* -> implement (batch I).
2. Second main: *"I'm not sure about this for cost reasons at least if we search everything this
   way. This will require some thought."* -> NOT settled. Build the cheap per-turn shape
   (`ProviderSecondMainLive` in BeastsProvider: open a searched main 2 only on a turn where a
   combat-time event put a card in hand -- Harbinger dig, Gargaroth draw mode, a Packleader draw off a
   combat-created token), keep deck-wide reachable via `MTG_FORCE_USES_M2`, and at Stage 4 measure
   none / per-turn / deck-wide (units, ms/game, avg win turn) for the USER. Per-turn is the
   provisional shape in the suite rows.
3. Gargaroth life mode: *"Yes, we don't need this in the search."* -> provider fold approved.
4. Packleader/Bellower order: *"That's fine."* -> disclose as ordering collapse (approved).
5. Merfolk Turns Cavern/Seahunter: *"That should be fixed, but only when there is time."* -> deferred,
   `docs/design/cavern-of-souls-creature-type.md`.
6. Omnitool dig: *"If choosing the top is built into the search this is a notable problem, yes.
   However, if it is just that the provider returns one item it is not an issue."* -> checked: it IS
   the former (EtbDigCandidates returns the legal set unpruned; the combat fire site takes the top,
   no searched pin). Unreachable in shipped play (Omnitool is sideboard on the shipped list, deck
   stamp is mainboard-only). Fix = reuse Harbinger's combat-dig pin (batch D) after it lands; deferred
   doc `docs/design/omnitool-combat-dig-greedy.md`.
7. Deferrals: *"For Migloz we should include the power pump mode for sure. The other two can be
   heuristically pruned."* -> Migloz +2/+2 pump searched; vigilance/menace and destroy pruned in the
   provider (APPROVED). Warchief regenerate, Questing Beast D2-D4, Gargaroth "or blocks", Harbinger
   hexproof-from-black/PW, Packleader "may": still awaiting an explicit word. Bellower: asked what the
   issue is -- answered (it is the fail-to-find-while-legal-cards-remain option only).
8. Cast order: *"Okay, I'll try to look over what you draft tomorrow."*

## Open USER questions (surfaced, not blocking)

1. **Kaheera companion (sideboard).** The deck MEETS Kaheera's companion condition (every
   creature is a Beast). The simulator has no companion mechanic, so the sideboard Kaheera is
   currently unreachable (coverage reports `reachable: false`). Ignoring it is NOT provably inert
   (an extra 3-mana lord for {3}, once per game), so the default taken is to IMPLEMENT companion
   (CR 702.139: "{3}: put your companion into your hand", sorcery speed, once per game; Tier 3) and
   teach the coverage scan that a companion makes the sideboard reachable. Say if you would rather
   it stay unmodelled.

## Provider routing (Stage 4a, found in Stage 2)

Today the deck routes to **BoarsProvider** (Herald's `creature_cast_trigger_plus_counters`), a
foreign provider = Stage 4a failure; without Herald it would misroute to Goblins
(`reduces_spell_subtype`). Plan: a `BeastsProvider` routed above `boars`, signature OR-ing
Beasts-only params from several new cards.

2. **Second main (USER-owned main-1-vs-main-2).** Contested Cliffs (fight after attacking),
   Elder Gargaroth (draw mode / token -> Packleader draw) and Garruk's Harbinger (combat-damage
   dig) all create combat-time resources, so the deck needs a searched post-combat main.
   Default taken: deck-wide `DeckUsesSecondMain` (Hellkite / Tectonic precedent). The cheaper
   alternative is a per-turn gate live only after a combat-time draw/dig.
3. **Elder Gargaroth's life mode** is folded away in the provider as a sound dominance (nothing
   reads our life; max self-damage 13 < 20). The viewer still offers all three modes.

4. **Packleader + Bellower trigger order (CR 603.3b).** When Bellower enters, Packleader's draw
   and Bellower's search trigger together; the engine always resolves the draw first (pre-shuffle
   top card). Default: disclose as an ordering collapse; the alternative is a searched order axis.
5. **Other deck (Merfolk Turns):** Cavern of Souls' "any creature" simplification lets its
   coloured mana pay 4 Seahunter (Human Mercenary, not Merfolk). Fixing it moves Merfolk Turns GT.
6. **Adaptive Omnitool** (another deck) appears to take its combat dig as the provider's top
   pick with no searched pin — a possible in-window greedy (found while drafting Harbinger).

## Approved deferrals

* Migloz {1}/-1 oil vigilance+menace: pruned (USER 2026-11-21).
* Migloz {3}/-3 oil destroy artifact/enchantment: provider-pruned (USER 2026-11-21).
* Elder Gargaroth gain-3 mode: provider fold (USER 2026-11-21).
* Packleader/Bellower trigger order collapse (USER 2026-11-21).
