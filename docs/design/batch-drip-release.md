# `--batch` drip: release a condemnable cell once it has been JUDGED (deferred)

Not a problem for FiveColour, whose static tail (H5) outlasts all metered work — but worth having
logic for (user, 2026-08-12).

## What the pool does today

`BatchRunner::RunManifest` partitions work in two:

```cpp
const bool condemnable = condemn.enabled && j.depth > condemn.never_condemn_depth;
if (condemnable) { pool.pending[j.cell_id].push_back(wi); }   // metered
else             { keep.push_back(wi); }                      // static, LPT/weight order
```

Metered games are seeded `drip` per cell into `front`, and `Pool::Take` drains `front` **before** any
static work. `OnFinished` releases that cell's next game each time one lands uncondemned.

The rationale is sound and worth keeping: a condemnable cell's work is mostly about to be thrown away,
so dispatching it by cost would put every thread on the cell most likely to be discarded, and the
verdict cannot arrive until those games finish.

## The two defects

**1. Metering never ends.** The comment scopes it to a "not-yet-judged cell", but nothing releases a
cell once judged. A cell that has passed `reference_games` and was NOT condemned is proven tractable
and should rejoin the normal pool; instead it stays metered for its whole 400-game target.

Measured on FiveColour, 2026-08-12: V6/V7/V8 run ~15-17 s/game against a 60 s/game limit, so they are
never condemned — yet with 16 depth>5 cells at `drip=1` they held up to 16 of 24 threads, ahead of all
static work, and H1-H5 / V1-V5 sat at zero games. Cost here is bounded (~1.7 h at half the box, and
those 4,800 games are legitimate work), but the ordering is backwards: proven-tractable rows outrank
the protected d<=5 ladder that the trust-depth decision actually reads.

**2. Threads can idle at the end.** `Take` blocks on `cv.wait` when static work is exhausted and any
cell is still dripping. In-flight is then capped at (number of metered cells x drip), so a run whose
static work finishes first ends with `threads - cells*drip` idle. FiveColour dodges this because H5 is
static and outlasts everything; a deck whose expensive cells are all condemnable would not.

## The logic to add

Release a cell from metering the moment it is judged tractable — i.e. at the same site that already
decides condemnation (`n >= condemn.reference_games`), on the branch where the mean is UNDER the
limit — and, separately, flush all remaining metered work when static work runs dry.

The one thing to get right is where released games go. They must NOT go to `front` (that keeps them
ahead of static work, making defect 1 worse), and they must NOT simply be appended after all static
work either: the matrix's schedule advances every cell through offset levels together so that a stop
banks the run (see `valueleaf_depth_matrix.build_queue`), and dumping a released cell's games at the
end would leave those rows trailing, which is exactly what the banking rule's `B` is computed from.
They belong in the static stream **in weight order**, which is the ordering the manifest already
carries.

That argues for the alternative shape: keep condemnable games in the static list all along and make
the drip an in-flight CAP rather than a separate queue — `Take` skips a static item whose unjudged
cell already has `drip` games running, and retries it when one lands. Same protection, no second
ordering to reconcile, and both defects disappear (a judged cell simply stops being skipped; nothing
is held back at the end).

## Gate

Engine change, so it needs the standing gate — smoke + regression — plus a check that a condemned
cell still caps at `reference_games` and that `[batch] metering ...` still reports. It also moves
`HEAD:src`, so it must land BEFORE or AFTER a matrix generation, never during: the artifacts are
engine-state fingerprints and a mid-run change invalidates the table (see `.claude/skills/value-leaf.md`
Rule 0).
