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
| hinata  | 0     | -      | 1000  | ~0s         | ~48% win at d0 |
| hinata  | 3     | 10     | 400   | ~237s (1thr) | **~0.59 s/game, tail-inclusive** — post max-mana gate (commit 9229b25); was ~40x this pre-gate |
| hinata  | 5     | 20     | 300   | ~375s (1thr) | **~1.25 s/game** — the gate tamed the multi-minute combo turns |
| auras   | 0     | -      | 1000  | ~1s         | ~99.7% win at d0; goldfish horizon is max_turns=8 |
| auras   | 3     | 10     | 2000  | ~47s (1thr) | **~0.023 s/game**; at budget 80 it is ~0.066 s/game (2.8x) |
| auras   | 5     | 20     | 1200  | ~10s (1thr) | **~0.0086 s/game** — CHEAPER than its own d3, see note below |
| kitty   | 3     | 10 / 20 | —    | 0.46 / 0.83 s/game | see "kitty + dragons" below |
| kitty   | 5     | 20 / 40 | —    | 0.50 / 1.65 s/game | |
| dragons | 3     | 10 / 20 | —    | 0.98 / 1.39 s/game | |
| dragons | 5     | 20 / 40 | —    | 2.00 / 3.60 s/game | |
| breaching | 3   | 10 / 20 | —    | 0.38 / 0.43 s/game | pooled-probe 2026-09-03, dragonstorm anchor (0.055 s/game d3 b10 in-batch) |
| breaching | 5   | 20 / 40 | —    | 0.56 / 0.60 s/game | search plateaus at d3, so 2x budget is nearly free; no value leaf yet (d5 GT will move on adoption) |

### kitty + dragons: sized by RATIO, because the box was contended

Probed 2026-08-29 when an external process held ~half the machine, so absolute
wall-clock was not trustworthy. The probe therefore pooled **minotaur as an
in-batch anchor** alongside both candidates (one `--batch`, identical
conditions), and sized from the per-game cost *ratio*, which is robust to
uniform contention in a way absolute times are not:

| deck | d3 b10 | d5 b20 | d3 b20 | d5 b40 | vs minotaur |
|---|---|---|---|---|---|
| minotaur (anchor) | 2.20 | 3.82 | 2.58 | 7.99 | 1.00x |
| kitty | 0.46 | 0.50 | 0.83 | 1.65 | **0.21x / 0.13x** (gate), 0.32x / 0.21x (overnight) |
| dragons | 0.98 | 2.00 | 1.39 | 3.60 | **0.44x / 0.52x** (gate), 0.54x / 0.45x (overnight) |

Both are cheaper than minotaur at every searched cell, so both carry minotaur's
exact case shape. Added overnight cost ~1.8 (kitty) + ~3.6 (dragons) = ~5.4
core-hours on a tier that ran ~9.6 — comfortably inside the 8 h budget.

Two traps this probe walked into, recorded so the next sizing avoids them:
- **Use a different seed per depth and you cannot read depth-monotonicity from
  the result.** The first probe did exactly that; it sized cost correctly and
  said nothing about whether d5 beats d3. The smoke run (one seed, all depths)
  is what actually confirmed monotonicity.
- **Probe at the budget you will ship.** b10/b20 understates the overnight
  cells, which run b20/b40; kitty's d5 ratio moves 0.13x -> 0.21x between them.

### Auras: why d5 costs less than d3, and why its budgets differ per depth

Measured 2026-07-28 (single-thread, 4 overnight seeds). Two Auras-specific facts drive
its matrix entries:

- **d5 is cheaper than d3.** The d5 case drops the `depth` key so the profile's
  `value_play` block owns it (`target_depth 5`, `regime light`), routing the leaf
  through the O(1) value model; the d0/d3 coverage cases carry `ignore_play_profile`
  and so pay full heuristic rollouts. Hence d5 ~0.0086 s/game vs d3 ~0.023 s/game.
- **d5 is CONVERGED at budget 20.** Re-running all four overnight seeds at budget 80
  reproduced every play digest **byte-identically** (96deaf67 / 40d23059 / 1422766f /
  d0945e74) at the same wall time, so overnight generosity at d5 would be provably
  zero-value — it stays at 20. d3 is *not* converged at 10 (budget 80 moved all four
  digests, ~0.004 avg turns), so d3 takes the generous 80 overnight.

No heavy tail at any of the seven suite seeds, which is why Auras carries burn-tier
game counts (1000/seed deep) rather than the smaller th/hinata/dragonstorm sizing.

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

Makespans below re-measured 2026-07-01 after (a) the max-mana backtracker gate
(commit 9229b25, ~15x faster Hinata deep search) and (b) rebalancing Hinata's
deep-search counts up now that it is affordable (smoke +d3 150/d5 75; regression
+d3 200×2/d5 100×2; overnight d3 40→400, d5 25→300 ×4).

- **smoke** (seed 1001): **51s measured** — d0 1000g/deck + small d3/d5 (Hinata's
  d5 75g is the long pole at ~94s single-thread, absorbed by other threads).
- **regression** (seeds 2002,3003): **3m7s measured** — the deep-search decks
  parallelize well at these counts.
- **overnight** (seeds 4004,5005,6006,7007): **30m8s measured** — the Hinata bump
  added ~4.5 min over the pre-rebalance 25m35s. slivers d5 and TH d5 (1000g ×
  4 seeds) still dominate the makespan; Hinata is now a real sample (~2800 deep
  games) without dominating.

All three sit far under their limits (15 min / 45 min / 8 h) — the gate opened up
a lot of headroom. Re-check the total when adding decks; trim slivers first if a
mode overruns (its tail is the least linear in game count).
