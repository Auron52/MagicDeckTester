# The depth gate must be THREAD-LOCAL (batch runs mix depths in one process)

2026-07-31. A determinism bug, found by reading `BatchRunner::RunManifest` while adding a second
consumer of the same flag. Reproduced, fixed, and worth writing down because the same trap catches
any future "is this run searched?" style global.

## The trap

Two adopted features gate themselves on whether the run's play is searched:

* cantrip-first ordering (`CantripFirstEnabled`)
* the Ponder keep-vs-shuffle branch (`SearchedPlayActive`)

Both read a flag written by **every `AIEngine` constructor**:
`TurnSolver::SetSearchedPlay(lookahead_depth > 0)`. As a shared global that is safe only if one
process never runs games of different depths at the same time — and the batch runner does exactly
that. `RunManifest` **flattens every game of every job into one work list** and hands it to a thread
pool, so a depth-0 game of one job and a depth-3 game of another are in flight simultaneously. The
d0 engine's constructor then flips the flag under the d3 game's search.

The LPT scheduler sorts depth-descending, which keeps the d0 games near the end and makes the
overlap window small — which is why this hid rather than exploding.

## Reproduced

A manifest with one hinata d3 job (60 games, the deck with cantrips) plus six hinata d0 jobs
(300 games each), forcing the overlap:

| run | avg | case digest |
|---|---|---|
| `--threads 1` | 6.1500 | `96f532db6a7e4a04` |
| `--threads 24` (a) | 6.1667 | `0c0627cdc3a52776` |
| `--threads 24` (b) | 6.1667 | `e2d81ba44847ef9d` |
| `--threads 24` (c) | 6.1667 | `e2d81ba44847ef9d` |

Two identical invocations produced two different digests. That breaks the property the whole
regression harness rests on: *same seed + budget => same result on any core count*. Ground truth,
A/B arms and play digests are all meaningless without it.

## Fix

`static thread_local bool g_searched_play` instead of `static std::atomic<bool>`. Each batch worker
constructs and uses its own engines, so thread-local is the correct scope — a game can only ever be
affected by its own engine's depth. After the fix all four runs above return `96f532db6a7e4a04`,
matching the single-threaded reference exactly.

## The rule

**Any flag written per-`AIEngine` and read during play must be `thread_local`.** A shared global is
only safe for something fixed for the whole process (an env-derived setting read once). If it is
derived from a *job's* parameters — depth, budget, profile — batch mode will mix it. When adding
one, the cheap test is the reproducer above: a mixed-depth manifest at `--threads 1` vs
`--threads 24`, twice.
