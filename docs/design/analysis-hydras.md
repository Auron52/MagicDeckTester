# Hydras deck analysis

Per-deck ledger for the analyze-deck workflow (`.claude/skills/analyze-deck.md`). It is the durable
state for this analysis across compaction and handoffs; keep it current after every milestone.

## Deck and scope

- Source: `decks/Hydras/Hydras.cod` (committed as-is 8c2a7aac3, 2026-10-08; routing pinned to Generic in
  `test/unit/test_pirates_provider.cpp`).
- Main deck: 60 cards, 15 names, mono-green, 23 Forest. No sideboard, no wish, so nothing outside the main is reachable.
- Intended line (working hypothesis): ramp on basic Forests (Search for Tomorrow, Nature's Lore, Three Visits),
  double Forest mana (Vernal Bloom, Gauntlet of Power, Nissa), cheapen Hydras (Gargos), then cast large X
  Hydras; Kalonian Hydra doubles counters on attack. Lightning Greaves gives haste.
- Earlier provisional cast-order draft (written before any card existed):
  `docs/design/cast-order-drafts-2026-10-14/Hydras.md`. Cast order is a USER decision; that draft is input.
- Analysis worktree: `.claude/worktrees/hydras`, branch `hydras-analysis`, based on
  `origin/phase-1-2-deck-analyzer` at c79006605 (2026-11-10).

## Stage 1 — coverage (2026-11-10)

- Full: Forest, Search for Tomorrow, Nature's Lore, Lightning Greaves.
- Missing (11): Kalonian Hydra, Neverwinter Hydra, Primordial Hydra, Steelbane Hydra, Voracious Hydra,
  Gargos, Vicious Watcher, Vernal Bloom, Nissa, Who Shakes the World, Gauntlet of Power, Three Visits,
  Garruk, Primal Hunter.
- Existing precedents in cards.json: Mossborn Hydra / Scythecat Cub (counter doubling), Walking Ballista
  (enters with X counters), Utopia Sprawl / Overgrowth / Wild Growth (extra mana on a land tap), Ruby
  Medallion / Goblin Warchief (cost reduction), Apex Altisaur (fight), Gideon / Oko / Chandra (loyalty).
- Routing hazard noted in the cast-order draft: params `tutor_to_hand` / `fetch_land_types` set the `anti`
  archetype signature and would route the deck to AntiLifegainProvider. Stage 4a gives Hydras its own provider.

## Stage 2 — research and implementation

Research fan-out (one Opus agent per card group, read-only, compact drafts):
A Kalonian + Primordial · B Voracious + Neverwinter + Steelbane · C Gargos · D Vernal Bloom + Gauntlet +
Three Visits + Nissa's static · E Nissa's loyalty abilities + Garruk.

| Card | Tier | Status | Viewer bucket | Notes |
|---|---|---|---|---|
| (filled in as drafts land) | | | | |

## Provisional deferrals (need USER sign-off)

(none yet)

## Stage 5 verification

(not started)

## Approved deferrals

(none)
