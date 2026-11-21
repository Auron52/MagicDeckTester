# Unit-test order dependency (found 2026-11-21 by sharding the unit run)

## Why it matters

The USER (2026-11-21): *"Nothing should be single-threaded if we can avoid it..."*. `build/Release/mtg-test`
runs its ~3,800 doctest cases serially on one core, about 1.5-4 minutes, on every validation chain.
`scripts/unit_par.sh [binary] [jobs]` shards the case list with doctest's `--first/--last` across
`nproc` cores. It checks that every case ran exactly once and that every shard passed. On 4 lane cores it
took 55 s against the serial run's minutes.

Sharding runs each range in a fresh process, so it exposes hidden dependencies between test cases. The first
run found one. **`unit_par.sh` cannot replace the serial run until that dependency is fixed.**

## The dependency

- **Fails alone:** `INT-9 free casts: a revealed Thrumming Stone is cast and the legend rule keeps the copy the
  cast names` (`test/unit/test_shadowborn_int9.cpp:184`, `REQUIRE(casts.size() == 2)` gets 1). It fails both
  as the only case and as part of its own file alone (`-sf="*test_shadowborn_int9.cpp"`).
- **Passes after:** `Servant haste keep: the Signet leaves the tagged {C}{C} for the Firemind, both worlds`
  (`test/unit/test_servant_haste_keep.cpp`). It is test #3273 in run order, found by bisecting `--first`; the
  pair passes together.
  (`-tc="Servant haste keep: the Signet leaves*,INT-9 free casts: a revealed Thrumming Stone*"`).
- **Not the cause:** the Servant test's own lever (`MTG_SERVANT_HASTE_KEEP=0` alone does not make the Shadowborn
  test pass).
- **Suspect:** it is probably lazily built global state on the path the Servant test drives: `TurnSolver::ApplyPlan`
  and the executor (`AIEngine::TakeTurn` via `RunExecutorHk`). Examples would be a per-process cache, or a
  registry populated on first use, that the ripple/legend-keep twinning in `Casts9` relies on.

## Which side is wrong

The serial suite has always passed, because the Servant file sorts first. So either:
- the Shadowborn test misses a setup step that real play always has (the test is wrong), or
- the twinning really depends on uninitialised global state, which would make play depend on what the process
  did earlier (an engine defect).

The second is the dangerous one, since batch games share a process. Find out which before fixing.

## To do

1. Find the state the Servant test initialises. Bisect its body, executor call versus `ApplyPlan`, then the global.
2. Fix the right side: the test's setup, or the engine's lazy initialisation.
3. Run `scripts/unit_par.sh` over the whole suite until it is green and the counts add up. Then make it the unit
   step of the validation chains (the push-queue landing, the analysis chains).
