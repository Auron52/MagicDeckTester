# Does the search favour the heuristic leaf over the value leaf? (2026-10-20)

**Question (USER, after Landfall's value-leaf run):** *"There is no reason why default, which I assume is
heuristic rollouts at every depth should win against all alternatives in cost"*, and *"it seems we have been
giving advantages just to the heuristic."* Landfall's phase E verdicts (8,000 held-out games each, against
the shipped play: no sidecar, the greedy horizon leaf, `search_leaf_depth 0`):

| Shape | Cost | Win turn |
|---|---|---|
| staged learned leaf | 1.710x | -0.029 (faster) |
| no-leaf escalation (`nlesc`) | 1.441x | +0.055 |
| no-leaf single pass (`nlfit`) | 1.238x | +0.093 |

This page records what was checked, what was fixed, and what is left. Companion to
`leaf-pick-and-dead-end-memo-2026-10-17.md`.

## Fixed

1. **Dead ends were not memoised once a pass had spent its budget** (`MTG_PROVISIONAL_NOWIN`, default ON,
   commit "search: keep dead ends found after the budget is spent"). A no-win is stored only when nothing
   beneath it was cut short; past the budget every node skips its wave phases and books that as a cut, so
   every transposition of a dead end was searched again at full price. Now kept under a per-pass provisional
   key. Play-identical on smoke and regression; 0 games moved in every Landfall arm below.
2. **The lazy-leaf probe ran where its miss was already known** (unlimited budget only; commit "search: no
   lazy-leaf probe where its miss is already known"): under a no-leaf ladder the probe IS the ladder, and
   the hybrid's heuristic escalation re-proved the value pass's leafless miss. Digest-identical at d5 b0 on
   10 decks; Goblins 0.913x, Minotaur 0.911x, Angels 0.872x units.

## Measured, NOT a heuristic advantage

**Same seeds (820000/821000 x 200), memo fix on vs off, Landfall:**

| Shape | memo fix on/off units | vs live, fix off | vs live, fix on |
|---|---|---|---|
| live (greedy leaf) | 0.933x, 0 moved | -- | -- |
| staged leaf | 0.995x, 0 moved | 1.61x, -0.018 (se 0.018) | 1.72x |
| nlesc | 0.993x, 0 moved | 1.32x, +0.070 | 1.41x |
| nlfit | 1.000x, 0 moved | 1.10x, +0.095 | 1.17x |

The memo defect cost the HEURISTIC most on Landfall; fixing it widens the gap slightly.

**Strict child cut at an O(1) leaf** (`MTG_FS_STRICT_CUT_O1`, default off since 2026-10-17 on a measurement
that showed it RAISING value-leaf cost). With the memo fix on, held-out over every deck shipping a value
sidecar plus Landfall's staged leaf (31 decks x 400 paired games): **3 of 12,800 games moved, pooled units
1.003x** (0.92-1.28x per deck). The "raises cost" was the memo defect; the cut simply has nothing to save at an
O(1) leaf (its saving is the tie turn a rollout plays out). Stays off; the lever comment records this.

**Why Landfall's leaf costs 1.7x: it is never trusted, by its own table.** `value_leaf_table` (400 x 4
seeds): V5 5.7153 vs H5 5.6915 (paired gap 0.0238, upper95 0.0304 against tol 0.002). The leaf only matches
the greedy leaf at V6-V8 (5.6896 / 5.6884 / 5.6878), and trust is granted only at depths <= the play depth
(5): *"a trust_depth > search_depth is equivalent to UNSET in play."* So every unverified decision pays the
value pass AND a full heuristic escalation. For this deck a single greedy playout is a better and nearly as
cheap evaluator than the learned model; the remedy is a better model (or a different play depth), not search
plumbing. On 2026-10-17, simply raising Landfall's play budget bought more for less (40 ms -0.030 at 1.34x,
60 ms -0.053 at 1.53x; before the keep table, to be re-checked).

## Measured, a TRADE (USER's call; nothing changed)

**The leafless exhaustion stop** (`ConstantExhaustMult`, default 1 = a no-leaf pass stops at its budget; the
model pass runs on to the 25x overrun ceiling). Its stated reason was the memo defect, so with the fix on it
was re-measured: every deck shipping `leaf: "none"` (12), seeds 830000-832000 x 200, run-on 1 / 4 / 25:

| Deck | 4x / 25x vs 1x |
|---|---|
| Fluctuator | -0.0050 (3 games), 1.007x |
| Snow | -0.0050 (3 games), 1.34x / 1.46x |
| Mirrorwing Dragon | -0.0034 (2 games), 1.08x |
| Fungus | -0.0017 (1 game), 1.14x |
| treasure_hunt | +0.0017 (1 game), 1.05x |
| the other 7 | identical play, 1.00-1.06x |
| **pooled** | **1.205x / 1.269x** |

Small, mostly-positive quality for ~20% more work: a budget trade, not a free fix. Per-deck
`value_play.exhaust_mult` already exists if the USER wants it on a deck. (An audit read the code as pushing the
six FIT decks to heuristic depth 1 when the probe exhausts the budget; play is identical on all six at 4x/25x,
so that does not bind in practice.)

## Parked / unmeasured

- **The take rule for `leaf: "none"` probes.** `line_constant` is wired to the `nl_esc` route only, so a
  constant-model probe line goes through the model-calibrated crossover table. Extending the guard
  (patches kept in `logs/tgt/leafless_take_rule*.patch.txt` of the work tree, not committed) measured
  quality-neutral on 12 decks and **1.27x / 1.29x units on Dragonstorm / Goblins** (Goblins play identical;
  Dragonstorm one seed's digest moved at the same average): the crossover skip it disables was doing useful,
  safe work there. Needs a better design than extending the
  guard; not adopted.
- **O(1) leaf estimates are stored order-exact** (the rollout's past-cutoff results become bounded no-wins
  under the order-free key, so zone-order twins share them; a model estimate becomes an order-exact WIN).
  The model's features are counts, so the estimate itself is order-invariant. Unmeasured; size it with the
  `MTG_OF_WAVE_PROBE` counters on a model-leaf deck before building anything.
- **The strict start-gate alpha default for no-leaf ladders** (1.10 vs the model's 8.8) is justified in code by
  the memo defect. Every shipped no-leaf sidecar already sets `alpha: "relaxed"`, so flipping the default
  moves no shipped play; left until a new deck needs it.

## Open USER decisions

1. Landfall: adopt no leaf (recommended), and re-check the 40 / 60 ms play budget with the keep table live.
2. Leafless run-on (`exhaust_mult` 4) per deck: a ~20% cost trade for the small gains above.
