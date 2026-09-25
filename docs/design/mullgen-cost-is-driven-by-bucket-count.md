# Mulligan-gen cost is driven by BUCKET COUNT, and a mana-base change can multiply it

*Measured on Fungus candidate B (`decks/Fungus/candidate-b-2026-09/`), 2026-09-24.*

## The finding in one line

Two Fungus lists that play almost the same deck differ by **12.1x in keep-table size**, and the
entire difference is the mana base. Nothing about the engine changed between them.

| list | K | bucket caps | size-7 cells |
|---|---|---|---|
| shipped `decks/Fungus` | 14 | **[19,** 4,4,4,4,4,4,4,4,4,3,2,2,1] | 62,936 |
| candidate B | 22 | [4,4,4,4,4,4,4,4,4,4,4,2,2,2,2,2,1,1,1,1,1,1] | **761,048** |

The shipped list's 19-cap bucket is not a clever equivalence merge. It is **`Forest x19`** — one
card, nineteen copies. Candidate B replaced it with a five-type mana base (Forest, Peat Bog,
Hickory Woodlot, Blooming Marsh, Secluded Courtyard, four each). One bucket became five, and the
hand space is a multiset count over buckets, so it multiplied.

**This is the lesson: a deckbuilding change that looks orthogonal to the mulligan table -- swapping
basics for a real mana base -- is the single biggest input to what that table costs.** It is not a
tuning problem and no budget fix touches it. 761,048 cells stay 761,048 cells.

## What it cost, measured

Candidate B, `fast` recipe (R30, floor R=2, adaptive bottoming), 24 cores, **23.9/24 busy** --
i.e. fully saturated, so this is raw work volume and not a scheduling defect:

```
monitor: 3600s  phase=floor  roll7=2233   frozen=0/1522096 (0.0%)  sub=0/560212  journal=998
monitor: 3900s  phase=floor  roll7=5559   frozen=0/1522096 (0.0%)  sub=0/560212  journal=2660
monitor: 4200s  phase=floor  roll7=8028   frozen=0/1522096 (0.0%)  sub=0/560212  journal=3895
monitor: 4500s  phase=floor  roll7=11042  frozen=0/1522096 (0.0%)  sub=0/560212  journal=5402
```

9.8 roll7/s and 4.9 journal/s => **2.44 core-seconds per generation rollout**, at depth 1 with a
3 ms per-decision budget and an 8-turn horizon. The floor pass alone (R=2 over 1,522,096 cells) is
**~86 h = 3.6 days**, with adaptive refine toward cap R=30 and 560,212 sub-table batches still to
come. `frozen` was still 0.0% after 15 minutes.

For contrast the shipped list's floor pass, at the same measured rate, is ~7 h.

## Pricing the fix before spending the hours

Bounded multiset counts (per-bucket copy caps, not `C(K+6,7)`), floor pass at the measured 9.8/s:

| merge | K | size-7 cells | vs none | floor pass |
|---|---|---|---|---|
| none (as run) | 22 | 761,048 | 1.0x | 86.3 h |
| 2 tapped lands (Peat Bog, Hickory Woodlot) | 21 | 547,305 | 1.4x | 62.1 h |
| 3 untapped (Forest, Blooming Marsh, Secluded Courtyard) | 20 | 385,994 | 2.0x | 43.8 h |
| both groups, separately | 19 | 266,512 | 2.9x | 30.2 h |
| all 5 lands as one bucket | 18 | 179,160 | 4.2x | 20.3 h |
| all 5 lands + 5 small thallids | 14 | 42,028 | 18.1x | 4.8 h |

**Read this table as a warning, not a menu.** Even collapsing the entire mana base into one bucket
-- a strong and probably indefensible claim, since Peat Bog enters tapped and depletes while Forest
does neither -- leaves a 20 h floor pass. There is no merge here that turns candidate B into an
overnight job while remaining honest about the cards. The conclusion that follows is about
*scheduling the work*, not about finding a clever merge.

## The two merge routes are NOT interchangeable, and the difference is 58 minutes

Both end up in `cfg.force_merge` and both reach `bucket_fp` the same way, so they produce identical
bucketing and pool identically (`ExhaustiveKeep.cpp:841`). They differ in the **discovery cache**:

* **`<stem>.buckets.json`** (`BucketPolicy.h`, loaded at `main.cpp:177`) -- its canonical form is
  `eq_policy_fp`, which **is** part of the discovery cache key (`ExhaustiveKeep.cpp` ~790,
  `policy_fp`). Adding or editing it **forces full re-discovery**. On candidate B that is **58
  minutes** (3484 s). It is the committed, reviewable route, it demands a `why` on every group, and
  it survives regenerations, threshold changes and card edits.
* **`MTG_EQUIV_FORCE_MERGE`** (`main.cpp:232`) -- read straight into `cfg.force_merge` and applied
  at `ExhaustiveKeep.cpp:843`, *after* the cache read. It is **not** in the cache key, so it
  **keeps** an existing gencache.

So: experiment with the env var (free), commit the ruling as `.buckets.json` (costs one
re-discovery). Do not reach for the file first while iterating.

## Consequences worth acting on

1. **Price K before starting a generation, not after.** K is known the moment discovery finishes,
   and `ExhaustiveKeep.cpp:922` already prints it. The cell count and a measured rollout rate give
   a wall-clock estimate in one line of arithmetic. A gen that will take days should be refused at
   that point, the way the K-mismatch gate at `:907` already refuses a changed K.
2. **`expected_buckets` is a cost contract, not just a shape guard.** It exists so K cannot move
   silently; the same number predicts the bill. Recording it for a list is what makes the *next*
   list's K change legible as "this got 12x more expensive".
3. **A mulligan profile is not automatically worth generating for a non-final list.** Candidate B
   is explicitly a candidate. Spending days of box time fitting a keep table to a mana base that
   may not survive the next revision is the expensive kind of thorough.

## UPDATE 2026-09-25 — after a 65x engine speedup, the verdict is UNCHANGED

The slow rollouts this run exposed were largely an O(N^2) board-width defect in the haste scans,
fixed in `b4c7f657` + `19d3e3a8`: the replayed slow cell went **41.0/42.9/45.1 s -> 0.65 s (~65x)**
and the smoke suite's batch makespan halved (185 s -> 88 s). Byte-identical (smoke 93 / regression
129, `play-changed: 0`). So the per-rollout cost axis is now genuinely much cheaper.

**It does not rescue this generation.** Resuming the gen on the new binary (discovery a CACHE HIT,
journal resumed at 81,298 cell-sides -- the `play_digest` is unchanged, so prior work survives):

| | first run (old binary, 36,304 s) | resumed probe (new binary, 900 s) |
|---|---|---|
| keep-rollouts (`roll7`) | 162,836 | 621 |
| rate | **4.48 roll7/s** | **0.69 roll7/s** |
| slow (>30 s) keep-rollouts | 628 (0.39%) | **12 (1.9%)** |
| worst cell | 25,713 s | 873 s |

> **Correction (2026-09-25).** An earlier version of this table read *"slow cells: 620 in ~9 h vs
> **640 in 15 min**"* and gave the first run's rate as 9.8 roll7/s. Both were wrong. **640 was the
> probe's `roll7` COUNT**, read off the monitor line and mislabelled as a slow-cell count — the
> probe's actual slow-rollout count was **12**. And 9.8 roll7/s was the *instantaneous* early rate
> (the 3600–4500 s window), not the run's average, which was 4.48. The corrected figures do not
> change the conclusion — the slow FRACTION really is ~5x higher in the probe's region, and the
> overall rate really did fall 6.5x — but they shrink the headline gap from ~50x to ~5x. The trap
> was reading a monitor line for a number it does not report; the slow count comes from
> `grep -c SLOW-ROLLOUT.*keep-rollout` on the `.slow.log`, and nowhere else.

The rate fell *despite* the speedup because the odometer left the cheap corner of the hand space.
Note what the two rows say together: slow rollouts arrive at about the same rate **per second** in
both runs (0.017/s vs 0.013/s) but the probe completes 6.5x fewer rollouts while they do, which is
what "the same tail against a much smaller denominator" looks like.
The first run only ever covered **5.3%** of cells, all of them free of buckets 0-6; the probe has
moved into the **Doubling Season x3/x4** cells (540 of its 640 slow cells hold Doubling Season, 247
hold Mycoloth). Four Doubling Seasons is a **16x** token multiplier and Sol Ring deploys it early,
so those boards are astronomically wide. Pre-optimization those same cells would have been ~65x
worse -- i.e. 10+ hours EACH.

**The lesson for sizing: an average rollout rate measured early is not a projection.** The cell space
is wildly heterogeneous and the odometer walks it in bucket-index order, so the early rate is
sampled from the cheapest corner. The 86 h floor-pass estimate above was optimistic for that reason,
not pessimistic.

## UPDATE 2026-09-25b — a second 8.7x, and the answer to "is it fast enough now?" is NO

`dc6001f6` removed the other half of the per-rollout cost: `CreateToken` recomputed Doubling
Season's multiplier by walking the whole battlefield, so `for (k < n) CreateToken(...)` was
O(n x board) — and under four Doubling Seasons Mycoloth's `n` is itself 16x inflated. It was **79%
of total runtime** on the worst cell. Bulk creation (`CreateTokens`) took the worst cell
**211.3 s -> 24.3 s single-threaded (8.7x)**, and the profile afterwards is FLAT (top symbol 9.4%).

**Like-for-like resume, same odometer region, 900 s, 24/24 cores busy both times:**

| | haste fix only | + bulk tokens | ratio |
|---|---|---|---|
| `roll7` | 621 | **1403** | 2.26x |
| journal cell-sides | 191 | **582** | **3.05x** |
| slow (>30 s) rollouts | 12 | 18 | — |

So the engine work was real and it compounds. It still does not make candidate B tractable:

* remaining floor-pass cell-sides: 1,522,096 − 81,508 = **1,440,588**
* at the measured 0.647 cell-sides/s (this expensive region): **25.8 days**
* at run 1's 2.48 cell-sides/s (the cheapest corner): **7.4 days**

Either way the **floor pass alone is weeks**, before adaptive refine toward cap R=30 and before the
560,212 sub-table batches. Two rounds of optimisation totalling ~570x on the worst cell moved this
from "impossible" to "still impossible", which is the point: **the cost is K=22, and K is a
property of the decklist, not of the engine.** The honest projection is not a single number — the
cell space is heterogeneous enough that any single rate is a statement about *where the odometer
currently is*, which is exactly the trap the correction above documents.

## UPDATE 2026-09-25c — a THIRD instance of the same family, and a profiling error worth recording

### The error first, because it is the transferable part

The obvious next move after `dc6001f6` was "read the profile, fix the top symbol". I did exactly
that, shipped five careful prefilters, and bought **1.06x** (302.8 s → 285.6 s on the worst cell).

The profile I read was real, correct, and **about a different workload**: `/tmp/candb_cell2.perf`,
3,417 samples captured in the previous session from a **17-second** cell. The worst cell in the new
slow log takes **302 seconds**. Those two cells do not share a cost centre:

| symbol | stale 17 s cell | the ACTUAL worst cell |
|---|---|---|
| `ApplySacCreatureOutlet` (inclusive) | not in the top 20 | **55.8%** |
| `ComputeLordBonus` (self + its two lambdas) | 9.7% | **56.7%** |
| `ResolveCombatDamage` | 9.4% | < 1% |
| `vector<Permanent>::push_back` | 7.8% | 2.5% |
| `PendingAttackDamage` | 5.7% | < 1% |

**The rule this yields: re-profile the cell you are actually trying to speed up, every time.** The
candidate-B cell space is heterogeneous enough that a profile is scoped to ONE hand, not to the deck
— the same heterogeneity that makes a single rollout rate a statement about where the odometer is
(see the correction in UPDATE 2026-09-25 above). A 3.4k-sample profile of the wrong cell is worse
than no profile, because it is confidently wrong. The re-capture used 29,818 samples, taken by
attaching `perf record -p` *after* the `[replay] ... -> running` line so the multi-threaded
play-digest battery is excluded.

### The defect

Deathspore Thallid's outlet ("Sacrifice a Saproling: target creature gets -1/-1") ranks its target
at resolution — deliberately, because fanning ~30 interchangeable Saprolings into plan variants is
the documented cost centre this archetype already fought. But the ranking loop asks **four
board-level questions about every creature on the battlefield** — lord bonus, aura bonus, equipment
bonus, and "would this death pay?" — and each of those answered by walking the battlefield again.
That is O(creatures × board) per activation, and `ApplySacCreatureOutletBurst` runs it once per body
sacrificed, so on a Saproling board the whole thing is **cubic in board width**.

Same family as the haste scans (`b4c7f657` / `19d3e3a8`) and `DoublerShift` (`dc6001f6`): a
board-level answer recomputed per element of a loop over the board.

### The fix

`BoardSources` + `GatherBoardSources` in `SpellEffects.h` — the generalisation of the existing
`HasteSources` and of `ResolveCombatDamage`'s hand-rolled lord/double-strike prefilter. One walk
yields six lists (haste, lords, conditional anthems, double-strike granters, lifelink granters,
attachments, paying death watchers); every consumer keeps its own per-permanent tests and iterates
a usually-empty list. Byte-identical by construction: each list is built from exactly the predicate
its consumer's loop body tests, so an empty list is a *proof* the walk would find nothing.

Wired into `ApplySacCreatureOutlet`'s ranking loop (two lists, one per controller, since it ranks
both sides), `ResolveCombatDamage`, and `PendingAttackDamage` — which turned out to be
`ResolveCombatDamage`'s un-prefiltered twin, carrying neither of the two lists that function had had
since the lord prefilter was added. Plus two purely local ones: `CreatureHasLifelink` did up to
**three** `LookupCached` calls per permanent with no `def_absent` short-circuit, and
`CreateTokenOnce` copied its 296-byte `Permanent` into the battlefield instead of moving it.

### Result

| | |
|---|---|
| worst cell, single-threaded, Profile build | **302,754 ms → 197,755 ms = 1.53x**, `win_turn=6` both |
| ... of which the first (mis-targeted) round | 302,754 → 285,630 ms = 1.06x |
| smoke batch makespan | 82 s → **75 s** |

Gates: play digest `f1f8288da6dff73e` unchanged, scenarios 103/103, unit SUCCESS (2,635,895
assertions), smoke 93 passed with `configs changed: 0` and `play-changed=0` on both arms.

**1.53x does not change the verdict in UPDATE 2026-09-25b.** The floor pass was 7–26 days; it is now
5–17 days. Still weeks, still because K=22.

## Related

* `docs/design/slow-rollout-tail-and-the-uncharged-greedy-walk.md` -- the *other*, independent cost
  axis: cost per rollout, and the budget that fails to bound it (worst generation rollout here:
  198.9 s). Fixing that shortens each of the 3,044,192 rollouts; it does not reduce their number.
  Both are needed; neither substitutes for the other.
* `docs/design/keepgen-no-off-switches.md` -- why the floor/cap schedule has no knob to cheapen.
