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

## Iteration 2 (S2, the exhausted-budget fix, same seeds): close, and every remaining loss is SHALLOWER

| deck | g units / thread-CPU / dturn (better/worse) | g_pz10 |
|---|---|---|
| Hydras (1000) | 0.945x / 0.973x / -0.0010 (3/2) | 0.929x / 0.934x / -0.0010 |
| Alchemist (400) | 0.767x / 0.630x / +0.0050 (2/4) | 0.734x / 0.644x / +0.0050 |
| EDF (400) | 0.891x / 0.766x / +0.0050 (0/2) | 0.764x / 0.641x / +0.0100 |
| Snow (400) | 0.996x / 1.141x / +0.0050 (0/2) | 0.931x / 1.026x / +0.0050 |
| Midrange Burn (400) | 0.942x / 0.951x / -0.0025 (3/1) | 0.899x / 0.860x / +0.0000 |

Attribution (`attrib.py`): DEEPER divergences are now quality-neutral on every deck (Alchemist T1 deeper 1 better /
0 worse, all others 0/0); EVERY loss is a T1 SHALLOWER divergence (Alchemist 1/4, EDF 0/2, Snow 0/2). The two EDF
losers (`lose.py`): game 312 -- heur admitted pass 2 on c1 x 6 = 10.3k and the pass cost 54k on an 18k budget (3x;
our price 75k was RIGHT, heur's overspend found T5 vs T6); game 369 -- heur's pass 2 cost 14.6k (fits), our 2-sample
price was 107k +- 44k (7x high: the real pass finds a T3 incumbent early and every later rollout is cut at 2; the
samples ran at the pass-1 incumbent's cut). So: a refusal is the lossy side, and it is taken on 2 samples.
Snow's thread-CPU 1.14x at 0.996x units: the pricing work is CPU-dearer than its unit charge.

S3 (in flight) screens, on the same seeds, one pooled batch: _pf15/_pf20/_pf30 (ROBUST-OR-EXACT: a verdict in the
band (thr/F, thr*F] hands the decision to the heuristic ladder from pass 1, gate credited with the pricing spend),
_pfa20 (asymmetric: only refusals need the margin), _pd15 (admit at 1.5x thr), _prx (REFUSE-EXACT: at the first
refusal run pass K on rollouts and continue on the heuristic ladder's own gate, c_{K-1} = the priced cost, remaining
credited back to the heuristic ladder's spend), _pmr80/_pmr160 (a refusal needs 8/16 samples). A 10-game probe of
_prx (EDF games 310-319) recovers game 312 but costs 1.74x units: pass K's real cost is WARMED by the sampled rollouts
(721317 T1: 3.5k vs heur's 12.6k), so the heuristic gate sees a cheap c_K and admits a deeper pass (112k).

## Iteration 3 (S3, 28,600 games, 11 arms, same seeds): nothing clears the bar on all five decks

Thread-CPU vs heur / dturn (better/worse games):

| arm | Hydras | Alchemist | EDF | Snow | Midrange Burn |
|---|---|---|---|---|---|
| g | 0.981x -0.0010 (3/2) | 0.657x +0.0050 (2/4) | 0.763x +0.0050 (0/2) | 1.139x +0.0050 (0/2) | 0.951x -0.0025 (3/1) |
| _pf15 | 0.972x -0.0010 | 0.834x +0.0025 | 0.697x +0.0075 (0/3) | 1.218x +0.0050 | 0.962x -0.0050 |
| _pf20 | 0.981x -0.0010 | 0.821x +0.0025 | 0.754x +0.0075 (0/3) | 1.148x +0.0025 | 0.952x -0.0025 |
| _pf30 | 0.986x -0.0020 | 0.864x +0.0050 (0/2) | 0.853x +0.0050 | 1.159x +0.0025 | 0.994x -0.0025 |
| _pfa20 | 0.991x -0.0010 | 0.859x +0.0025 | 0.861x +0.0050 | 1.244x +0.0025 | 1.022x -0.0075 |
| _pd15 | 1.021x -0.0020 | 0.883x 0 (2/2) | 0.830x +0.0050 | 1.291x 0 | 0.999x -0.0075 |
| _pd20 | 1.031x -0.0020 | 1.013x -0.0025 | 0.907x +0.0050 | 1.361x -0.0025 | 1.025x -0.0075 |
| _pd25 | 1.054x -0.0030 | 1.105x -0.0050 | 0.923x +0.0050 | 1.467x -0.0050 | 1.050x -0.0100 |
| _prx | 1.112x -0.0030 (3/0) | 1.132x -0.0025 (2/1) | 1.156x 0 (0/0) | 1.665x -0.0050 (2/0) | 1.181x -0.0075 (3/0) |
| _pmr80 | 1.006x -0.0010 | 0.790x +0.0050 | 0.761x +0.0050 | 1.174x +0.0050 | 0.950x -0.0025 |

Read: ROBUST-OR-EXACT (_pf) does not remove the losses at F <= 3: the losing refusals are priced 1.0-7x over
their threshold, and in most of them the price was RIGHT -- the heuristic ladder admitted a pass whose real cost
was at or beyond 1.1 x remaining (Alchemist 159: priced 18.2k +- 0.5k on 64 samples vs thr 18.0k, heur paid 18.7k;
395: heur 18.5k on thr 18.5k; Snow 199: priced 20.5k, heur 18.0k on thr 17.1k; EDF 312: heur 54k on 18k). The
heuristic ladder's quality edge on those games IS its overspend. Leaning deeper (_pd) or handing refusals to its gate
(_prx: 2 shallower decisions left on Snow, quality best everywhere) buys the quality back, but deeper commits are
expensive (prx 1.09-1.67x CPU: pass K's real cost is warmed by the samples, so the heuristic gate sees a cheap c_K
and admits K+1). Snow's pricing is as expensive as what it saves (early sampled 3.42M units vs heur's superseded
3.63M), which is why g sits at 1.14x CPU there.
9 of the 9 S2 losers inspected: 7 are heur committing pass 2 where g committed pass 1 -- the heuristic ladder's
BOOTSTRAP check (c1 x 6.0 vs 1.1 x remaining), whose only input is the rollout cost of pass 1, the cheapest pass.

S4 (in flight): _pb1 BOOTSTRAP-EXACT -- pass 1 runs on rollouts exactly as the heuristic ladder runs it, its own
bootstrap check decides pass 2 on that real cost, and prices admit passes 3+ only (variants _pz10, _pd15, _pmr80).
10-game probe: both EDF losers (312, 369) recovered.

## Next steps (checkpoint, 2026-11-13 ~05:45 UTC)

- S2 (iteration 1 + the exhausted-budget fix; heur / g / g_pact / g_pz10 on the S1 seeds) is QUEUED on the box lock
  behind another agent's 176,808-game batch: `build/scratch/p18/S2`, DONE marker `S2/DONE`, binary `mtg_b3`
  (`dae1250e5`). Read with `jobsum.py S2/out.log`, `parity.py`, `attrib.py`, `split.py`.
- Offline: heur's formula applied to the PRICED costs reproduces heur's K worse (Hydras T1 31-43% parity) than the
  direct rule (72%): emulation through samples is dead.
- If S2 is still net worse: (a) ROBUST-OR-EXACT -- keep the priced verdict only when it clears the threshold by a
  margin factor F, else run heur's own rollout ladder for that decision (exact parity there); sweep F. (b) root-cause
  the deeper divergences that lost in S1.
- Handoff: `logs/hydras-research/P18-handoff.md` (primary checkout).
