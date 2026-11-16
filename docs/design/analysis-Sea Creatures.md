# Analysis ledger — Sea Creatures

Per-deck ledger for `decks/Sea Creatures/Sea Creatures.cod`, per the `analyze-deck` skill. Updated
continuously. Resume point: `docs/design/sea-creatures-analysis-checkpoint.md`.

**Run started:** 2026-11-16 (Claude orchestrator session). Worktree `.claude/worktrees/sea-creatures`,
branch `sea-creatures-analysis`, base commit `f92bb3b7c` (shared branch `phase-1-2-deck-analyzer`).
Subagents never push the shared branch. Scryfall JSON for the new cards:
`/workspaces/MagicDeckTester/build/scratch/sea/scry/`.

## Decklist (60 main, no sideboard; no wish)

| n | card | role | status at Stage 1 |
|---|---|---|---|
| 4 | Utopia Sprawl | Enchant Forest land aura (+1 chosen colour) | implemented |
| 4 | Wild Growth | land aura (+G) | implemented |
| 4 | Overgrowth | land aura (+GG) | implemented |
| 1 | Sol Ring | rock | implemented |
| 3 | Worn Powerstone | rock, enters tapped, {C}{C} | **MISSING** |
| 2 | Whelming Wave | sorcery: bounce all non-sea creatures | **MISSING** |
| 4 | Summon: Leviathan | 6-mana Saga creature 6/6 ward 2: I bounce non-sea creatures; II-III attack-draw | **MISSING** |
| 3 | Serpent of Yawning Depths | 6-mana 6/6, sea-creature evasion | **MISSING** |
| 4 | Spawning Kraken | 6-mana 6/6, sea creature combat damage -> 9/9 Kraken token | **MISSING** |
| 2 | Koma, World-Eater | 7-mana 8/12 trample ward 4, combat damage -> four 3/3 Serpents | **MISSING** |
| 2 | Hullbreaker Horror | 7-mana flash 7/8, cast trigger: bounce up to one | **MISSING** |
| 2 | Kiora Bests the Sea God | 7-mana Saga: 8/8 hexproof Kraken; tap opp; steal | **MISSING** |
| 1 | Sin, Unending Cataclysm | 7-mana 5/5 flying trample, as-enters counter removal | **MISSING** |
| 1 | Trench Gorger | 8-mana trample, ETB exile any number of library lands = base P/T | **MISSING** |
| 1 | Lorthos, the Tidemaker | 8-mana 8/8, attack: may pay {8} tap up to eight | **MISSING** |
| 2 | Brinelin, the Moon Kraken | 8-mana 6/8, ETB + MV>=6 cast: bounce up to one | **MISSING** |
| 4 | Breeding Pool | shock | implemented |
| 4 | Misty Rainforest | fetchland (OLD in-hand model) | implemented — **GAP** |
| 2 | Windswept Heath | battlefield fetchland | implemented |
| 1 | Flooded Strand | battlefield fetchland | implemented |
| 1 | Flooded Grove | filter land | **MISSING** |
| 1 | Hedge Maze | Forest Island, tapped, surveil 1 | **MISSING** |
| 5 | Forest, 2 Island | basics | implemented |

## Stage 1 — Coverage (DONE 2026-11-16)

* missing (14): Trench Gorger, Summon: Leviathan, Sin, Worn Powerstone, Lorthos, Serpent of Yawning
  Depths, Koma, Spawning Kraken, Hullbreaker Horror, Brinelin, Kiora Bests the Sea God, Whelming Wave,
  Flooded Grove, Hedge Maze.
* `fetch_hold_review`: Misty Rainforest `hold_modelled: false` (old immediate-on-play model);
  Windswept Heath / Flooded Strand already the battlefield model. **Agent default (SURFACED, PROVISIONAL):**
  migrate Misty Rainforest to the battlefield fetchland model, as Boars did for Windswept Heath.
  **USER APPROVED 2026-11-16, with Breeding Pool fetched first (see Open USER questions #1).**
* Every creature in the list is a Kraken / Leviathan / Octopus / Serpent, so the "except sea creatures"
  bounces (Whelming Wave, Summon: Leviathan I) only ever hit the opponent's spawned creatures, which
  never block or attack: faithful but payoff ~0 in goldfish.

## Stage 2 — research drafts

| draft | cards | file | status |
|---|---|---|---|

## Integration (serial)

| step | change set | commit | status |
|---|---|---|---|

## Open USER questions (surfaced, not blocking)

1. ~~Misty Rainforest: migrate to the battlefield fetchland model~~ — **ANSWERED (USER 2026-11-16):**
   *"Yes, misty rainforest should automatically get Breeding Pool, kind of like how Boars gets Stomping
   Ground."* So: migrate Misty to the battlefield model, and the Sea Creatures provider ranks Breeding
   Pool first on the fetch target list while it remains in the library (the Boars shock-first shape,
   commit 910e1959f, `MTG_BOARS_SHOCK_FIRST`), default ON with an =0 off switch.
   **Refined (USER 2026-11-16):** *"These should be heuristically pruned. Though we might want to
   prioritize Island over forest once the breeding pools are gone because of the composition of the
   deck."* So the rule is a PROVIDER ONE-OPTION PRUNE (not a reorder): Breeding Pool while one remains,
   else Island over Forest. Applies to Misty / Windswept Heath / Flooded Strand. Human play still offers
   every target; =0 restores the full searched set. Island-vs-Forest stays switchable for a Stage 5 A/B
   (the USER said "might").
2. Hedge Maze (Forest Island, enters tapped, surveil 1) as a fetch target: where in the pruned order?
   Agent default (PROVISIONAL): after Island and Forest.
3. ~~Utopia Sprawl colour~~ — **USER 2026-11-16:** *"Utopia sprawl should prioritize blue as well. Green
   is a minor colour in the deck."* Implemented as: the provider's ETB colour axis restricted to {U, G}
   (W/B/R have no consumer: sound fold), plus a default-ON one-option prune to BLUE (=0 restores {U, G}
   as searched branches). Viewer keeps the full choose_color list. Taken as a PRUNE by analogy with the
   fetch ruling; if the USER meant "search U first but keep G", say so.

## Approved deferrals

(none yet)
