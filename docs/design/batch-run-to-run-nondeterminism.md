# Batch run-to-run nondeterminism — REPRODUCED (2026-09-03)

**Status: open bug, now with a ~13-minute repro.** This was first seen on 2026-08-25 (one overnight
run of three diverged, on exactly one deck's 12 cells) and has been carried since as *cause unknown,
never reproduced*. It is reproducible now, on demand, on `EldraziDisplacerFlicker`.

## The observation

Same binary, same manifest, run twice back to back. No code change, no rebuild, nothing else on the
box. `logs/edf_tapyield/out.run1.txt` vs `out.run2.txt`:

| job | avg r1 | avg r2 | digest r1 | digest r2 | verdict |
|---|---|---|---|---|---|
| off_s4101 | 7.07 | 7.07 | `1ad5deba38ddb12e` | `1ad5deba38ddb12e` | stable |
| off_s4202 | 7.07 | 7.07 | `09beee6f982df55e` | `0f8cb9712226171f` | **diverged** |
| off_s4303 | 7.09 | 7.09 | `2e730b016fbdbf4a` | `5f38514adc38a918` | **diverged** |
| off_s4404 | 7.07 | 7.07 | `46ea3a80199f7a14` | `27d61c435fc30a1a` | **diverged** |
| on_s4101 | 7.07 | 7.07 | `9d4da40162b007a3` | `9d4da40162b007a3` | stable |
| on_s4202 | 7.07 | 7.07 | `8eaf798d77bc2d75` | `41ceaec2ae505230` | **diverged** |
| on_s4303 | 7.09 | 7.09 | `3e0242dcb250f1b5` | `7e055a89fcc3b7ee` | **diverged** |
| on_s4404 | **7.06** | **7.07** | `e4199309b1e9be64` | `899f231c42bcd18d` | **diverged, avg moved** |

**6 of 8 jobs diverged in play; 1 of 8 moved its average** (by 0.01 — one game's win turn shifted by
one turn). Both arms of an A/B diverge, so this is not a property of the lever under test.

Seed 4101 is stable in *both* arms while the other three seeds are not, so the divergence is
seed-dependent rather than uniform — which differs from the 2026-08-25 sighting (uniform across
game indices, all faster) and may or may not be the same underlying cause.

## Why this matters more than its size

**A play digest is not a valid byte-identity check on this deck.** Anything that concluded "the
digests changed, so the change had an effect" — or "the digests match, so the change is inert" — was
reading apparatus noise. That is a live hazard across the repo, because digest-identity is the
standard byte-identity argument (smoke/regression "0 configs changed", the A/B off-arm control).

Two consumers to re-examine:
* **The off-arm-as-control pattern.** Running a default-off lever's OFF arm and comparing its digest
  to a previous run is the natural way to prove a lever inert. On this deck it proves nothing.
* **Averages are far more robust than digests, but not exempt.** The measured noise floor here is
  ~0.01 turns on a 100-game job. Any effect at or below that needs a repeat-run bracket before it
  means anything — the same discipline `deck-screening.md` applies to the apparatus bias floor.

The immediate consequence for this deck: the −0.135-turn mana-fix result (`analysis-Eldrazi
DisplacerFlicker.md`) stands, because it is 10x the noise floor and consistent across four seeds at
three depths — but its "all twelve digests changed" line was withdrawn as evidence.

## Reproduction

```
mtg --batch logs/edf_tapyield/manifest.json > run1.txt
mtg --batch logs/edf_tapyield/manifest.json > run2.txt
diff <(grep avg run1.txt | sort) <(grep avg run2.txt | sort)
```

8 jobs x 100 games, EldraziDisplacerFlicker at d5/b20, ~13 min on 24 cores. Seeds 4101/4202/4303/4404.

## Already refuted in the 2026-08-25 investigation (do not re-derive)

A binary difference (all copies md5-identical), a missing keep model, the rollout bottomer
(`MTG_BOTTOM_ROLLOUTS=0` was digest-identical), the `.bincache`, a sibling value model, a load-time
race (differences were uniform, not clustered early), and a plain concurrency race (48 concurrent
copies of one job gave 48/48 identical digests).

## NARROWING (2026-09-03, this session) — it needs the POOL, and it looks like budget, not play

Three experiments, each the same job (`off_s4303`, EDF d5/b20, 100 games, seed 4303):

| pool | runs | s4303 digest | jobs diverged |
|---|---|---|---|
| 1 job, 100 games (`logs/edf_nondet/`) | 3 | `2e730b01…` all three | 0 of 1 |
| 4 jobs, ONE config, 4 seeds (`logs/edf_samearm/`) | 2 | `18b562c3…` then `1630f11f…` | 1 of 4 |
| 8 jobs, two arms (`logs/edf_tapyield/`) | 2 | `2e730b01…` then `5f38514a…` | 6 of 8 |

> **RETRACTED: "it needs the pool" and "the rate scales with pool pressure".** Both were read off
> this table, and the table is UNDERPOWERED. The real unit of divergence is the GAME, and the rate
> is **~1–2% of games**; a job-level digest diverges iff at least one of its games does, so a
> 100-game job has only a ~1−e^−1.5 ≈ 78% chance of showing it at all and three identical runs is
> an unremarkable coincidence (~25%), not evidence of stability.
>
> **Measured properly: a SINGLE job of 400 games, run twice at 24 threads, diverges 8/400 (2.0%)**
> (`logs/edf_onejob/`, 2 of the 8 changed a win turn). One job is enough. Job count, mixed arms and
> mixed depths are all irrelevant — they only ever changed how many games were in the sample.
>
> This is a good illustration of the trap: every row above is *true*, and the conclusion drawn from
> them was still wrong, because "did this job's digest change" is a coarse, sample-size-dependent
> proxy for "did a game change". Count the games.

Seed 4101 never diverged in any run, and its digest `1ad5deba38ddb12e` is identical across every run
*and* across the pre-fix binary, which makes it a usable byte-identity control — with the caveat
above that "never diverged" over a handful of runs is weak evidence for any single job.

**A job run ALONE is deterministic; the same job inside a pool is not.** That kills the simplest
stories (a wall-clock read in the decision path would diverge the solo run too) and points at state
that a worker thread carries ACROSS games — the pool's interleave decides which games precede which,
and that interleave is not reproducible.

**And there is a strong hint about the mechanism.** In the single-config pool, the d5 job for seed
4202 produced digest `cdc8f27a9d0d8b9d` — which is *exactly* the digest the **d3** job produced for
that seed in the re-measurement. A d5 run landing precisely on the d3 play is not a coincidence at
32 bits of prefix; it means **the depth actually reached varied**, which is the known starvation
story (this deck's first iterative-deepening pass can consume the whole decision budget, so d3 and
d5 collapse to the same computation — see `analysis-EldraziDisplacerFlicker.md`).

That yields a concrete, testable mechanism:

> The budget is denominated in deterministic work units, but a CACHE HIT COSTS FEWER UNITS THAN A
> MISS. A cache that survives across games on a worker thread therefore makes the units consumed by
> a given search depend on which games ran before it — so at the starvation edge, where one extra
> ID pass either fits in the budget or does not, the depth reached flips with the interleave. The
> budget is deterministic; the *work* priced against it is not.

This is the same class as the already-fixed defect where the m2 solve memo's dead cross-game entries
shifted cap-clear timing and broke work-unit thread-invariance. That fix covered one cache; this
would be a second one still carrying state across games.

It also explains the seed-dependence: seed 4101 is stable everywhere because its games are not
sitting on the budget boundary, while 4202/4303/4404 are.

**Consequence if confirmed: the effect is concentrated on decks at the starvation edge**, which is
where the budget boundary is close enough for cache-warmth to tip it. That is a much narrower blast
radius than "all measurements are unreliable" — but EDF is squarely in it, and any deck whose
`id_depth` histogram is not saturated may be.

**Corroborating evidence for exactly that narrowness:** the smoke tier — 48 configs, itself a pooled
batch — reports `configs changed: 0` run after run, and the regression tier likewise. So the shipped
suites are NOT visibly affected, which is why this survived so long: the harness that would have
caught it is made of decks that are not on the boundary. Do not read "smoke is green" as "the engine
is deterministic"; read it as "no smoke deck is starved".

### CONVERGENT EVIDENCE, found independently the same day (46e8efb4)

Another agent's recoverability audit landed a fix whose description is the mechanism class above,
arrived at from a different direction:

> `BpEnumBuildKey` folds the whole heurarm `t_arm`: the bp-enum plan cache + canon verdict memo are
> **thread_local, survive batch job switches, are NOT in `ClearPerGameCaches`** — a MIXED-ARM pooled
> batch shared enumeration entries across arms whenever a lever changes enumeration output (the
> batch-pool-contamination class, second instance).

That is exactly "a cache that survives across games on a worker thread", confirmed and fixed for the
**mixed-arm** case. It very likely explains the 8-job (two-arm) result here — 6 of 8 diverging.

**It does NOT explain the single-config result, and I re-ran that case ON the fixed binary to be
sure.** Keying the cache by the heuristic arm cannot help a pool whose jobs all share one arm, and
it doesn't:

| single-config 4-job pool, run twice | diverged | averages moved |
|---|---|---|
| before the arm-key fold | 1 of 4 | 0 |
| **after the arm-key fold** | **3 of 4** | **2** (6.98→6.97, 6.93→6.94) |

So the residual is not only real, it is the *larger* half on this deck, and post-fix it moves
AVERAGES rather than just digests. (The two rows are not a controlled comparison of the fix — the
same commit also changed play via the B&B aura bound — so read them as "still broken, at least as
badly", not as "the fix made it worse".) Seed 4101 remains stable in every run, before and after.

The caches still survive job and game switches within an arm, so cross-*game* contamination remains
even with the arm folded into the key. The open question is narrower and sharper than it was this
morning:

> Which thread_local caches survive a game boundary without being in `ClearPerGameCaches`, and is
> any of them keyed on something that does not fully determine its value?

Note also that the same commit fixed a **fifth** land-Aura blind spot (the B&B max-mana bound in
`SourceMaxNet` / `source_max_net`, which could prune a payable cost). Measured here on the same
4 seeds x 100 games: **7.068 → 6.92**, another −0.145. Three independent mana-modelling fixes landed
on this deck in one day, each worth ~0.14 turns, for a cumulative **7.206 → 6.92 (−0.29)** — every
one of them larger than any tuning lever ever tried on it. That is the strongest available evidence
for the conclusion recorded in the deck ledger: on this deck, hunt modelling gaps, not levers.

### The next test, and it is cheap
Run the pooled manifest with `MTG_ROLLOUT_STATS` and compare the `id_depth` histogram and
`units_total` across two runs. If units move run-to-run on identical games, the mechanism above is
confirmed and the hunt narrows to *which* cache is not cleared per game. If units are identical and
only play differs, this is wrong and the cause is elsewhere.

## ROOT-CAUSE PASS 2 (2026-09-03, later) — it IS concurrency, and here is what it is not

Every experiment below counts DIVERGING GAMES over 400-game runs (see the retraction above for why
job digests are the wrong unit), EldraziDisplacerFlicker seed 4303, d5/b20.

### The one solid structural fact: single-threaded is deterministic

| condition | diverged |
|---|---|
| `--threads 1`, 200 games, two independent runs | **0 / 200** |
| `--threads 24`, 400 games, two runs | **8 / 400 (2.0%)** |
| serial vs 24-thread over the same games 0–199 | 198 / 200 identical |

At the measured 2% rate, seeing zero divergence in 200 serial games has probability 0.98^200 ≈ 1.8%.
**Concurrent execution is required.** The serial answer also equals the isolated single-game answer,
so serial/isolated is the "true" result and the threaded run is the one that deviates.

### And it is NOT cross-game state carried on a worker thread

The obvious story — a thread_local cache surviving a game boundary, so a game's result depends on
which games preceded it — is *false* here. Games 47 and 67 give byte-identical digests whether run
completely alone or after 46 / 66 preceding games on the same thread:

```
isolated (game alone):  47 -> 7bb35aaf366f0679 | 67 -> 7e1b0e3ef2f9b550
serial  (after 46/66):  47 -> 7bb35aaf366f0679 | 67 -> 7e1b0e3ef2f9b550
```

So it needs threads running *at the same time*, not merely a dirty thread.

### Eliminated, each at proper power or by construction

* **The bp-enum plan cache + canon verdict memo** (the leading suspect, and the thing the arm-key
  fix touched). Fully disabled via `MTG_NO_BP_ENUM_CACHE=1`: **10/400 diverged, vs 8/400 with it on**
  — an unchanged rate. Note the earlier "identical digest at cap 8192 / 64 / off" check was itself
  underpowered (one 100-game digest per arm, the same ~25%-coincidence trap); this replaces it.
* **Data races** — a ThreadSanitizer build (`build/TSan`, deliberate explicit-build-type route)
  reported **zero** warnings across three workloads (5 concurrent games; 4 jobs x 6; 4 jobs x 40
  partial). Not conclusive for a rare race, but nothing on the hot path.
* **Uninitialised memory** — `valgrind --tool=memcheck --track-origins=yes` on a diverging game:
  **0 errors from 0 contexts**.
* **The per-game abandon ceiling** — `abandon_*` are 0 here, and `need` is a property of the
  manifest, not of completion order (the code says so and means it).
* **In-flight condemnation / `max_game_sec`** — defaults to 0 without a `condemn` block.
* **The profile LRU** (mutex-guarded, keyed on path+value-sidecar, hands out `shared_ptr<const>`),
  **the sidecar-resolution memo** (mutex-guarded — already chased for the 2026-08-26 incident),
  **`LookupCached`** (`std::atomic_ref` by design), **every instrument map** (all mutex-guarded),
  **the ID start gate** (constants plus the game's own measured growth ratio), **wall clocks in the
  decision path** (only two, both instrument-gated), and **pointer-derived hashing** (one cast, for
  a temp filename).

### Where it bites, and the one live lead

The divergence is in **committed search depth** — the `id_depth` and `committed_depth` histograms
differ between two runs of the same manifest — which is why it is confined to a small, *repeatable*
set of games: those sitting exactly on the iterative-deepening budget boundary, where one more pass
either fits or does not. Games 146, 265, 305, 317 and 367 diverged in two or more independent
conditions.

### The reporting paths MODULATE the rate but are not the cause

Bisected, one 400-game pair per condition:

| condition | diverged / 400 |
|---|---|
| baseline (both reporting paths on) | 8 |
| `MTG_NO_BP_ENUM_CACHE=1` | 10 |
| `MTG_BATCH_HEARTBEAT=0` (heartbeat thread off) | 7 |
| `MTG_SLOW_GAME_MS=0` (worker-thread stderr off) | 4 |
| both off | **2**, and **2** on a replicate |

The heartbeat *thread* accounts for none of it. What matters is the **SLOW-GAME `fprintf`+`fflush`
that WORKER threads perform** — this deck emits dozens per run, and serialising workers on stderr is
exactly the kind of timing perturbation that changes which interleavings occur.

**So these flags are an amplifier, not the mechanism, and the honest conclusion is the harder one:
the underlying nondeterminism is TIMING-SENSITIVE and survives with all reporting off (4/800).**
Neither flag can change play by construction — both are pure `fprintf` after a game has finished,
and `MTG_SLOW_GAME_MS`'s only other use is in `GoldFishRunner`, which the batch path does not use.

### Where that leaves it

Something about concurrent execution changes **how much work a game's search does**, which flips the
committed depth for games sitting exactly on the iterative-deepening budget boundary — and it is not
a data race TSan can see, not uninitialised memory, not a clock in the play path (every remaining
`steady_clock` in the tree is in `src/analyzer/`, unused by batch play), and not the known caches.

### ROOT CAUSE CHAIN, ESTABLISHED (`MTG_DUMP_UNITS=1`, 400 games x2, 24 threads)

Per-game work units are recorded by `MTG_DUMP_UNITS=1` (a `<job>.units` file beside `<job>.wins`).
Diffed across two runs of the same manifest:

* **69 / 400 games (17%) consumed a DIFFERENT number of work units.**
* **6 / 400 diverged in play — and all six are a SUBSET of the 69.**

```
game 305: units 50527 vs 87556   -> win turn 8 vs 7
game 143: units 167465 vs 153633
game 317: units  85921 vs  81671
```

So the chain is: concurrency -> a game's consumed work units vary -> the iterative-deepening start
gate (`estimated cost <= alpha * remaining budget`) sees a different remaining budget -> the
committed depth flips -> play differs. Play only moves for the minority of games sitting exactly on
the boundary, which is why the play-level rate is 2% while the underlying defect touches 17%.

**This falsifies the conclusion recorded at the `ClearPerGameCaches` fix site**, which says of the
same class of defect: *"Play was never affected; the work METER's determinism was."* The meter and
the search budget are the same currency (`SearchBudget::Consume` calls `gamework::Add`), so meter
nondeterminism reaches play by construction. That comment should be corrected when this is fixed.

**METHODOLOGY NOTE, and it is the most reusable thing here: use UNITS, not play, as the detector.**
It is 8x more sensitive (17% vs 2%), so a bisect needs ~8x fewer games for the same power. Every
underpowered mistake earlier in this document came from testing with the insensitive signal.

**REMAINING QUESTION (narrow):** why does a game's work vary at all? Its inputs are identical and it
is deterministic single-threaded. The unit sites are the 13 `ConsumeAt(budget, unitsite::...)` calls;
`MTG_ROLLOUT_STATS` prints the per-site partition but only as a PROCESS aggregate, so the next step
is a per-game site partition (or a bisect over the sites) on one fragile game such as 305.

**CONFIRMED (2026-09-03): work units are deterministic single-threaded — 0 of 50 games differ.**
50 games x2 at `--threads 1` under `MTG_DUMP_UNITS`, byte-identical `.units` files
(`logs/edf_units1/{a,b}`). At the threaded 17% rate this had P(false clean) = 0.83^50 ~ 1e-4, so it
is a high-power check, not a token one. The chain above is therefore concurrency-gated end to end:
the perturbation cannot originate inside a game's own deterministic computation, so it enters
through **state a game does not own** — either shared across threads, or inherited from whatever ran
on that thread before it.

That is worth stating precisely, because it is what makes the remaining search tractable: the
culprit must be reachable from the search AND carry information across a game boundary or a thread
boundary. Pure per-game state, however complex, is exonerated wholesale.

**(superseded) THE NEXT PROBE, and it is cheap: `MTG_DUMP_UNITS=1` records per-GAME work units.** Diff them
across two runs for a fragile game:
* units differ ⇒ the game genuinely does different work ⇒ bisect the `ConsumeAt` unit sites
  (`unitsite::` partition) to find which one varies;
* units identical but play differs ⇒ the divergence is NOT in work accounting at all, and the
  budget/depth story above is wrong — start again from the decision that differs.

Either answer is decisive, which is why it is the right next step rather than more code reading.

## LOCALIZED (2026-09-03) — it is INHERITED THREAD HISTORY, and here are the two caches

The decisive measurement cost nothing: it is a re-read of data already on disk
(`logs/edf_units/{a,b}`, 400 games, 24 threads). Bucket the unit-divergent games by game index —
which, under a pooled queue with roughly equal game costs, is dispatch order:

| games (by index) | unit-divergent |
|---|---|
| 0-23   (dispatch wave 0) | **0 / 24** |
| 24-47  (wave 1)          | **0 / 24** |
| 48-95                    | 2 / 48 |
| 96-199                   | 16 / 104 |
| 200-299                  | 21 / 100 |
| 300-399                  | 28 / 100 |

**The first two dispatch waves — the 48 games whose worker threads have no history yet — do not
diverge at all.** The rate then climbs monotonically with position in the run, to ~25%.

This explains every earlier confusion at once. The 100-game run's 2/100 is not a different rate: it
is quartile 1, reproduced independently. So the rate is not a function of thread COUNT (which was
the "needs the pool / scales with pool pressure" story, already retracted) and not a property of
concurrency as such. It is a function of **how many games have already run on that thread**. Once a
thread's carried state differs between two runs, every subsequent game on that thread is exposed.

That kills the remaining "shared state accessed concurrently" hypotheses outright: a race on shared
state would hit game 0 as readily as game 399. It has to be state a thread inherits from its own
predecessors — and, since single-threaded runs are clean (0/50), state whose *contents depend on
which predecessors ran*, which in a pool is not reproducible.

### The two caches

`ClearPerGameCaches` resets `solvememo::t_cache`, `solvememo::t_m2cache` and `enummemo::t_cache`.
Two more capped `thread_local` caches were function-local statics, unreachable from it:

* the breakpoint plan cache in `BpEnumEntryFor` (`MTG_BP_ENUM_CACHE_CAP`, default 8192), and
* canon's ACT-vs-PASS verdict memo (same cap x4).

Both are cleared **only on overflow** (`if (cache.size() >= cap) cache.clear()`), so a game inherits
its predecessors' FILL LEVEL, the cap-clear lands at a different point, and the hit/miss pattern
moves. A hit returns without calling `EnumeratePlansWithLand`; a miss pays it. Different work, same
game — which is the whole chain's first link.

### And it is not only a determinism problem

The three caches that ARE cleared are also **epoch-scoped**: every hit demands
`entry.epoch == g_decision_epoch`. That check is load-bearing, and the codebase says why, in
`solvememo`'s own doc block: the key's library digest is *size + front card*, and that
"only implies content within a single decision, the TranspositionTable's own scoping argument."

The two breakpoint caches use the same `BuildBreakpointKey` and do **not** fold or check the epoch —
they hit on the key alone. Within one game that is defensible: the library is a fixed permutation,
so remaining size does imply remaining content. **Across games it is not** — two different shuffles
sharing a library size and front card share a key, and the second game gets the first game's plans.
Clearing per game closes that cross-game path. It does NOT close the cross-decision path within a
game, where the residual exposure is a mid-game shuffle (a fetchland, a tutor-and-shuffle) changing
the permutation while the cache still holds pre-shuffle entries. FiveColour is the deck to check
that on; EDF barely shuffles. **Flagged, not fixed, and not yet measured.**

### The fix

Hoist both caches to namespace scope (`bpcache::t_plans`, `bpcache::t_verdicts`) and clear them in
`ClearPerGameCaches`. Scenarios: 54/54 pass.

**This is not byte-identical and cannot be** — changing when a cap-clear happens changes work units,
which is the entire point. Expect starved decks to move play and GT to need a rebaseline; that is
the cost of the determinism, and it should be a deliberate, reported decision rather than a quiet
one.

## What is NOT yet ruled out, in priority order

Serial determinism (above) narrows this list sharply: the cause must cross a game boundary or a
thread boundary.

1. **Scheduling-dependent cross-game state in a worker thread.** Still the leading candidate by
   shape: a batch worker owns its thread across many games, so any cache/memo that survives a game
   and is keyed loosely makes game N's work depend on which games ran before it on that thread —
   and the pool's interleave is not reproducible. Direct precedent: the m2 solve memo's dead
   cross-game entries once shifted cap-clear timing and broke work-unit thread-invariance.
   **Note the earlier elimination of this was UNDERPOWERED and should not be trusted.** It replayed
   two games (47, 67) alone versus after their predecessors and found them byte-identical — but at
   a 17% per-game rate, two games have only a 31% chance of showing the effect at all. Worse, it
   tested the *same* predecessor sequence, whereas the pool's defining property is that the
   predecessors DIFFER between runs. It is not evidence.
2. **Shared state that is thread-SAFE but not deterministic.** ThreadSanitizer being clean does not
   exonerate this class — a mutex-guarded or atomic cache has no data race and still returns
   different results depending on which thread arrived first. Everything eliminated earlier "by
   inspection" was eliminated for thread-safety, which is the wrong property. Re-examine
   `MulliganProfileIO`'s policy cache and sidecar-resolve map, `CardDatabase::LookupCached`'s
   `atomic_ref`, and any shared memo, asking only: *can a hit cost different work than a miss?*
   (`NameRegistry::Intern` is safe here on inspection: it hands back pointers into a node-based set
   whose element addresses are stable, and identity is preserved. `SubtypeRegistry` is safe by
   construction — ids are assigned during single-threaded load and the registry is frozen before
   the worker pool exists.)
3. **A wall-clock or timing input reaching a decision.** Budgets are deterministic work units
   (`NODES_PER_VIRTUAL_MS`), and every surviving `steady_clock` read is in `src/analyzer/`, which
   batch play does not use — so this is third, not first.
4. **Unordered-container iteration order** over pointer- or address-keyed maps. Note that heap
   addresses vary between runs even single-threaded (ASLR, and glibc's per-thread arenas), so if
   address ordering reached a decision the SERIAL runs should have diverged too. They did not,
   which argues against this — but only for the paths those 50 games exercised.

## Next step

**Running now: is it the per-decision MEMOS, as a class?** One 100-game job, twice per arm, 24
threads, `MTG_DUMP_UNITS=1`; arm ON is the shipped config, arm OFF sets
`MTG_SOLVE_MEMO=0 MTG_ENUM_MEMO=0 MTG_FS_NOWIN_CACHE=0 MTG_NO_BP_PREFIX_CACHE=1
MTG_NO_M2_SEARCH_MEMO=1 MTG_NO_BP_ENUM_CACHE=1`. Driver and results: `logs/edf_memo_off/`.
100 games gives P(false clean) ~ 1e-8 at the observed rate, so a clean OFF arm is conclusive.
This test is chosen because it settles hypothesis 1 for the whole memo class in one run instead of
bisecting caches one at a time — and because the earlier single-cache result
(`MTG_NO_BP_ENUM_CACHE=1`, 10/400 vs 8/400) only exonerated ONE of the six.

If OFF is also ~17%, the memo class is out and the next probe is a per-game unit-SITE partition
(the 13 `ConsumeAt(budget, unitsite::...)` calls; `MTG_ROLLOUT_STATS` prints that partition only as
a process aggregate today, so it needs a small per-game dump) on one fragile game such as 305.

The experiment binary is snapshotted at `logs/nondet_bin/mtg.nondet` (commit `56ba0979`) precisely
so that concurrent source edits by other work cannot silently change what is being measured
mid-investigation.

Note that a single-game repro needs `--game-index` — `--seed base+gi` alone plays a *different*
game, not game `gi` of that seed.
