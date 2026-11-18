# Maze's End P5b -- three search identity folds (results)

Branch `wip/maze-p5b` (from 7093daceb). Scratch: `build/scratch/maze/p5b/`.

## Fold 1 -- engine duplicate-hand-copy fold: ALREADY EXISTS; the residual is not an identity (nothing built)

USER: "I thought we already did the duplicate merge, but if not then yes, let's do it."

**It exists.** The hand-copy merge is the canonical-prefix fold: `FinalizeFoldTags` / `HandCastEquivTag`
(`MTG_FOLD_HAND_CASTS`, default ON since 2026-09) plus the activation half (`MTG_FOLD_ACT_SOURCES`), hoisted into
the greedy odometer (`BuildFoldPrefixMap`, non-canonical positions skipped before `consider`, so never charged)
and, per deck opt-in, into the search walk (`MTG_FOLD_SEARCH_ODO`). In Maze's End every hand cast is tagged:
`MTG_BF_CENSUS` on seed 91021 gi20 d5 b10 (P1d binary) reads `hand_seen=315757 hand_untagged=0
kept_classes=15009`.

**The 1.79x is the census's priced UPPER bound, and it is mostly not hand copies.** Attribution of the priced
factor by class (temporary diag, current engine 7093daceb, same game; sum of ln(factor) over calls):

| class (source, k copies) | tagged? | calls | sum ln |
|---|---|---|---|
| Maze's End battlefield activation, k=2 | no | 4,394 | 1,264.1 |
| Maze's End battlefield activation, k=3 | no | 133 | 92.2 |
| Maze's End battlefield activation, k=4 | no | 1 | 1.2 |
| Burgeoning hand cast, k=2 | yes (folded) | 145 | 41.7 |
| Sylvan Scrying hand cast, k=2 | yes (folded) | 23 | 6.6 |

~96% is Maze's End copies on the battlefield. The census signature omits `tutor_target`, `rad_mode` and execution
order, so each Maze's End's fetch menu (one variant per Gate target) prices as identical bits. These copies are
NOT interchangeable as an identity: the plan's activations execute in battlefield order, so "copy A fetches X,
copy B fetches Y" and its swap differ in FETCH ORDER, and order matters here -- Gond Gate fetched first makes the
second Gate enter untapped; Guild Summit's queued draw after the first shuffle can take the other target out of
the library. No exact fold exists, so none was built (completeness rule). (On the current engine this game's
whole priced factor is 1.185x, not 1.79x: the game itself changed since P1d.)

**Turning the existing folds off/on changes nothing on this deck**, i.e. no work is left in the hand-copy axis:

| run (91021 gi20) | units | turn |
|---|---|---|
| P1d binary d5 b10, default | 2,060,479 | 8 |
| `MTG_FOLD_HAND_CASTS=0` | 2,060,479 | 8 |
| `MTG_DUP_ACTION_FOLD=0` (+ `FOLD_HAND_CASTS=0`) | 2,060,479 | 8 |
| `MTG_FOLD_SEARCH_ODO=1` | 2,060,479 | 8 |
| current d3 b10, `FOLD_SEARCH_ODO=0` / `=1` | 688,111 / 688,111 | 5 / 5 |
| current d3 b10 seed 91054 gi53, `=0` / `=1` | 26,293 / 26,293 | 6 / 6 |

The census comment now says the price is an upper bound and why (TurnSolver.cpp, the INTERCHANGEABLE-COPY block).

## Fold 2 -- Exploration back-to-back land-order fold: BUILT (`MTG_FOLD_LAND_ORDER`), shipped DEFAULT OFF

USER (VERBATIM): "7. True, the order doesn't matter much here unless the second land is played after other things
or we are paying for a cost like Gateway Plaza."

Commit 28ddf156a. In `EnumeratePlansWithLandUncachedBody` (shared by the search, the continuation list
`BpEnumEntryFor` and the executor's replay -> lockstep): when the turn so far has done nothing but play lands
(pre-combat; `spells_cast_this_turn == 0`; pool empty; every own permanent that did not enter this turn is
untapped; every permanent that entered this turn is a land that was PLAYED -- entered count == lands played;
main-1 hand mask live), and every entered land and the candidate land Y share one commutation class >= 0
(`DecisionProvider::LandEntryOrderClass`), a LAND-FIRST plan (`land_slot == 0`, no fetch, no face) for Y is
dropped when Y's land signature sorts below a land already played that was held since main 1, and every hand copy
of Y was held since main 1. Plans that play Y after casts are never touched.

Maze's End provider classes: -1 (order-sensitive) for an as-enters colour (CLB Gates, Thran Portal), Gateway
Plaza, the fastland condition (Thran Portal), Gond Gate (`gates_enter_untapped`), The Black Gate (pay life), Karoo,
fetch-on-play, MDFC; -1 for every land with 2+ Amulets out (the float's colour follows the line at that moment);
with Guild Summit out, Gate = 1 and non-Gate (Maze's End) = 2 (so no draw moves to another point of the turn);
else 0. Default -1 for every other provider: byte-identical (and the engine also requires
`deck_has_static_land_drops && !deck_has_landfall`, which only Maze's End has).

Soundness (twin argument, in the code comment): sort the held lands among the held positions of the turn's land
sequence and leave drawn lands where they were; one class means every entry draws the same, so every draw lands at
the same point; every prefix of the sorted sequence passes the filter; the root (first land) is never filtered.
Key-neutral: it reads only fields that BuildBreakpointKey / the enumeration memo already fold (battlefield with
entered_this_turn and tapped, hand card numbers, the main-1 hand mask, spells cast, lands played, pool).

Unit tests (`test/unit/test_mazes_end_p5b.cpp`): exactly one order of two commuting Guildgates is offered; a
colour Gate, a land drawn this turn, a spell cast, a tapped old permanent, two Amulets, and a Summit with a
Gate/non-Gate pair all keep both orders. All 63 Maze's End unit cases pass (incl. I3's two- and three-drop wins).

Probe (current engine, d5 b20, seeds 141001+gi, 40 games, sequential single-thread): the fold fires in 29/40
games; every game the same turn (228 = 228); units 4,704,892 -> 4,689,583 (-0.33%). The saving is small because
the continuation children are a small share of the work. (It was first built default ON as a sound collapse; the
held-out A/Bs below moved it to DEFAULT OFF.)

## Fold 3 -- Archdruid's Charm tutor-axis c==0 duplicate: NOT an identity (not folded)

The tutor axis emits the base plan (`tutor_choice -1`: index 0 at resolution, UNPINNED entry) plus one variant
per (index, land-entry pin), index 0 included. Unpinned, `LockEtbChosenColor` asks the provider's
`EtbChosenColor` FIRST (generic hand-demand vs supply rule) and only then `LandEntryPinCandidates().front()` (P1's
rule). The unit test shows they differ: Cliffgate with Guild Summit ({2}{U}) in hand and no green or blue source
-> unpinned locks U, the pin menu says G. So the pinned c==0 variant is a different line. A second mechanism also
separates them: the pins are computed at the TURN-START state, the unpinned default at the entry state (after the
plan's land and prefix casts), and P1 reads the sources on the battlefield.

Possible follow-up (not built; a play change, would need the USER): make the unpinned default of a land with a pin
menu equal the provider's `LandEntryPinCandidates().front()` at the entry (the hook's own documented contract says
"first = the unpinned default"), and encode the tutor pin as an index into that list at resolution; then the c==0
pinned variant would be the base plan by construction and could be folded.

## Units, scenarios, smoke + regression (one combined run, binary at 28ddf156a)

All 3,541 unit cases / 3,621,600 assertions pass; scenarios 561/561. Smoke + regression (`--smoke --regression`,
one pooled batch): every key PASSES byte-identical except Maze's End's d5 rows -- the fold is gated to the one
deck with static extra land drops, so no other deck can move:

| key | GT | now | changed games |
|---|---|---|---|
| mazesend_smoke_d5_s1001 | 5.7652/cd57a304 | 5.7652/183459b0 | 5 same-score play changes (gi27, 48, 55, 69, 125) |
| mazesend2hg_smoke_d5_s1001 | 5.6333/b4d13c7a | 5.6333/f5134988 | gi27 same score |
| mazesend_regression_d5_s2002 | 5.7337/d6028cf0 | 5.7391/b09a3a96 | **gi80 5->6**; gi23, 46, 170 same score |
| mazesend_regression_d5_s3003 | 5.7200/de9a323c | 5.7200/35883ca2 | gi107 same score |

d0, d3 and the backstop rows are unchanged. gi80 (seed 2082 gi80, d5 b20): the two arms diverge at TURN 1 (Sea
Gate vs Rakdos Guildgate), where no Exploration is in play -- the fold fires only inside the lookahead turns, so
this is the budget being spent on a different set of lookahead branches (the fold arm spent 94K units vs 67K).

## Held-out A/B 1 (one pooled batch, arms innermost; seeds 151001-154001 + gi)

| shape | games | off | on | better/worse | net | units/game off -> on | ms/game off -> on |
|---|---|---|---|---|---|---|---|
| d5 b20 | 250 | 5.6680 | 5.6680 | 0/0 | 0 | 150,287 -> 149,758 | 3,414 -> 3,299 |
| d3 b10 | 150 | 5.7533 | 5.7533 | 0/0 | 0 | 89,810 -> 89,103 | 2,197 -> 2,030 |
| d1 b3 | 100 | 5.7300 | 5.7400 | 0/1 | +1 | 42,473 -> 42,383 | 1,119 -> 1,118 |
| d1 b1 | 100 | 5.6700 | 5.6800 | 0/1 | +1 | 35,846 -> 35,754 | 837 -> 746 |

Slower: d1b1 gi7 (seed 153008), d1b3 gi30 (seed 154031), plus regression gi80. Net +2 over 600 is not <= 0, so a
second, larger held-out sample was run (A/B 2) and every slower game replayed at d8 b0.

## Held-out A/B 2 (seeds 161001-164001 + gi, same binary) and the d8 b0 rechecks

| shape | games | off | on | better/worse | net |
|---|---|---|---|---|---|
| d5 b20 | 400 | 5.6425 | 5.6400 | 1/0 | -1 |
| d3 b10 | 400 | 5.6575 | 5.6625 | 0/2 | +2 |
| d1 b3 | 1000 | 5.6670 | 5.6690 | 1/3 | +2 |
| d1 b1 | 1000 | 5.7660 | 5.7700 | 0/4 | +4 |

Both samples together: 2 better / 11 worse, **net +9 turns over 3,400 games** -- not neutral, so the fold as first
built (applied in EVERY enumeration) is NOT adopted.

d8 b0 (20M-unit ceiling, `ignore_play_profile`, repro seed = base + gi): every slower game of A/B 1 and the tier
recovers on both arms -- d1b1 gi7 (seed 153008) T5/T5, d1b3 gi30 (seed 154031) T5/T5, regression gi80 (seed 2082)
T5/T5 with the identical digest. So nothing is unfindable; the loss is a policy effect under a budget.

**Root cause.** Every slower game examined (regression gi80, d1b1 gi7, A/B 2 d1b1 gi0) diverges at a turn where
the fold cannot fire at the ROOT (turn 1, no Exploration in play): the fold changed the evaluation of the
lookahead / beyond-horizon PLAYOUTS. In a playout nothing branches over the first land, so "the twin order exists"
is false there: dropping "Y first" after "X only" removes a line the playout policy would have taken, and the
playout never compensates by playing Y first. The identity holds only where the search branches.

**Amended fold (stage 2):** live only on the search's own enumerations (`g_rollout_nest == 0`), with that context bit
folded into the enumeration keys (`FoldPhysicalCards`) whenever the fold applies to the deck, so a search list and a
playout list never share a cache entry. Re-measured on fresh held-out seeds (A/B 3, 171001-174001).

## Held-out A/B 3 -- the amended (search-only) fold, fresh seeds 171001-174001 + gi

| shape | games | off | on | better/worse | net | units/game off -> on |
|---|---|---|---|---|---|---|
| d5 b20 | 400 | 5.6575 | 5.6550 | 1/0 | -1 | 151,736 -> 151,376 |
| d3 b10 | 400 | 5.6900 | 5.6925 | 0/1 | +1 | 99,797 -> 99,515 |
| d1 b3 | 1000 | 5.7000 | 5.7020 | 0/2 | +2 | 55,763 -> 55,506 |
| d1 b1 | 1000 | 5.7410 | 5.7420 | 0/1 | +1 | 43,690 -> 43,596 |

Search-only halves the play churn (same-digest games 391-995 of each shape vs 391-974) and the damage (net +3 /
2,800, 1 better / 4 worse, vs +7 / 2,800 for the fold-everywhere form), but it is still not "net <= 0". Units fall
0.2-0.5% per game. Remaining mechanism (hypothesis, not proven per game): under a binding budget the root's plan
loop may not reach the twin root plan ("Y only") and its continuation before the budget runs out, so dropping the
earlier route to the same board can lose it; at d8 b0 every slower game examined recovers.

**Shipped: `MTG_FOLD_LAND_ORDER` DEFAULT OFF** (search-only form; heurarm `FOLD_LAND_ORDER`; =1 arms it). With it
off the engine is byte-identical to 7093daceb for every deck (the fold block, the key bit and the provider hook are
all behind the flag; the new heurarm slot was shown byte-neutral by the stage-1 tier run, where every non-Maze key
passed). No GT moves, so no accept.

**For the USER (your rule, measured; default taken = OFF):** "the order doesn't matter much here" holds as an
identity where the search branches, and I kept it as a lever. On budgeted play it measured slightly worse twice
(fold everywhere: 2 better / 11 worse over 3,400 held-out games; search-only: 1 better / 4 worse over 2,800).
Possible amendment to try next: instead of deleting the lower-sorted twin, keep the twin the ROOT reaches first
(order the twins by the root's move order), or demote the twin to a later wave (budget-staged, nothing removed at
unlimited budget) -- either keeps the earliest route to the board under a binding budget.
