# Phase E emits 1000-game jobs, so one slow game strands 983 behind it

**Status:** deferred, diagnosed, not implemented. Applies to `phase_measure` in
`scripts/valueleaf.sh`. Found 2026-08-16 during the Mirrorwing run.

## What happened

Phase E ran 24,000 games as **32 jobs**. Two of them -- the `live`/`staged` A/B pair on seed 603000 --
both hit a pathological game at index 17 and sat on it for **1.61 h and 1.47 h**, on 2 of 32 cores,
with **983 of each job's 1,000 games still queued behind that single game**. Nothing stopped it:
phase E jobs carry no `abandon_units` and no condemnation, unlike matrix cells.

## Why: granularity, not scheduling

| | total games | jobs | games per job |
|---|---|---|---|
| Phase C (matrix) | 5,942 | **680** | <= 25, mostly 1-5 |
| Phase E (A/B + sweep) | 24,000 | **32** | **500-1,000** |

`phase_measure` emits ONE job per (arm, seed) via `h_job ... "$AB_GAMES"`, and `AB_GAMES=1000`
(`PLAY_GAMES=500`). A job is executed sequentially by a single worker; the pool parallelises ACROSS
jobs, never WITHIN one. With 32 jobs on 32 cores the box saturates only at the start -- as jobs
finish, the freed cores CANNOT help the survivors, because a job is indivisible.

This obeys the letter of the repo's pooling rule (one batch, one queue) while defeating its purpose.
The rule is that cores stay saturated down to A SINGLE TAIL. Here the tail is a **job**, not a
**game**: one pathological game costs 984 games of latency rather than one.

## The fix

Chunk phase E the way the matrix already chunks cells: emit `AB_GAMES` as N jobs of ~25 with
consecutive `game_index`, rather than one job of 1000. `h_job` already takes a game count and the
manifest already carries `game_index`, so this is a loop change with no semantic effect -- the games,
seeds and identities are unchanged, only their packaging.

Effect on the observed run: the 24,000 games would be ~960 jobs, keeping all 32 cores busy through
the ~16 minutes of real work, and the 1.6-hour game would block only its own 25-game chunk. The run
would have been waiting on ONE game after ~16 min, visibly, instead of discovering the stall 1h43m
in with 30 cores idle.

## Related

Same family as the abandonment findings, but distinct in kind: this one is pure scheduling and has an
obvious mechanical fix, whereas [[edge-table-pairwise-mutual-completion]] changes what the table
measures. Worth doing first for that reason.

Also note phase E has NO per-game guard at all -- no work ceiling, no wall-clock backstop. The matrix
at least has both (bounded by `never_condemn_depth`, which exempts H1-H5 and V1-V5 entirely). If a
guard is ever added to phase E, chunking must come first, because a ceiling on a 1000-game job would
void 1000 games rather than one.
