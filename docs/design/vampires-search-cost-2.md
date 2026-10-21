# Vampires search cost, round 2 (2026-10-21)

Goal (USER 2026-10-21): make the Vampires search cheaper by real speedups or sound branch cuts, never by
mulligan settings; the deck is 5.8x the 3x suite-gate reference and is out of the suite until it gets under.
Base commit `1a1cf9b9e`. One section per agent (P per-node CPU, B branch cuts, R pass / overrun structure).

## Agent B -- branch cuts (branch `vampires-cuts-b`, commits `e8f300134` code + this doc)

### B.1 Where the units are

Units are deterministic; wall on this shared box is not a measurement. Per-decision census
(`MTG_TURN_CENSUS`) on 2HG d5 s2002 gi22 (seed 2024), the worst row's typical game:

- **99% of the units are mulligan-bottoming trial games** (`probe=1`), and inside them the **second main of
  turns 1-3** (`FullSearchLineHybrid`, `pre=0`): T3 55%, T2 25%, T1 15%.
- By unit site: `la_cand` 51% (one unit per candidate the lookahead scores), `rollout_step` 20%,
  `greedy_fallback` 18% (the depth-0 greedy solve inside a playout), the rest < 5% each.
- **Almost all `la_cand` candidates are scored INSIDE a playout.** The horizon rollout plays every
  future turn with a 1-ply `SolveWithLookahead` (the engine default `search_leaf_depth` = 1), which scores
  every candidate with a nested greedy playout. `MTG_DEDUP_CENSUS` on the 2HG d5 row reads the breakpoint
  wave-0 arms as in-playout 444,500 vs searched 87.

`MTG_VAMP_CAND_CENSUS` on 2HG gi29 (no breakpoint variants in that game): main 2 plain 28.6%, main 1 plain
20.7%, main 1 convoke 13.8%, main 2 Blood 12.2%, main 1 Blood 7.4%, every convoke class together 29.5%.
11% of the charged candidates land on a post-apply state an earlier sibling already reached
(`dup_rate 0.111`). Those duplicates are not breakpoint arms (`dup_bp=0`), so the only live post-apply skip,
the breakpoint one, never sees them.

### B.2 `MTG_BP_PO_W0_FOLD` -- the playout wave-0 identity (sound identity fold, DEFAULT ON)

This is ledger question 41's EMPTY-arm collapse, built for the case where it provably holds: inside a playout.

*Mechanism.* Both breakpoint canon defaults stand down inside a playout (`BpUnbranchedCanon` requires
`g_rollout_nest == 0`), so there a BASE plan resolves every breakpoint it reaches to the unconditional EMPTY
fallback. Each breakpoint-opening base plan still got its wave-0 arms (rank 0, rank 1, EMPTY, CHAIN, ...).
On the 2HG d5 row they re-applied the base's own line: EMPTY 100%, CHAIN 98.6%, rank 74% of applies landed
on a state a sibling had already reached. The post-apply dedup then threw each one away, after paying a
unit, a `GameState` copy and an `ApplyPlanDirect`.

*The fold* (`src/ai/TurnSolver.cpp`, namespace `pofold` beside `w0collapse`; host in `SolveWithLookahead`'s
candidate loop, `po_here`). The base plan's apply is MEASURED for the three control-flow asymmetries a
`bp_choice` carries in `ApplyPlanDirect`: (1) the node partition (`node_owns_site` holds only for a
variant); (2) site 9, whose gate short-circuits on `bp_choice >= 0`; (3) site 8's look continuation, which
branches on `bp_searched_plan`'s return value. It also checks, defensively, that every occurrence the base
resolved was resolved by the fallback. Rank 0's apply measures the targeted continuation list's length `n0`
and its chainable indices. Then, before the unit charge and the apply, a variant whose family is clean is
declined when it provably re-applies a twin:

- the EMPTY arm (it is the base);
- rank k >= n0 at bp_at 0 (it overruns onto the fallback);
- the chain slot j when the apply reached exactly one class-on occurrence and the list has fewer than
  j+1 chainable continuations (EMPTY again);
- the chain slot whose j-th continuation is a rank < W whose own apply reached one class-on occurrence
  (it IS that rank).

What the skip leaves behind is what the post-apply dedup's skip left: the candidate is still counted for
the overrun guard, and a rank variant's two records (`BpWaveWalker::W0Len`, the uniform collapse's class-on
count) are written as its twin's. "No evidence, no skip": a family the memo knows nothing about is never
declined. Searched applies (`g_rollout_nest == 0`) are untouched. `MTG_BP_PO_W0_FOLD=0` restores every
apply (heurarm slot `BP_PO_W0_FOLD`). `MTG_BP_PO_W0_VERIFY=1` applies every arm the fold would decline and
compares its post-apply dedup key with the twin's; play is then the fold-off play.

*Soundness evidence.*
- VERIFY: 296,414 declines ok / **0 MISMATCH** (2HG d5 gi22); 280,365 ok / **0 MISMATCH** (std d5 s3003 gi1).
- Unit tests `test/unit/test_po_w0_fold.cpp`: the decision table (every arm, every "no evidence" case, the
  asymmetric family, bp_at 1, uniform, demoted, chain past W, a rank that saw two occurrences), and the real
  apply on a Pit Fighter draw, an empty continuation list included.
- The benchmark below: every row's play digest identical to the baseline, 600 / 600 win turns identical.

*Benchmark* (the six searched regression rows, one pooled batch, fold ON vs `=0`; the `=0` arm reproduces
the brief's baseline to the unit on every row):

| row | units/game off -> on | change | digest | win turns |
|---|---|---|---|---|
| 2HG d5 s2002 x50 (worst) | 248,047 -> 236,956 | **-4.5%** | identical | 50/50 same |
| 2HG d3 s2002 x100 | 140,298 -> 132,967 | -5.2% | identical | 100/100 same |
| d5 s3003 x75 | 124,147 -> 123,275 | -0.7% | identical | 75/75 same |
| d5 s2002 x75 | 82,078 -> 82,827 | +0.9% | identical | 75/75 same |
| d3 s3003 x150 | 71,370 -> 67,358 | -5.6% | identical | 150/150 same |
| d3 s2002 x150 | 57,609 -> 54,778 | -4.9% | identical | 150/150 same |
| all six | 61,245,932 -> 58,922,423 | **-3.8%** | | 600/600 same |

*Why one row went UP.* The per-decision census on gi22 shows the mechanism. The fold makes four decisions
(turns 1-4 of trial games) cheap enough that the ladder's CLIMB starts a deeper pass the fold-off arm did not
start. That pass costs up to 369K units against an 18K budget, because most of its work is in-playout
scoring that the decision budget does not bill, so the climb's estimate cannot see it. The game's units go
1.52M -> 1.69M with identical play. This is freed budget re-spent on depth (CLAUDE.md: a collapse is judged on
work removed, not on wall). The size of the climbed pass is a pass / overrun question, handed to agent R
(repro: `MTG_START_LIFE=30 MTG_OPPONENT_HEADS=2 MTG_TURN_CENSUS=<f> mtg decks/Vampires/Vampire.cod --profile
<bench>/Vampire.profile.json --seed 2024 --game-index 22 --games 1 --depth 5 --budget-ms 20`, census rows
64-67 with the fold on vs `MTG_BP_PO_W0_FOLD=0`).

*Other decks.* Engine-wide breakpoint code, but only inside playouts and only for a base plan that opens a
breakpoint variant family. See B.5 for the smoke tier.

### B.3 Measured for the USER, not adopted: `search_leaf_depth: 0` (a settings lever, changes play)

Landfall and Melira Pod ship `"search_leaf_depth": 0`. The horizon rollout then plays every simulated
turn greedily instead of with a 1-ply lookahead that scores every candidate with a nested greedy playout.
Landfall's held-out A/B was -0.141 turns at 0.47-0.65x units. This is NOT a sound cut: it changes the leaf
evaluator at every budget, so it is out of scope for this round's own changes and is put to the USER.
Measured on the six searched regression rows (one pooled batch, fold ON; per-job profiles carrying only
the added key; per-game win turns vs the 1a1cf9b9e baseline):

| row | baseline units/game | leaf 0 | leaf 0 + first turn 1 |
|---|---|---|---|
| 2HG d5 s2002 x50 | 248,047 | 225,084 (-9.3%), 50/50 same | 478,186 (+93%) |
| 2HG d3 s2002 x100 | 140,298 | 102,702 (-26.8%), 1 slower (gi79 4->5) | 285,399 (+103%), 1 slower |
| d5 s3003 x75 | 124,147 | 120,467 (-3.0%), 1 faster (gi12 5->4) | 200,831 (+62%), 1 slower |
| d5 s2002 x75 | 82,078 | 79,912 (-2.6%), 75/75 same | 154,567 (+88%) |
| d3 s3003 x150 | 71,370 | 57,187 (-19.9%), 2 slower (gi62, gi73 4->5), 1 faster (gi12 5->4) | 155,364 (+118%), 2 slower |
| d3 s2002 x150 | 57,609 | 39,042 (-32.2%), 150/150 same | 100,002 (+74%) |
| all six | 61,245,932 | 50,987,161 (**-16.8%**; -13.5% vs the fold alone), net +1 turn / 600 games | 117,408,929 (+92%) |

Read with care: the d5 rows are BUDGET-BOUND. A cheaper leaf lets the ladder search more leaves inside the
same billed budget, so the d5 units barely move. The d3 rows, whose tree is depth-capped, show the leaf's own
saving. Leaf 0 + first-turn 1 costs MORE, not less (the outermost rollout's searched first ply, at every leaf).

**CPU on the gate row, same batch** (2HG d5 s2002 x50, default vs leaf 0, both arms interleaved in one
`--batch` while this agent held the box lock; `ms` is summed per-game wall): default 552,775 ms
(11.1 s/game, 11,625,913 units) vs leaf 0 325,024 ms (6.5 s/game, 11,254,206 units) = **0.59x the wall at
-3.2% units**, 50/50 win turns identical. A leaf-0 unit is far cheaper than a 1-ply-leaf unit, which units
alone hide. This is the largest lever found in this lane, and it is the USER's: it changes play, so adopting
it needs a held-out A/B (Landfall's was -0.141 turns). It was NOT adopted here.

### B.4 `MTG_BP_PO_W0_BASE_LEN` -- the measured base reads its first continuation list (sound, DEFAULT ON)

`MTG_DUP_DIFF` + `MTG_DEDUP_CENSUS` on 20 games of the worst row with the fold ON (`build/scratch/vampires/
cuts-b/dd1`): 10.2% of the 1.72M charged candidates were still post-apply duplicates. The biggest class was
in-playout rank 0 landing on its OWN base's state, 87,644 of 121,016 (72%). The fold applies rank 0 because
rank 0 is what measures the targeted list. When that list is EMPTY, rank 0 overruns onto the fallback,
which is the base's own line.

The measured base reaches the same occurrence first, at the same state (identical statements up to it is
what a clean family means). So it now reads the list there: `BpEnumEntryFor` by reference, sizes only,
memoized on state. When the list is not empty, rank 0's own read is the memo hit this read paid for.
`pofold::AdoptBaseLen` uses only the EMPTY case: n0 = 0 and no chainable index, which is exactly what rank
0 would have measured. Rank 0 then becomes a kRank twin, and the whole family collapses onto the base
without rank 0's apply. A non-empty list is left to rank 0, whose chainable-index walk the chain arm needs.
If the read moved the class-on counter, the family is marked asymmetric (no skip). Heurarm slot
`BP_PO_W0_BASE_LEN`; `=0` leaves rank 0 as the measurer (the control arm reproduces the fold-only binary's
units and digest exactly).

*Evidence.* VERIFY with the read ON: 354,419 declines ok / **0 MISMATCH** (2HG d5 gi22; the read collapsed
58,005 families), 336,442 / **0 MISMATCH** (std d5 s3003 gi1; 56,077). Unit tests: the empty-list family
now declines rank 0 as well, the non-empty family's read equals rank 0's, and `=0` restores the measurer.
`mtg-test` 1662/1662.

*Benchmark* (one pooled batch, the six searched rows, fold + read ON; per-game win turns vs the 1a1cf9b9e
baseline):

| row | baseline | fold only | fold + read | win turns |
|---|---|---|---|---|
| 2HG d5 s2002 x50 (worst) | 248,047 | 236,956 | **232,518 (-6.3%)** | 50/50 same, digest identical |
| 2HG d3 s2002 x100 | 140,298 | 132,967 | 131,238 (-6.5%) | 100/100 same, digest identical |
| d5 s3003 x75 | 124,147 | 123,275 | 121,635 (-2.0%) | 75/75 same, digest identical |
| d5 s2002 x75 | 82,078 | 82,827 | 83,888 (+2.2%) | 75/75 same, digest identical |
| d3 s3003 x150 | 71,370 | 67,358 | 66,619 (-6.7%) | 150/150 same; ONE play hash moved (gi133, T5 both) |
| d3 s2002 x150 | 57,609 | 54,778 | 54,511 (-5.4%) | 150/150 same, digest identical |
| all six | 61,245,932 | 58,922,423 (-3.8%) | **58,333,499 (-4.8%)** | 600/600 same |

d5 s2002 is up for the same reason as in B.2 (freed work re-spent by the ladder's climb). The one moved line
(d3 s3003 gi133, seed 3136) is checked at a large budget in B.5.

### B.5 Other decks, and the moved games at a large budget

The fold is engine-wide breakpoint code, but it acts only inside playouts, and only for a base plan that
opened a breakpoint variant family. **Smoke tier** (fold + read ON, `THREADS=20 MTG_PGO=0`, never
`--accept`): 126 of 128 configs byte-identical. Two configs moved, with 0 slower games: mirrorwing d3 gi29
6 -> 5 (faster) and gi58 a different line at the same T4; soldiers d3 gi82 a different line at the same T4.
Every other deck's play is unchanged, so the GT re-accept that landing needs covers exactly those two smoke
rows (plus whatever the regression tier shows; it was not run here).

**The three moved smoke games at a large budget** (one pooled batch, single-game chunks, fold ON vs
`MTG_BP_PO_W0_FOLD=0`; `smk/` in this agent's scratch dir):

| game | b10 off | b10 on | b100 and unlimited, BOTH arms | units at unlimited, off -> on |
|---|---|---|---|---|
| mirrorwing d3 gi29 (seed 1030) | T6 | **T5** | T5, digest identical (`fd62edd5`) | 34,960 -> 32,004 (-8.5%) |
| mirrorwing d3 gi58 (seed 1059) | T4 | T4, = the large-budget line | T4, digest identical (`e6010b12`) | 20,504 -> 19,137 (-6.7%) |
| soldiers d3 gi82 (seed 1083) | T4 | T4, another line | T4, digest identical (`6dbb7020`) | 14,800 -> 14,654 (-1.0%) |

At b100 and at unlimited budget the fold is byte-identical in play on all three games, and cheaper. At the
shipped b10 the freed work moved two of the three onto the line a large budget finds. This is the "budget
spent better" case, not a loss.

**The moved Vampires game at a large budget** (d3 s3003 gi133, seed 3136; single-game repros, read ON vs
`MTG_BP_PO_W0_BASE_LEN=0`):

| budget | read ON | read =0 |
|---|---|---|
| b10 (shipped) | T5, a different line | T5 |
| b100 | T4 (1,239,569 units) | T4 (1,094,571) |
| unlimited | T4 (3,064,996 units) | T4 (3,135,484) |

At unlimited budget the two arms run the same search apart from the declined applies: identical enum /
solve / m2 memo hit and miss counts, identical leaf-eval counters, 2.2% fewer units. The b10 line change is
the budget re-pricing a finite budget is allowed (CLAUDE.md: a budget may re-price work, never make a line
unfindable).

### B.6 Measured and NOT changed: `MTG_CAND_DEDUP` (default OFF)

The 11% plain post-apply duplicates of B.1 (`dup_bp=0`) still get a full rollout, because the only live
post-apply skip is the breakpoint one. `MTG_CAND_DEDUP` (left OFF in 2026-09 on wall-clock grounds, before
the USER's 10-01 "collapse unconditionally" ruling) skips exactly those rollouts. It is sound: an identical
state gives an identical rollout, and the later copy cannot take a tie, since the tie-breaks are win turn,
then the leaf grade, then `plan.value` over a value-sorted list. So it was given a heurarm slot (`CAND_DEDUP`)
and pooled into the B.4 batch. Result: **inert on Vampires**, within 21 units of ~58M on every row, every
digest identical. The duplicate candidates' rollouts are already cut short by the tie-prune / branch-and-bound
cutoff (they would need a strictly earlier win than an identical sibling). Nothing to adopt; the slot stays
for future A/Bs.

### B.7 What remains in this lane, and where the cost is

After the fold and the base read, the post-apply duplicates are small: rank k >= 1 twins about 15K, copy
permutations ("SAMEACTS" 35.6K, identical Voldaren Estates 15K), and the uniform arms 1.7K, out of 1.72M
candidates per 20 games. No further identity fold of this kind is worth more than ~1-2%.
The cost that is left is DISTINCT work. Every leaf rollout plays every future turn with a 1-ply lookahead
that scores every candidate with a nested greedy playout (B.1), inside mulligan-bottoming trial games that
are 99% of the units. Sound branch cuts cannot reach a 2x on the worst row from here. The levers that could
are (a) the leaf fidelity (B.3, a USER settings question) and (b) the pass / overrun structure (agent R: a
climbed pass costs 20x its billed budget because in-playout scoring is not billed).

### B.8 Open questions for the USER (work continued on the stated default)

- **B-Q1. The two identity folds ship DEFAULT ON** (`MTG_BP_PO_W0_FOLD`, `MTG_BP_PO_W0_BASE_LEN`). They are
  sound by the measured-asymmetry argument in B.2 and VERIFY's 0 mismatches, and they are engine-wide
  breakpoint code (playouts only). They move two smoke rows (mirrorwing d3: 1 faster, 1 same-score line;
  soldiers d3: 1 same-score line) and one Vampires line (gi133, same win turn). [default: ON; GT for the two
  smoke rows NOT accepted here; the regression tier was not run]
- **B-Q2. `search_leaf_depth: 0` for Vampires** (B.3): 0.59x the wall on the gate row (same batch), -16.8% units
  across the six rows, net +1 turn over 600 regression games (3 slower, 2 faster). It is a play change, like
  Landfall / Melira Pod, which adopted it on held-out A/Bs. [default: NOT adopted; recommended next step: a
  held-out A/B at play settings, standard + 2HG]
- **B-Q3. `MTG_CAND_DEDUP`** measured inert on Vampires (B.6). [default: unchanged, OFF]
