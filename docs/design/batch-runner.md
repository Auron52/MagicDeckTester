# Batch runner (`mtg.exe --batch`)

## Problem

The regression suite runs ~15 separate `mtg.exe` invocations (one per
deck×depth×seed config). Each invocation pays its own **load-imbalance tail**:
games vary wildly in cost (a pathological deep game can pin one core for minutes
while the rest of the batch is long done — e.g. `slivers_d5_s2002` 500g took 1390s
while the same config at other seeds took ~150–300s). Run serially, those ~15
tails add up.

## Idea

`mtg.exe --batch manifest.json` pools **every game from every job** into one
shared work list driven by a single atomic cursor. A slow game in one job is then
backfilled by another job's games instead of stranding a core, so the ~15
per-invocation tails collapse into **one tail for the whole batch**.

## Why it's lossless (no locking)

- Each game is seeded purely by its job's `seed + game_index` (`SetupGame`), so a
  game's result is independent of which worker runs it or in what order. Results
  are written to a pre-sized `win_turns[job][game]` slot — disjoint per game, no
  synchronisation.
- `cards.json` is loaded **once** before workers start; `CardDatabase::Lookup` is
  read-only thereafter (already shared safely by the existing within-run pool).
- Every per-job input (deck, profile, depth, budget, …) is passed **by value**
  into the worker. No shared mutable state on the hot path.

Because order is irrelevant, the pool is free to **reorder** for scheduling: work
items are stable-sorted by descending `(depth, budget_ms)` — an **LPT** heuristic
with search cost as the proxy — so the slow deep games start first and cheap `d0`
games backfill the tail. Each worker keeps one `AIEngine`/`GameEngine` and rebuilds
it only on a job change; the stable sort keeps a job's games contiguous, so reuse
matches the single-run path's per-thread engine lifetime exactly.

## Manifest format

```json
{
  "jobs": [
    { "name": "slivers_regression_d5_s2002",
      "deck": "decks/slivers_vial.txt",
      "profile": "decks/slivers_vial.profile.json",   // optional; else deckname.profile.json
      "games": 300, "seed": 2002,
      "depth": 5, "budget_ms": 200,             // optional, default 0
      "max_turns": 20,                          // optional, default 20
      "lookahead_bottoming": true }             // optional, default false
  ]
}
```

Output, one line per job (manifest order):

```
=== BATCH (N jobs, M games) ===
<name>: played=<n> won=<w> (<pct>%) avg=<avg_win_turn>
```

## Status

- **Done:** pooled execution + LPT ordering (`src/runner/BatchRunner.{h,cpp}`,
  wired as `mtg.exe --batch`). **Validated lossless:** every job byte-identical to
  its standalone `mtg.exe` run, and thread-invariant (`--threads 1` ≡ `--threads 0`).
- **Next:** Phase C — have `regression.sh` emit its `regression_cases.sh` matrix as
  a manifest and call `mtg.exe --batch` once instead of N invocations (parse the
  per-job lines into the existing `won/avg` fingerprints). Then measure suite
  makespan vs the current serial sweep (perf is tail-dominated and noisy, so report
  spread over several runs — it is not a pass/fail gate).
