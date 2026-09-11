# Make the plan-cache budget a GLOBAL pool (user-approved 2026-09-11, to implement AFTER the current set)

## Why

`plancache`'s budget is `thread_local` (`t_enum_bytes + t_bp_bytes`), so `MTG_PLAN_CACHE_KB` divides the
memory 32 ways instead of pooling it. The 2026-09-11 `MTG_ENUM_HIWATER_KB=262144` diagnostic on a Melira
batch measured what that costs:

* **162 single enumerations exceeded 256 MB** in ~40 jobs. Sites: `m1` 154, `bp` 8.
* **Largest single enumeration 1,545 MB; median of the giants 776 MB.**
* At the 256 MB/thread cap, **all 162 were REFUSED** -> recomputed. The cap is not a dormant guard on
  this deck, it binds constantly.

Same 8 GB ceiling, two ways to spend it:

| scheme | outcome |
|---|---|
| per-thread, 32 x 256 MB | a worker needing 1,545 MB cannot have it; 31 others hold 256 MB each mostly idle. ALL giants refused. |
| global pool, 8 GB | one giant = 19% of the pool, two = 38%, three = 57%, four = 75%. All satisfied, ceiling still hard. |

User: *"We have 32 threads, so there is a good chance that some of them won't be running heavy games, so
the design definitely has notable upside"* and *"the risk might be worth it given that we can actually let
heavy games stretch their legs."*

The precedent already exists and is in production: the FSL byte pool (`TurnSolver.cpp:32029`) is GLOBAL for
exactly this reason -- *"a uniform cap cost the 6.4 h monster 3.3x while most of the budget sits unused...
A shared pool lets the rare monster draw millions of entries while typical workers hold thousands, and the
GLOBAL bound holds no matter which workers peak together."*

## The change: 3 acquires, 6 releases, one file

```
accumulators   30489-30490   t_enum_bytes, t_bp_bytes   (KEEP: they become this thread's BORROWED amount)
Fits()         30494         called at 30738, 30855, 38997, 39003
acquire (+=)   30740, 30857, 39005
release (= 0)  30660, 30755, 30784, 30871, 30888, 39001
```

```cpp
inline std::atomic<long long>& PlanPoolUsed() { static std::atomic<long long> v{0}; return v; }
inline long long PoolBytes();            // MTG_PLAN_CACHE_KB * 1024 -- now GLOBAL, not per-thread
inline bool Fits(long long add)
{ const long long b = PoolBytes();
  return b <= 0 || PlanPoolUsed().load(std::memory_order_relaxed) + add <= b; }
inline void Acquire(long long n, long long& local)
{ local += n; PlanPoolUsed().fetch_add(n, std::memory_order_relaxed); }
inline void ReleaseAll(long long& local)
{ if (local) { PlanPoolUsed().fetch_sub(local, std::memory_order_relaxed); local = 0; } }
```
Relaxed atomics are correct for the same reason the FSL pool documents: the counter guards MEMORY, not
results, and the insert rate is far below the lookup rate.

**THE INVARIANT, and the only real hazard: every `= 0` must first RELEASE its current value.** A single
missed release leaks the pool permanently, and the symptom is the nastiest kind -- "pool full forever" =
recompute everywhere = a uniform silent slowdown with BYTE-IDENTICAL PLAY, because the contract is
result-neutral. Nothing looks broken.

Mitigation, to land WITH the change, not after:
* a high-water mark, and
* a check that `PlanPoolUsed()` returns to ~0 at decision boundaries / end of batch. If the counter
  ratchets upward across a batch, a release is missing.
* release on thread exit as a backstop (workers are long-lived, so the decision-boundary release is the
  real mechanism).

## SEMANTICS CHANGE -- `MTG_PLAN_CACHE_KB` meaning shifts by 32x

It currently means PER THREAD; afterwards it means GLOBAL. Nothing ships it (default 0 = unbounded), so
only the batch scripts are affected, but they must be updated in the same commit:
`262144` (256 MB/thread = 8 GB total) becomes `8388608` (8 GB global). Getting this wrong the other way
would set a 256 MB global pool -- 1/6th of ONE observed giant enumeration.

## Verification (the change is self-checking)

1. **Units must be BYTE-IDENTICAL**, not merely close, between old and new on the same seeds -- the pool
   guards memory, not results. Any units movement means the implementation is wrong. Per-block digest
   equality is the proof.
2. **Wall** on the two known heavy cases: the 175 s fivecolour monster (`--seed 9982205 --game-index 205
   --games 1`, 68 s single-threaded) and a Melira batch. This is what we are buying.
3. **Peak RSS** must stay under the ceiling -- and note Melira's 23 GB kills came from these very
   enumerations being UNBOUNDED (1.5 GB x 32 workers = 49 GB), so the bound must be hard, not soft.
4. **Thrash check** (the user's stated risk): with a 776 MB median and the pool near its ceiling, grant/
   refuse can oscillate, and recomputing a 1.5 GB enumeration is not cheap. Measure, do not assume:
   compare wall at pool = 4 GB / 8 GB / 12 GB. Units identical throughout by construction.

## Sequencing

User: *"When we are done with the current set of changes at least."* So: finish the in-flight measurement
+ adoption pass first, then implement. Also per CLAUDE.md, never build beside a running batch (23 GB box).
