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

| | first run (old binary) | resumed probe (new binary) |
|---|---|---|
| rate | 9.8 roll7/s | **0.63 roll7/s** |
| slow (>30 s) cells | 620 in ~9 h | **640 in 15 min** |
| worst cell | 25,713 s | 873 s |

The rate fell *despite* the speedup because the odometer left the cheap corner of the hand space.
The first run only ever covered **5.3%** of cells, all of them free of buckets 0-6; the probe has
moved into the **Doubling Season x3/x4** cells (540 of its 640 slow cells hold Doubling Season, 247
hold Mycoloth). Four Doubling Seasons is a **16x** token multiplier and Sol Ring deploys it early,
so those boards are astronomically wide. Pre-optimization those same cells would have been ~65x
worse -- i.e. 10+ hours EACH.

**The lesson for sizing: an average rollout rate measured early is not a projection.** The cell space
is wildly heterogeneous and the odometer walks it in bucket-index order, so the early rate is
sampled from the cheapest corner. The 86 h floor-pass estimate above was optimistic for that reason,
not pessimistic.

## Related

* `docs/design/slow-rollout-tail-and-the-uncharged-greedy-walk.md` -- the *other*, independent cost
  axis: cost per rollout, and the budget that fails to bound it (worst generation rollout here:
  198.9 s). Fixing that shortens each of the 3,044,192 rollouts; it does not reduce their number.
  Both are needed; neither substitutes for the other.
* `docs/design/keepgen-no-off-switches.md` -- why the floor/cap schedule has no knob to cheapen.
