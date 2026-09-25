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
  **AND THE 167x WAS PARTLY AN ARTEFACT -- see [§4](#4-that-run-was-never-configured-the-2026-09-25-labeller-derivation).**
  That run labelled at **d5/b20**, the built-in gen default, because the deck has no `.value.json` to
  say otherwise. No other deck in the repo labels anywhere near that. Corrected to a derived d2/b1 it
  runs at **60 roll7/s against 20.8**, so FAST projects **~47 h** instead of ~137 h.
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

## 2b. The filter rescue re-derives the same "yes" 300 million times (2026-09-24)

### CHARGED vs UNCHARGED work -- read this before judging any Snow optimisation

Section 2's cell A/B found a lossless 11.5% win on completing games that read as a 1.8% REGRESSION at
the cell level, because 90.2% of the cell's wall is games pinned at `abandon_units`. That is only
half the rule. The other half is which KIND of work a change removes:

* **CHARGED work** (`SearchBudget::Consume` -- search branching, wave applies, rollout steps). A
  ceiling-bound game spends its ceiling either way, so removing charged work buys COVERAGE, not
  seconds. This is why `MTG_FOLD_SEARCH_ODO` rescued `g26` and still cost 3.2% on ceiling-bound games.
* **UNCHARGED work** (the greedy subset walk and everything it calls -- `MTG_SOLVE_CHARGE` is DEFAULT
  OFF, so `consider()` and its payability checks consume nothing). Removing this lowers the wall per
  unit, so a ceiling-bound game reaches the same 40M units SOONER. **It shrinks the cell, including
  the 90% of it that banks nothing.**

The scale of the uncharged half, measured on the whole 49-game cell (`MTG_ENUM_STATS`):
**7,985,627,146 greedy `consider()` visits against 287M charged units on the 12-game chunk** -- the
ceiling that decides which games are discarded is denominated in a quantity that excludes the single
largest consumer of wall. So uncharged work is where Snow's matrix cost actually is.

### The funnel

    entered                    : 7,985,627,146
      passed subset rules      : 4,650,064,904   -3,335,562,242
      passed flat mana         : 3,526,345,144   -1,123,719,760
      passed SubsetPayable     : 3,504,690,267   -21,654,877   (0.6%)
      passed ColorFeasibility  : 3,497,964,207   -6,726,060    (0.2%)
      survivors (fully scored) : 3,497,964,207
      [rescue] calls=1,308,927,499  rescued=299,864,980  subset-casts-aura=0

On the 12-game chunk the `subset rules` drop was **exactly** the fold's `guard_reject` (909,313,023 to
the unit), so the canonical-prefix fold is the largest single filter in the greedy walk -- which is
independent confirmation of section 2's lever. The two colour gates reject 0.6% and 0.2%: effectively
inert on this deck while costing per-visit work.

### Where the payment cost is: the SUCCESSFUL rescues, not the failures

`SubsetPayableWithFilters` copies the board and runs a REAL payment through `TapForCostDirect`. It is
armed by `any_filter`, a BOARD fact -- and Snow runs 4 Arcum's Astrolabe, so once one is untapped
every flat-mana failure pays for a real payment for the rest of the game. 1.31 BILLION calls.

The instinct is to attack the 1.01 billion FAILURES. That is the wrong half: an unpayable cost is
refuted by the top-level flow oracle in ~1 backtracker node, while proving a cost PAYABLE takes ~60.
So the cost is the **299,864,980 successful rescues**, and that is where the degenerate game's 28.7%
in `TapForCostBacktrackWorker` lives.

**The mechanism is a modelling gap, not a search problem.** Astrolabe is modelled as
`{1}, {T}: Add one mana of any color` -- one mana in, one out, pure re-colouring. The flat
`AvailableManaPool` cannot express that, so it reports "cannot pay" for a subset that is in fact
payable; the rescue then re-derives the same "yes" by full backtracking, 300 million times, for a
board fact that does not change within a turn.

**WHY THE 299.9M SUCCESSES FAIL THE FLAT CHECK, decomposed.** The per-clause probe records, for each
candidate total-shortfall clause, how many calls tripped it AND were nonetheless rescued -- which
turns those counters into an attribution of the successes:

| the flat check failed because... | of the 299,864,980 successes |
|---|---:|
| the RAW pool lacked total mana | **0** |
| the DEBITED `eff` pool lacked total mana (`tap_debit`) | 54,226,702 (18.1%) |
| the NONCREATURE pool lacked total mana | 22,598,684 (7.5%) |
| ...leaving COLOUR as the reason | ~223,000,000 (~74%) |

So there are **two separate modelling gaps**, and neither is a search problem:

1. **COLOUR, ~74%.** Note this is *not* "the pool cannot see Astrolabe's colours" --
   `AddSourceToPool` books every multi-colour source as one `ManaPool::wild` and `CanPayFlat` lets a
   wild pay ANY pip, so the pool is already permissive about colour PRESENCE. What it cannot express
   is that Astrolabe's yield is a **swap, not an addition**: `{1}, {T}` consumes a unit to produce
   one of a chosen colour, so its NET is zero units but +1 colour freedom. A flat additive pool has
   no way to say that, so it must either over-credit the total or under-credit the flexibility.
   Teaching it the conversion exactly (N untapped Astrolabes => up to N units may have their colour
   reassigned, total unchanged) would make `mana_ok` true for most of those subsets without any real
   payment. It must be EXACT: a permissive pool admits unpayable subsets and moves play, and this
   area already has a design doc of its own (`colour-blind-subset-affordability.md`) -- so it is a
   deliberate mana-modelling change, not a skip to bolt on.
2. **`tap_debit` IS TOO PESSIMISTIC, 18.1%** -- was the guess. It is the opposite, and the real
   answer turned out to be much bigger than the 18.1% that pointed at it. See below.

### The rescue lets a source pay for its own tap (MTG_RESCUE_TAP_SOURCE)

`SubsetPayableWithFilters` pays every selected action's mana cost -- casts AND `{cost}, {T}`
activations -- but **never applies the activation's own `{T}`**. So Scrying Sheets (`{1}{S}, {T}` to
dig, and it also taps for `{C}`) can pay for its OWN activation and stay available to fund the rest
of the subset. That is exactly the self-funding behaviour `PermAbilityTapDebitOf` removes from the
flat path, whose flag comment calls it *"a correctness fix, not a heuristic -- the plans it drops are
ones the engine could never execute."* This function is the hole in that fix: the flat check drops the
plan, `any_filter` routes it here, and this re-admits it.

`PermAbilityTapDebitOf` is exact (`full - AvailableManaPool(state, &p)`), so the flat side was never
the suspect -- which is why the 18.1% clue above pointed the wrong way.

**The size of it, measured by arming the fix on the whole cell:**

    rescued:  299,864,980  ->  13,208      (a 99.996% collapse)

and all three per-clause `rescued-anyway` counters go to **0**, including the 370M debited-`eff` one.
So essentially **every** rescue this deck was getting was a self-funding artifact; with the `{T}`
applied the real payment agrees with the flat path everywhere, and the 13,208 that remain are the
genuine filter routings the mechanism exists for.

**It is a TRADE, not a clean win, and that is why it ships default OFF.**

| axis | result |
|---|---|
| decks affected | **Snow only** -- 90 of 93 smoke configs byte-identical (it needs `any_filter`) |
| smoke, searched d3/d5 | `slower=0 faster=0 play-changed=12`; aggregates unchanged (6.1200, 6.2200) |
| smoke, d0 greedy | `slower=3 faster=5`; mean 6.7060 -> 6.7040, but `gi413` goes **8 -> loss** |
| H5 d5 cell (49 games) | **-0.0312 turns**, 30/32 digests identical, the 2 that move are BOTH better (`g12` 7->6) |
| cost | 0.995x cell wall; `entered` RISES 5.9% as the search redistributes its breadth |
| abandonment | unchanged, 17 of 49 |

Every aggregate improves and nothing on the searched axis regresses, but three d0 games get worse and
one of them loses a game it used to win. By the standing bar -- a clean win is *no* regression on
*any* axis against the shipped baseline -- that is a reserved decision, so the flag is OFF and the
evidence is here rather than in ground truth.

**WHY REMOVING PLANS IMPROVES PLAY** (the direction is not obvious). The engine's own
`etb_untap_lands` note states the cost of an unsound enumeration credit: *"the enumerator spends its
breadth offering plans whose mana the executor then cannot make."* A self-funding plan that wins the
score gets committed, the executor then cannot pay it, and the cast is silently dropped -- so the
line actually played is worse than the line chosen. Removing them makes the search commit to plans it
can perform.

**AND WHY d0 COULD STILL LOSE -- REPLAYED AND ROOT-CAUSED (2026-09-25).** The mechanism is PARTIAL
EXECUTION, and it is only half the story; the other half is a separate defect in the executor. See
[§2c](#2c-a-plan-must-not-spend-the-source-it-has-to-tap-mtg_act_tap_reserve) -- with both halves in
place this game wins on turn 8 again and the whole d0 tier ends up **better** than the shipped
baseline. The hypothesis recorded here before the replay -- that the fix therefore belonged at
EMISSION (offer the subset without the unaffordable activation) -- was **wrong**, and wrong in an
instructive way: emission was not the problem, so a fix there would have added a second enumeration
axis to a deck whose whole problem is enumeration width, and it would not have touched the actual
cause.

**COST IS NOT THE REASON TO DO IT.** Skipping ~300M expensive real payments is worth almost nothing
(0.995x), because the freed breadth is immediately spent elsewhere -- `entered` rises 5.9%. The case
for this change is correctness and searched-play quality, not speed.

### A sound shortcut that turned out not to be worth much (measured, kept default OFF)

`MTG_RESCUE_TOTAL_GATE` + `ConversionTotalPreserving`. The argument: a re-colouring filter cannot
conjure mana, so a subset the board cannot fund AT ALL is beyond the rescue's help. The predicate
refuses that argument for every source that can RAISE the total (a filter land's `{1}->{W}{W}`, Three
Tree City's scaled tap, a pending land Aura's unseen bonus), so it is conservative by construction.

**The first formulation was UNSOUND and its own self-check caught it.** The probe counts, alongside
each candidate clause, how many calls it would skip that the real payment then RESCUED -- a number
that must be 0. Over the 49-game cell:

| clause | would skip | rescued anyway |
|---|---:|---:|
| raw pool, `combined.ManaValue() > pool.Total()` | 83,275,603 | **0** |
| debited `eff` | 346,662,936 | 54,226,702 |
| noncreature pool | 81,574,815 | 22,598,684 |

The original test OR'd all three and would have deleted ~54M payable subsets. `tap_debit` SUBTRACTS
from `eff` to reserve a source for another use, so `eff.Total()` is not a bound on what the real
payment may tap; and the noncreature pool is a deliberately restricted pool answering a different
question. **Only the raw-pool clause is sound** -- zero counterexamples in 7.99 billion visits -- and
it covers just **6.4%** of rescue calls, all of them from the CHEAP (failing) half. So it is worth a
fraction of a percent. Kept default OFF because it is proven sound and may matter on a filter-LAND
deck, where the raising cases are real; it is not a Snow answer.

**The lesson worth keeping is the method.** A shortcut over billions of events is not adjudicated by
an A/B -- a 0.25% wall change is indistinguishable from noise, and an unsound gate that deletes 4% of
payable subsets could easily have read as a win. Pairing every candidate clause with a
"rejected-but-actually-rescued" counter settled soundness and size in ONE run, before any play moved.

## 2c. A plan must not spend the source it has to tap (MTG_ACT_TAP_RESERVE)

Replaying `snow_smoke_d0_s1001 gi413` -- the one game the gate above turned from a turn-8 win into a
loss -- found a SECOND, independent defect, on the execution side. The two are halves of one fix, and
shipping either alone is a trade.

### The replay

`MTG_RESCUE_TAP_TRACE` prints every selection the gate deletes that would have been rescued without
the activation taps -- payable-without / unpayable-with, i.e. exactly the plans the fix removes. The
whole game produces **six**, and the decisive one is at turn 4:

    [rescue-tap] FLIP turn=4:
      Scrying Sheets[ACT src=39,T] cost={2} + Scrying Sheets[ACT src=38,T] cost={2}
      + Frost Augur[ACT src=20,T] cost={1}
      UNTAPPED{Sheets#39, Astrolabe#8, Sheets#38, Astrolabe#7, Boreal Druid#10,
               Rimewood Falls#34, Coldsteel Heart#15, Snow-Covered Island#56}

Adjudicated by hand: **5 mana of activation cost.** Both Sheets and the Augur tap for their own `{T}`,
so what is left to pay with is Druid + Rimewood + Coldsteel + Island = **4 mana** (the two Astrolabes
are `{1}, {T}` re-colourers -- net zero, and `ConversionTotalPreserving` is why the earlier total-gate
work could rely on that). **The plan is illegal and the gate is right to delete it.**

What the shipped engine did with it is the interesting part. The executor committed the 3-activation
plan, executed two of the activations, and could not pay the third -- and the accidental 2-activation
residue (`Sheets`, then `Augur`) is a perfectly good line that wins on turn 8. **PARTIAL EXECUTION was
laundering an illegal plan into a legal one.** That is the hypothesis this doc recorded, confirmed.

### But the legal replacement plan was then STRANDED

With the gate on, the greedy falls to the best legal plan: `{activate Sheets #39, activate Sheets #38}`
-- 4 mana of looks against exactly the 4 mana available. The turn should still get two looks. It got
one:

| arm | turn 4 actions | result |
|---|---|---|
| base (shipped) | land; **ABILITY Sheets; ABILITY Augur** | win T8 (from an ILLEGAL plan) |
| `MTG_RESCUE_TAP_SOURCE` | land; ABILITY Sheets | **loss** |
| `MTG_ACT_TAP_RESERVE` | land; **ABILITY Sheets; ABILITY Sheets** | win T8 |
| both | land; **ABILITY Sheets; ABILITY Sheets** | win T8 |

The trailing-activation pass taps Sheets #39 for its `{T}`, then pays `{1}{S}` with the first sources
the payment walk reaches -- which include **Sheets #38**, because a Scrying Sheets also taps for `{C}`.
#38 is now tapped, so the second activation is stranded and silently dropped. Nothing was unaffordable;
the payment spent the source the plan's own later action had to tap. The board after the turn says it
plainly: the `resv` arm leaves Rimewood Falls untapped and both Sheets spent on looks, the `gate` arm
leaves Rimewood **and** Druid **and** Coldsteel up while dropping a look it could afford.

### The fix, and why it is free of risk by construction

`MTG_ACT_TAP_RESERVE` adds every planned `{cost}, {T}` activation's source to
`g_plan_reserved_sources`, the plan-scoped reservation that already exists for the mana-unlock equip
and the colour-critical hold. That list rides **reserve-then-fallback**: the held attempt runs first
and a payment that genuinely needs a reserved source still gets it on the unrestricted retry. So it
can only ever change WHICH source pays -- never whether a cost is payable, and therefore it can never
drop an action.

Two insertion points, one shared producer (`TurnSolver::ActivationTapReserve`), because the executor
and the rollout must not drift:
* `PlanReserveSources` -- the CAST section, both apply paths, already wired;
* the two trailing dispatchers' own `PlanSourceReserveScope` -- `ApplyPlanDirect`'s and `AIEngine`'s
  twin. Needed separately because the cast-section scope has already exited by then and the defect
  here is activation-vs-**activation**.

### Measured (smoke, 93 configs, vs committed ground truth)

| arm | d0 (1000 games) | d3 (100) | d5 (50) | configs changed |
|---|---|---|---|---|
| ground truth | 6.7060 | 6.1200 | 6.2200 | -- |
| `gate` alone | 6.7040 | 6.1200 | 6.2200 | 3 |
| `resv` alone | 6.7000 | 6.1300 | 6.2400 | 3 |
| **both** | **6.6950** | 6.1300 | 6.2400 | 3 |

`both` per-game audit: d0 `slower=15 faster=24`, searched `slower=2 faster=0`. **The d0 regression the
gate was reserved for is gone** -- that tier is now better than the shipped baseline, and `gi413`
specifically is back to a turn-8 win. Still Snow-only: **90 of 93 configs byte-identical**, because
neither flag can fire without a plan that taps a mana source for an activation cost.

The two searched "slower" entries are **one physical game scored at two depths** (`gi2` at d3 and d5,
seed 1003, both 6->7), and the harness's own explain output classifies it: *"DRAWS DIVERGE from T3 (old
drew 'Ice-Fang Coatl' vs new 'Snow-Covered Island') -> a fetch/shuffle resolved differently; physically
different from there on."* Its turn 2 is strictly more productive under the fix (`land; Astrolabe;
Boreal Druid; ATTACK` against `land` alone), which is what re-ordered the draws. A 1-game move on 100
is not evidence either way -- so it was re-measured on held-out seeds, and that is where the story
changes.

### HELD OUT, AND THE RESERVE IS REFUTED

Four arms of the 2x2 in ONE pooled batch (per-job `flags`, so all 7,200 games share one work queue and
one tail), Snow at d3/b10 x 300 games and d5/b20 x 150 games, seeds **9001-9004** -- touched by no tier
(smoke 1001, regression 2002/3003, overnight 4004-7007). 1,800 searched games per arm:

| arm | mean turn | vs base | cells worse |
|---|---:|---:|---|
| base | 5.9839 | -- | -- |
| `gate` (RESCUE_TAP_SOURCE) | 5.9828 | **-0.0011** | 0 of 8 (6 of 8 score-identical) |
| `resv` (ACT_TAP_RESERVE) | 5.9972 | **+0.0133** | **8 of 8** |
| `both` | 5.9872 | **+0.0033** | 4 of 8 (4 equal) |

...and the same four arms at **d0**, same four seeds, 1,000 games each (16,000 games in 0.4 s wall --
d0 costs nothing on this deck because the expense IS the search):

| arm | mean turn | vs base | cells worse |
|---|---:|---:|---|
| base | 6.6913 | -- | -- |
| `gate` | 6.6935 | **+0.0023** | 4 of 4 |
| `resv` | 6.6958 | **+0.0045** | 4 of 4 |
| `both` | **6.6898** | **-0.0015** | 0 of 4 (3 better, 1 equal) |

**The two axes disagree about which arm wins, and every effect except one is inside the noise.** The
exception is `resv` on searched play: **8 of 8 cells worse** is p ~ 0.004 on a sign test, and it is worse
at d0 as well (4 of 4). Everything else -- gate -0.0011 searched / +0.0023 d0, both +0.0033 searched /
-0.0015 d0 -- is a few thousandths of a turn with the sign flipping between depths, which is exactly the
regime where the smoke tier's single seed told two different stories on two runs.

**`MTG_ACT_TAP_RESERVE` is worse, and the 8-of-8 sign pattern is not noise.** It is REJECTED as a play
change; the flag stays default OFF as the record of the measurement. The smoke d0 improvement it
produced (6.7000, the best d0 of any arm) did not survive contact with searched play.

**Why holding the source back loses, which is worth understanding before anyone re-proposes it.** It is
NOT an executor/rollout divergence -- `ApplyPlanDirect` reserves too, so the search scores the reserved
line and there is nothing out of lockstep. It is that the trade is genuinely bad on average for this
deck: reserving Scrying Sheets #38 forces the first look's `{1}{S}` onto Rimewood Falls and Snow-Covered
Island, and **those** are then unavailable to whatever else the turn wanted. A second Scrying Sheets
look is worth less than the mana flexibility it costs. The stranding is therefore not a bug at all --
the plan is partially realised *identically in both worlds* -- it is a tap-order HEURISTIC, and the
heuristic-optimization rule applies: it was measured, and it lost.

**What this does for the gate.** It does not make it a clean win -- held-out d0 is +0.0023 on 4 of 4
seeds -- but it removes the *unexplained* part. The gi413 loss is now fully accounted for (an illegal
plan laundered by partial execution), and the one fix for it measures worse than the loss it repairs, so
it is a known and priced single-game cost rather than an open question. What the gate is actually worth
stands on its other three legs: **299,864,980 unexecutable rescues removed**, the **H5 d5 cell -0.0312
turns** (30/32 digests identical, both movers better), and held-out searched play **-0.0011 with no cell
worse**. It stays a user decision, now made against a complete picture.

**VERIFICATION OF THE DEFAULTS.** Smoke with both levers off: **93 passed, 0 failed, 0 configs changed**.
That is load-bearing rather than ceremonial, because `PlanSourceReserveScope` is NOT a no-op when handed
an empty list -- it saves and REPLACES `g_plan_reserved_sources`, and the trailing dispatcher is
re-entered from inside the cast section (the human-order interleave applies one activation inline) where
that outer reserve is live. The first version installed the scope unconditionally and would have cleared
it with the lever off. Guard: install only when the union is non-empty
(`ActivationTapReserveUnion` returns empty to mean "add nothing"). The four-arm table above was measured
on the pre-guard binary and re-verified on the guarded one -- `resv`/`both` d3 s9001 digests
`fefb8935b2136bb5` / `f71c4ce2fa801419` both sides, so the guard is inert here and the numbers stand.

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

## 4. That run was never configured (the 2026-09-25 labeller derivation)

The cancelled 2026-09-24 generation's own log header says it:

    rollout depth   : 5  (source: gen-default)
    rollout budget  : 20 ms  (source: gen-default)

"gen-default" means the deck has no `.value.json`, so `src/analyzer/main.cpp`'s
`vp.MullGenDepth(5)` / `vp.MullGenBudgetMs(20)` fell all the way through to the built-in default.
**Every other deck in the repo carries a MEASURED setting, and none of them is anything like that:**
`mull_gen_depth` is **1 on 12 of 23 decks** (median 2, max 6), and `mull_gen_budget_ms` is **3 on 18 of
23** (max 20). So the least tractable deck in the suite was labelling its mulligan cells at the most
expensive setting any deck uses. That is also where the 30 s / 52 s / **161 s** single rollouts in that
log come from, and it means the headline "~167x slower than the `mulligan-profile.md` ~110/s/core guide"
was never purely a statement about Snow -- it was substantially a statement about d5/b20.

There is **no env override**: the only route is `value_play.mull_gen_depth` in `<deck>.value.json`.

### The sweep (prescribed, not invented)

`docs/design/fivecolour-mullgen-labeller-sweep.md`, whose subject deck was in the same position (FAST
projected 220 h), uses the comp-scorer's **hand mode** -- which exists exactly for a deck with no
mulligan artifacts yet, because the bucket map a composition needs is an *output* of the generation
being configured. 200 openers from the real opening distribution, forced kept (the gen's shape), R=30,
`MTG_SCORE_HAND_SEED` fixed so **every arm scores the same hands** (common random numbers, so arms
compare element-wise). Script: `logs/snowopt/mullgen_labeller.sh` + `mullgen_labeller_report.py`.

**A labeller is judged on RANK fidelity, never on its mean.** A uniform shift in hand scores flips no
keep -- the policy compares a 7-card hand against its own 6-card sub-hands and both move together.
DISPERSION is what re-orders pairs and flips decisions.

| arm | units/rollout | cheaper | rho | shift | disp | pair-agree (11,378 pairs) | wall |
|---|---:|---:|---:|---:|---:|---:|---:|
| **d5/b20** (the gen default) | 52,189 | 1.00x | 1.0000 | 0 | 0 | 100.0% | 8m11s |
| d3/b20 | 50,269 | 1.04x | **1.0000** | +0.0000 | **0.0000** | 100.0% | 8m06s |
| d3/b10 | 30,267 | 1.72x | 0.9992 | +0.0052 | 0.0212 | 100.0% | 5m23s |
| d3/b3 | 21,080 | 2.48x | 0.9987 | +0.0092 | 0.0281 | 100.0% | 4m00s |
| d2/b3 | 20,918 | 2.49x | 0.9986 | +0.0088 | 0.0277 | 100.0% | 4m00s |
| **d2/b1** | **18,202** | **2.87x** | 0.9984 | +0.0087 | 0.0294 | **100.0%** | **3m27s** |
| d1/b3 | 20,333 | 2.57x | 0.9985 | +0.0087 | 0.0290 | 100.0% | 3m56s |
| d1/b1 | 18,012 | 2.90x | 0.9984 | +0.0088 | 0.0303 | 100.0% | 3m22s |
| d0/b0 | (0 units) | -- | **0.9125** | **+0.5683** | **0.3408** | **97.4%** | -- |

Draw side agrees throughout (d2/b1 rho 0.9987, disp 0.0255).

**Three results, in order of how much they change:**

1. **Depth 5 buys literally nothing over depth 3.** `d3/b20` ranks the 200 hands *identically* --
   rho 1.0000, dispersion 0.0000, every one of 11,378 separated pairs ordered the same -- at 1.04x the
   cost. The BUDGET binds first. This is the same finding the FiveColour sweep reached ("budget was the
   unswept knob"), and it means the gen default's depth was pure waste on this deck.
2. **The fidelity floor is ~2.9x, and everything above it is free.** Every arm from d3/b10 down to
   d1/b1 keeps **100.0% pairwise ordering agreement** with rho >= 0.9984 and dispersion <= 0.030 turns
   -- a third of the flip_eps the generator itself uses (0.02) is the scale to compare that against.
3. **d0/b0 is a cliff, not the next step down.** The greedy labeller shifts +0.57 turns, its dispersion
   is 11x d2/b1's, and it drops to 97.4% ordering agreement. It also reports **zero work units**, which
   is the CHARGED/UNCHARGED split of §2b showing up again: greedy work is invisible to
   `SearchBudget`. Not usable.

**Chosen: d2/b1** -- within 1% of the cheapest arm, marginally better dispersion than d1/b1 on both
sides, and the same setting the FiveColour precedent adopted. Installed as
`decks/Snow/Snow.value.json`, carrying `value_play.mull_gen_depth`/`mull_gen_budget_ms` and nothing
else.

### What it does and does not change

* **Play is untouched, verified.** Smoke with the file in place: **93 passed, 0 failed, 0 configs
  changed.** A presence-only sidecar with no `eval_model` and no `target_depth` leaves
  `value_play.present()/drives()` false (`MulliganProfileIO.h`). This had to be *measured*, not
  assumed -- CLAUDE.md warns that sidecar presence activates the value-leaf hybrid, and it is the
  `eval_model` that does so.
* **K is unchanged at 17.** Discovery deliberately runs at shipped play settings, not `mull_gen_*`
  (the 2026-08-15 split), so a cheaper labeller cannot re-bucket the deck. Re-verified: the scout
  re-discovered and got 17 raw buckets again.
* **`expected_buckets` is deliberately NOT set.** Recording K is the user's bucket ruling, not
  something a derivation may install.

### The corrected projection, and it is still not an overnight job

Both rates below are the **floor phase of a live `fast` run**, which is the only apples-to-apples
comparison available: `roll7` per second off the monitor line, at the same recipe (R30 adaptive, floor
R=2), on the same 32 cores.

| setting | floor rate (roll7/s) | FAST | COMPLETE |
|---|---:|---:|---:|
| d5/b20 -- the cancelled run, averaged over its whole 6.5 h life | 20.8 | 137 h | 275 h |
| **d2/b1 -- derived** | **60.4** | **47 h** | 95 h |

**2.90x in wall, against 2.87x predicted from work units.** Those agreeing is worth noting: on this
deck the unit currency and wall clock track each other across a 20x budget change, which is not
something §2b's CHARGED/UNCHARGED split guaranteed.

Do NOT compare either figure against the earlier `recommend` scout's 35-45/s -- that pass runs floor
R=1, i.e. half the rollouts per cell, so it is a different quantity. An earlier version of this section
made that comparison and reported 1.7x / ~81 h; both were wrong.

**It is still not an overnight job.** ~47 h FAST is ~6x the 8 h `MTG_KEEP_OVERNIGHT_H` target: a
multi-night, journal-resumed commitment. And it is an OPTIMISTIC 47 h, for a reason the parallel
Fungus work states plainly (`mullgen-cost-is-driven-by-bucket-count.md`, UPDATE 2026-09-25): *"an
average rollout rate measured early is not a projection"* -- the odometer walks the cell space in
bucket-index order, so an early rate samples the cheapest corner, and on candidate B the rate fell
6.5x once it left that corner. The remaining levers are the deck tweak (-1 Rimescale Dragon +1
Rimefeather Owl takes K to 16 and cells to 0.769x, so ~36 h -- the user's decklist call) and the
BUDGET-CURRENCY repair the user has already ruled for (`slow-rollout-tail-and-the-uncharged-greedy-walk.md`,
USER RULING 2026-09-24: *"fix the BUDGET, not discovery"*), which is the one direction with an order of
magnitude in it -- and which will change what a budget buys, and therefore the play digest, and
therefore invalidate any journal banked before it lands.

### Two corrections to this section's own process, worth more than the result

1. **`logs/<Deck>_mullgen/gen.log` is APPENDED (`>>` in `scripts/mullgen.sh`).** Reading its first
   settings block gives you the OLDEST run's configuration. That is how the 2026-09-25 `fast` launch
   was killed six minutes in for "running at d5/b20" when it was running correctly at d2/b1 -- the
   header read was 2026-09-24's. Always take the LAST block:
   `L=$(grep -n "MULLIGAN PROFILE GEN SETTINGS" gen.log | tail -1 | cut -d: -f1)`.
2. **`scripts/derive_mullgen_setting.py` already exists** (landed 629a6107 by the parallel Fungus
   work, same day) and is the prescribed route. The sweep above was hand-rolled before it was found;
   the method matches -- `MTG_SCORE_HANDS`, paired openers, rank fidelity, a rho floor -- and its rule
   is "cheapest arm clearing the floor", which on this table picks **d1/b1** rather than the d2/b1
   installed. They are 1% apart in cost and 0.001 apart in dispersion, so this is not worth
   regenerating for; but the script is what a future deck should use.

## 5. Rule 1: don't offer a dig activation while the mana could deploy a permanent (MTG_DIG_MANA_LAST)

**USER 2026-09-25**, refined across six messages, and the refinements are the specification:

> *"Is it time to try out the 'don't activate draw abilities until you are playing your current
> threats' rule?"* → *"we need to be a bit careful about it, though, since putting down junk may not
> be better than drawing"* → *"I guess anything that draws is also okay to play."* → *"Acceleration
> may also be all right, so it likely would be just when we can max out our mana from cards in
> hand"* → *"(not including Skred, which we should not cast)"* → *"It's a real question as to whether
> this will be 100% lossless, but it seems worth a try."*

And then the correction that decided the SHAPE, after a first answer that reached for ordering levers:

> *"No, what I mean is drop the possibility to do activations while you can use the mana for
> something else in hand."*

That is a PRUNE AT ENUMERATION, not a reordering. Ordering was already structurally settled — dig
activations run after every cast in both worlds (`apply_trailing_activations` in the rollout,
`exec_trailing_activations` in the executor) — so an ordering lever could not have done anything.

### What it is

`DigManaWantedInHand` (TurnSolver.cpp, beside `AnyHandCastableNow`) plus a single gate at the
`CollectActions` TapDraw emission site. The activation is not OFFERED when the pool cannot fund both
the cheapest castable thing in hand and the activation. Every one of the user's refinements is
derived from card PARAMS rather than a name list, so it generalises:

| in hand | counts as a use of the mana? | why |
|---|---|---|
| land | **no** | the drop is free; it never rivals the mana |
| any permanent (incl. mana rocks/dorks) | **yes** | *"acceleration may also be all right"* |
| non-permanent that draws (`cast_draw` / `etb_self_draw`) | **yes** | *"anything that draws is also okay to play"* |
| non-permanent that does not draw (**Skred**) | **no** | spends mana, leaves nothing: *"which we should not cast"* |
| `goldfish_inert` | **no** | by definition it does nothing here |

Applied at `CollectActions` ONLY. Deliberately not at `BpAvailablePermAbilityModes`, not at
`CollectActivationKeys` (memo keys stay at least as fine, so no false sharing), and not at `CheckLine`
— human play keeps the full offer, the standing rule for every judgment prune in this engine.

### Measured at a FINITE budget: quality-neutral, cost-neutral-to-worse

4 held-out seeds (9401-9404), 4 cells, 14,000 games, both arms in one pooled batch:

| depth | games | off | on | delta | digests identical |
|---|---:|---:|---:|---:|---:|
| d0 | 4,000 | 6.7343 | 6.7330 | **-0.0013** | 0/4 |
| d2/b1 | 1,200 | 6.0300 | 6.0300 | +0.0000 | 0/4 |
| d3/b10 | 1,200 | 6.0241 | 6.0241 | +0.0000 | 0/4 |
| d5/b20 | 600 | 5.9117 | 5.9117 | +0.0000 | 0/4 |

Wall: d2 0.91-0.98x, d3 0.97-0.98x, **d5 1.07-1.13x WORSE**. That sign pattern is the third
independent measurement of *freed breadth is respent* (§2b): under a budget the search spends its
allowance regardless, so removing candidates redistributes work instead of removing it, and the
deeper the budget lets it go the more there is to respend into.

**A correction to this doc's own earlier claim.** A 150-game one-seed probe reported the play digest
BYTE-IDENTICAL at d2 and d3. The 14,000-game run gives **0 of 16 cells identical**. The byte-identity
was a one-seed artifact; do not cite it.

### Measured at budget_ms 0 -- and this is where it pays, for a reason I predicted wrongly

The H5 s8008 cell (depth 5, `budget_ms: 0`, `abandon_units` 40M, max_turns 8, `MTG_LAZY_LEAF=1`),
2 arms x 49 games, read out BY FATE because this cell's aggregate wall has already made a lossless
11.5% win read as a 1.8% regression (§1).

| readout | off | on | |
|---|---:|---:|---|
| **abandonment** | 17/49 (34.7%) | 17/49 (34.7%) | 0 rescued, 0 lost |
| **charged units**, ceiling-bound | 680,012,784 | 680,012,839 | **1.000000x** (max per-game gap 2,035 of 40,000,000) |
| **wall**, ceiling-bound (90.8% of the cell) | 22,509.9 s | 20,452.0 s | **0.9086x**, 14 of 17 faster |
| **wall**, completing | 2,277.0 s | 2,210.6 s | 0.9708x summed, median **1.0167x** |
| **play digests**, completing | | | **32 of 32 IDENTICAL** |
| **cell wall** | 24,786.9 s | 22,662.6 s | **0.9143x** |

**I predicted the saving would show up as a lower abandonment rate. It did not -- that is a flat
null, 17/49 both arms, nothing rescued and nothing lost.** It showed up instead as the one thing the
CHARGED/UNCHARGED framing (§2b) says to look for and I did not look for here: the ceiling-bound games
do **provably identical charged work** -- 1.000000x, agreeing to 0.005% per game because both arms
stop at the same 40M-unit ceiling -- and still finish **9.1% sooner**. The removed candidates were
never simulated turn-steps. They were enumeration, payability tests and scoring inside the greedy
walk, which `SearchBudget::Consume` does not charge, so deleting them lowers wall PER UNIT. That is
the definition of an uncharged-work saving, measured directly rather than argued.

**And at b0 it is LOSSLESS, which is the user's own open question answered.** 32 of 32 completing
games byte-identical, 0 abandoned games rescued or lost. The finite-budget digest movement above is
therefore budget TRUNCATION, not a quality loss: at unbounded budget the search reaches the same play
without the removed candidates, i.e. they are never part of an optimal line at depth 5 over 8 turns.
Consistent with the means: unchanged to 4 dp at d2/d3/d5 and BETTER at d0 (-0.0013).

The median `1.0167x` on completing games is not a counter-example. That population is 9.2% of the
cell's wall, and the slowdowns are confined to games under 50 s where a few ms is 10%; every
completing game over 100 s is faster (gi=12 1047.8 -> 1030.2 s, gi=47 254.6 -> 207.9 s, gi=8
165.7 -> 145.6 s).

### The generator's own labeller: a POLICY change, not a free speedup

The user's hypothesis: *"In the mulligan profile generation, though, it might help more, since we use
a lower depth?"* Re-asked with the comp-scorer hand mode (the same harness that derived d2/b1 in §4),
whose `units_per_rollout` is deterministic and immune to host load:

```
off  units_per_rollout=18202.3      on  units_per_rollout=17560.3      -3.5%
```

**Confirmed.** But the same run shows **41 of 200 hands score differently**, so at b1 the lever
re-ranks the labeller. The gen's table is a function of that ranking, so this is a table-policy
change for the mulligan stage, not a free speedup -- and per
[[bucket-ruling-is-user-only]]-adjacent discipline, changing what the table measures is the user's
call, not an optimisation to take. Judge it on RANK fidelity if it is ever considered (§4), never on
the mean.

### Where this leaves the lever

Default **OFF** (`heurarm::DIG_MANA_LAST` / `MTG_DIG_MANA_LAST`), and the default-off path is proven
inert: digests byte-identical to HEAD on three cells by name-keyed pairing (d0 `d383ecaa6e23cd7f`,
d2 `849fe310eb25c531`, d3 `8cb3f2dae18654c4`), smoke 93 passed / 0 failed / 0 configs changed.

- **For a `budget_ms: 0` workload -- which is what the value-leaf depth matrix runs -- it is an 8.6%
  wall saving with provably identical charged work and identical play.** That is the clean-win shape.
- **For anything at a finite budget it is not a cost win** and at d5/b20 it is 7-13% worse.
- **For the mulligan generator it is a policy question**, not an optimisation.

The firing counter (`[rollout-stats] dig_mana_last drops=`) exists because this family's standing trap
is a narrowing whose digests match because it emitted nothing ([[digest-equality-can-mean-broken]]);
a zero count prints `NO POWER` rather than letting silence read as neutrality.

## 6. Three dead ends on the frontier, and one census bug worth more than the results (2026-09-25)

USER, after §5 came back narrow: *"If it isn't paying off then let's try a different option."* Three
options were priced. All three are dead, and the cheapest lesson is in how the first one nearly
wasn't.

### 6a. Rule 2 -- "skip candidates that play strictly less than an alternative"

USER: *"We could also potentially skip candidates that play strictly less than an alternative."* /
*"Since more is always better in this deck."*

Censused before building (`MTG_PLANDOM_CENSUS`, default OFF) at the end of
`EnumeratePlansWithLandUncached` -- the list whose SIZE IS the branching factor, so a plan dropped
there removes a whole subtree. Three nested tiers, because "more is better" must be measured and not
assumed: extras all land/permanent (the user's rule), extras also activations and non-permanent draws,
extras unrestricted (an upper bound only -- that tier would prune a plan because a rival casts Skred).

**THE FIRST NUMBERS WERE WRONG, AND THE BUG IS THE REUSABLE PART.** The axes key folded **4 of the 21**
plan-level sub-decision fields. An omitted field in an EXACT-MATCH key is fail-**OPEN**: two genuinely
different plans compare EQUAL, so every column inflates. The code carried a comment asserting the
opposite ("an axis omitted here costs the census REACH, never correctness"), which is exactly backwards
and is the same hazard `Dominance.h` documents for `GameState`/`Permanent`/`Player`. Snow makes it
concrete: eight repeatable card-to-hand permanents mean `bp_all` / `bp_wave0` / `bp_sched` / `bp_base`
/ `bp_self` are live on most frontiers, and all five were invisible.

| on the same 17 ceiling-bound games | 4 of 21 axes | **all 21 axes** |
|---|---:|---:|
| exact duplicates | 9.97% | **0%** |
| perm-only extras (the user's rule) | 61.8% | **32.0%** |
| mean frontier width | 23.1 -> 8.8 | **29.3 -> 19.9** |
| branching reduction | 2.62x | **1.47x** |

So half the mass was an artifact, and the *free* half -- exact duplicates, which was to be built first
because losslessness would have been provable rather than gated -- **does not exist at all**. Every
apparent duplicate was two plans differing in an axis the key could not see.
`static_assert(sizeof(TurnSolver::Plan) == 376)` now makes a new Plan field a BUILD FAILURE that lands
the author at the key. That assert is the only reason the corrected numbers are trustworthy, which is
the argument for keeping it despite the tripwire it puts on Plan.

**Verdict: not built.** 1.47x of branching behind a full judgment gate (must-find, suite audit,
held-out) is thin, and §6b is why the gate is the expensive part.

### 6b. EOT state dominance on Snow -- the shelving condition opened, and the deck still says no

`docs/design/eot-dominance-pruning.md` shelved `MTG_DOM_PRUNE` with an explicit re-open condition:
*"don't re-measure unless production starts using unbounded budgets."* The value-leaf depth matrix runs
`budget_ms: 0`, so the condition is MET, and `MTG_DOM_CENSUS` was armed alongside the census above at
no extra cost.

**Snow: 3,898,122 dominated of 133,833,782 EOT states = 2.91%.** That is mirrowing's shape (2.2%, which
converted to 0.15% of the tree and a NET COST), not fivecolour's (18.1% -> 1.59x). The door opened and
the evidence closed it again. Do not re-open it for this deck.

### 6c. Letting ORDINARY plans skip a repeated post-apply state (MTG_FS_PRE_STATE_SKIP)

`FSLineWin`'s frontier loop already computes every plan's post-apply state key, inserts it into
`bp_seen_states`, and learns whether an earlier sibling reached it -- then discards the answer unless
the plan carries a `bp_choice` ("Ordinary plans only RECORD"). The lever is that one condition. It is
an IDENTITY relation, so unlike dominance the evaluator is irrelevant and the only failure mode is a
key hole; it is a `continue` rather than an erase, so the positional `bp_base`/`bp_self` indices need
no remap. `MTG_BP_WAVE_PROBE` already priced it -- no census needed -- at
**`pre-plans dup=1393042/8489237` = 16.4%**.

**USER, immediately and correctly: *"Don't we already do something like this with the transposition
table?"*** Yes, and that is the entire answer. Five dedup mechanisms already exist (TT; `FSLineCache`;
`MTG_CANON_SIMKEY`, default-ON, which collapses play-order permutations; the variant dedup; the m2
host's `node_key_origin`, which has the identical record-never-skip gap). The call order is
`apply -> BuildDedupKey(s) -> FSLineTail -> SimulateEndAndStartNextTurn(s2) -> FSLineWin(s2) ->
BuildSimKey + lc->find`, so the duplicate's SUBTREE is already a cache hit and only the approach cost
is left. The hypothesis was that the approach cost is CHARGED, which would convert into coverage on a
unit-ceilinged cell -- the thing §5's lever could not do.

**Measured, and the hypothesis is refuted.** 32 completing games of the H5 s8008 cell, both arms:

| | off | on | |
|---|---:|---:|---|
| **charged units** | 68,842,378 | 68,842,478 | **1.0000x -- fewer on 0 of 25 games** |
| play digests | | | 0 of 32 differ (identity held) |
| wall | 1,198.4 s | 1,198.0 s | 0.9996x, median 0.9960x, 16/32 faster |
| abandonment (49-game run) | 17/49 | 17/49 | 0 rescued, 0 lost |

248,695 skips fired and removed **zero** simulated turn-steps. The skipped work was already being
collapsed by the memos. And the one positive-looking figure -- ceiling-bound wall -- is **unstable
across two runs of the same 17 games**: 0.9694x (34 jobs) vs 0.9036x (98 jobs), while the off-arm moved
only 1.8%, so it is pool contention rather than the lever.

**Verdict: kept, default OFF, measured NEGATIVE.** The firing counter stays so the next person can see
it is live and still worthless.

### Two process traps from this section

1. **`MTG_SLOW_GAME_MS=0` SILENCES the per-game report; it is not a 0 ms threshold.** Three scripts
   here set 0 expecting "report every game" and emitted no unit lines at all, which is why the first
   49-game readout printed `NO PAIRED UNIT DATA` for its headline metric. Use `=1`. (The §5 unit
   comparison is unaffected -- it read `[goldfish] ABANDONED` lines, a separate path.)
2. **A firing counter with no PRINTER is not a firing counter.** The first `MTG_FS_PRE_STATE_SKIP` A/B
   was read with the counter wired and never reported, so its 0.9694x had no evidence behind it that
   the new path ran at all -- the standing trap in
   [[digest-equality-can-mean-broken]]. Trace that it fires BEFORE reading any A/B.
3. **Selecting the population for the metric makes the metric vacuous.** That same A/B sampled only
   the 17 games that were ALREADY ceiling-bound and then reported "abandonment 17/17 -> 17/17" --
   a result selected for, which also removed the marginal games where a rescue is the most likely.

## 7. The Snow cast order, measured at last -- and it is worse (2026-09-25)

`MTG_SNOW_CAST_ORDER` and its five sub-levers (`SNOW_ORDER_FIXER`, `SNOW_ORDER_SPLIT`,
`SNOW_ACT_ORDER`, `SNOW_ORDER_DRAW_EARLY`, `SNOW_ORDER_TAPDRAW_EARLY`) were written from the user's own
2026-09-15 and 2026-09-18 wording, have shipped default OFF since, and **no doc in this repo recorded a
verdict on any of them.** The arms existed; the measurement did not. `logs/snowopt/castorder_sweep.sh`
was written on the morning of 2026-09-25 and never run. This is that run.

7 arms x 4 seeds (**9101-9104**, held out from every tier -- smoke 1001, regression 2002/3003, overnight
4004-7007 -- and from the 9001-9004 the self-funding work used) x d0/d3/d5, **84 jobs, 40,600 games, ONE
pooled batch**, 32 threads, 1,546 s wall / 48,746 s CPU (3,181% = 31.8 of 32 cores, checked at 90 s).
Measured on engine `afc34763`; two viewer commits (`e7b6c6e0`, `c2316d0f`) landed during the run and
touch `SpellEffects.h` / `GameLogger` / `main.cpp`, so re-run before quoting these digests against a
later HEAD -- the MEANS are what the argument rests on, and those are differences between arms that all
shared one binary.

| arm | d0 (4,000) | d3 (1,200) | d5 (600) | seeds better/worse/= |
|---|---:|---:|---:|---|
| base (shipped) | 6.6990 | 6.0541 | 5.9666 | -- |
| `order` (the full rule) | +0.0040 | **+0.0125** | +0.0050 | 0/4/0 . 0/4/0 . 0/3/1 |
| `drawearly` | +0.0020 | +0.0059 | +0.0050 | 0/4/0 . 0/4/0 . 0/3/1 |
| `drawtap` | +0.0020 | +0.0059 | +0.0050 | 0/4/0 . 0/4/0 . 0/3/1 |
| `nofixer` (order minus the hoist) | **+0.0303** | +0.0075 | +0.0084 | 0/4/0 . 0/4/0 . 0/4/0 |
| `nosplit` (order minus the tie split) | +0.0040 | +0.0125 | +0.0050 | 0/4/0 . 0/4/0 . 0/3/1 |
| `noact` (order minus Sheets-before-Augur) | **-0.0020** | +-0.0000 | **-0.0066** | 4/0/0 . 0/0/4 . 4/0/0 |

**`ident` is 0 of 4 in every cell of every arm**, so each lever demonstrably reached the play -- the
`MTG_FS_PRE_STATE_SKIP` trap from section 6 does not apply here.

**The headline: `MTG_SNOW_CAST_ORDER` as written is WORSE at all three depths, on every seed.** 0/4/0 at
d0 and d3 is 8 of 8 paired cells against it. It stays default OFF, and now for a measured reason rather
than a procedural one. The user's caution about the rule was well placed.

### The attribution is worth more than the headline, because the arms disagree in three ways

1. **The Astrolabe hoist is LOAD-BEARING, and it is the only sub-lever that clearly earns its place.**
   Removing it costs **+0.0303 turns at d0** -- 7.6x the whole `order` arm's own regression, and worse
   on 4 of 4 seeds at every depth. So within the ordering, putting the fixer right after the land drop
   is doing real work; the rule's net loss is in spite of it, not because of it.
2. **`MTG_SNOW_ACT_ORDER` (Scrying Sheets activates before Frost Augur) is the harmful part.** Dropping
   it improves `order` by 0.0060 at d0 and **0.0117 at d5**, and carries `noact` BELOW the shipped
   baseline: 4 of 4 seeds better at d0, 4 of 4 better at d5, 4 of 4 exactly equal at d3 -- **8 better,
   0 worse, 4 tied**, p ~ 0.004 on a sign test over the non-tied cells. That is the same evidential
   strength that refuted `MTG_ACT_TAP_RESERVE` in section 2c, and it points the same way: on this deck,
   a rule about WHICH snow source acts first costs more in mana flexibility than the ordering buys.
   Sheets taps for `{C}` as well as digging, so committing it first spends a source the turn's other
   activation may have needed -- the identical mechanism, at the activation layer instead of the
   payment layer.
3. **`MTG_SNOW_ORDER_SPLIT` changes the play and changes NOTHING ELSE.** `nosplit` matches `order` to
   four decimal places in all three depth cells -- and the per-cell digests all DIFFER
   (`order_d0_s9101 = d7736425660c2749` vs `nosplit_d0_s9101 = acf94116da96e931`). So the within-cost
   tie split (mana -> permanent -> non-permanent) reorders real casts in every game and moves the
   outcome in none of them, across 5,800 games. It is not inert code; it is a distinction without an
   outcome, which is the cheapest kind of lever to delete.
4. **The draw band helps the rule and does not save it.** `drawearly` halves `order`'s d0 and d3
   regressions (+0.0040 -> +0.0020, +0.0125 -> +0.0059), which is evidence FOR the user's 2026-09-18
   ask ("move all of the draw to just after Astrolabe") as an amendment to the rule -- but both draw
   arms are still worse than base at all three depths on 4 of 4 seeds. And `drawtap` is byte-for-byte
   the same result as `drawearly` in every cell, so moving the tap-draw PERMANENTS into that band is a
   no-op on this list.

### What this does and does not authorise

Nothing here is adopted. Cast order is user-reviewed per deck by the set site's own rule, and the one
arm that measures BETTER than the shipped baseline (`noact`) is a play change worth 2-7 thousandths of a
turn -- which is the magnitude the section-2c held-out work showed can flip sign between depths on a
single seed. The 8-of-8-plus-4-ties pattern is stronger than that, but `noact` is still
`MTG_SNOW_CAST_ORDER` ON, i.e. adopting it would turn on the whole ordering machinery to gain a
hundredth of a turn, and it has not been run against the 93-config smoke tier at all.

The two conclusions that ARE settled, and cost nothing to act on:

* **`MTG_SNOW_CAST_ORDER` stays OFF**, measured, not deferred.
* **`MTG_SNOW_ORDER_SPLIT` is outcome-neutral on 5,800 games** while provably changing play, so it can
  be deleted rather than carried as an untested arm.

The useful negative for planning: none of this touches the abandonment rate, and the whole sweep's
effect range (0.03 turns at the extreme) is an order of magnitude below what the H5 matrix's censoring
does to its own mean win turn. Cast order is a play-quality question on this deck, not a cost one.
