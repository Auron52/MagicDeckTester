# Analysis ledger — 1 Drop Burn

Deck: `decks/1 Drop Burn/1 Drop Burn.cod` (60 cards, mono-R: 14 Mountain, 2 Sunbaked Canyon, 3 Fireblast,
4 Lightning Bolt, 2 Needle Drop, 4 Goblin Guide, 3 Grim Lavamancer, 4 Monastery Swiftspear, 4 Vexing Devil,
4 Boltwave, 4 Chain Lightning, 4 Lava Spike, 4 Rift Bolt, 4 Skewer the Critics).

Started 2026-11-17 on `phase-1-2-deck-analyzer` @ 17262a61f.

## Stage 1 — coverage

* Missing: **Needle Drop**, **Vexing Devil**.
* All other 12 cards `full`. Goblin Guide carries a bracket note (reveal implemented against the passive
  opponent's library; the actual-deck opponent model is a separate architecture project) — existing,
  shared with `decks/burn`, already accepted.
* No sideboard; no fetchlands (`fetch_hold_review.required = false`).

## Stage 2 — cards

| card | Scryfall cost | oracle | tier | status |
|---|---|---|---|---|
| Needle Drop | `{R}` Instant | deals 1 damage to any target that was dealt damage this turn. Draw a card. | 3 | **implemented** -- `direct_damage` + `cast_draw: 1` + new `target_dealt_damage_this_turn` (see below) |
| Vexing Devil | `{R}` Creature — Devil 4/3 | When this creature enters, any opponent may have it deal 4 damage to them. If a player does, sacrifice this creature. | 2 | **implemented** -- `vanilla_creature` + new `etb_opp_may_take_damage_to_sac: 4` (PROVISIONAL opponent policy) |

### Stage 2 implementation notes

* **Damage tracker.** New `GameState::player_dealt_damage_this_turn[2]` (DAMAGE only -- combat or noncombat,
  never bare life loss / life payment; index 1 = the opposing team in 2HG). Written through
  `GameState::MarkPlayerDealtDamage` at every damage-to-a-player site (combat damage in both combat paths,
  `RecordNoncombatDamageToPlayer`, `DealPermanentAbilityDamage`, the armed damage-event model, and each
  open-coded opponent-damage site that sets `opponent_lost_life_this_turn`; the pure life-loss sites -- drains,
  attack-trigger life loss, Tainted Remedy flips -- deliberately do not mark). Reset at both untap sites;
  rolled back by every mana-payment probe that rolls back `opponent_lost_life_this_turn`; folded into
  `dominance::Build` / `BuildSimKey` only when true AND the deck constant `deck_reads_damage_dealt` is
  stamped (only decks holding Needle Drop), so other decks are byte-identical. Self-damage from painlands is
  not marked (no Needle Drop deck has one; it only affects targeting yourself).
* **Needle Drop legality.** Shared helpers in `SpellEffects.h`: `DamagedThisTurnPinLegal` (players by the
  tracker, creatures by marked damage), `DamagedTargetCastLegal` (the pre-payment cast gate in BOTH apply
  worlds, beside the Path / Aura gates), `FixedBurnCandidates` filters the cast-time list (the viewer's
  `target` decision reads it -- Bucket A) while the enumeration list (`pending`) keeps every target,
  `ResolveFixedBurnPin` / the face-only resolution re-check on resolution (CR 608.2b: no draw on a fizzle),
  `FreeCastHasLegalTargets` for the free-cast walks. A set holding Needle Drop pays per cast
  (`PP_DYNAMIC_COST`) so a refused cast never leaves pre-paid mana tapped. `DeckUsesSecondMain` detects the
  param (2c-bis). Planeswalker targets are not modelled (no per-turn damage record; no deck faces one).
* **Cast order (USER-owned -- surfaced, not decided).** `GenericProvider::CastOrderRank` gives a
  `target_dealt_damage_this_turn` card tier **29** (after creatures 10 and other noncreature spells 20, before
  the on-cast self-damage tier 30), so a same-main Bolt / Vexing Devil makes the opponent a legal target
  before Needle Drop is cast. Byte-identical for every other deck. The Burn / MidrangeBurn providers' user
  orders were NOT touched; whichever provider this deck routes to owns the final order.
* **Vexing Devil.** `ResolveVexingDevilEtb` runs from the universal own-ETB cascade (`FireOwnEtbTriggers`,
  after the entrant's other ETBs, the champion placement) -- every enter route, executor and rollout alike.
  Policy in ONE function, `VexingDevilOpponentTakesDamage`: take it iff `life > 4`. Taken: 4 noncombat
  damage via `DealPermanentAbilityDamage` (marks dealt-damage + lost-life, feeds spectacle / Needle Drop),
  then `SacrificePermanentAt` (graveyard, death/sacrifice watchers, morbid). The play viewer's history gets
  a `trigger`/`damage` event naming the opponent's choice. Not a search branch, not a viewer decision.
* **Tests.** `test/unit/test_needle_drop_vexing_devil.cpp` (both apply worlds per case).

### Modelling decisions

* **Vexing Devil opponent policy — PROVISIONAL (awaiting USER).** The goldfish opponent is passive, so
  the "any opponent may" choice needs a model. Chosen: **the opponent takes the 4 damage unless that
  would be lethal to them (life <= 4, team life in 2HG); otherwise the Devil stays as a 4/3.** Rationale:
  against a goldfish that never blocks or removes, a surviving 4/3 deals >= 4 the next turn, so taking 4
  now is the opponent's better choice except when it is immediately lethal. Alternatives surfaced to the
  user: adversarial (the search evaluates both branches and assumes the worse one for us), always-take,
  never-take (a fully passive opponent never opts in).
* **Needle Drop "dealt damage this turn"** is DAMAGE, not life loss: needs a per-turn damage tracker for
  the opponent player (and marked damage for creature targets). It is an instant, so it is castable after
  combat damage; the deck already enables the second main via Skewer the Critics' spectacle, but Needle
  Drop's own param must also be detected by `DeckUsesSecondMain` (2c-bis).

## Open questions for the USER

1. Vexing Devil opponent policy (above).

## CHECKPOINT 2026-11-17 (orchestrator; resume here)

* **Committed (local, NOT pushed):** 15a6a5c86 -- Needle Drop + Vexing Devil (impl agent; handoff
  `logs/1dropburn/impl-handoff.md`). Cost audit rc 0 (658/658). Field audit: snapshot refreshed
  (`src/cards/data/scryfall_reference.json`, UNCOMMITTED) and offline audit rc 0; the Vexing Devil oracle
  advisory is only its bracket note.
* **UNCOMMITTED, NOT YET BUILT:** `OneDropBurnProvider` (empty DeckProvider derivation, DecisionProviders.h
  beside MidrangeBurn; routed by `one_drop_burn` flag = target_dealt_damage_this_turn ||
  etb_opp_may_take_damage_to_sac, returned right after `lfe`); the Needle Drop cast-order tier 29 MOVED
  from GenericProvider::CastOrderRank into `OneDropBurnProvider::CastOrderRank`; tests updated
  (test_needle_drop_vexing_devil.cpp sets the provider; test_pirates_provider.cpp table row
  `{"1 Drop Burn","OneDropBurn"}`). Before this the deck routed to `Generic`.
* **RESOLVED -- the 39 smoke mismatches are PRE-EXISTING upstream drift, not 15a6a5c86.** An exact
  parent build (17262a61f, scratch worktree, since removed) replayed all 39 failing smoke jobs plus two
  passing canaries (midrangeburn d0/d3) from the primary checkout: all 41 per-game `.wins` files are
  byte-identical to the 15a6a5c86 smoke run (`build/scratch/1dropburn/parent_replay.json`). Same 39 keys
  P18i recorded as unaccepted upstream drift (sampled-gate-ladder.md). Left alone; not accepted here.
* **Next after the fix:** build, unit tests, smoke byte-identity -> commit provider + snapshot -> Stage 3
  coverage re-run -> Stage 4 profile (`analyze_deck.py --no-rebuild`) -> 4a provider_audit -> 4-bis suite
  rows in all 3 tiers (template: midrangeburn rows) + accept -> Stage 5 (verify_deck.py, 5a-5i, claude-play
  sweep on Opus/Sonnet) -> Stage 6 report.
* **Open USER questions:** (1) Vexing Devil opponent policy (PROVISIONAL: take 4 unless lethal);
  (2) Needle Drop cast-order tier 29 (after creatures + other spells) -- user-owned; (3) deferrals:
  Needle Drop cannot target planeswalkers; painland self-damage not marked (both inert for this deck).
