# The lookahead-bottoming rollouts leave per-game memo entries the real game reuses

**Status: OPEN, deferred (found 2026-10-24 at the NivMizzetCombo keep-table adoption). Not fixed: the fix
moves play on every deck that bottoms without a keep table, so it needs its own A/B.**

## What happens

`AIEngine::HandleMulligan` clears the per-game caches once, at game start
(`TurnSolver::ClearPerGameCaches()`: the solve memos, the enumeration memo, the breakpoint plan cache, the
canon-front memo, the combo-route memos, the hybrid's learned R/G, the payable-mana cache). Then it runs the
mulligan loop and, after a mulligan, `BottomCards`. Without a keep table, `BottomCards` takes the lookahead
path: it plays a full game at play settings for every candidate bottom (`RolloutWinTurn`). Those rollouts
search the same deck, often from exactly the state the real game will reach, and they fill the per-game
memos as they go. **Nothing clears the memos after the bottoming pass**, so the real game's first searches
start with the rollouts' entries in place. A memo hit changes what a finite budget pays for, so it can
change the committed line.

With a keep table, the bottom comes from a lookup, no rollouts run, and the real game starts with empty
memos. So a keep-table adoption changes some games that keep the same hand AND bottom the same cards: the
game was never a function of its hand alone, the bottoming pass's leftovers were part of it.

## The evidence

`test/keep_adoption_attribution.py overnight niv` (and `niv2hg`) reported one UNEXPLAINED game each:
`niv_overnight_d5_s4004` game 70 (repro `--seed 4074 --game-index 70 --games 1`, d5 b20, profile attached).
Both sides mulligan once and bottom the same single card (Izzet Signet), so hand and library are
identical; play diverges on turn 4 (table off: Preordain in main 1, T8 win; table on: Preordain held to
main 2, T7 win). A scratch build with
`if (mulligan_count > 0 && EnvOn("MTG_DIAG_CLEAR_AFTER_BOTTOM")) { TurnSolver::ClearPerGameCaches(); }`
right after the `BottomCards` call in `HandleMulligan`:

| arm (same binary, Release) | result | digest |
|---|---|---|
| table off | T8 | 9af6eff31cfb67b9 (= the tier's table-off run) |
| table on | T7 | f29ede881b8a7578 (= the tier's table-on run) |
| table off + clear after bottoming | **T7** | **f29ede881b8a7578** |
| table on + clear after bottoming | T7 | f29ede881b8a7578 |

Clearing the leftovers makes the table-off game byte-identical to the table-on game, so the change is
the carry, not the table reaching into play.

## Why it matters beyond the attribution

* **A game should be a pure function of its own state at each decision.** The batch-determinism work
  (`docs/design/batch-run-to-run-nondeterminism.md`) made every game a pure function of its spec, so this
  carry is deterministic and invisible to every determinism check. It still means a deck's searched play
  depends on how much rollout work its mulligan did.
* **The rollouts are clairvoyant.** They play over the real library. In goldfish play that is the same
  library the real game sees, so no hidden information crosses. In a NON-clairvoyant game
  (`shuffle_salt_search`, `MTG_NC_SEARCH`) the bottoming rollouts decorrelate themselves, but whether
  every memo they fill is keyed tightly enough to keep their knowledge out of the real game's NC search
  has NOT been checked.
* **It contaminates every keep-table adoption's attribution**, at a rate of about 1 game in 11,200 for
  Niv (the only cases the classifier can see are the ones where the bottom matched; where the bottom
  differs, the carry hides inside an "explained" change).
* The keep generator (`RolloutKeepWinTurn`) has the same shape: ClearPerGameCaches, then `BottomCards`
  (lookahead, with rollouts, for mulligan_count > 0), then the playout. Its labels therefore include
  the carry too.

## The fix (proposed, not applied)

Call `TurnSolver::ClearPerGameCaches()` after the bottoming pass whenever the lookahead path ran
rollouts: in `HandleMulligan` after `BottomCards`, and in `RolloutKeepWinTurn` after its `BottomCards`.
It is a play change for every deck that bottoms without a keep table (and for keep generation), so it
needs a held-out A/B on those decks and a smoke/regression GT accept. Decks WITH a keep table are
unaffected unless a hand is unbucketed (the table falls back to the lookahead path).

Expected cost: the real game loses whatever its first searches gained from the rollouts' memos. Measure
it; but per CLAUDE.md the decision rests on soundness, not wall.
