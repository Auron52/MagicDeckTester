# In-game progress reporting — say where a long game is, while it is still running

**Status:** designed, not implemented. Blocked only by the FiveColour value-leaf freeze (a `src/`
change would move `HEAD:src` and stop the in-flight run's remaining phases). Implement as soon as
phase E completes.

**Requested by the user, 2026-08-08:** *"at regular, but significantly spaced intervals we output
where we are in a specific game. So if something is running for minutes we get an occasional status
update"* … *"it could actually tell us where it is in the rollouts (depth and something like
approximate % in) and we could decide whether or not to continue the process."*

## The gap this closes

Every progress signal we have is **post-hoc or coarse**:

| signal | granularity | when |
|---|---|---|
| `MTG_SLOW_GAME_MS` | one line per game | AFTER the game finishes |
| matrix cell counters | 25-game batch | after a whole batch |
| `scripts/valueleaf.sh status` | cells at target | any time, but cell-level |

A FiveColour H5 batch is 25 games at ~400 s = **2.8 hours** during which nothing is emitted, and a
single H3 game has run for **5.5 minutes**. So the operator's real question — *is this progressing,
or is it stuck, and should I kill it?* — is unanswerable exactly when it matters. That is what forced
the "let it run and hope" position this design exists to remove.

## Shape

A **watchdog thread**, not instrumentation on the hot path. The search must not pay per-node cost for
this; anything in `Solve`'s inner loop is unacceptable at 943 M instructions per game.

- Each worker thread publishes a small POD snapshot to a slot it owns: `{game_index, turn, depth,
  pass, plans_done, plans_total, rollouts}`. Written with relaxed atomics at points that are already
  coarse — top-level plan boundaries and depth/pass transitions — so the cost is a handful of stores
  per plan, not per node.
- One watchdog thread wakes every `MTG_PROGRESS_MS` (default **0 = off**; the driver sets it, the way
  it already sets `MTG_SLOW_GAME_MS`) and prints any slot whose current game has been running longer
  than that interval.
- Interval semantics match `MTG_SLOW_GAME_MS`: a **millisecond value**, not a boolean, so it is read
  with `getenv` + parse rather than `EnvOn` (see `.claude/skills/coding-conventions.md` — `EnvOn` is
  for booleans only).

Suggested line, one per report, stderr, same prefix convention as SLOW-GAME:

```
[goldfish] PROGRESS gi=14 t=182s  turn=5 depth=3 pass=2/3  plans 41/118 (35%)  rollouts=612k
```

## What "% in" can honestly mean

There is no single denominator for a search, so the report must say which one it is using rather than
inventing a fake total:

1. **Top-level plans** — `EnumeratePlans` produces a concrete list before the loop runs, so
   `plans_done / plans_total` is exact and is the most useful number. This is the primary figure.
2. **Pass within the ladder** — escalation climbs passes 1..d; `pass=2/3` is exact and cheap.
3. **Rollouts** — `g_rollout_calls` already exists behind `MTG_ROLLOUT_STATS`
   (`src/ai/TurnSolver.cpp:60`). Report it as a rate, not a fraction: it has no meaningful total, but
   a *stalled* counter is the clearest possible "this is stuck rather than slow" signal.

Deliberately NOT reported as a percentage: anything requiring an estimate of remaining subtree work.
A made-up denominator that sits at 90% for an hour is worse than no number.

## Determinism

Reporting must not perturb results. The snapshot is write-only from the worker's side and read only
by the watchdog; no worker ever reads another's slot, and nothing branches on the clock. Verify the
usual way — a run with `MTG_PROGRESS_MS=1000` and one with it unset must produce identical per-game
win turns under `MTG_DUMP_WINS`, not merely identical averages.

## Why it is worth doing before the next expensive generation

The decision it enables is the expensive one. On 2026-08-08 the operator had to choose between
cancelling a 96%-complete value-leaf matrix and waiting an unknown number of hours, with the only
available evidence being a per-batch counter that moves every ~2.8 h. With this in place the answer
is a line of output.

Related: `docs/design/fivecolour-search-cost.md` (why this deck's games are minutes long in the first
place), `.claude/skills/value-leaf.md` (the pipeline whose phase C pays it).
