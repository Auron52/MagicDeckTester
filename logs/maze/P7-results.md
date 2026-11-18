# P7 results (Maze's End performance: b0 degeneracy + b20 starvation) -- 2026-11-18

Worktree `.claude/worktrees/maze-p7`, branch `wip/maze-p7` (from 7093daceb). Scratch: `build/scratch/maze/p7/`.

## 1. Census on the current tree (7093daceb, before any P7 change)

Single-thread runs, `build/scratch/maze/p7/c0/` and `c1/` (binary = the primary checkout's Release build of the same
`HEAD:src`).

### The three d8 b0 games P5 could not finish all FINISH -- the 2M-unit ceiling was simply too low
| game (repro) | d8 b0 result | units | wall (1 thread) | P5 budgeted base -> final | recovered? |
|---|---|---|---|---|---|
| d3b10 gi5 (`--seed 142006 --game-index 5`) | T5 | 2.47M | 51 s | T5 -> T6 | yes (T5) |
| d5b20 gi95 (`--seed 141096 --game-index 95`) | T6 | 4.28M | 200 s | T6 -> T7 | yes (T6) |
| d1b1 gi41 (`--seed 143042 --game-index 41`) | T6 | 15.89M | 516 s | T6 -> T7 | yes (T6) |
At d8 the lazy leaf's leafless probe usually proves the win in-window, so the cost is the leafless tree (probe
1.5-2.9M units) plus one leafed ladder on the first decision.

### d3 / d5 b0: not a loop -- every decision pays a full leafed tree at depth 3
`--seed 30001 --game-index 0` and `--seed 91005 --game-index 4`, d3 b0, abandoned at 5M units after 2 committed
decisions: the turn-2 decision alone costs **2.0-2.7M units** (the commit pass; warm passes 5-12% on top). The
leafless lazy probe is cheap (7-11K units per decision) and misses (no win inside 3 turns from turn 2), so the
ladder rolls out EVERY horizon leaf: 228K-549K rollout calls for 2 decisions, leaf-TT hit rate 7.6-10%.
Units by site: greedy_walk (the rollout policy's subset walk, billed at W = 1) 35-71%, rollout_step 10-25%,
greedy_fallback 10-25%, la_cand + la_bp_wave 10-14%; interior nodes only 3-4% of the steps. Per-turn plans:
T2 426 -> T3 13K -> T4 204K (30001). So the b0 cost is (horizon leaves) x (rollout), and the leaf count is the
product of the per-turn plan menus: **branching, not a near-loop or repeated states** (no option group wider than
3; no duplicate continuation lists, `[bp-cands] duplicate=0`).

A separate b0 multiplier: a hand that mulligans pays one FULL b0 game per legal bottoming subset
(`BottomCards -> RolloutWinTurnFrom`, seen on 30001 gi0's stack). It vanishes once the keep table exists
(exhaustive bottoming), so it is a pre-keep-table artifact, not a search defect.

### The menus (MTG_PLAN_DUMP, seed 30001, d3 b10, `dump/t{3,4,5}`)
Turn 4 (Baldur's Gate, Amulet, Golgari Guildgate in play; 2 Exploration, Burgeoning, Gond Gate x2, Manor Gate,
Maze's End in hand): 16 base plans. Ten of them are dominated: a land alone while "land + Exploration" is
affordable, "pass", a land-less Exploration while "land + Exploration" exists. Turn 5: 12 base plans, 7 dominated
the same way. That is the "extra branching given how easy the mana is" the USER pointed at.

## 2. The prune: CAST / LAND DOMINANCE FOLD (`1ce7f76d5`)

Engine `CastDominanceFold` (TurnSolver.cpp, every plan enumeration, root and continuation) under a provider-declared
premise (`DecisionProvider::CastDominanceLive` / `CastDominanceClass`, new hooks; default not live, so every other deck
is untouched). Maze's End declares it (`MTG_MAZE_CAST_DOMINANCE`, `MTG_MAZE_LAND_DOMINANCE`, both DEFAULT ON):
* **Fold A (cast):** drop plan P when the menu also holds P + one more cast of Exploration / Burgeoning / Manabond /
  Amulet of Vigor, with the same land, the same other actions in the same order, and every other plan field equal
  (pins, breakpoint coordinates; checked with `Plan::operator==` after clearing actions and evaluation-only fields).
* **Fold B (land):** drop a land-less plan whose actions are only Exploration / Burgeoning / Manabond casts (nothing
  at all = "pass") when the menu holds the same plan plus a land (every non-land field equal).
* **Premise (stated for the USER):** each of the four cards only adds options (an extra drop; a "may" put; a "may"
  dump; an untap), touches no library and no hand count, so having it in play is never worse than holding it; and
  mana a main-phase plan leaves unspent has no other use. The two off-main mana uses in this list make it NOT live:
  an untapped Treasure-making Gate controlled or one in hand (the last-chance Treasure), and a Gateway Plaza in hand
  (Burgeoning / Manabond can put it outside our main and it needs {1}). Guild Summit vetoes (its rest-of-line count
  reads what the line still owes). Amulet is fold A only (worth more cast before the land). **Must be re-checked when
  P6 (off-main Maze's End activation) lands: mana held for that activation breaks the premise.**
* Sound dominance (restrict-only, maximal plans always kept, the error always toward keeping), so it ships ON per
  "COLLAPSE WASTED SEARCH UNCONDITIONALLY"; findable at every budget (the dominating sibling family -- including its
  EMPTY-ARM "stop after the cast" continuation -- stays on the menu).
* Units: 3540/3540 (new `test_mazes_end_p7.cpp`: the turn-4 menu folds 16 -> fewer, restrict-only, maximal plans kept,
  fold B lever, the four premise cases).

### Effect on one d3 b0 game (30001 gi0, abandon 3M, single thread, `q2/`)
| arm | decisions committed in 3M units | commit units per decision | rollout calls |
|---|---|---|---|
| fold off | 2 (the turn-2 decision alone 1.99M) | 1.99M | 303K |
| fold on | 19 (15 ladder + 4 lazy-leaf in-window hits) | 163K | 425K |
~12x cheaper per decision. The folded menus on that game (`dump2/`): turn 4 = 3 plans, turn 5 = 3 plans (land, land +
Maze's End activation, activation alone), all genuine choices.

## 3. Held-out A/B (ONE pooled batch, arms innermost, 32 threads, 5060 jobs; `ab1/`, binary `mtg.p7b` = 1ce7f76d5)
Arms: `base` = fold off (`MTG_MAZE_CAST_DOMINANCE=0`, identical to 7093daceb's play), `fold` = A + B (shipped),
`folda` = fold A only. Seeds (new, held out): d5 b20 151001+gi x300, d3 b10 152001+gi x150, d1 b1 153001+gi x100,
d1 b3 154001+gi x100, d0 155001+gi x1000; plus the V1 5c starvation list (d5 b20 111001+gi, gi 32/33/100/124/142/145/147)
and the sweep's 61001 gi0/7/13. Loss = 9. Net = arm minus base, in turns.

| cell | base avg | fold avg | fold better/worse | fold net | folda net | units/game base -> fold | same digest |
|---|---|---|---|---|---|---|---|
| d5 b20 (play) x300 | 5.6500 | 5.6100 | 16 / 4 | **-12** | -11 | 154.7K -> 132.8K (-14%) | 205/300 |
| d3 b10 x150 | 5.7533 | 5.7400 | 6 / 4 | -2 | -3 | 89.8K -> 73.9K (-18%) | 106/150 |
| d1 b3 x100 | 5.7300 | 5.7600 | 0 / 3 | +3 | +2 | 42.5K -> 29.0K (-32%) | 86/100 |
| d1 b1 x100 | 5.6700 | 5.7000 | 2 / 5 | +3 | +3 | 35.8K -> 25.3K (-30%) | 81/100 |
| d0 x1000 | 6.9470 | 6.9470 | 0 / 0 | 0 | 0 | -- | 1000/1000 |
| **held-out total (650 searched)** | | | **24 / 16** | **-8** | -9 | | |
| starvation st111 x7 (d5 b20) | 6.7143 | 6.4286 | 2 / 0 | -2 | -2 | 274K -> 311K | 2/7 |
| sweep 61001 gi0/7/13 (d5 b20) | 5.0000 | 5.0000 | 0 / 0 | 0 | 0 | 76.6K -> 62.5K | 2/3 |

* The fold is net better on Maze's End (-8 held-out turns over 650 searched games; -10 with the starvation list) and
  cheaper in every searched shape (14-32% fewer units). Fold A alone is about the same quality (-9) but 5% dearer at
  d5 b20 (139.9K), so A + B ships.
* d0 is byte-identical (1000/1000 digests): the depth-0 runner's choice is never one of the dominated plans.
* The two d1 shapes (the mulligan-generation shapes) are +3 each (2/5, 0/3) -- within noise, every one checked at d8 b0
  below.
* Every other deck: the fold is live only for MazesEndProvider (`CastDominanceLive` defaults false) -- smoke below.

### Budget-0 (the 28-block screen's first 4 blocks, gi0-4, d3 and d5, abandon 10M units both arms)
| shape | base: abandoned / finished units | fold: abandoned / finished units |
|---|---|---|
| d3 b0 x20 | 3 / 41.8M over 17 | **0** / 31.5M over 20 |
| d5 b0 x20 | 5 / 35.2M over 15 | 5 / **10.1M** over 15 (3.5x cheaper) |
Every game both arms finish (32) has the same win turn (28 with identical digests), and the fold finishes the three
d3 games base abandoned (30001 gi0 T5, 30041 gi0 T5, 30061 gi4 T6). The 5 d5 games still over 10M on the fold arm: 30001 gi0, 30025 gi4 and 30061 gi0 MULLIGAN
(1, 1 and 2 mulligans): with no keep table, each legal bottoming subset is a full b0 d5 trial game
(`AIEngine::BottomCards`); that vanishes with the keep table (exhaustive bottoming). 30042 gi1 and 30065 gi4 are
kept 7s: genuine heavy d5 b0 searches (census below).

## 4. Smoke (Release, MTG_PGO=0, 32 threads, `build/scratch/maze/p7/smoke1.log`)
**224 configs unchanged, 3 changed -- all Maze's End** (`mazesend_smoke_d3`, `mazesend_smoke_d5`, `mazesend2hg_smoke_d5`);
every other deck byte-identical, as the provider gate says (the fold is live only for MazesEndProvider).
| key | GT | P7 | games | net turns |
|---|---|---|---|---|
| mazesend_smoke_d3_s1001 | 5.4000 | 5.3000 | 10 | -1 |
| mazesend_smoke_d5_s1001 | 5.7652 | 5.7652 | 132 | 0 (6 slower / 6 faster) |
| mazesend2hg_smoke_d5_s1001 | 5.6333 | 5.6667 | 30 | +1 (gi2, gi10 = the same games as the 1-head row; the deck reads no heads) |
Searched slower 8 (d5 gi2, gi10, gi32, gi33, gi60, gi66; 2HG gi2, gi10), faster 7, play-changed at equal score 49;
d0 unchanged. Every slower game is checked at d8 b0 below.

## 5. b20 starvation (d5 b20, the V1 5c list, seeds 111001+gi; same batch)
| gi | V1 5c b500 (old tree) | base b20 | **fold b20** | reading |
|---|---|---|---|---|
| 32 | 7 | 6 | 6 | at/under its b500 result on both arms |
| 33 | 7 | 7 | **6** | fold beats the old b500 result |
| 100 | 6 | 7 | 7 | STILL one turn slow: fold b100 T7, b500 T6 (`g100/`) |
| 124 | 6 | 6 | 6 | recovered (both) |
| 142 | 7 | 7 | 7 | recovered (both) |
| 145 | 7 | 7 | 7 | recovered (both) |
| 147 | 7 (b2000 6) | 7 | **6** | fold reaches the b2000 result at b20 |
Sweep 61001 gi0/7/13: T5 / T4 / T6 on both arms.
**gi100, the one left:** its turn-3 menu (fold on, `g100d/`) holds "Dimir Guildgate + Amulet + Burgeoning (+ a second
Burgeoning that is DROPPED at apply -- no second green)" and "Golgari Guildgate + Amulet + Burgeoning + Burgeoning".
b20 plays the Dimir line (one Burgeoning), b500 the Golgari line (two) and wins a turn earlier. A genuine land choice
the budget does not resolve, not a prune target; a move-order hint (land that makes more of the plan castable first)
would be the shape to try, and it would need its own A/B.

## 6. d8 b0 on fold1's slower games -- a SOUNDNESS HOLE in the first cut, fixed (`d8a/`)
fold1 = the first cut measured in sections 2-5 (`1ce7f76d5`). Its d8 b0 (abandon 200M) on all 16 held-out slower games:
14 reach the same d8 b0 turn as base, but **d3b10 gi61 (seed 152062) LOSES at d8 b0 on fold1 (base T8)** and d1b1 gi71
(153072) is T7 vs base T6. gi61's base line: T7 land + Exploration + Sylvan Scrying -> (breakpoint) play the fetched
Maze's End and activate it. fold1 had dropped that plan for "the same + Manabond", whose continuation lacks the {3}:
**a plan's breakpoint continuation can spend the mana the plan leaves unspent**, so "P + a free cast >= P" holds only
when P opens no breakpoint. Fix (fold2): fold A drops P only when every action of P is a cast-dominant cast that
opens no breakpoint (Exploration carries the new `kCastDomOpensBp` bit), and the fold is not live while we control a
Guild Summit (its Gate-entry draw makes any land play a breakpoint). Fold B is unchanged (a land-less plan of
Exploration / Burgeoning / Manabond casts vs the same plan with the land played first: the continuation reproduces).
The fold1 numbers above are therefore SUPERSEDED by section 7 (fold2).

## 7. P7b: fold2 + the P6 guard ("fold3"), measured ON TOP OF P6 (coordinator decision: P6 ships)

**wip/maze-p7 now contains P6's code commit** (`5ac60ceba`, cherry-picked as `cc32d32a3` on top of fold2; additive
conflicts only, P6's two heurarm slots placed first, matching wip/maze-p6's slot order). The coordinator integrates
this branch AFTER P6. chain2 (fold2 on the pre-P6 base) was stopped before its build started; no ab2 exists.

### The guard (`ccf65938c`): the fold keeps a plan whose unspent mana pays an off-main Maze's End activation
P6 activates an untapped Maze's End ({3},{T}) at our end step (respond to Manabond's yes, or bank after its dump) and
on the opponent's turn (respond to a Burgeoning trigger, or bank after its put), from the sources the main phase left
untapped. That is a use for unspent mana, so "P + cast >= P" (fold A) and "P + land >= P" (fold B: P's held land, put
off-main, opens a bank window P + land never sees) no longer follow. New engine hook `DecisionProvider::CastDominanceKeep`
(per fold candidate; default false; a kept plan is never dropped). `MazesEndProvider::CastDominanceKeep`: P is kept when
P6 is on (`MTG_MAZE_OFFMAIN_RESPOND` or `_BANK`), a Maze's End could be activated (one untapped in play, or one in hand),
and the over-counting bound
`U(P) = UntappedManaUpperBound + floating + E - mv(P's casts) - 1 >= 3`, where E = land cards in hand that can still
enter before our untap = min(hand lands, land drops left incl. P's Exploration + off-main puts: all hand lands with a
Manabond controlled/cast by P, one per Burgeoning), and the -1 is the activating Maze's End tapping itself (it is
always one of the counted sources). U only over-counts, so the error is toward keeping. Units: 4 new cases
(`test_mazes_end_p7.cpp`); suite **3546/3546**.

### Held-out A/B (`build/scratch/maze/p7/ab3/`, ONE pooled batch, arms innermost, 3444 jobs, 32 threads, 11m41s, max RSS 6.5 GB)
Binary `mtg.p7d` = `ccf65938c`. base = fold off (`MTG_MAZE_CAST_DOMINANCE=0` = 7093daceb + P6 play), fold3 = shipped.
Same held-out seeds as ab1.
| cell | base avg | fold3 avg | better / worse | net | units/game base -> fold3 | same digest |
|---|---|---|---|---|---|---|
| d5 b20 (play) x300 | 5.6267 | 5.6033 | 11 / 4 | **-7** | 143.0K -> 147.1K (+2.9%) | 251/300 |
| d3 b10 x150 | 5.7333 | 5.7067 | 5 / 1 | **-4** | 83.9K -> 76.8K (-8.4%) | 131/150 |
| d1 b3 x100 | 5.7300 | 5.7200 | 1 / 0 | -1 | 41.3K -> 34.2K (-17.3%) | 97/100 |
| d1 b1 x100 | 5.6500 | 5.6600 | 0 / 1 | +1 | 31.9K -> 25.7K (-19.5%) | 96/100 |
| d0 x1000 | 6.9190 | 6.9190 | 0 / 0 | 0 | -- | 1000/1000 |
| **held-out total (650 searched)** | | | **17 / 6** | **-11** | | |
| starvation st111 x7 | 6.4286 | 6.5714 | 0 / 1 | +1 | 276K -> 307K | 4/7 |
| sweep 61001 gi0/7/13 | 5.3333 | 5.3333 | 0 / 0 | 0 | 65.9K -> 77.3K | 3/3 |
(fold1 on the pre-P6 base, for reference: -8 held-out, units -14/-18/-30/-32%.) d5 b20 units rise 2.9% (ms/game
3976 -> 4348): the budget binds there, and the cheaper menus are spent deeper inside it -- not wasted work
("COLLAPSE WASTED SEARCH UNCONDITIONALLY"); every other searched shape is 8-20% cheaper. Maze's End is the only deck the
fold is live for, so "net <= 0 per deck" = Maze's End: -11 held-out (-10 with the starvation list). PASS.

### b0 (the 28-block screen's first 4 blocks, gi0-4, abandon 10M units both arms)
| shape | base: abandoned / finished, units over finished | fold3 |
|---|---|---|
| d3 b0 x20 | 3 / 17, 37.8M | **0 / 20**, 43.1M (incl. the 3 base could not finish) |
| d5 b0 x20 | 5 / 15, 32.2M | 5 / 15, **15.9M** (2.0x cheaper) |
All 32 games both arms finish have the same win turn. The 5 d5 abandons (both arms) are the known ones: 30001 gi0,
30025 gi4, 30061 gi0 (mulligans: one full b0 trial game per bottoming subset, gone with a keep table) and 30042 gi1,
30065 gi4 (genuine heavy kept 7s; census = step 5).

### d8 b0 (abandon 200M)
* fold1's 16 held-out slower games + the 6 smoke-slower games, both arms: **every one the same d8 b0 turn on both arms**
  (net 0 in every cell, 21/22 identical digests), including the soundness hole d3b10 gi61 (152062) and d1b1 gi71.
  fold3 is far cheaper there: e.g. d8b0 d1b1 3.77M -> 0.56M units/game.
* fold3's 7 new slower games (`d8c/`): **all 7 identical digests at d8 b0 on both arms.**
| game (repro) | base b | fold3 b | d8 b0 base = fold3 | units d8 b0 base -> fold3 | reading |
|---|---|---|---|---|---|
| d1b1 gi95 (`--seed 153096 --game-index 95`) | 7 | 8 | 7 | 14.18M -> 1.05M | recovers to base's budgeted turn |
| d3b10 gi17 (`--seed 152018 --game-index 17`) | 5 | 6 | 6 | 90K -> 55K | = base d8 b0; base's b10 T5 beat its own d8 b0 |
| d5b20 gi73 (`--seed 151074 --game-index 73`) | 7 | 8 | 7 | 1.28M -> 0.82M | recovers |
| d5b20 gi46 (`--seed 151047 --game-index 46`) | 6 | 7 | 6 | 6.48M -> 2.20M | recovers |
| d5b20 gi216 (`--seed 151217 --game-index 216`) | 5 | 6 | 6 | 0.66M -> 0.32M | = base d8 b0; base's b20 T5 beat its own d8 b0 |
| d5b20 gi261 (`--seed 151262 --game-index 261`) | 6 | 7 | 6 | 3.60M -> 0.92M | recovers |
| st111 gi147 (`--seed 111148 --game-index 147`) | 6 | 7 | 6 | 1.55M -> 0.97M | recovers |
Both readings (as P5 reported them): 5/7 reach base's budgeted turn; 7/7 equal base's d8 b0 exactly (same digest). The
two that do not reach base's budgeted turn are base-side budget luck: base's own d8 b0 plays the same T6 as fold3.

## 8. P7b smoke + regression on the P6-inclusive tree (Release, MTG_PGO=0; `smoke3.log`, `reg3.log`) -- ACCEPTED
Smoke: **223 unchanged, 4 changed, all mazesend*** (every other deck byte-identical). Regression (`--deck=mazesend`): 6
keys changed. GT moves (pre-P6 GT -> P6 + fold3; both P6 and the fold move these keys):
| key | GT | new |
|---|---|---|
| mazesend_smoke_d0_s1001 | 6.9120 | 6.8980 (P6 alone: d0 is fold-identical, ab3 1000/1000) |
| mazesend_smoke_d3_s1001 | 5.4000 | 5.3000 |
| mazesend_smoke_d5_s1001 | 5.7652 | 5.7348 |
| mazesend2hg_smoke_d5_s1001 | 5.6333 | 5.5333 |
| mazesend_regression_d0_s2002 | 6.9020 | 6.8960 |
| mazesend_regression_d3_s2002 | 5.7333 | 5.7333 (play changed) |
| mazesend_regression_d5_s2002 | 5.7337 | 5.6685 |
| mazesend_regression_d5_s3003 | 5.7200 | 5.6933 |
| mazesend_regression_d{3,5}_s30081 (b0 backstop) | 5.0000 | 5.0000 (play changed) |
Searched slower: smoke 6, regression 15. **d8 b0 (abandon 200M) on all 21, fold off vs fold3 on this tree: 21/21
identical digests** (`d8d/`, `d8e/`) -- the fold makes nothing unfindable. Against the pre-P6 GT turn: 13/21 recover
(smoke gi45/55/72; reg s2002 d3 gi7, d5 gi7/133/149/173/51/86, s3003 gi139/39/96), 8 persist equally with the fold
OFF (smoke gi53/60/121; reg s2002 gi21/95; s3003 gi121/56/73), so they are P6's or GT budget luck, not the fold's.
Attribution batch `d8f/` (pre-P6 binary `mtg.p7b`, fold off, d8 b0) -- see the line below once it lands.
Accepted in the worktree: `--smoke --accept-with-regressions --deck=mazesend,mazesend2hg` and
`--accept-with-regressions --deck=mazesend`; `check_gt_logs.py`: 1290 consistent, 0 stale. **Accepted on base
7093daceb + P6; the shared branch is now e682d7942 (P6 + cleanup + ~100 upstream commits): the coordinator rebases
wip/maze-p7 and re-accepts.**

## 9. Step 5: census of the two heavy d5 b0 kept 7s (`cen3/`, fold3, abandon 20M, 2 jobs pooled -> rows CONTENDED)
Both games abandon on the **turn-1 decision** (30042 gi1: 12.2M units, 30065 gi4: 20M; `idwaste_units` 18.6M,
`lad_warm_units` 11.7M vs `lad_commit_units` 0.5M): the ladder's warm passes never reach the commit pass. ~70% of the
units are `greedy_walk` (horizon rollouts), space_plans 13M. **The fold is not live in either tree**: 30042's T1 hand
holds Heap Gate, 30065's holds Gateway Plaza, and both are GLOBAL vetoes in `CastDominanceLive`. `[cast-dom]` over the
two games: dropped_cast 322K, dropped_land 241K, **kept_offmain_mana 1.98M** -- the P6 guard keeps 3.5x what the fold drops.
### Next prune candidates (sound refinements of the fold's premise; each needs its own A/B + d8 b0, NOT measured here)
1. **Tighten the P6 guard to the windows P6 actually has.** P6's respond and bank, and the off-main win's NEW boards,
   all need a Manabond or a Burgeoning (respond = their trigger; bank = after their put; the end-step off-main win
   without a put sees main 2's board, where the activation is already a searched plan action). So
   `CastDominanceKeep` can return false when no Manabond is controlled or cast by P and no Burgeoning is (or
   `!opponent_plays_lands`). One line: `if (dumps == 0 && (burg == 0 || !s.opponent_plays_lands)) return false;`
   before the U test. Also require a fetchable Gate in the library (`MazeOffMainMayAsk`'s library test).
2. **Replace the global Heap Gate / Gateway Plaza vetoes with per-plan keeps** of the same shape: Heap Gate's
   last-chance Treasure needs its {cost} from P's leftover (keep P only if U(P) >= Treasure cost and an untapped
   Treasure Gate can exist), Gateway Plaza's off-main put needs {1} AND a put source (Manabond / Burgeoning, as in 1).
   This turns the fold on in exactly the two census trees.
3. The turn-1 blow-up itself is ladder warm passes over a 5-turn plan product (horizon rollouts); after 1-2, re-census
   single-threaded (`--threads 1` is a diagnostic exception) to see what is left; then per-node cost (greedy_walk).

## 10. P7c (2026-11-18): rebase onto e682d7942, then (a) + (b)

### Step 1: rebase (integrable fold2 + P6 guard state) -- `17d414e8f`, HEAD:src `c9f26082e`
wip/maze-p7 rebased onto origin/phase-1-2-deck-analyzer `e682d7942` (already holds P6), dropping the P6 cherry-pick
`cc32d32a3` and the old GT accept `82eeffb80`; conflicts additive (heurarm slots: upstream's, then P7's). Units
3658/3658. Smoke (Release, MTG_PGO=0): 228 unchanged, 3 changed, all mazesend*, each **equal to the P7b accepted
aggregate + digest** (5.3000/5c70708f, 5.7348/6bce5a98, 2hg 5.5333/97dccd64). Regression (all decks): 365 unchanged, 3
changed, all mazesend*, each equal to P7b's (d3 s2002 5.7333/ef82be5a, d5 s2002 5.6685/75da65bb, d5 s3003
5.6933/3d185283). Re-accepted `--deck=mazesend,mazesend2hg` (smoke) and `--deck=mazesend` (regression);
check_gt_logs 1301 consistent / 0 stale. Pushed wip/maze-p7; Windows CI 44404519142 success.

### d8f attribution (folded into ab4 as arms p6 / noP6, fold off, d8 b0, abandon 200M)
The 8 games that persist against the pre-P6 GT with the fold off (smoke gi53/60/121; reg s2002 gi21/95; s3003
gi121/56/73): **all 8 reach the same d8 b0 turn with P6 on and P6 off** (6 identical digests; smoke gi60 / gi121
differ in play at the same turn). They are GT budget luck, not a P6 regression.

### (a) + (b) -- `fc204f2f8` (HEAD:src `c291bb996`)
(a) `MTG_MAZE_CASTDOM_WINDOWS`: the off-main Maze's End keep needs a P6 window (Manabond, or Burgeoning while the
opponent plays lands; controlled or cast by P, or held when P opens a breakpoint) and a fetchable Gate. (b)
`MTG_MAZE_CASTDOM_PERPLAN`: Heap Gate's Treasure / Gateway Plaza's off-main {1} are per-plan keeps instead of
switching the fold off. Both default ON; premises at `MazesEndProvider::CastDominanceKeep`. Units 3660/3660.

Held-out A/B `ab4/` (ONE pooled batch, 2016 jobs, arms innermost, 32 threads, 10m05s, max RSS 6.4 GB; binary
`mtg.p7f`; base = both levers 0 = step 1's play). Fresh seeds.
| cell | base avg | narrow avg | better / worse | net | units/game base -> narrow | same digest |
|---|---|---|---|---|---|---|
| d5 b20 x300 (161001+gi) | 5.6233 | 5.6233 | 2 / 1 | 0 | 122.8K -> 121.2K (-1.3%) | 289/300 |
| d3 b10 x150 (162001+gi) | 5.6467 | 5.6400 | 1 / 0 | -1 | 97.1K -> 97.8K (+0.7%) | 144/150 |
| d1 b1 x100 (163001+gi) | 5.8200 | 5.8300 | 0 / 1 | +1 | 33.5K -> 32.2K (-3.9%) | 97/100 |
| d1 b3 x100 (164001+gi) | 5.5700 | 5.5700 | 0 / 0 | 0 | 41.4K -> 39.2K (-5.3%) | 99/100 |
| d0 x300 (165001+gi) | 6.8833 | 6.8833 | 0 / 0 | 0 | -- | 300/300 |
| **held-out searched (650)** | | | **3 / 2** | **0** | | |
| starvation st111 x7 | 6.5714 | 6.5714 | 0 / 0 | 0 | 306.9K -> 303.2K | 7/7 |
| sweep 61001 x3 | 5.3333 | 5.3333 | 0 / 0 | 0 | 77.3K -> 74.5K | 3/3 |
b0 (gi0-4 of 30001/30021/30041/30061, abandon 10M): d3 20/20 finish both arms, finished units 43.1M -> **38.5M
(-10.8%)**; d5 15/20 both arms (same 5 abandons: 30001 gi0, 30025 gi4, 30061 gi0 mulligans; 30042 gi1, 30065 gi4),
finished units 15.9M -> **14.0M (-11.9%)**; every finished game identical digest on both arms.
d8 b0 (abandon 50M, `d8g/`) on both slower games, both arms:
| game (repro) | base b | narrow b | d8 b0 base = narrow | units d8 base -> narrow | reading |
|---|---|---|---|---|---|
| d1b1 gi58 (`--seed 163059 --game-index 58`) | 5 | 6 | 5 (same digest) | 392K -> 144K | recovers to base's budgeted turn |
| d5b20 gi254 (`--seed 161255 --game-index 254`) | 7 | 9 | 8 (same digest) | 2.08M -> 1.80M | = base d8 b0; base's b20 T7 beat its own d8 b0 |
Net 0 <= 0 (the bar: "0 is neutral"), sound, cheaper everywhere but d3b10 (+0.7%, inside noise): ships ON.

### Step 5: re-census of 30042 gi1 / 30065 gi4 on fc204f2f8 (d5 b0, abandon 20M, ONE game per process, `--threads 1`; `cen4a/`, `cen4b/`)
| game | fold | turn | units | wall (1 thread) | us/unit | where the units go |
|---|---|---|---|---|---|---|
| 30042 gi1 | **live now** (Heap Gate in hand was a blanket veto) | T1 **commits** (P7b: abandoned on T1) | 12.08M | 93 s | 7.7 | greedy_walk 63%, rollout_step 12%, greedy_fallback 12% -> **87% beyond-horizon playouts**; la_cand + la_bp_wave 7.5%; lad_warm 11.5M vs commit 38K |
| 30042 gi1 | live | T2 (abandoned at the 20M total) | 7.92M | 247 s | **31** | fs_bp_wave 6.85M (87%): interior breakpoint waves; 12.9M plans emitted, odometer 25.6M, every plan dedup-distinct, newonly drops 25% |
| 30065 gi4 | **live now** (Gateway Plaza in hand was a blanket veto) | T1 (abandoned) | 20M | 238 s | 11.9 | greedy_walk 70%, la_bp_wave 9%, rollout_step 7%, greedy_fallback 7%; idwaste 18.8M (the aborted pass = the abandon itself) |
[cast-dom]: 30042 dropped 29K cast + 6K land, kept_offmain_mana 304K; 30065 dropped 373K + 242K, kept 2.04M. (kept counts
every candidate the keep saved, with or without a dominating sibling, so it over-states what the keep blocks.)
Readings: (1) The kept 7s are still dominated by horizon playouts at turn 1 (70-87% of units: ~10 subsets walked per
rollout call), with low reuse (rollout-game-cache 40% hits, m1-rollout-memo 0.3%). That residual is PER-NODE playout
cost, not branching: the next move there is profiling the playout's greedy subset walk (`greedy_walk`). (2) 30042's
turn 2 is the next BRANCHING target: a 2-land turn whose depth-5 subtree emits 12.9M plans through breakpoint waves,
at 4x the per-unit cost of turn 1. (3) Sound refinement (c), not built: Heap Gate's and Gateway Plaza's keeps only
need to block fold A -- fold B's twin (P + a land, same actions) holds every source P holds plus the land, so it pays
the Treasure / Plaza at least as well. The keep returns one answer for both folds today.
