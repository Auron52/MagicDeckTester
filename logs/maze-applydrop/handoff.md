# Maze's End APPLY-DROP triage: handoff (wip/maze-applydrop, 2026-11-24)

Branch `wip/maze-applydrop` (worktree `.claude/worktrees/maze-applydrop`), based on origin/phase-1-2-deck-analyzer
`20b9994d5`. The input was gate `claude-queue-20261121a`: 5000 games, src `0fc791485b17`, report
`.claude/worktrees/batch-1121/logs/mana_clean/claude-queue-20261121a/report.md`. Its full flag lines are in
`build/wt/batch-1121/scratch/mana_clean/claude-queue-20261121a/on.*.err`. The extract is
`build/scratch/claude_maze_applydrop/ad.txt` (1928 lines, 535 (seed, turn) decisions).

## Families (by the oracle's paying line; need-later = the lost cast was not declared in the plan)

| family (the oracle's line pays through) | flags | decisions | need-later |
|---|---|---|---|
| Baldur's Gate FED mode (`Baldur's Gate#7:<colour>`) | 1373 | 413 | 1209 |
| a colour-locked CLB Gate / Thran Portal (Cliffgate, Citadel, Manor, Sea, Black Dragon Gate) | 421 | 113 | 388 |
| Heap Gate / Planar Nexus filter (incl. Treasure-vs-filter, s30009745 T4; payer miss depth0 s30006553 T4) | 103 | 24 | 38 |
| other | 31 | 8 | 27 |

Dropped card: Guild Summit 643, Sylvan Scrying 406, Exploration 253, Archdruid's Charm 198, Burgeoning 183, Manabond 150.
Mechanisms, from 17 repros (`build/scratch/claude_maze_applydrop/land/`, MTG_MANA_ORACLE=3 + ONLY_TURN, gate binary):
1. **RANK READS THE DEFINITION (fixed, fix 1).** `ManaSourceRank` read an etb_choose_color land's DEFINITION, which
   lists all five colours, so the land ranked rainbow (50). A Cliffgate locked to R/G is really a dual (20). Gond Gate
   ({C} + the Gates' colours) ranked 40, so it paid the {G} first. s30006540 T3: Gond paid Sylvan Scrying's {G}
   instead of the Cliffgate, and the continuation's Guild Summit {U} stranded: only Gond makes {U}, once Heap Gate
   is in. Also seen in s30008041 T3 and s30005399 T3.
2. **NEED-LATER via Baldur's Gate fed mode (partly addressed, fix 2, default OFF).** A breakpoint continuation (a
   Scrying/Charm search, Summit draws, an Exploration drop) casts a card that was already in hand. The payment before
   the breakpoint spent the colour that card needs, and often only Baldur's Gate fed by two other sources pays both.
   Example s30006137 T3. The payer cannot see the continuation, so the only routes are a deck hold-up (the USER's
   Fluctuator precedent, mechanism 4 in `docs/design/mana-completeness-oracle.md` 13.14) or a search-level fallback.
   Some cases need a fed-mode surplus taken on a bill whose generic is under 3 (s30005936 T5: Summit's {2}{U} plain,
   then Exploration short). MTG_SCALED_FED_FLOAT only fires at generic >= 3, and holding out one source does not reach
   that payment. NOT addressed.
3. Pain-last vs colour (s30008269 T3): Thran Portal's 1 life is kept, and Manor Gate's only {U} is spent on
   Exploration. NOT addressed.
4. Summit ETB tap set / Treasure (s30005323, s30009745): pay-first territory (wip/maze-summit).

## Commits

- `9093dc8ea` WIP: both fixes plus the unit tests, scripts and A/B generator.
- (next commit) hold-up U/G pip fix and this handoff. See `git log`.

Fix 1, **MTG_RANK_OBJECT_COLORS** (heurarm RANK_OBJECT_COLORS), **default ON**, a correctness fix of the rank input
(the coordinator agreed 2026-11-24). `RankObjectScope` (DecisionProvider.h) is opened at every rank site that holds
the permanent: the greedy, plain-tier filter, filter feed, SpellEffects sac-land pick, TurnSolver's two sites, and both
EtbTapSubtypeOrder sites (Guild Summit's which-Gates order). `ManaSourceRankBase` then reads `EffectiveProducesFor` for
a colour-locked etb_choose_color object. Decks it can move: Maze's End (6 such lands) and Snow (Coldsteel Heart). Not
done: other per-object colour cases (an islandized land, an empty mana-counter land). They are a possible follow-up.

Fix 2, **MTG_MAZE_HOLD_UP** (heurarm MAZE_HOLD_UP), **default OFF**. It is a payment PREFERENCE, so it is heuristic.
`MazesEndProvider::PaymentHoldUp` returns the costs of the hand's spells, deduplicated, cheapest first; Gates Ablaze
is left out. `ProviderHoldUpTargets` now also holds {U}/{G} pips, even when the bill carries that colour. That is
byte-identical for the existing B/R/W holds (Fluctuator, Prevent Damage); the Gods green hold is default OFF.

## Measurements (decision slice: `build/scratch/claude_maze_applydrop/slice.py`)

The slice re-runs the top-100 flagged (seed, turn) decisions of the gate, which covered 1007 of the 1928 flag lines.
It uses MTG_MANA_ORACLE=2 with MTG_MANA_ORACLE_ONLY_TURN=t, one batch per turn, and per-job heurarm arms, on the
branch's Release binary. Caveat: with a fix ON, play before turn t can differ.

| arm | APPLY-DROP | games better / worse (of 78 with any) | APPLY-DROP-ORDER | ENUM-GAP | ORACLE-GAP |
|---|---|---|---|---|---|
| base (both off) | 674 | - | 17 | 14 | 13 |
| rank (fix 1) | 575 (-15%) | 14 / 2 | 17 | 14 | 13 |
| hold v1 (fix 1 + 2, with the U/G-zeroing bug) | 522 (-23%) | 23 / 3 | 16 | 14 | 17 |
| hold v2 (U/G fix) | see `build/scratch/claude_maze_applydrop/fix2/summary.json` | | | | |

**Pay-first** (frozen `build/scratch/claude_maze_summit/b2/mtg`, MTG_MAZE_SUMMIT_PF_ALL3=1, PAY_FIRST off vs on, top-60
decisions): APPLY-DROP 565 -> 529, 8 games better and 0 worse. It fully removes s6540 (also fixed by fix 1) and s9745
(not fixed by mine). On the same top-60, fix 1 removed 90 flags in 12 games and fix 2 v1 removed 125 in 16 games.
Only 4 of those games overlap with pay-first: 5323, 5399 and 6540 are fixed fully by fix 1 and only partly or fully
by pay-first, and 7959 by both. So pay-first does not cover fix 1's family. The b2 baseline differs from the landed
binary (565 vs 503 on the same decisions).

## Unit tests

`test/unit/test_mazes_end_rank_object.cpp`: the rank-object ON / OFF / scope cases pass, and the F6 tests pass. The
hold-up ON case FAILED on the first build because of the U/G zeroing bug, now fixed. Re-run results:
`build/scratch/claude_maze_applydrop/units2.log`.

## A/B (for the coordinator, under the box lock)

Generator: `logs/maze-applydrop/mk_ab.py`. The manifest is already built at `build/scratch/claude_maze_applydrop/ab.json`
(37,500 jobs). Rows: Maze's End at its play setting (d5 b20, default resolution) x 2500, 2HG x 2000, d0 x 4000, each
with arms base / rank / hold; Snow at its play setting x 2000 and d0 x 4000, each with arms base / rank. The seeds are
fresh: 25200000 / 26200000 / 27200000 for Maze's End and 25300000 / 27300000 for Snow. Run it with
`build/Release/mtg --batch ab.json --threads <nproc>` from the worktree, after rebuilding on the final commit.

## Remaining, in order

1. Confirm the units2 / fix2 results (chain3: `build/scratch/claude_maze_applydrop/chain3.sh`, markers `chain3.DONE` /
   `chain3.STOPPED`).
2. The coordinator runs the A/B. Fix 1 is default ON and needs the usual play-change validation: smoke and regression
   GT accept, plus any slower game recovering at d8 b0. Fix 2 needs the USER's call after its A/B.
3. Family 2's fed-surplus-at-generic<3 case, family 3 (pain vs colour), and the filter payer miss (s30006553 T4 depth0)
   are open.

## Rulings in force (verbatim from the brief / coordinator)

- "a fix that makes a legal line payable is correctness -> default ON; ... If a fix is heuristic/lossy, put it behind a
  default-OFF flag and say so."
- Coordinator 2026-11-24: "MTG_RANK_OBJECT_COLORS as a correctness fix, default ON, and MAZE_HOLD_UP default OFF: both
  are fine."
- "never push phase-1-2-deck-analyzer, never use scripts/push_queue.py"; "Do NOT run tiers or A/Bs yourself".
