# Analysis ledger — Beasts

Started 2026-11-21. Worktree `.claude/worktrees/beasts`, branch `beasts-analysis`, based on
`origin/phase-1-2-deck-analyzer` @ c9a2e1ff6. Deck file `decks/Beasts/Beasts.cod` (copied from the
primary checkout's untracked `decks/Beasts/`, original left in place). Nothing pushed yet.

## Checkpoint (update after every milestone)

* **Last completed stage:** Stage 1 coverage; Stage 2 research drafts 11/13 in
  `logs/beasts/drafts/*.md` (Thragtusk, Contested Cliffs, Packleader, Cragplate Baloth, Krosan
  Warchief, Questing Beast, Elder Gargaroth, Garruk's Harbinger, Kaheera(+Cavern recheck), Migloz,
  Nykthos).
* **Still running (background agents, read-only):** Woodland Bellower, Bloated Contaminator
  research. Both were sent the correction that Contaminator is a Phyrexian Beast.
* **Baseline build:** `./build.sh` (Release) of the worktree, log
  `/workspaces/MagicDeckTester/build/scratch/beasts_build0.log` (rc appended at the end).
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
| Thragtusk | 2 | drafted | new ltb_self_creates_tokens on FireLeavesBattlefieldTriggers |
| Contested Cliffs | 3 | drafted | new PermAbilityMode::TapFight (Kessig shape + Voracious fight); main 2 |
| Cragplate Baloth | 2 | drafted | new kicked_enters_with_plus_counters (pre-entry kicked branch) |
| Kaheera companion (sideboard) | 3 | planned | companion {3} put-into-hand, once/game, sorcery speed |
| Krosan Warchief | 1 | drafted | reduces_spell_subtype Beast; regenerate PROVISIONAL deferral (dominated) |
| Questing Beast | 1 | drafted | deathtouch via fight helper; D2-D4 PROVISIONAL (no blocks / no prevention / no PWs); dup-legend prune risk |
| Elder Gargaroth | 3 | drafted | new attack_choose_one 3-mode searched axis (Tectonic-style), viewer attack_mode widened to 3; 'or blocks' PROVISIONAL; life mode = provider dominance fold |
| Garruk's Harbinger | 3 | drafted | new combat-damage dig (PerformLookTakeDig, random bottom), searched card-number pin (Lackey shape); hexproof-from-black + PW PROVISIONAL |
| Kaheera | 2 | drafted | lord_effect multi-subtype + NEW grants_vigilance; companion: implement (see Q1) |
| Migloz | 3 | drafted | oil_counters int; searched pump (ActivatePump mode 4); Steelbane destroy w/ oil + own-target provider prune; vig/menace PROVISIONAL |
| Cavern of Souls (recheck) | - | no change | exact for this list |
| Nykthos | 2 | drafted | Three Tree City scaled-land shape + NEW mana_per_devotion (per-colour yield, cache key!) |
| Garruk's Packleader | 1 | drafted | own_creature_enters_draw + creature_enters_min_power 3; "may" PROVISIONAL |

Drafts: `logs/beasts/drafts/*.md` (gitignored, host drive).

## Viewer classifications (2c-ter)

(pending)

## Verification verdicts (Stage 5)

(pending)

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

## Approved deferrals

(none yet)
