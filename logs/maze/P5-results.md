# P5 results (Maze's End provider prunes) -- 2026-11-18

Worktree `.claude/worktrees/maze-p5`, branch `wip/maze-p5`. P5 built the levers (589cf5847, handoff 124ca922f);
P5-2 finished units, added the USER's three Treasure/Talon refinements, measured, and set the defaults.
Commits: `84bdd4a56` (units green, strict Treasure hook, Talon readings), `42a4b7a38` (heapstrict ON, max_lands 5,
2HG smoke row). Scratch: `build/scratch/maze/p5/` (`ab/`, `fin/`, `d8/`, `d8b/`, `d8c/`, `eq/`, smoke logs).

## Shipped shape (every item a coordinator default, USER sign-off pending)
| # | Item | Lever (default) | Definition P5 chose |
|---|---|---|---|
| 1 | Maze's End "find nothing" pruned | `MTG_MAZE_FIND_NOTHING` (ON) | the fail-to-find twin is withheld whenever the menu (after the Plaza drop) still holds a legal Gate target |
| 2 | Burgeoning menu = land-drop menu | none needed | both menus are distinct land names x `LandEntryPinCandidates`, plus none; `LandPlayOffered` restricts both |
| 3 | Archdruid's Charm land -> battlefield | none needed | the card only ever puts the land onto the battlefield; no to-hand variant exists |
| 4 | Gateway Plaza: pay or don't play | `MTG_MAZE_PLAZA_PAY` (ON) | entry menu {pay}; a drop / Burgeoning put / Charm / Maze fetch of a Plaza that cannot pay {1} on entry is not offered. Payable = an Amulet, a Gond Gate, or a probe payment of owed+{1} (optimistic). Charm never whiffs on it; Maze's fetch may empty to the decline. Caveat: with Guild Summit out a sacrificed Plaza still draws |
| 5 | Heap Gate Treasure | `MTG_MAZE_HEAP_STRICT` (ON, new) over `MTG_MAZE_HEAP_TREASURE` (ON) | **the Treasure is never a searched branch** (every Treasure plan leaves the menu) and `PerformLastChanceTreasure` makes it at the last opportunity before our untap step -- after the opponent's notional turn, i.e. after our end step's Manabond dump, the opponent's land, Burgeoning's put and every off-main Maze's End activation -- whenever the untapped sources still pay {1} + {T} Heap Gate + tap another Gate. Both worlds, same position (GameEngine::RunTurnFrom / TurnSolver::SimulateEndAndStartNextTurn, after the opponent-turn block). Off in human play and under MTG_UNPRUNED. **Dominance premise: a goldfish; the tapped permanents untap at our untap step, nothing happens before it, and a Treasure only adds.** (`=0`: P5's rule -- a Treasure plan is removed when the menu also holds "the plan without it + one more spend", kept when next turn's mana or a colour binds.) |
| 6 | Exploration land-order fold | not built | handed to P5b (below) |
| 7 | One colour per entry | P1 `MTG_MAZE_ONE_COLOUR` (ON); variant `MTG_MAZE_COLOUR_ALT` (OFF) | P1 already gives one colour at every route; the alternating rule (G while green sources <= blue sources, else U) measured identical (0/0) and is not shipped |
| 8 | Basilisk Gate pump | none needed | never offered autonomously (needs a creature target; the list has none) |
| 9 | Manabond "no" with an all-land hand | `MTG_MAZE_MANABOND_ALL_LANDS` (ON) | "no" withheld when the hand is all lands, no land has 2+ pins, and no Plaza in hand is unpayable at the end step |
| 10 | A1 weights -> v5 | `MTG_MAZE_FETCH_EVAL_V5` (ON) | new-name fetch eval 100 (was 200); MTG_MAZE_WSEL_* deleted |
| 11 | Talon "lacking extra land drops" | `MTG_MAZE_TALON_LACKDROPS` / `_ATMOST1` / `_EFFECTIVE` (all OFF) | (a) none of Exploration/Burgeoning/Manabond held or controlled; (b) at most one copy; (c) Exploration always counts, Burgeoning/Manabond only with a spare land in hand (land cards > open land drops). **All three measured worse than the always-offered amendment: not shipped.** |
| 12 | max_lands | profile `max_lands` **5** (P5 had set 6) | 6 measured worse in every shape (net +16 turns / 550 games); reverted |
| 13 | Suite rows | `test/regression_cases.sh` | P5's d3 -> d5 moves, plus P5-2: `mazesend2hg` smoke canary d3 b10 x50 -> **d5 b20 x30** (50 x 76.7K = 3.83M units = 29 d5 games at 130.5K; measured on this tree). GT NOT accepted |

## Units
3446/3446 (both commits). The P5 handoff's "real bug" at `test_mazes_end_p5.cpp:161-162` was NOT a code bug: P5's
build1 compiled `DecisionProviders.cpp` while it was being edited (source 07:20:56, object finished 07:25), so ninja
treated the stale object as current in builds 2-4. The first full rebuild passes the test unchanged (the probe
`TapForCostDirect({4})` on one Forest is false; the drop empties the list). **The handoff's 07:27 `mtg` binary was
built from that stale object; nothing was measured on it.**

## Smoke (Release, MTG_PGO=0, vs F2's `build/scratch/maze/f2/smoke-f2/wins`, case by case)
Shipped binary (42a4b7a38): **219 of 223 cases byte-identical**; only `mazesend*` moved.
| case | games compared | better / worse | net turns |
|---|---|---|---|
| mazesend d0 | 1000 | 90 / 56 | -51 |
| mazesend d5 b20 | 100 common (row is now 132) | 4 / 1 (gi51) | -2 |
| mazesend d3 b10 | 10 common (row is now 10) | 0 / 0 (4 play changes) | 0 |
| mazesend2hg | new key (d5 b20 x30) | -- | -- |
gi51 (seed 1052): T6 -> T8 at d5 b20; **at d8 b0 base and shipped play the same game, T6** (recovers).

## Base equivalence
All P5 levers off + max_lands 5 on the P5 binary vs F2's final `mtg_f2b`: **160/160 games byte-identical digests**
(d5 b20 x60 seeds 141001+, d3 b10 x60 142001+, d1 b1 x40 143001+), and every search counter line identical.

## The pooled A/B (ONE batch, arms innermost, 8 workers, 7172 jobs, 8/8 busy at the 10-min heartbeat)
Seeds: d5 b20 141001+gi x200, d3 b10 142001+gi x150, d1 b1 143001+gi x100, d1 b3 144001+gi x100 (550 games/arm).
`all` = P5 defaults with max_lands 6 (as P5 shipped it); `final` = the shipped shape (all + heapstrict + max_lands 5,
run on the 42a4b7a38 binary as a second pooled batch on the same seeds). Loss = 9. Net = arm minus reference.
| arm | vs `all`: better/worse | net (d5/d3/d1b3/d1b1) | vs `base`: better/worse | net (d5/d3/d1b3/d1b1) | d5 b20 units/game |
|---|---|---|---|---|---|
| base | 26/36 | +2 (-2/+5/+0/-1) | 0/0 | 0 | 144.1K |
| all | 0/0 | 0 | 36/26 | -2 (+2/-5/+0/+1) | 92.3K |
| nofind | 2/9 | +7 (+4/+2/+1/+0) | 31/28 | +5 (+6/-3/+1/+1) | 98.3K |
| noplaza | 2/8 | +6 (+1/+2/+2/+1) | 31/27 | +4 (+3/-3/+2/+2) | 91.6K |
| noheap | 1/1 | 0 (-1/+0/+0/+1) | 35/25 | -2 (+1/-5/+0/+2) | 96.5K |
| nomb | 0/0 | 0 | 36/26 | -2 (+2/-5/+0/+1) | 92.9K |
| nov5 | 13/9 | -3 (-4/+2/+2/-3) | 33/19 | -5 (-2/-3/+2/-2) | 93.1K |
| ml5 | 12/6 | -16 (-4/-3/-6/-3) | 31/14 | -18 (-2/-8/-6/-2) | 121.9K |
| colalt | 0/0 | 0 | 36/26 | -2 (+2/-5/+0/+1) | 92.2K |
| talonlack (a) | 7/23 | +17 (+5/+7/+2/+3) | 39/45 | +15 (+7/+2/+2/+4) | 91.8K |
| talon1 (b) | 7/13 | +6 (+1/+5/+0/+0) | 39/35 | +4 (+3/+0/+0/+1) | 91.9K |
| taloneff (c) | 5/15 | +10 (+3/+6/-1/+2) | 37/37 | +8 (+5/+1/-1/+3) | 91.7K |
| heapstrict | 6/1 | -5 (-2/+1/-1/-3) | 41/26 | -7 (+0/-4/-1/-2) | 89.2K |
| **final (shipped)** | 17/7 | -20 (-6/-2/-6/-6) | **35/14** | **-22 (-4/-7/-6/-5)** | **118.5K** |
Reading the leave-one-out rows (net > 0 = removing the lever costs turns): find-nothing (+7) and Plaza (+6) help;
the Heap (P5 rule) and Manabond prunes are quality-neutral and cut work (noheap +4.6% units, nomb +0.7%); v5 vs the
old 200 is 13/9 (-3), noise-level; max_lands 6 loses in every shape. Every arm wins every A/B game by T8.
**The shipped shape vs the pre-P5 engine: 35 better / 14 worse, net -22 turns over 550 held-out games, every shape
better, and 18% fewer units at d5 b20 (118.5K vs 144.1K).**

## Slower games at d8 b0 (the shipped shape vs base; every one of its 14)
| game (seed) | budgeted: base -> final | d8 b0: base / final | d8 b2000: base / final | recovered? |
|---|---|---|---|---|
| d1b1 gi27 (143028) | T6 -> T7 | T6 / T6 | T6 / T6 | yes, b0 |
| d1b1 gi41 (143042) | T6 -> T7 | ABANDON / ABANDON (same game) | T6 / T6 (same game) | b0 cannot finish; yes at b2000 |
| d1b1 gi43 (143044) | T6 -> T7 | ABANDON / T6 | T6 / T7 | final reaches base's T6 at b0; T7 at b2000, attributed to MTG_MAZE_FIND_NOTHING (nofind restores T6 at b1) |
| d1b1 gi91 (143092) | T6 -> T7 | T6 / T6 (same game) | T5 / T5 | yes, b0 |
| d1b3 gi1 (144002) | T6 -> T7 | T7 / T7 (same game) | T6 / T6 | yes, b0 |
| d3b10 gi5 (142006) | T5 -> T6 | ABANDON / ABANDON (same game) | T5 / T5 | b0 cannot finish; yes at b2000 |
| d3b10 gi31 (142032) | T4 -> T5 | T5 / T5 | T4 / T4 | yes, b0 |
| d5b20 gi13 (141014) | T6 -> T7 | ABANDON / T7 | T6 / T6 | yes at b2000; at b0 base cannot finish (attributed to v5: nov5 restores T6 at b20) |
| d5b20 gi27 (141028) | T5 -> T6 | T6 / T6 | T5 / T5 (same game) | yes, b0 |
| d5b20 gi63 (141064) | T5 -> T6 | T6 / T6 | T5 / T5 | yes, b0 |
| d5b20 gi95 (141096) | T6 -> T7 | ABANDON / ABANDON (same game) | T6 / T6 (same game) | b0 cannot finish; yes at b2000 |
| d5b20 gi111 (141112) | T5 -> T6 | T6 / T6 | T5 / T5 | yes, b0 |
| d5b20 gi113 (141114) | T7 -> T8 | T7 / T7 | T7 / T7 | yes, b0 |
| d5b20 gi199 (141200) | T6 -> T7 | T6 / T6 | T6 / T6 | yes, b0 |
| smoke d5 gi51 (1052) | T6 -> T8 | T6 / T6 (same game) | -- | yes, b0 |
Games that CANNOT finish at d8 b0 (2M-unit ceiling, the P7 b0 degeneracy): d1b1 gi41, d3b10 gi5, d5b20 gi95 on
both arms; d1b1 gi43 and d5b20 gi13 on base only. b2000 is reported as the large-budget readout for those.
The earlier per-lever d8 pass (`d8/`, 103 jobs, on the `all` arm with max_lands 6) found the two non-recoveries,
d3b10 gi78 (all T7 vs base T5 at d8 b0, ml5 T5) and d1b3 gi61 (all T8 vs base T5); gi78 is max_lands 6, which
is why the shipped shape is back on 5; neither game is slower in the shipped shape.

## Starved games (d5 b20, base vs shipped)
| set | base | final | |
|---|---|---|---|
| V1 5c list 111001+gi (32, 33, 100, 124, 142, 145, 147) | 6.714 | 6.714 | identical turns; 30% fewer units (274K vs 392K/game) |
| sweep 61001+gi (0, 7, 13) | 5.333 | 5.000 | one game a turn faster (at b20, the slow one found at b100-500) |
Per game, shipped vs base: every st111 game the same turn (gi32 T6, gi33 T7, gi100 T7, gi124 T6, gi142 T7, gi145 T7,
gi147 T7); sweep gi0 T5 = T5, gi7 T4 = T4, **gi13 T7 -> T6**. The `all` arm (max_lands 6) LOST st111 gi32 (no win
by T8; base and shipped win T6) -- a third max_lands-6 failure. Whether gi0/gi7 still trail their b100-500 result was
not re-measured here (P7).

## Cost (vs the 3x Hinata gate, 15,510 ms)
From the shipped-binary smoke batch (8 workers on a shared 32-core box, contended), Maze's End and Hinata in the
same pooled run: mazesend d5 b20 **1,629 ms/game (0.81x Hinata d5's 2,006)**, 127.6K units/game (1.59x Hinata's
80.5K); mazesend2hg d5 b20 3,046 ms/game (1.52x Hinata's largest row); mazesend d3 b10 633 ms. The largest Maze's End
average (the 2HG row) is ~0.2 of the gate. Not re-timed uncontended (the box had other agents compiling).

## Handed to P5b (engine folds; out of P5's scope)

### Item 6: the Exploration back-to-back land-order fold (NOT BUILT; design copied from logs/maze/P5-handoff.md)
The fold point is the site-10 breakpoint CONTINUATION enumeration. That enumeration has no record of the previous
section, i.e. "the last section was land-only, land X". It would need either a state field folded into the
continuation's cache key or a thread-local set at the continuation (TurnSolver ~78282 `LandDropNewlyOpen` arming /
the deferred re-solve). Rule to build:
- when the previous section played only land X, drop land Y from the continuation's land menu if Y < X (canonical
  order) and the two commute;
- "commute" = neither land has an entry pin choice (colour/Plaza), a fastland condition (Thran Portal), Gond's
  gates_enter_untapped, The Black Gate's pay-life entry, or a Karoo bounce.
USER (VERBATIM): "7. True, the order doesn't matter much here unless the second land is played after other things or
we are paying for a cost like Gateway Plaza." Check at d8 b0.

### The Archdruid's Charm tutor-axis duplicate (possible identity fold)
P1d item 8's "x2" is probably the tutor axis's c==0 duplicate: the base plan (unpinned) plus the pinned (index 0,
pins.front()) variant, TurnSolver ~93525. Check that the unpinned default == pins.front() for colours
(LockEtbChosenColor asks EtbChosenColor first); if so the pinned c==0 variant is an identity fold.

## Open for the USER (each a default taken and kept running; not blocking)
1. **Heap Gate's Treasure** ships as the per-plan dominance (never a branch; made at the last chance after the
   opponent's turn when the sources still pay). Measured vs P5's leftover-or-binds rule: 6 better / 1 worse, net -5,
   cheaper; the one slower game (d3 gi31) recovers at d8 b0. Sign-off on the rule and its premise (goldfish; the
   tapped Gates untap at our untap step).
2. **Talon Gates:** all three readings of "lacking extra land drops" ((a) none +17, (b) at most one +6, (c) effective
   only +10 turns worse over 550) lose to the always-offered amendment, so the amendment stays. (c) is closest to the
   USER's "Burgeoning ... not going off" intuition and still loses 5/15.
3. **v5 fetch weight** (the USER's "rest of the table"): the old 200 beats it 13 better / 9 worse, i.e. v5
   costs 3 turns over 550 (d5: 200 is 4 better; d3 and d1b3: v5 is 2 better each). Kept v5 (USER-adopted); the difference is within noise.
4. **max_lands back to 5** (the USER left 2-6 to the agent): 6 lost in every shape (+16 turns / 550) and lost
   st111 gi32 outright; 5 costs ~30% more units at d5 than 6 but the shipped shape is still 18% under pre-P5.
5. **mazesend2hg** smoke canary moved to d5 b20 x30 (coordinator default, GT not accepted).
6. **The colour-alternation variant** measured identical to P1 (0/0); P1 stays. Delete the variant lever?
7. **d8 b0 cannot finish** 3 of the 14 slower games on either arm (and 2 more on base): the P7 b0 degeneracy blocks
   the "recover at d8 b0" check for them; b2000 is reported instead.
