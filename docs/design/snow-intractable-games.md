# Snow's intractable games: a third of the deck's population cannot be played at searched depth

Status: **OPEN**. The Snow value-leaf depth matrix was cancelled at 11% on 2026-09-24 because it
projected ~150 h of wall clock, and the reason is this. Everything below is measured from the
cancelled run's own artifacts; nothing here was re-derived or re-run.

This doc is the POPULATION-level record. The MECHANISM is already root-caused in
`snow-breakpoint-degeneracy.md` (2.95M breakpoint consultations per game from the deck's
card-to-hand engine) and is not repeated here.

## The number

| seed | offsets reached | skipped | intractable |
|---|---:|---:|---:|
| 8008 | 75 | 26 | 34.7% |
| 9009 | 75 | 25 | 33.3% |
| 11011 | 75 | 25 | 33.3% |
| 10010 | 50 | 13 | 26.0% |
| **total** | **275** | **89** | **32.4%** |

"Intractable" = the pool abandoned the game at its per-game ceiling, so it banked no result. It was
32.0% at 200 offsets reached and 32.4% at 275, so this is not a small-sample artifact. The deepest
arm is worse: **H5 alone is 34.5%**.

Overlap across seeds is at chance (25 of 57 offsets intractable in more than one seed, against ~19
expected at p=0.32 over four seeds), so the blow-up is a property of the individual shuffled game,
not of the offset. There is no index arithmetic to fix.

## What it cost

`logs/snowopt/slowgames/` holds the ledger: `observations.tsv` (one row per slow game, with the
repro line the pool printed) and `by_game.tsv` (rolled up per distinct game), beside raw copies of
`slow_games.log`, `matrix.txt.skipped.json`, `matrix.txt.cells.json` and `matrix.log`.

* 1,391 slow-game observations over **227 distinct games**
* **419.6 core-h — 78% of the run's entire CPU spend** (541 core-h total: 271 banked + 270 wasted)
* **64.4% of that was abandoned**, i.e. banked nothing

The multiplier that makes this so expensive is structural: **the same game is attempted once per
cell**, 9-13 times, and abandons in most of them. The worst offenders:

| seed | gi | attempts | abandoned | total | worst | repro |
|---|---:|---:|---:|---:|---:|---|
| 11011 | 7 | 11 | 7 | 15.70 h | 3.50 h | `--seed 11018 --game-index 7 --games 1` |
| 11011 | 26 | 11 | 2 | 11.19 h | 3.36 h | `--seed 11037 --game-index 26 --games 1` |
| 11011 | 42 | 10 | 3 | 9.99 h | 3.50 h | `--seed 11053 --game-index 42 --games 1` |
| 11011 | 10 | 12 | 6 | 9.27 h | 3.50 h | `--seed 11021 --game-index 10 --games 1` |
| 9009 | 34 | 9 | 7 | 8.02 h | 2.00 h | `--seed 9043 --game-index 34 --games 1` |

3.50 h is exactly `max_game_wall_sec`; 2.00 h is the `max_game_predict_sec` cut. Seed 11011's games
pin the hard cap repeatedly while 9009/10010 stop at the predictive one.

**The caps bound a GAME, not the run.** `never_condemn_depth: 5` exempts every H cell from
condemnation by design (`BatchRunner.cpp`), and the skip list makes `target` GROW (`target() =
args.target + len(skiplist)`), so each abandonment both wastes its own wall clock and pushes the
finish line further out.

## Why the matrix cannot simply be made cheaper

The obvious lever is fewer games in the expensive cells -- `valueleaf_depth_matrix.py` even declares
`--hgames-depth D:G` for exactly this, with help text naming the combo-deck case. **It does not
work, for two independent reasons.**

1. The flag is dead code in the incremental path. `args.hgames`, `args.vgames` and `hgames_depth`
   are declared and never read; `target()` uses `args.target` alone.
2. More fundamentally, **400 games/cell is not padding -- it is the minimum that resolves the
   tolerance the derivation uses.** `valueleaf_table_to_metadata.py` reports its own resolution as
   `3/n` over the PAIRED game set: 0.01775 at n=169, against `tol=0.0020`. The full target of
   400 x 4 seeds = 1,600 gives 3/1600 = 0.001875, just under tol. Cutting games does not buy a
   cheaper verdict, it buys no verdict.

So the cost is not a scheduling or configuration defect. It is the sample size the question needs,
multiplied by a population where a third of the draws never terminate.

## What the partial table already says

Paired over the common game set at 11% completion (`trust=UNSET`):

```
H:  5.7799     —     5.7614   5.7733   5.7733
V:  5.9929  5.8692  5.8009  5.8087  5.7733  5.7733  5.7683  5.7683
```

H3 is the best cell in the table and the value leaf never beats it; the derivation flags
`H3->H4 WORSENS by +0.0119` as non-monotonic. This is consistent with the independent 2026-09-20
shape probe (6,000 games: leafless `fit_nl` 1.160x cost, z=+2.35 BETTER play) and with this
pipeline's own phase E (staged model −0.005 at t=−6.5 for 1.25x = a trade, sidecar never installed).

It is **formally inconclusive** at tol=0.0020 -- every measured gap is inside 0.012, well under the
0.01775 resolution. The open question for a human is whether a leaf-necessity verdict needs
0.002-turn precision at all, given three independent measurements all pointing at "the leaf buys
this deck nothing".

## Causes RULED OUT (do not re-investigate)

Beyond the three in `snow-breakpoint-degeneracy.md` (plan-cache ratchet, mana-side enumeration,
prefix-scoped prepay), one more was proposed and killed on 2026-09-23:

* **The cleanup discard is NOT a cost.** Priced with `ShedStats::CostScope` at **0.011%-0.11% of
  CPU** across budgeted play and two unbudgeted tail games (0.160 s of 145.05 s; 0.179 s of
  473.85 s; 0.017 s of 148.85 s). All three searched forms are inert on this deck: the out-of-band
  trial-per-candidate pass is retired by `MTG_DISCARD_NODE`, and the in-search axis needs
  `CleanupDiscardSearchWidth() > 1` against a default of 1.
  The deck DOES shed constantly in branches it does not follow -- 138,331 in-search cleanups against
  1 real shed per 50 games, at hand sizes 8/9/10 -- so a real-play census answers the wrong question
  here. That reach is why the discard RULE was still worth authoring (see
  `SnowProvider::CleanupDiscardCandidates`); it is a play-quality hook, not a performance one.

## What has not been tried

* **A keep table.** Lookahead bottoming is 37-48% of Snow's CPU in shipped play and was 75.7% of one
  tail game from a single decision. Snow is the only suite deck with no exhaustive keep table, so
  every bottoming decision falls through to a clairvoyant rollout. This retires the largest single
  cost AND closes the standing 2026-09-14 directive ("I don't want to use lookahead bottoming for
  anything").
  **STARTED 2026-09-24 15:36 UTC** (`bash scripts/mullgen.sh run decks/Snow fast`, alone on the
  box; log `logs/Snow_mullgen/gen.log`). Two things make this the right lever rather than filler,
  now that the cost model above is known:
  * `gamework::Begin(ceiling)` wraps `engine->RunGame`, and `AIEngine::RunGame` calls `BottomCards`
    INSIDE it -- so **bottoming rollouts are charged against the same `abandon_units` ceiling**.
    Retiring them does not merely save wall, it hands the ceiling back to the search on the ~17.6%
    of games that mulligan, which is the abandonment rate the section above identifies as the only
    quantity the matrix's cost is linear in.
  * Bucket discovery merges NOTHING on this deck -- 17 distinct cards -> 17 classes, nearest
    neighbours 0.015..0.215 against a 0.01 threshold -- so K=17, 175,972 size-7 cells (351,944
    slots) plus 162,004 fused sub-table batches.
  **THE COST, measured not projected: 21 rollouts/s on 32 cores**, i.e. 0.66/s/core against the
  `mulligan-profile.md` guide's ~110/s/core. Snow is ~167x slower per rollout than the model every
  other deck was sized with -- the same intractability this document is about, reappearing in the
  apparatus. The floor pass alone is therefore ~10 h, and generation + sub-tables + the 12-seed x
  500-game validation is a multi-day commitment, not an overnight one. It is journalled per cell and
  resumes on the identical command, so stopping it costs only the live cell.
* **Shipping `leaf: none`.** Would make the V arm's 571 core-h moot and stop phase A ever running
  again. StompySurprise and Goblins already ship this shape; note their sidecars still carry a real
  `eval_model` and a full `value_leaf_table`, so it is a `value_play` setting, not an absent file.
* **Truncate-at-emission**, the named-but-unbuilt fix from `bp-node-partition.md` for the 1,192,267
  cross-bucket duplicate children (~10% of node units).

---

# ROOT CAUSE, measured 2026-09-24

The sections above are the ledger. This is the diagnosis, taken after the USER asked for the cause
to be root-sourced with profiling rather than inferred. It is three stacked mechanisms, none of
which is "Snow is a hard deck to play" -- the intractable games are ordinary turn-6 and turn-7 wins.

**Tooling correction first, because it is why nobody had profiled this.** `perf` is NOT broken in
this container. It fails *only* when writing `perf.data` onto the repo bind mount
(`/workspaces/...`), which returns EFAULT -- "failed to write perf data, error: Bad address" --
before the workload starts. The same command with `-o /tmp/...` records and symbolises fine.

## 1. Mana payment degrades superlinearly in the number of SOURCES (~50% of runtime)

Flat profile of s11011 gi26 (d3, unbudgeted, the ledger's 2nd-worst game): `TapForCostBacktrackWorker`
and its lambda are **28.7% of all samples**, and the wider payment family (`TapForCostShared*`,
`ColorFeasibility::Payable`, `SourceMaxNetLive`, `PermanentManaYield`, `ManaPool::CanPayFlat/Add`,
the `TapBacktrackMemoHash` table) is **~50%**.

| | control game (9 perms, 7 sources) | gi26 (18 perms, 13 sources) |
|---|---|---|
| payment questions | 10,410 | 1,381,215 |
| backtracker nodes per question | **3.2** | **251.5** |
| ...on *payable* questions | 4.2 | **367.6** |
| greedy solves it outright | 99.9% | 85.0% |
| total backtracker nodes | 32,969 | **347,351,346** |

UNPAYABLE costs reject in 1.0 nodes on BOTH boards. So this is not hard mana -- the USER's point,
and the data agrees: every one of those nodes is spent finding an answer that exists and is easy.

**Partly fixed (650cc8ba).** `MTG_MANA_CACHE_CANON` now defaults ON: nodes 347.4M -> 141.3M (-59%),
hit rate 80.5% -> 90.3%, 86.2s -> 69.3s, byte-identical. Levers TESTED AND REFUTED here, do not
retry blind: `MTG_FLOW_ORDER` (-4.8% nodes, -2.4% wall -- not the 13.6x it gave elsewhere) and
`MTG_TAP_TRIM` (inert). The remaining term is question VOLUME (684k vs 10k), i.e. search breadth.

## 2. The breakpoint wave is EXHAUSTIVE under an unlimited budget

`BpWavesHere` (TurnSolver.cpp) returns true unconditionally when `budget->Unlimited()`, and the wave
loop's only exit is `!Unlimited() && Exhausted()`. `BpWaveMode`'s own comment: *"MTG_BP_WAVES=1
DEFAULT: waves run while the node's budget allows; unlimited => exhaustive."* The matrix runs every
cell at `budget_ms: 0`. Snow's SHIPPED settings never do -- `regression_cases.sh` is d3/b10 and
d5/b20 -- and the wave's adoption (abdecb42) measured its gains entirely on budgeted tiers.

`MTG_BP_WAVE_PROBE` on gi26: **`scored=5,128,800` continuation variants, `improved=2`,
`max-rank=93`, `budget-stopped=0`.** Switching waves off is byte-identical on **82 of 82** games
(61 control at d3, 20 at d5, gi26) while running 1.26x-4.7x faster. On the ten worst ledger games:
all ten FINISH (three of them abandon at a 2400 s cap shipped), 7/7 comparable digests identical,
and the set costs 2,474 s against >=15,010 s.

**The fix is NOT a cap.** USER, 2026-09-24: *"It seems to be just another way to budget something
that is intended to be unbudgeted by construction. Our job is to not budget and make the vast
majority affordable and leave only a few special cases to be dropped by the bounds."* Correct --
a ceiling under `Unlimited()` silently redefines the regime. `MTG_BP_WAVES=0` is also not shippable
for a second reason: `FSLineWin` returns the first in-horizon win it finds, sound only if the
shallower passes were complete, and the unlimited-budget wave is what makes them complete.

**The direction instead: prove more variants REDUNDANT and drop them losslessly**, the way
`w0_nobp` (12.4M skipped), `w0_unif_collapse` (10.5M) and `bp_newonly` (56.7% dropped) already do.
`improved=2` out of 5.1M says the ranking is already putting good lines early, so the deep ranks are
near-pure duplication rather than merely low value.

### Where the wave's cost actually is (corrected 2026-09-24, on the rebased engine)

The first thread taken was the probe's `stillborn` / `first-empty` line -- variants resolving to
EMPTY, which looked like work producing nothing. **Both that thread and the probe's own cost split
were wrong, and the probe was wrong first.** `[bp-waves]` printed `retired` + `dupstate` as THE
split of the scored-minus-rolled gap while incrementing them at only ONE of the two wave-scoring
loops, so on gi26 they accounted for 0.5% of the gap they were printed next to (4,483 + 13,820
against 3,438,085). The tell was an impossibility, not a hunch: `slots_stillborn` is incremented
inside `Walker::Report` and so fires for every walker, and `stillborn=368,059` cannot be a subset of
`retired=4,483`. Fixed in `0e2a10bd`; the gap now closes exactly, and relocates the cost:

| population | gi26 | share of wave applies |
|---|---:|---:|
| duplicate post-apply state (`dupstate`) | 3,069,985 | **57.7%** |
| reached a rollout (`rolled`) | 1,879,016 | 35.3% |
| past-the-end probe (`retired`) | 368,100 | 6.9% |

The **LOOKAHEAD** walker is 99.4% of every wave apply (5,286,396 of 5,317,101). `FSLineWin`'s wave
phase -- which this document treated as the cost centre -- is 0.6%. Attributed:
`dup_cross=1,742,926 (56.8%)`, `dup_w0=701,020 (22.8%)`, `dup_self=626,039 (20.4%)`.

**Two candidate lossless declines are now REFUTED by measurement, not argument.**

* *The EMPTY pre-skip.* The node host (site 3) declines its EMPTY arm whenever the continuation list
  already holds an apply-empty entry -- exact, because reaching `k == n` means every cands index was
  applied. That argument transfers to the walker exactly in the `n == k0` case. But the input does
  not exist here: `g_bp_cands_has_empty` is filled only for node-hosted sites and `BpNodeSites()`
  defaults to `1<<3`, while Snow's is site 8. `MTG_BP_EMPTY_CENSUS` (added in `0e2a10bd`) widens the
  scan: **0 of 1,330,535 lists** hold an apply-empty entry, across all three Snow sites. So the
  368,058 first-empties are genuinely distinct lines -- "a line no other rank produces", as
  `BpProbe`'s own comment says -- and there is no decline to take.
* *Pre-apply dedup on plan identity.* `MTG_ROLLOUT_STATS`' list-fingerprint census on gi26:
  `lists=1,330,535 entries=5,941,057 distinct=5,941,057 duplicate=0 (0.0%)`. The lists are not
  internally redundant by plan, so nothing is declinable on the safe "same plan, same state"
  inference.

Together those say the 3.07M duplicates are **different plans converging on the same state** -- the
inference the codebase has twice refuted as unsound to assume, but which is here the measured
majority of all wave work.

A third candidate died the same way. `dup_cross` splits `sameb=261,495 / diffb=1,475,976`, so 85% of
cross-slot duplicates are DIFFERENT BASE PLANS, not the nested `bp_at` axis. That suggested keying
the wave's prefix cache on the breakpoint STATE rather than the slot, since the enum memo's premise
is that one breakpoint state yields one cands list. Measured: `prefix-snaps=323,293
state-dup=43,242` -- only **13.4%** of prefixes repeat a state. The converging plans diverge at the
breakpoint and only meet at end of turn, so a state-keyed prefix cache does not reach them.

### The activation-source fold is never applied where Snow's width is (2026-09-24)

This supersedes the "zero degeneracy" reading in the retraction below. That census was run over a
cell whose population is dominated by games that finish cheaply; **on a game the work ceiling
discards the answer is completely different**, and those games are 55.3% of the cell's units.

Measured on H5 seed 8008, at the wave node, base plans only:

| game | plans | distinct | src-blind-distinct | degenerate |
|---|---:|---:|---:|---:|
| g33 (completes, 130 s) | 462,216 | 462,216 | 447,204 | **3.2%** |
| g26 (abandoned, 782 s) | 2,712,039 | 2,712,039 | 2,207,842 | **18.6%** |
| chunk off25, all 12 pooled | 19,441,343 | 19,441,343 | 18,135,223 | 6.7% |

So ~504,000 base plans per degenerate game differ from another base plan **only in which physical
permanent they touch**, and each one costs a whole subtree (its `bp_at` slots x their ranks x the
nested wave). Four measurements then narrow that to a single line of code:

1. **WHICH FIELD.** `BpCandFingerprint` takes a blind mask (`kBlindSac` / `kBlindHand`), so the two
   physical-identity axes can be counted separately instead of as one "source-blind" aggregate:

       node plans total=2712039 distinct=2712039 src-blind-distinct=2207842
                                                 sac-blind=2207842 hand-blind=2712039

   `hand-blind == total` and `sac-blind == src-blind`: **100% of it is `sac_source_id`, 0% is
   `hand_index`.** The hand-cast half of the fold (`MTG_FOLD_HAND_CASTS`) leaves nothing on the
   table; the activation half leaks all of it.

2. **NOT THE PREDICATE.** `MTG_FOLD_REFUSE_CENSUS` attributes every `PermIsPlainForFold` outcome to
   its clause. Over the whole chunk: **99,555,664 calls, 100.0% PLAIN, zero refusals.** So the
   "relax a clause and move the distinction into the tag" idea -- the shape that worked for spore
   counters -- has nothing to relax here. Coldsteel Heart's locked colour and Rimefeather Owl's ice
   counters were both plausible and are both innocent: those permanents are not the ones being
   enumerated.

3. **NOT CLASS VALIDATION.** `MTG_BF_CENSUS`: of 395,485,050 tagged actions, `drop_sig=0` (no class
   ever failed the field-identity condition) and `drop_src=1,157,008` (0.29%). The classes form and
   they are valid. 69.5% are singletons, which is not a leak.

4. **THE GUARD IS NOT CONSULTED.** The canonical-prefix rule is fenced behind `foldsel::Take()` --
   "this selection came off an odometer" -- because a hand-CONSTRUCTED line has no twin and
   rejecting it deletes it outright (knights gi497 lost a turn-4 kill exactly that way). The census
   splits the fence by call site:

       bf_foldsite greedy  calls=2,606,886,966  from_odometer=2,606,886,966 (100%)  rejected=909,313,023
       bf_foldsite search  calls=  330,079,681  from_odometer=   67,134,233 (20.3%) rejected= 14,462,840

   **79.7% of the search's selections never reach the fold**, because the search grew its own
   private copy of the subset walk and that copy never set the flag. And the search is exactly where
   Snow's width is: `bf_src` reports `Scrying Sheets activations=38,988,401 distinct_physical_sources=4`
   and `Frost Augur activations=37,586,408 distinct_physical_sources=3` -- the top two action kinds
   by more than 2x over anything else.

`MTG_FOLD_SEARCH_ODO` is the one-line lever for precisely that gap and is **already in the tree at
default OFF**, with a set-site comment that states the consequence outright: *"turning
MTG_FOLD_ACT_SOURCES off leaves the search's candidate widths BYTE-IDENTICAL (62.2844 either way)
... the deduplication everyone assumes is deduplicating Snow's four Scrying Sheets has never once
been applied where those widths are counted."* Its own stated adoption bar is `MTG_FOLD_VERIFY`,
which builds each rejected selection's canonical twin and counts `UNRECOVERABLE` -- a runtime check,
not an argument.

#### What arming it does (paired A/B, chunk off25, both arms in one pooled batch)

| | control | armed |
|---|---|---|
| completing games, digest | -- | **5 of 5 byte-identical** |
| `g26` (the 782 s game above) | `played=0` (discarded) | **`played=1 avg=7.0000`** |
| chunk wall | 5,177 s | 5,182 s (**1.001x**) |

**The wall is flat and that is the expected result, not a disappointment.** An abandoned game burns
its whole 40M-unit ceiling either way, so the ceiling -- not the search's efficiency -- sets its
wall. What the fold buys is COVERAGE PER UNIT: the same 40M units now reach the end of g26's tree,
so the game completes instead of being discarded. The benefit is therefore denominated in
ABANDONMENTS, not in seconds, and it lands on the 55.3% of the cell that currently banks nothing.

Byte-identical digests on every game that completes is the losslessness evidence: the rule is
removing arrangements whose twin was enumerated beside them, exactly as claimed, and the play it
produces is unchanged.

#### The soundness gate, run (MTG_FOLD_VERIFY, same chunk)

    bf_fold ... recoverable=1,053,475,731  UNRECOVERABLE=0  guard_reject=1,053,475,731
                                                            (greedy=976,672,216 search=76,803,515)
    bf_foldsite search calls=502,269,230 from_odometer=348,662,200 (69.4%) rejected=76,803,515

`VerifyFoldRecoverable` builds the canonical twin of **every** rejected selection and checks it is
itself enumerable. Over 1.05 billion rejections: **zero unrecoverable.** That is the precondition
the set site names, discharged on the population where the rule fires hardest -- search-site
rejections rise 14,462,840 -> 76,803,515 (5.3x) when the flag is armed, so the check is emphatically
not vacuous here.

#### Widened to the whole cell -- and the result reframes the matrix's cost model

49 games (H5 s8008 gi 0..48), both arms in one pooled batch, 98 jobs on 32 cores, 41 min:

| population | games | control | armed | ratio |
|---|---:|---:|---:|---:|
| games that COMPLETE (bank a row) | 32 | 2,240.3 s | 1,982.7 s | **0.885x** |
| games that hit the unit CEILING | 17 | 20,617.0 s | 21,277.4 s | 1.032x |
| whole cell | 49 | 22,857.3 s | 23,260.1 s | 1.018x |

**32 of 32 completing digests byte-identical**, and one ceiling-bound game rescued (`g26`).

The 0.885x is pooled and should not be quoted alone -- it is weighted by `g12` (1,013.1 s ->
807.5 s, 0.797x), which is 45% of the completing total. Over the 18 completing games above 1 s the
**median is 0.961x and 14 of 18 are faster**. Both numbers are the same fact seen twice: the benefit
RISES WITH GAME SIZE, which is what a branching cut should do -- a bigger tree carries more
source-degeneracy for the fold to remove (3.2% on a completing game, 18.6% on a discarded one).

So the lever is an 11.5% lossless speedup on every game that produces a row -- and the cell still
gets 1.8% SLOWER, because the two populations are nothing alike:

* **34.7% of games are ceiling-bound, and they are 90.2% of the cell's wall** (mean 1,212.8 s
  against 70.0 s for a completing game).
* A ceiling-bound game spends exactly `abandon_units` whatever the search does. Making the search
  more efficient therefore cannot make it finish sooner -- it makes it cover MORE TREE in the same
  40M units. The 3.2% is the guard's own per-selection cost, paid against a fixed unit budget.

**THE COST MODEL THIS IMPLIES, which is the important part.** A cell needs `target` COMPLETED games
and `target() = args.target + len(skiplist)`, so at a 34.7% abandonment rate a 400-game cell runs
~612 games: ~212 ceiling burns at ~1,213 s against 400 completions at ~70 s. **~90% of the matrix's
cost is games that bank nothing**, and that share is set by the ABANDONMENT RATE, not by the speed
of the search.

Three consequences, and they should be read before any further optimisation work on this deck:

1. **Wall-clock A/Bs on this cell are actively misleading.** They are dominated by a fixed unit
   budget that is spent in full either way. A change can be a large, provable, lossless win on every
   game that matters and still read as a 1.8% regression. This one did.
2. **The metric to optimise is the ABANDONMENT RATE.** That is the quantity the user's instruction
   ("cut branching or whatever is making this degenerate") actually names, and it is the only one
   the cell's cost is linear in. `MTG_FOLD_SEARCH_ODO` moves it 17/49 -> 16/49, i.e. 5.9% relative:
   real, measured, and an order of magnitude short of what a 12-hour matrix needs.
3. **No search optimisation can save more than ~10% of this cell's wall** while the ceiling stands,
   because that is all the non-ceiling-bound work there is. Halving the cell means halving the
   abandonment rate -- 17 of 49 games have to start finishing.

#### What is still NOT settled: default-ON

Losslessness is not play-neutrality **under a budget**. Units are the budget's currency, so a node
that consumes fewer of them buys the rest of the search more search -- the same trap `LazyLeafOn`
documents and refuses to fire under a limited budget for. The matrix runs `budget_ms: 0`, where the
coupling does not exist; the regression suite does not. So:

* **For the matrix** (unbounded) the lever is sound, lossless, and strictly reduces censoring. It
  can be set per-job from the driver without touching any other deck.
* **For shipped budgeted play** it is a reserved decision: it would move ground truth on every deck
  whose search reaches the odometer, and that needs the full A/B plus a rebaseline, not this chunk.

**One consequence to put in front of the user rather than decide.** A rescued game ENTERS THE
SAMPLE, and the games the ceiling discards are the hard ones, which win late (`g26` at turn 7
against a chunk whose completing games are 5, 5, 5, 5, 6). So arming this does not merely make the
cell cheaper -- it makes the cell's mean win turn less censored, and therefore different. Every
already-banked row was measured under the old censoring. Per the standing rule, an optimisation that
collects the SAME data more cheaply is adoptable on evidence, but one that changes WHICH data is
collected is the user's call. This is the second kind. It is also, on the merits, a bias being
removed rather than introduced: today H5 silently drops its 12 hardest games while a shallower arm
drops fewer, so the depth comparison the matrix exists to make is already being taken across
different populations.

### RETRACTED: the "unfolded physical-source axis" (2026-09-24)

An earlier revision of this section reported that 41.3% of the wave's duplicates
(`dup src-blind same=975,749`) were physical-source variants -- distinct plans differing only in
which interchangeable permanent they touch -- and named extending `MTG_FOLD_ACT_SOURCES` as the
next work. **That finding is void and the work it proposed does not exist.**

`BpCandFingerprint` folds neither `bp_choice` nor `bp_at`. Every wave variant is
`plans[sl.base]` with only those two overwritten, so each variant carries its BASE PLAN's
fingerprint and "source-blind equal" degenerates to "same base plan" -- a near-tautology for a
duplicate, and nothing to do with physical source identity. The counter was exactly the base split
it was supposed to be independent of: on Snow H5 s8008, `src-blind diff=4427` against
`dup_cross diffb=4427`, to the unit.

The question is settled properly by censusing the BASE PLANS, where source-blinding is meaningful.
On H5 s8008, over the base plans that actually open wave slots:

    node plans total=202,359  distinct=202,359  src-blind-distinct=202,359

**Zero degeneracy, by either key** *on that population*. The retraction of the duplicate-level
counter stands: it measured base-plan identity and nothing else. But the conclusion drawn from this
follow-up census -- "there is no unfolded physical-source axis" -- was **wrong, and wrong for a
reason worth naming: it was measured on games that finish.** Re-run on a game the ceiling discards
it reads 18.6%, and those games carry 55.3% of the cell. See the section above. Neither number is
noise; they are different populations, and only one of them is where the runtime is.

### Where H5's units actually are (measured 2026-09-24, current engine)

`MTG_ROLLOUT_STATS` over the whole H5 s8008 cell, 576,150,077 units:

| site | units | share |
|---|---:|---:|
| `la_bp_wave` | 161,513,558 | 28.0% |
| `rollout_step` | 136,811,391 | 23.7% |
| `greedy_fallback` | 134,942,321 | 23.4% |
| `la_cand` | 116,964,332 | 20.3% |
| `fs_pre` + `fs_bp_wave` | 25,918,475 | 4.5% |

No single class dominates, which is the main planning fact: the deferred wave is 28%, so even
eliminating it entirely is 1.4x. `greedy_fallback` and `la_cand` together are 43.7% and have never
been examined.

Wave shape on the same cell: 13,693 nodes of which **11,024 (80.5%) open no slots at all**; over
the rest, 75.8 base plans and 36.3 slots per node, 225,471 wave applies, 67,696 rollouts,
`improved=26`. Duplicates are 92,226 (40.9% of applies), split `w0=49,266 (53.4%)` /
`cross=27,006` / `self=15,954` -- so on H5 the majority of the wave's duplicates repeat a state
WAVE 0 or an ordinary plan already reached, which is the opposite of gi26's cross-dominated split
at d3.

## 3. London bottoming, on the 17.6% of these games that mulligan

`[bottom-cost]`: **682 SECONDS per bottoming decision** at d3 unbudgeted (af6ecbf8 measured 4.8 s at
d5/b20), and **97.6%** of that subset's CPU (6,819 s of 6,986 s; the `MTG_BOTTOM_ROLLOUTS=0` arm
reports `cpu=0.000s` and halves total CPU). Bounded by census: 40 of the 227 ledger games mulligan
at all, carrying **17.3% of the tail's CPU** -- a game that never mulligans contributes exactly 0,
by `AIEngine.cpp:852`. So the keep table above is worth ~17% of the tail, not the whole of it.

**Do not re-run the 10-worst-games A/B to test bottoming.** It returns exactly 0.0% and the number
is meaningless: all ten keep their opening seven, so `BottomCards` is never entered and the arm
cannot show an effect. That selection (the most expensive games) anti-correlates with mulliganing --
14 of the 15 most expensive games in the ledger take zero mulligans.
