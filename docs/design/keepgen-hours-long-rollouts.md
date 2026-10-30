# Hinata2 keep generation: the hours-long rollouts (2026-10-30)

**Status: DIAGNOSED AND FIXED (play-identical).** The fix is on branch `keepgen-slow-tail-fix` (commit on
top of the generation's own tree `7b22b6db3`, `src` = `beb79a7eb977783b4188e3280f257ee9f38e1f52` before
the change), lever `MTG_BP_SUMMARY` (DEFAULT ON, `=0` restores the old path). It takes the generation's
seven worst rollouts from **5-15.7 h to 2.5-9 min** (single thread, on a loaded box), with the keepgen
play digest unchanged (`79871cfd49865d33`) and per-rollout units identical. Measurement details are in
section 5. Commits `69924ec05` + `31298eff3` (local); applied to the worker as `2bb4176a3` + `dbd062489`
(`src` tree `303bb918`), and Hinata2 resumed from its journal on 2026-10-30.

USER: *"Games that are in the hours range should be practically nonexistent when we run with a budget."*
USER: *"If we can heavily optimize it with caching that should be better than just charging it."*
USER: *"If we are adding caches we should make sure they don't change the units."*

Generation: worker `artifact-worker-MagicDeckTester2-76d2cc`, recipe `fast` (R=30), K=20, d1 / b3 ms,
`max_turns` 8, `MTG_DECISION_WORK_X=1000`, PGO binary, 24 threads. Profiling build: worktree
`.claude/worktrees/keepgen-prof` (detached at `7b22b6db3`), `./build.sh profile`. Fix:
`.claude/worktrees/keepgen-fix`. Scratch, scripts and logs: `build/scratch/keepgen-slow/` on the volume.

## 1. The data (the generation's own slow log)

`slow.log` holds every keep rollout over 30 s since the run began (2,075 lines; `gen.log` carries the same
set). Wall is per rollout, under full-box load:

| band | rollouts | thread-hours |
|---|---|---|
| > 30 s | 2,075 | 198.3 |
| > 10 min | 81 | 159.8 |
| > 1 h | 38 | 141.2 |
| > 4 h | 13 | 99.7 |

**What the tail costs.** Five run segments (four resumes) total 78.6 h of wall, about 1,886
thread-hours. Rollouts over 30 s account for 10.5% of all thread-hours, and those over 1 h for 7.5%,
although they are 81 of roughly 9 M rollouts. The wall cost is larger than that share: the 300 s
monitor shows **6.6 h (8.4% of wall) at under 20% of median throughput**. During those stretches every
thread sat inside a straggler; one stretch had the journal silent for 11,255 s. Each resume also
discarded the tail rollouts in flight and reran them from zero, which the slow log cannot show.
Estimate: bounding the tail saves roughly 6-10% of this generation's wall and removes the stalls.

**Which hands.** The comparison is against the cell population (431,144 size-7 cells, uniform over
compositions), not the deck draw:

| starting hand | all cells | slow 30-60 s | > 1 h |
|---|---|---|---|
| >= 2 Forbidden Orchard | 8.0% | 69% | 71% |
| >= 3 Orchards | 1.8% | 35% | 39% |
| >= 1 Soulfire Eruption | 30% | 37% | 53% |
| >= 1 Hinata | 30% | 11% | **3%** |

A hand without Hinata plays long. The opponent gets one 1/1 Spirit per Orchard per turn
(`SpawnForbiddenOrchardTokensTurnStart`). When Soulfire finally resolves on that wide board, it stages
one card per opponent creature into hand (`SoulfireTargetCount` / `SoulfireDig`, `SpellEffects.h`), and
the hand reaches 24-31 cards by T7-T8.

## 2. The mechanism (measured)

**Units vs wall.** The units are normal; each unit is very slow. Replay of `size7 play r=1` (Soulfire,
2 Ponder, 4 Orchard; 11.6 min in the gen), Profile build, one thread: 583.6 s, win T6, 45,119 units.
The decisions ran T1 108 s for 6,779 units, T2 399 s for 16,238, T3 54 s for 3,924, and T4 21.7 s for
17,520. That is 16-25 ms per unit, against roughly a microsecond normally. On the seven worst seeds,
the first T1 decision ran for more than 2.5 h.

**Where the wall goes.** Measured with `MTG_BP_DERIVE_SLOW_MS`, `MTG_BP_USE_CENSUS` and perf on the >1 h
seeds, first ~28 min of each replay:
* **60-75% of wall** is in breakpoint-continuation derivations that take over 1 s each
  (`BpEnumEntryFor -> BpDeriveContinuationList -> EnumeratePlansWithLand -> EnumeratePlans`). All of
  them are `reason=2` (the wave-variant `eligible` consumer in `ApplyPlanDirect`) at `rollout_nest=1`, on
  T7/T8 boards with a 24-31 card hand. Each list holds **0.17-0.77 M plans and 0.6-3.3 GB**, because a
  Plan is 496 B plus ~7 Actions of 512 B each.
* **98.4-99.6% of that time re-derives a key the thread had already derived.** Each replay holds only
  **2-8 distinct lists**, each derived 60-150 times, in a perfect cycle.
* **Why the cache misses:** the shared plan pool is 100 MB (`plancache`), so every list is refused and
  served from `BpEnumEntryFor`'s one-entry scratch, which is reset at the next lookup. A refused store
  also wipes the thread's whole bp cache (clear-on-full).
* **Why the cycle:** the deferred wave walker visits ranks k = 0, 1, 2, ... of the list (with
  `MTG_BP_UNIFORM_DEV`, default ON, variant k takes rank k at EVERY breakpoint it reaches). Every variant
  re-applies the turn, reaches the same 5 breakpoint states in order, and re-derives all 5 lists to read
  one entry of each. That is about 10-15 s per rank. The walk is QUADRATIC: R ranks cost R x 5 full
  derivations.
* **Why the budget does not bound it:** a derivation charges no units (measured: gamework and
  decisionwork deltas are 0 across every big derivation). A rank therefore bills a handful of units for
  10-15 s of work, and the 3 ms budget and the 1000x decision ceiling allow hundreds to thousands of
  ranks. This is the same unbilled-enumeration class as EDF (`edf-slow-rollout-tail-2026-10-17.md`) and
  AccomplishedAlchemist.

perf, base worst seed (2.5 h, flat): `plancache::ApproxPlansBytes` 8.5% (pricing lists that will be
refused anyway), `vector<Action>` destroy/copy 14%, `BpDeriveContinuationList` 6.8%,
`AppendSubdecisionAxes` 10%, `Plan` moves 4.6%, `PlanSignatureFast` 2.6%, `EnumeratePlans` walk.

**Hypotheses that did NOT hold:**
* H2 (per-node board-width cost): the per-unit cost explodes only inside these derivations.
* "A bigger dig is the cost": the dig only makes the hand big. The cost is re-deriving the resulting
  list once per walked rank.

## 3. The fix: a continuation-list SUMMARY (`MTG_BP_SUMMARY`, default ON)

`src/ai/TurnSolver.cpp`, namespace `bpsum` (beside `bpcache`). There are two halves.

* **Filed** in `BpEnumEntryFor`'s scratch path, i.e. only for a list the pool refused. It stores: the
  length; the first H entries (the HEAD); the demoted list; whether any entry is apply-empty; every
  entry's put-back identity; and the first 8 entries that open a further breakpoint (with their plans).
* **Served** by `BpSummaryServe` at the top of the `eligible` block, whenever everything that block will
  read is in the summary: the rank is below H, or the chain slot is covered. Otherwise the full block
  runs, which means a derivation, exactly as before.
* **The head grows on demand.** A rank past H falls back. That fallback derivation refiles the entry
  with at least 2H (and at least the missed rank), so walking R ranks costs O(log R) derivations per
  list instead of R.
* Memory is a per-thread LRU byte cap, `MTG_BP_SUMMARY_KB` (default 192 MB). The cap affects speed only,
  never play (section 5).

**Why it is exact (hit == miss, units included).**
* A summary is filed only when the derivation charged no unit (gamework and decisionwork unchanged) and
  made no nested bp-cache lookup. A re-derivation's cost depends on the memo state it meets, so units
  cannot be replayed honestly, and those lists are never summarised.
* What the derivation did move is replayed on a hit:
  * the four MONOTONIC breakpoint counters its trial applies bump (`g_bp_any_last`,
    `g_bp_classon_last`, `g_bp_latent9_last`, `g_bp_truncasym_last`), as recorded deltas;
  * the two LAST-APPLY channels (`g_bp_seen_last`, `g_bp_fired_last`). These are set to the derivation's
    final value if it wrote them absolutely (an assignment or reset, counted by
    `g_bp_seen_absw` / `g_bp_fired_absw`), else moved by its increments.
* Before serving, a hit does to the plan cache exactly what the miss path would do with the list: the
  clear-on-full. If the list would now fit, the hit falls back to the full path, which stores it.
* It uses the same key and epoch rule as the cache. The diagnostic whole-list readers
  (`MTG_ROLLOUT_STATS`, `MTG_BP_EMPTY_CENSUS`, `MTG_BP_CONT_TRACE`, `MTG_BP_W0_CHAIN_COLLAPSE`,
  `MTG_DEDUP_CENSUS`, the verify modes) disable it.
* `MTG_BP_SUMMARY_VERIFY=1` re-runs the full path on every hit, starting from the same channel values.
  It compares the length, has-empty, the served plan's fingerprint, the chain index, and the
  re-derivation's units and counter deltas against what the hit replayed. It reports every BAD live.
  Diagnostic only.
* Diagnostics: `MTG_BP_SUMMARY_STATS`, `MTG_BP_SUMMARY_TRACE=<n>`.

**What it does NOT change:** the main bp cache, its pool, or which lists it stores. In particular it
leaves alone the cache's existing non-neutrality: a main-cache HIT saves the units its derivation would
charge (`TurnSolver.cpp:78821` in `ClearPerGameCaches`, and `:95231`, gen tree). Measured there on
KittyEquipment v2 d5 s2002 gi34: 48,188 units with the shipped cache against 58,187 epoch-scoped and
58,273 uncached. Fixing that moves play, so it is for the USER.

## 4. Things considered and rejected

* **A one-entry keyed scratch:** 0 hits, because the repeats cycle over 2-8 lists and are never
  adjacent.
* **Caching full lists:** 3.4-13.7 GB per heavy thread. Not viable at 24 threads.
* **Cutting the first derivation** (building only the summary): not exact. The length (read by the wave
  loop through `g_bp_cands_last` and by the overrun test) and the ranking need the full enumeration.
* **Stopping the clear-on-full wipe:** not play-identical, because main-cache hits are not unit-neutral.
* **Charging derivations** (`MTG_BP_DERIVE_METER`, `MTG_ENUM_CHARGE`): play changes. They stay a
  separate USER decision.
* **A constant per-thread plan-pool quota** (to remove the thread coupling in section 5): dropped. USER:
  *"a 100MB quota will really starve the slow-heavy games."* It takes memory away from exactly the
  heavy threads that borrow from the shared pool today, which costs wall and still moves their units.
  The right fix is unit-neutral hits (the cache audit). After that, capacity affects only speed and a
  shared pool is fine.

## 5. Verification

All runs use the generation's `src` tree plus the fix, `MTG_DECISION_WORK_X=1000`, and the gen's
incumbent buckets (`MTG_KEEP_BUCKETS_FROM`, a copy of the worker's `.PREV.json`). Replays go through
`MTG_KEEP_REPLAY` / `MTG_KEEP_REPLAY_LIST`; the list mode now also prints each rollout's units. The host
was heavily shared during these runs (load 33-42 from another container), so absolute walls are
inflated; ratios and units are what count.

**Play digest.** `79871cfd49865d33` in every run: summary off, on, 8 MB cap, verify mode, 1 thread
and 4 threads. That is the generation's digest, so the Hinata2 journal resumes.

**Units: the deciding test.** It uses single-thread processes, so there is no shared-pool coupling
(see below). The set is 300 ordinary size-7 rollouts (`MTG_KEEP_REPLAY_SAMPLE`) plus 20 tail rollouts
from the 30-60 s band:

| comparison | rollouts | units identical | win turn identical |
|---|---|---|---|
| off vs on (default cap) | 314 | **314/314** | 314/314 |
| off vs on, 8 MB cap (constant eviction) | 314 | **314/314** | 314/314 |
| on vs 8 MB cap | 319 | **319/319** | 319/319 |

(Of the 320 rollouts, 6 heavy tail ones never finished in the OFF arm: they were stopped after ~2 h
each. The on and 8 MB arms finished them in minutes.) `MTG_BP_SUMMARY_VERIFY` on the `size7 play r=1` seed: ok 39, BAD 0, and its per-decision units are
identical to the base build (6,779 / 16,238 / 3,924 / 17,520 / 653 / 5; total 45,119; win T6).

**The seven worst rollouts** (Release + fix, 1 thread each, loaded host). For comparison, the gen's
logged wall was PGO at 24 threads, and the base build's T1 decision alone ran > 2.5 h on the 15.7 h seed:

| rollout | gen | fixed | win | units | summary hits |
|---|---|---|---|---|---|
| size7 draw r=1 (Soulfire x2, Preordain, Bluffs, Orchard x3) | 15.7 h | **519 s** | T8 | 473,322 | 15,152 |
| size6 draw r=10 (Soulfire x3, Orchard x3) | 14.0 h | **292 s** | T8 | 472,767 | 15,651 |
| size6 draw r=15 (Soulfire x2, Pool, Island, Orchard x2) | 10.2 h | **338 s** | T8 | 473,358 | 14,504 |
| size6 play r=22 (Mountain x2, Soulfire, Bluffs, Orchard x2) | 9.75 h | **276 s** | T8 | 473,075 | 15,915 |
| size6 draw r=3 (Reality Spasm x2, Orchard x4) | 7.87 h | **318 s** | T8 | 152,145 | 11,216 |
| size6 draw r=25 (Mountain, Pool, Ornithopter x2, Orchard x2) | 7.59 h | **248 s** | T8 | 16,364 | 4,892 |
| size6 draw r=8 (Wake, Spasm, Ponder x3, Orchard) | 5.0 h | **153 s** | T8 | 475,253 | 14,513 |

Every one of these files its lists side-effect-free: "not filed (side effects)" is 0 everywhere.
**Pooled (4 threads, summary ON):** all 360 rollouts (the 60-rollout tail list + 300 ordinary) finished
in 38 min of wall. The OFF arm, under the same conditions, had finished 22 rollouts after 3 h. One of
them took 10,304 s off and 316 s on.

**The pre-existing thread coupling (NOT the summary).** In the 4-thread arms, 2 of 22 paired rollouts
differ by 1-4 units (420,198 off / 420,194 on / 420,196 8 MB). The same rollout alone gives 420,204.
That rollout takes 78 s with summary OFF in the 4-thread run, but more than 1.5 h alone. The plan pool
(`plancache::Fits`, `TurnSolver.cpp` ~78149 gen tree) is ONE global budget of `workers x 100 MB`
(`membudget::DefaultPlanCacheKb`). A list of 100-400 MB therefore fits at 4 workers and is refused and
re-derived per rank at 1. So the pool's capacity depends on the worker count and on what other threads
hold, and refusals move units, because main-cache hits are not unit-neutral (section 3). A constant
per-thread quota (e.g. 100 MB) would decouple it, but the USER rejected that: it starves exactly the
heavy threads that borrow from the shared pool today, which costs wall and still moves their units. The
right fix is to make hits charge what a miss would (the cache unit-neutrality audit,
`docs/design/cache-unit-neutrality-audit.md`). Once that is done a shared pool is fine, because capacity
then affects only speed.

**Unit tests** (Release, summary default ON): 2,045 / 2,045 test cases, 3,559,153 assertions, 0 failed.

**Commits** (branch `keepgen-slow-tail-fix`, linear on `7b22b6db3`; `src` tree after them
`303bb918f3025a710daf8201e068a0fac25dd191`, NOT pushed):
* `69924ec05` diag(keepgen replay-list): per-rollout units in the list output
* `31298eff3` search(bp continuations): the continuation-list SUMMARY (`MTG_BP_SUMMARY`, default ON)

They cherry-pick onto the worker (HEAD `68c68ff0b`, the same `src` tree `beb79a7e`). The play digest is
unchanged, so the Hinata2 journal resumes. `MTG_BP_SUMMARY=0` (an `EnvOn` read) turns the memo off;
`MTG_BP_SUMMARY_VERIFY` stays default off.

**Memory at 24 threads (pathological case: pre-existing, not the summary).** All 81 tail rollouts over
10 min were run concurrently on 24 threads (`MTG_KEEP_REPLAY_LIST`). That run hit the RSS cap (35.28 GB,
`MTG_RSS_CAP_GB`) about 36 s into the replay, peak 38.6 GB. The identical run with `MTG_BP_SUMMARY=0` also
hit the cap, about 55 s in: RSS went 16 -> 27 -> 35 GB in 20 s, with the plan pool full at 2.4 GB. The
cause is 24 threads at once each holding a GB-scale transient continuation list (0.6-3.3 GB) while
deriving. The summary does not add to that: it holds at most `MTG_BP_SUMMARY_KB` (192 MB) per thread,
and it shortens the time a heavy rollout spends in that state.

A realistic mix was stopped at about 2 min to hand the box back to the generation: the 60 tail rollouts
of 30 s-10 min plus 2,000 ordinary samples, 24 threads, summary on. RSS was 7-9 GB at 80-110 s. The
generation's own rss-cap monitor is the live check from here; the old binary peaked at ~27 GB and never
aborted.

## 6. Open (for the USER)

1. The main bp cache's unit non-neutrality (section 3). Leaving it alone keeps Hinata2's journal
   resumable.
2. Wave-walker depth inside horizon playouts: the walker spends a decision's budget walking hundreds of
   ranks of a 0.2-0.8 M-plan list, and each rank costs almost nothing in units. With the summary that is
   now cheap. Whether those ranks are worth walking at all is a search-design question, not a waste one.
3. Orchard modelling: every Orchard is tapped every turn (`cards.json`), and the search never weighs
   not tapping one. That assumption is where the board width comes from.
