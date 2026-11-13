# Sampled-gate ladder (P18, 2026-11-13): choosing the committing depth without superseded rollouts

Status: IN PROGRESS (checkpoint). Default-off prototype `value_play.sampled_gate` on branch `p18-ladder`
(worktree `.claude/worktrees/p18-ladder`, pushed as `wip/p18-ladder`). No shipped deck changes; adoption is a USER
decision. Follows `single-ladder-skip-leaves.md` (P16) and `logs/hydras-research/P17-audit.md` (P17).

## The problem

P17 showed that with PERFECT knowledge of the heuristic ladder's committing depth K, running passes below K
leafless (plus `MTG_P17_LEAFLESS_READ`) beats heur on all five trade decks at identical win turns (thread-CPU Hydras
0.872x, Alchemist 0.972x, EDF 0.891x, Snow 0.966x, Midrange Burn 0.760x). The open problem is choosing K without
foresight.

## Why emulating heur's gate exactly is out (measured)

heur admits pass k+1 iff `c_k * c_k / c_{k-1} <= 1.1 * remaining` -- a function of the EXACT rollout costs of the
two passes below, i.e. of the very rollouts we want to skip.

1. **Precision needed.** Perturbing the superseded passes' costs in heur's own traces by a lognormal error and
   replaying the gate (`build/scratch/p18/noise.py`): 10% error -> 98.0% depth parity on Hydras, 25% -> 93.8%, 50% ->
   86.4% (the mismatches are mostly SHALLOWER: the estimate is squared, so noise inflates it). P16's rollout_from=2
   lost +0.00125 turn at ~92% parity. So an emulation needs ~10% cost precision.
2. **What a sample buys.** Per-leaf rollout cost has CV ~1.3, so 10% needs ~170 rollouts -- more than most passes
   have leaves. 40-50% of the superseded rollout units sit in passes with fewer than 8 leaves (Hydras 3.2% of all
   units, EDF 5.1%, Alchemist 2.7%), where no sample is cheaper than the pass.
3. **In-order early stop is biased.** The first implementation (monitor the real rollout pass k in its own order and
   stop once its cost was bounded below the admit threshold) underestimated badly: the first leaves of pass k are
   the principal variation, whose rollouts pass k-1 already cached (cost 0), so the pass looked nearly free and the
   squared ratio of the next check refused passes heur admits (Hydras smoke: 1.054x units).
4. **heur's own extrapolation is not a cost model.** `c_k^2/c_{k-1}` against the cost of the pass that then ran
   (`extrap.py`): Hydras T1 d5 median 75x too high, d3 0.59x; Alchemist T1 d4 8x. heur's K is a noisy function of a
   noisy predictor; heur rarely admits a pass that then does not fit (1-12% of admissions).

## The design: a direct-priced ladder

Each pass k runs LEAFLESS first (a proven in-window win commits there, exactly where the rollout pass would have
stopped). That gives its tree cost l_k, its leaf count n_k and a uniform reservoir sample of its leaves. The rollout
pass's cost is then PRICED: `c_k ~ l_k + r_pv + (n_k - 1) * mean(sampled rollouts)` (the first leaf rolled out with the
pass's cut, the rest with the incumbent cut the rollout pass would hand them), one rollout at a time until the price
is decided against the gate at 2 standard errors -- "the number of rollouts required ... and no more". The ladder
commits the deepest pass whose price fits 1.1 x (the decision's remaining budget less the priced costs of the passes
below: the heuristic ladder's spend), and runs only that pass on rollouts, starting at the best win its samples
reached. The samples go through the rollout tables, so the committing pass reads its sampled leaves back.

Estimate error vs sampled rollouts (Hydras T1, first decision, priced cost vs the heuristic ladder's exact cost of
the same pass, `ver2.py`): median 1.0-1.3x at d1-d2, 1.4-1.7x at d3-d4 for m >= 8 -- the ~1.6x is the PRE-PAY heur's
pass k gets from pass k-1's rollouts, which the priced ladder never runs; the price is therefore the priced
ladder's OWN cost of the pass, which is what its fit test needs.

## Iteration 1 (S1, train seeds, one pooled batch, 32 workers): cheaper everywhere, WORSE everywhere

| deck | g units / thread-CPU / dturn | g_pact (actual remaining) | g_pz10 (z=1) |
|---|---|---|---|
| Hydras (1000) | 0.800x / 0.848x / +0.0130 | 0.757x / 0.786x / +0.0120 | 0.770x / 0.807x / +0.0100 |
| Alchemist (400) | 0.749x / 0.613x / +0.0175 | 0.789x / 0.666x / +0.0375 | 0.720x / 0.635x / +0.0200 |
| EDF (400) | 0.956x / 0.865x / +0.0025 | 0.894x / 0.800x / +0.0150 | 0.914x / 0.793x / +0.0075 |
| Snow (400) | 0.859x / 1.004x / +0.0300 | 0.796x / 0.931x / +0.0425 | 0.783x / 0.863x / +0.0275 |
| Midrange Burn (400) | 0.948x / 0.898x / +0.0075 | 0.900x / 0.845x / +0.0075 | 0.917x / 0.885x / +0.0025 |

(g_pnoasp = g without the final-pass bound: identical win turns to g on every deck, so the bound costs no quality.)
Depth parity with heur: 52-72% (Hydras T1 72.3%: 207 shallower, 70 deeper). Attribution by each game's FIRST
depth divergence (`attrib.py`): games whose decisions all matched heur's depth are IDENTICAL (Hydras 596/596 zero
delta); the whole loss sits in divergent games, in BOTH directions (Snow T2+ deeper: 6 worse, 0 better).
Root cause found in the traces: an exhausted budget. When a pass's sampled price (or its leafless pass) ate the
budget, the next passes priced at 0 against a 0 threshold and "fit", so the ladder committed a deep pass on an
empty budget (Hydras 67 of 1696 decisions, Snow 43/1063, Alchemist 25/841). Fixed (a truncated leafless pass or a
threshold <= 0 never fits; the threshold is capped by the real remaining budget; a pass 1 that does not fit stops
the ladder; at least 2 sampled rollouts before a verdict; the bound carries forward).

## Next steps (checkpoint)

- S1 screen running: `build/scratch/p18/S1` (heur vs g / g_pact / g_pnoasp / g_pz10, Hydras 1000 + four decks x 400,
  train seeds), DONE marker `S1/DONE`. Summaries: `jobsum.py S1/out.log`, `parity.py S1/err.log <deck> <arm>`.
