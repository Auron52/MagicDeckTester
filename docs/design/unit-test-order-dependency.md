# Unit-test order dependency (found 2026-11-21 by sharding the unit run)

## RESOLVED 2026-11-25 (branch wip/int9-fix): the TEST was wrong, the engine is fine

- **The state:** `g_searched_play` (`src/core/SpellEffects.h`, `thread_local`, default false). The legend-keep twin
  pass (`TurnSolver.cpp`, "LEGEND-KEEP PASS", CR 704.5j) is gated on it BY DESIGN: searched play twins the cast
  keep-old / keep-new, the d0 runner keeps the provider's keep. Every `AIEngine` constructor writes it from its
  depth (`SetSearchedPlay(lookahead_depth > 0)`), and the Servant test builds `AIEngine(..., 3, 20)` and never
  restores it. The INT-9 Stone test never set it, so it saw true only when a depth>0 engine had been built earlier
  on the same thread.
- **Why it "flipped with no code change" on 2026-11-25:** the binary and the code did not change, the RUNNER did.
  The serial suite (Servant sorts before Shadowborn) still passes 4167/4167 on the same binary; the 11-25 runs went
  through `scripts/unit_par.sh`, whose shard holding the INT-9 case starts in a fresh process.
- **Not an engine defect:** real games cannot inherit it, because every game's `AIEngine` constructor sets the flag
  unconditionally (both directions) before any decision.
- **Fix:** the test sets searched play itself through a restoring scope (`Searched9`) and also checks the d0 side
  (one cast, `legend_keep == 0`). No engine change.
- **`unit_par.sh` count bug fixed in the same branch:** doctest reports "passed" as (cases passing the filters) -
  failed, and cases `--first/--last` skip still pass the filters, so every shard's summary reported the whole suite
  (33360 / 4170 on 8 shards -> exit 3 even when green). Shards really do run only their range; the script now counts
  executed cases from `--duration=true`'s per-case lines.

The original 2026-11-21 notes follow.

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
