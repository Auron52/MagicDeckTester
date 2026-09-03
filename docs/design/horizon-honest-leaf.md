# The horizon-honest leaf: grading a no-win instead of flattening it

**Status: ADOPTED + SHIPPED (b3dab212, 2026-08-23). Default ON, ~~dragonstorm opted out~~, GT
rebaselined on all three tiers, CI green both platforms.** *(Updated 2026-09-03: the dragonstorm
opt-out was removed the same day — 8dc20bdc, user-approved; no deck opts out today, exactly as
§4b argues.)*
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

### What the tie-break actually DISPLACES, and how much exposure it has
`plan.value` is the greedy solver's score for the SET OF SPELLS CAST THIS MAIN PHASE, computed at
enumeration time -- tempo-aware (creatures at power x expected remaining attacks), routed through
`EvalCard` -> the provider's `ArchetypeCardValue`. It is the same value that orders the greedy
`Solve`. Critically it scores THE CARDS YOU CAST, never the position the rollout ends in.

So a graded leaf quantity does not add information to a vacuum: it DISPLACES `plan.value` as the key
under the win turn. Any further sub-key (a board term) in turn displaces `plan.value` under the life
term. The exposure is large, not marginal (`[leaf-eval]` telemetry, 400 games/deck):

| deck | no-win ties | life term EQUAL | share |
|---|---|---|---|
| hinata | 256,296 | 189,149 | **74%** |
| treasure_hunt | 143,319 | 140,475 | **98%** |

Two consequences worth keeping:
* A board sub-key would decide 74-98% of ties, so its measured "slightly worse" is a REAL negative
  result on a big exposure, not an underpowered one.
* Conversely life DISCRIMINATES in only ~26% (hinata) / ~2% (treasure_hunt) of ties -- and those few
  are worth the entire -45 / -13. A key that rarely speaks can still carry the whole gain.

### Does life beat `plan.value` CONSISTENTLY? (USER question, 2026-08-23) -- yes
Life-first tie-break vs today's `plan.value`-only, train (1001/2002/3003) vs held-out
(4004/5005/6006/7007), d3+d5. Negative = life is better.

| deck | train | held-out | all |
|---|---|---|---|
| **hinata** | -17 (14w/27b) | **-28** (15w/38b) | **-45** (29w/65b) |
| **treasure_hunt** | -8 (3w/7b) | -5 (8w/10b) | **-13** (11w/17b) |
| **fivecolour** | -1 (0w/1b) | -3 (0w/2b) | **-4** (0w/3b) |
| kitty | +1 (1w/0b) | +0 | +1 |
| antilife | +1 | +1 | +2 (2w/0b) |
| slivers, burn | +0 | +0 | 0 (inert) |

**TOTAL: -59 turns over 28,700 paired games, 43 worse : 85 better.** No deck changes sign between
train and held-out, and both negatives are 3 individual games in 14,400. That is the consistency
result the adoption rests on.

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
29 worse for life-only against 35 for all-permanents, at the same ~65 better). PAIRED directly
against life-only -- the comparison that actually asks the question, rather than each arm against
baseline -- adding a board sub-key costs: hinata +5 (all perms) / +4 (non-land), treasure_hunt +7
(all perms, and +7 of it on HELD-OUT alone: 10 worse : 5 better) / +1 (non-land).

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

### ALL 14 SUITE DECKS (2026-08-23) -- the coverage a global default needs
Life-first no-win tie-break vs today, train (1001/2002/3003) + held-out (4004/5005/6006/7007),
d3+d5, 48,300 paired games. Negative = better.

| | deck | train | held-out | all |
|---|---|---|---|---|
| **improve** | hinata | -17 (14w/27b) | -28 (15w/38b) | **-45** |
| | treasure_hunt | -8 | -5 | **-13** |
| | fivecolour | -1 | -3 | **-4** |
| | mirrorwing | -3 | +0 | **-3** |
| no win-turn change | slivers, burn, knights, auras, goblins, creature_giving | | | 0 |
| **worse** | stompy | +1 | +2 | +3 (4w/1b) |
| | antilife | +1 | +1 | +2 (2w/0b) |
| | kitty | +1 | +0 | +1 |
| | dragonstorm | +0 | +1 | +1 |

**GLOBAL: -58 turns / 48,300 games, 50 worse : 91 better.**
**PER-DECK (the four that improve): -65 turns, and no deck made worse.**

CAVEAT on the "no win-turn change" row: that means no CHANGED GAME WAS SAMPLED at these counts, not
byte-identical. The suite run below changes auras / goblins / dragonstorm / creature_giving digests,
i.e. their PLAY moves without (sampled) win turns moving. Only a per-deck hook guarantees a deck is
untouched.

### The suite with the lever ON (regression tier)
23 of 65 configs change; per-game audit **slower=5, faster=13, play-changed=53**; d0 untouched (there
is no search at d0, so no tie-break to move). Reference reproducibility is CLEAN: 208 refs,
0 play-drift / shuffle-dead / enum-gap / mull-drift / contract-fail. Averages move on th (-0.008),
hinata (net -0.035 over four keys), mirrorwing (-0.015) and stompy (+0.003); the rest are digest-only.
Adoption therefore needs a GT rebaseline of those 23 keys (fewer under the per-deck shape).

## 4. Recommendation

Adopt **shape A with the opponent-life quantity** (`MTG_LEAF_GRADE_NOWIN` alone); reject B, and
reject EVERY board term -- power, all-permanents and non-land-permanents all measured worse than life
alone once the sample was large enough to separate them (see the quantity section). Per-deck vs global is the open question:
the gain is concentrated on hinata (-45 / 5,600) and mildly positive on fivecolour, while kitty and
Anti-Lifegain are each ~+1 to +2 turns worse. A `DecisionProvider` hook would take hinata's gain
without paying those.

**Before adoption:** a full regression + overnight run, and a GT rebaseline across all three tiers
(this changes play). Both are outstanding.

## 4a. SHIPPED -- what landed and where (2026-08-23)

| piece | where |
|---|---|
| the hook, DEFAULT ON | `DecisionProvider::GradesNoWinLeaf()` |
| the one opt-out | `DragonstormProvider::GradesNoWinLeaf() -> false` (stored-value deck; see below) |
| global off-switch | `MTG_LEAF_GRADE_NOWIN=0` (verified byte-identical to the pre-lever baseline) |
| per-deck check | `scripts/leaf_tiebreak_check.py`, wired into `.claude/skills/analyze-deck.md` step 5c2 as MANDATORY for a new deck |
| rejected, kept as instruments | `MTG_LEAF_VALUE_RES` (inert -- the hybrid discards the value leaf), `MTG_LEAF_TB_BOARD` / `_TB_PERMS` / `_TB_NONLAND` (all worse than life alone) |

**Validation before the rebaseline** (the USER asked for root-cause first, then overnight):

* Nine regressed games escalated on BOTH arms at +1/+2 depth AND 20x budget. **Eight of nine are
  ordinary budget churn** and close at 20x. Exactly one survives: dragonstorm s5005 gi227.
* OVERNIGHT (held-out, 156 keys): 58 changed / 98 unchanged; slower=30 **faster=58**; 16 keys
  improved on average, 8 worsened, 34 digest-only; sum of per-key avg deltas **-0.0772**.
* SMOKE: 6 changed, slower=2 faster=5. REGRESSION: 21 changed, slower=5 faster=13.
* Reference reproducibility CLEAN throughout (208 refs, 0 play-drift / shuffle-dead / enum-gap /
  mull-drift / contract-fail). **d0 is untouched on every tier** -- no search at d0, no tie-break.
* Only antilife is net-worse anywhere (+0.005 over 7 overnight keys = 1-2 turns per 1000 games).
  **That "+" was NOISE and the reasoning used to dismiss it was wrong -- see 4b.**

**The one opt-out, and how to recognise the next one.** Opponent life at the horizon is a
DAMAGE-RACE proxy: a deck whose value is STORED rather than expressed as damage by the horizon has
its build-up priced at ZERO. Dragonstorm at T6 casts a 13-spell chain (three Apex of Power, four
rituals, Ruby Medallion, Scourge of Valkas) and wins turn 8; the life tie-break stops it after six
spells and LOSES, and it survives 20x budget. Combo / storm / ramp is the class.

## 4b. The escalation test was wrong, and antilife re-measured (2026-08-23, USER)

The USER's objection: *"this is one of the cases where we want a lower average. Checking for budget
churn isn't useless, but it provides little information when we are changing leaf tiebreak
behaviour."* Correct on both counts, and it invalidated the reasoning in 4a.

**Why escalation cannot vindicate a leaf lever.** The tie-break fires only when a rollout reaches the
horizon WITHOUT a win. Raise depth/budget and the search finds real wins inside the horizon, so the
leaf stops being consulted -- escalation deletes the regime under test. "It closed at 20x" reports
that the lever stopped FIRING, not that its valuation was sound. The test earns its keep for a lever
that changes which line the search COMMITS to at production budget; it is near-tautological here.
The replacement rule is the one the USER states: THE METRIC IS THE BAR.

**Antilife, re-measured at production settings (d3/10 + d5/20), 86 seed blocks x 1,000 games,
172,000 paired games:**

| split | paired | net turns | worse | better |
|---|---|---|---|---|
| half A | 86,000 | **-31** | 43 | 67 |
| half B | 86,000 | **-35** | 27 | 55 |
| d3 only | 86,000 | -55 | 49 | 86 |
| d5 only | 86,000 | -11 | 21 | 36 |
| **ALL** | **172,000** | **-66** | **70** | **122** |

192 changed games of 172,000 (0.112% binding). Better-beats-worse at 122:70 is ~3.8 sigma against a
50/50 null, the sign agrees on both halves and both depths, and d3 -- the more horizon-limited cell,
where the leaf binds more -- carries most of it. **The tie-break HELPS antilife. It keeps the default
and needs no opt-out.**

So the recorded +2 (2 worse / 0 better) was a two-event sample, and the suite-wide sweep's per-deck
rows are all of that character. The conclusion in 4a was right by luck; its justification was not.

### Dragonstorm's opt-out does not survive either gate (2026-08-23)

The opt-out rested entirely on s5005 gi227, recorded as "T8 win -> LOSS, survives 20x budget".
Re-tested against both gates, that game is not evidence of anything:

| where | pre-change | post-change |
|---|---|---|
| **d5/20 = the deck's PLAY setting** | win T7 | **win T7 -- no regression at all** |
| d3/10 (a gate cell), budget 10 | win T8 | LOSS |
| d3, budget 100 (10x) | win T6 | LOSS |
| d3, budget 1,000 (100x) | win T6 | **win T6** |
| d3, budget 100,000 (10,000x) | win T6 | win T6 |

Gate 2 (unlimited budget at the pre-regression winning depth): **RECOVERABLE** -- both arms converge
on T6. "Survives 20x" was simply not unlimited; it needed 100x. Gate 1: the game does not even
regress at play settings, so it never spoke to the shipping configuration.

Two traps worth keeping: **escalate to CONVERGENCE, not to a round number**, and **check whether a
regressed game regresses at PLAY settings** before building an argument on it.

**Gate 1 for dragonstorm, at PLAY settings (d5/20), 100,000 paired games over two independent seed
blocks:**

| sample | paired | net turns | worse | better |
|---|---|---|---|---|
| seeds 1.0M-5.9M | 50,000 | -4 | 6 | 9 |
| seeds 20.0M-24.9M | 50,000 | -6 | 4 | 10 |
| **combined** | **100,000** | **-10** | **10** | **19** |

29 changed games = **0.029% binding**, a tenth of the d3 rate. Both halves agree in direction; 19:10
is ~1.7 sigma, so the honest reading is *slightly positive, indistinguishable from neutral*. Average
delta -0.0001 turns/game.

**So at the configuration dragonstorm actually ships, the tie-break is very nearly a non-event, and
what little it does is not harmful.** Both gates clear: nothing unrecoverable (see the table above),
and the average does not degrade. The opt-out is therefore not justified by the evidence that
motivated it -- but removing it buys nothing measurable either, while costing a GT rebaseline of
dragonstorm's keys. That trade is a USER call; what must NOT stand is the current code comment,
which asserts a truncated-combo regression that does not occur at play settings.

**Still unmeasured:** stompy (+3 on 5 changed) and kitty (+1 on 1 changed) at play settings. Both
were flagged only by the retracted argument on tiny samples and neither has a pending decision.
NOTE for whoever picks this up: kitty is expensive at its shipping d5/20 -- 23 games over 30 s per
4,000, worst 75.3 s -- and it has NO rows in `test/regression_cases.sh`, so that cost has never
appeared in any suite tier. That is a performance question in its own right.

## 5. Method notes worth keeping

1. **Instrument the binding rate before believing an inert result** (see the frame-rule bug above).
2. **Pick the test bed by whether the mechanism can fire.** AL was the obvious deck (it owns the Aria
   case) and it was the wrong one -- it wins too fast for the horizon to bind. The deck that owns the
   *other* known instance, hinata, is where the signal was.
3. **CORRECTED (2026-08-23, USER): escalation answers a DIFFERENT question than I was asking it.**
   I had written "escalate before opting a deck out" here as a general rule after 8 of 9 suite
   regressions closed at 20x budget, and used it as grounds for KEEPING the default on a deck whose
   average got worse. That conflates the USER's two adoption gates, which are separate and both
   blocking:
   * **Gate 1 -- "are we improving the play generally?"** Aggregate average at PLAY settings, large
     sample. Escalation is worthless here: the tie-break fires only when a rollout reaches the
     horizon WITHOUT a win, so raising depth/budget lets the search find real wins inside the horizon
     and the leaf stops being consulted. "It closed at 20x" reports that the lever stopped FIRING.
   * **Gate 2 -- "are we preventing search from finding a win?"** Game by game, at UNLIMITED budget
     and the depth at which the game won BEFORE the change. Win returns => recoverable, gate 1
     decides. Win never returns => the change deleted the line structurally, and that blocks on its
     own.

   Escalation is gate 2's instrument, not gate 1's. Run BOTH; block on either.
4. **Size by CHANGED GAMES, not by games.** Binding rates differ by orders of magnitude across decks;
   antilife changed ~2 games in 3,450, so the suite-wide sweep gave it a sample of *two*. A run that
   reports "0 changed" has not cleared the deck -- it has failed to test it. This is lesson 1 again,
   one level up, and `scripts/leaf_tiebreak_check.py` now refuses to call a sign below 20 changed
   games and prints the scale a decisive run needs.
5. **A prune's value is not all reachable from the search.** Anything acting at EMISSION also
   constrains d0/greedy, which no tie-break inside `SolveWithLookahead` can ever reach -- so a
   search-side fix cannot subsume it by construction.
