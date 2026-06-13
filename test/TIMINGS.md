# Regression timing reference

Measured wall-clock with `--threads 0` (all cores) on the dev box, 2026-06-13.
Use these to size the case matrices in `regression_cases.sh` so each mode stays
within budget. Every depth>0 case uses `--lookahead-bottoming`.

Ground truth is recorded by running a mode and, once inspected, promoting it with
`bash test/regression.sh <mode> --accept` (no re-run; reuses the run's results).

## Per-case wall time (representative)

| deck    | depth | budget | games | wall        | notes |
|---------|-------|--------|-------|-------------|-------|
| slivers | 0     | -      | 1000  | ~0s         | trivial |
| slivers | 3     | 100    | 500   | 116s–417s   | **high seed variance** |
| slivers | 5     | 200    | 300   | ~465s (s2002) | |
| slivers | 5     | 200    | 500   | 298s–1390s  | **severe tail at s2002** |
| burn    | 0     | -      | 1000  | ~0s         | |
| burn    | 3     | 100    | 500   | ~6s         | |
| burn    | 5     | 200    | 500   | 30–37s      | (~2x the no-bottoming cost) |
| th      | 0     | -      | 1000  | ~0s         | 95% win at d0 |
| th      | 3     | 100    | 500   | 132–206s    | |
| th      | 5     | 200    | 300   | 213–305s    | |

## The slivers heavy tail — read before sizing

slivers deep games have a **severe seed-specific tail**: a few pathological hands
send the clairvoyant search into minutes-long single games that pin one core
while the rest of the batch is long done (the known single-core load-imbalance
tail — see `project-cpu-utilization-tail` / `project-perf-profile-2026-06-11`).

Concretely: slivers d5 **500g s2002 = 1390s** but **s1001 = 147s** (~9x) and
**s3003 = 298s**. Dropping to 300g cut s2002 to 465s. This is why the regression
matrix caps slivers d5 at 300g (and d3 at 400g) while burn/TH stay at 500g.
**When a mode overruns its budget, trim slivers first** — its tail dominates and
is the least linear in game count.

## Budget accounting (measured / estimated)

- **smoke** (seed 1001): **3m16s measured** — d0 1000g + small d3/d5 per deck.
- **regression** (seeds 2002,3003): ~33 min — d0 1000g, slivers d3 400g/d5 300g,
  burn & TH d3 500g/d5 300g. (The untrimmed 500g slivers version hit 52 min.)
- **overnight** (seeds 4004,5005,6006,7007): est ~3–5 h at the current counts —
  slivers d5 and TH d5 (1000g × 4 seeds) dominate, and a tail-heavy seed can add
  an hour. Safely under 8 h, but re-check the total on first real run and trim if
  a seed turns out pathological.

All three target under their limits (15 min / 45 min / 8 h) with headroom for
added decks.
