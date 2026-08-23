# The horizon-honest leaf: grading a no-win instead of flattening it

**Status: BUILT, MEASURED, default OFF. Adoption is a USER call (it changes play -> GT rebaseline).**
Origin: USER, 2026-08-22 -- *"I wouldn't change searched m2. However, we could change what we
evaluate in it."*

This doc is self-contained.

## 1. The defect

Beyond the search horizon, **every line scores the same number**, so an action whose cost lands past
the horizon is FREE. Two writers produce that flatness:

* the heuristic rollout (`SimulateToEndImpl`) returns a flat `max_turns + 1` for any no-win; and
* the value leaf (`FSLineWin`) computes a graded **milliturn** estimate and then rounds it to a whole
  turn *and* clamps anything past the horizon to `max_turns + 1` -- a model that does discriminate
  has its discrimination thrown away.

The pass loop in `SolveWithLookahead` then breaks the resulting tie on **`plan.value`** -- a
PLAN-level heuristic, not a state evaluation -- so among equally-hopeless lines the busiest-looking
plan wins.

**This one root cause has at least three narrow prunes written against it:**

| prune | the misplay it deletes |
|---|---|
| `SubsetHasUnbackedAltPayload` | alt-cost life gifts with no `lifegain_to_loss` enabler live |
| `SubsetHasUnbackedEtbGift` | Aria of Flame's ETB gift -- AL opponent 16 -> 26 -> 34 at a tie (2026-08-22) |
| Karoo self-bounce enumeration ban | Hinata gi226 replayed a Karoo T1-T4 rather than develop |

The Karoo comment states the cause outright: *"every line scores the same when the rollouts fail to
find the win, and nothing in the tie-break prefers a developed board."* Three prunes for one blind
spot is the signal that the EVALUATION is wrong, not the enumeration.

This is the **"HONEST where you SCORE"** half of the standing LAW. It does **not** touch the metric
(avg turn-to-win, unwon = max_turns+1) -- only the search's internal scoring.

## 2. What was built (all DEFAULT OFF -> byte-identical; smoke 39/39, 0 configs changed)

| lever | shape |
|---|---|
| `MTG_LEAF_GRADE_NOWIN` | **A** -- a no-win leaf publishes a graded board quantity, used as the tie-break where (and only where) every candidate ties at `max_turns+1` |
| `MTG_LEAF_VALUE_RES` | **B** -- the value leaf publishes its raw milliturn score when it would be clamped, so the model's own beyond-horizon ordering survives |
| `MTG_LEAF_TB_BOARD` | quantity: grade on board development too (power, land count, strictly *under* the life term), not opponent life alone |

All three are per-job `heurarm` flags, so every arm of the matrix pools into ONE batch. The primary
key stays the win turn: **a win inside the horizon always beats a no-win**, and equal REAL wins keep
`plan.value` (only no-win ties are graded).

### THE FRAME RULE -- and the bug that hid the whole mechanism
Every rollout frame publishes at EVERY one of its return points: a quantity at the natural horizon
exit, `kInvalid` everywhere else (a win needs no tie-break; an aborted line -- cutoff or budget
overrun -- never reached the horizon and has nothing honest to say). Nested frames always complete
before their parent returns, so the last write standing is the frame the consumer itself called.

The first implementation published only at the OUTERMOST frame, to avoid nested clobbering. That
silenced every INTERIOR node -- i.e. exactly the "several turns from lethal, nothing wins in horizon"
states this exists for. It measured inert, and the telemetry is what caught it:

| | published | ties | flips |
|---|---|---|---|
| outermost-only (wrong) | 21 | 15 | **0** |
| per-frame (correct) | 11,952 | 6,467 | **230** |

...over the same 400 AL games. **A lever that measures inert is worthless evidence until you know it
FIRED** -- "no effect" and "never reached the state" look identical in an aggregate. `[leaf-eval]
published/ties/flips` prints whenever a lever is on, for exactly this reason.

## 3. Measured

### Shape B is INERT on every deck -- and the reason is structural
0 changed games on all six decks tested. The hybrid value-leaf policy (`AIEngine.cpp`) re-runs any
line committed below the trust depth with the **heuristic** leaf, and the default `escalate_below` is
`m_lookahead_depth + 1`, i.e. always. So the value leaf's number is discarded downstream regardless
of how much resolution it kept. **Preserving the model's resolution cannot matter while the committed
leaf is the heuristic one.** Reject B as written; it would need the hybrid policy revisited first.

### Cross-deck sweep, shape A (1,000 games per deck per arm, d3+d5, seeds 1001/4004)
Negative = better.

| deck | A (opponent life) | A (+ creature power) |
|---|---|---|
| **hinata** | **-6** (6w/11b) | -4 (8w/11b) |
| fivecolour | -2 (0w/2b) | -2 (0w/2b) |
| treasure_hunt | +0 (6w/4b) | -1 (4w/3b) |
| kitty | +1 (1w/0b) | +1 (1w/0b) |
| slivers, burn | +0 (inert) | +0 (inert) |
| **total** | **-7** | -6 |

**Opponent life beats the power blend** (26 worse vs 19 worse on hinata for a smaller gain). The
permanent-count readings are settled in the quantity section below: also worse. Use life alone.
Note treasure_hunt reads +0 here at 1,000 games and **-13** at 5,600 -- this sweep was under-powered.

### The tie-break QUANTITY, settled (USER question, 2026-08-23)
The USER asked whether "opponent life, then PERMANENTS on board" would beat what was tried. It is a
different rule -- `MTG_LEAF_TB_BOARD` weights creature POWER and scores a land drop at 1/1000th of a
point of it, so it ignores equipment/enchantments entirely and barely notices the land BOUNCE the
Karoo case is about. Two readings were added and measured: `MTG_LEAF_TB_PERMS` (all own permanents)
and `MTG_LEAF_TB_NONLAND` (non-land only, for decks where a land is worth more in hand).

First sweep, 1,000 games/deck/arm, ranked non-land best (-10 total vs life-only's -7). **That ranking
did not survive more data.** At 5,600 games per deck per arm (train 1001/2002/3003 + held-out
4004/5005/6006/7007, d3+d5):

| quantity | hinata | hinata w:b | treasure_hunt | th w:b | th HELD-OUT |
|---|---|---|---|---|---|
| **opponent life only** | **-45** | 29 : 65 | **-13** | 11 : 17 | -5 (8w/10b) |
| + non-land permanents | -41 | 32 : 65 | -12 | 15 : 19 | -5 (10w/11b) |
| + all permanents | -40 | 35 : 67 | -6 | 15 : 17 | **+2 (12w/9b)** |

**Life alone wins on both decks.** A board term adds WORSE games without adding better ones (hinata:
29 worse for life-only against 35 for all-permanents, at the same ~65 better).

**The USER's treasure_hunt caveat is confirmed and is the sharpest signal here:** all-permanents is
the only arm that goes NEGATIVE on held-out for treasure_hunt (+2 turns, 12 worse : 9 better).
Excluding lands recovers nearly all of it. treasure_hunt pitches lands to Land's Edge, so a land in
HAND is ammunition and rewarding lands on the battlefield pushes it out.

**Why life alone is enough, and this is the useful part:** the quantity is measured AT THE HORIZON,
*after* the rollout has played every turn -- not at the decision point. Development has therefore
already been converted into damage by the time it is read. A developed board simply IS lower opponent
life at turn 8, and the Karoo durdle registers as "opponent life did not move". An explicit board
term double-counts what the life term already integrates, and contributes mostly churn.

METHOD: the 1,000-game margins between quantities were noise and were presented as a ranking; the
5,600-game run reversed them. Rank tie-break variants only at a sample where the arms separate.

### Hinata is where the horizon actually binds -- and there it is a real gain
Anti-Lifegain wins on turn ~4.2 of a max_turns=8 horizon, so "nothing wins in horizon" is rare there;
hinata is slow enough that it is common. Shape A, opponent life:

| split | games | net turns | worse | better |
|---|---|---|---|---|
| train 1001/2002/3003 | 2,400 | **-17** | 14 | 27 |
| held-out 4004 | 800 | -1 | 5 | 7 |
| held-out 5005/6006/7007 | 2,400 | **-27** | 10 | 31 |
| **total** | **5,600** | **-45** | **29** | **65** |

Held-out validates *stronger* than train, which is the direction you want.

### It does NOT subsume the prunes -- it is additive
* **Karoo (hinata).** The prune is worth +11 turns / 3,200 games on its own. With shape A live it is
  still worth +7. So the graded leaf absorbs ~4 of its 11 turns and the prune keeps the rest.
* **ETB gift (Anti-Lifegain).** The prune is worth +14 turns / 6,000 games. With shape A live it is
  worth ~+13 -- essentially no absorption. Much of that prune's value is at **d0**, where there is no
  search and therefore no tie-break to fix; the prune acts at EMISSION and reaches the greedy runner.

So "one principled fix replaces three prunes" is **refuted as stated**. The correct statement is
weaker and still worth having: the prunes stay, and the graded leaf is an independent gain on the
decks where the horizon binds.

## 4. Recommendation

Adopt **shape A with the opponent-life quantity** (`MTG_LEAF_GRADE_NOWIN` alone); reject B, and
reject EVERY board term -- power, all-permanents and non-land-permanents all measured worse than life
alone once the sample was large enough to separate them (see the quantity section). Per-deck vs global is the open question:
the gain is concentrated on hinata (-45 / 5,600) and mildly positive on fivecolour, while kitty and
Anti-Lifegain are each ~+1 to +2 turns worse. A `DecisionProvider` hook would take hinata's gain
without paying those.

**Before adoption:** a full regression + overnight run, and a GT rebaseline across all three tiers
(this changes play). Both are outstanding.

## 5. Method notes worth keeping

1. **Instrument the binding rate before believing an inert result** (see the frame-rule bug above).
2. **Pick the test bed by whether the mechanism can fire.** AL was the obvious deck (it owns the Aria
   case) and it was the wrong one -- it wins too fast for the horizon to bind. The deck that owns the
   *other* known instance, hinata, is where the signal was.
3. **A prune's value is not all reachable from the search.** Anything acting at EMISSION also
   constrains d0/greedy, which no tie-break inside `SolveWithLookahead` can ever reach -- so a
   search-side fix cannot subsume it by construction.
