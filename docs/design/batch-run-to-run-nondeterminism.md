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
| **1 job alone** (`logs/edf_nondet/`) | 3 | `2e730b01…` **all three — stable** | 0 of 1 |
| 4 jobs, ONE config, 4 seeds (`logs/edf_samearm/`) | 2 | `18b562c3…` then `1630f11f…` | **1 of 4** |
| 8 jobs, two arms (`logs/edf_tapyield/`) | 2 | `2e730b01…` then `5f38514a…` | **6 of 8** |

**The divergence rate scales with pool pressure**, and a single-config pool is enough — it does not
need mixed depths or mixed heuristic arms, so this is not a per-job-override leak. Seed 4303 is the
most fragile (it diverged in every multi-job pool); seed 4101 never diverged in any of them, and its
digest `1ad5deba38ddb12e` is identical across every run *and* across the pre-fix binary, which is
what makes it usable as a byte-identity control.

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

## What is NOT yet ruled out, in priority order

1. **Scheduling-dependent cross-game state in a worker thread.** The leading candidate by shape: a
   batch worker owns its thread across many games, so any cache/memo that survives a game and is
   keyed loosely can make game N's play depend on which games ran before it on that thread — and
   the pool's interleave is not reproducible. There is direct precedent: the m2 solve memo's dead
   cross-game entries once shifted cap-clear timing and broke work-unit thread-invariance. That was
   fixed; this would be a second instance in a different cache.
   **The discriminating test:** run the same job twice at `--threads 1`. Single-threaded scheduling
   is deterministic, so if digests become stable the cause is in this class. This is the next step
   and it has not been run.
2. **A wall-clock or timing input reaching a decision.** Budgets are deterministic work units
   (`NODES_PER_VIRTUAL_MS`), which is why this is second, not first — but any surviving real-time
   read (an abandon check, a slow-game guard, a cache eviction on elapsed time) would do it.
3. **Unordered-container iteration order** over pointer- or address-keyed maps.

## Next step

Narrow to a single game index. A run of one job three times with `--game-trace-dir` gives per-game
digests; the games that differ localise the bug to one `(seed, game_index)`, which is then cheap to
run hundreds of times under each hypothesis. `logs/edf_nondet/` holds that run.

Note that a single-game repro needs `--game-index` — `--seed base+gi` alone plays a *different*
game, not game `gi` of that seed.
