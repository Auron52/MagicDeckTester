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

## Iterations 4-6 (S4-S6, same seeds): BOOTSTRAP-EXACT + LEAFLESS CAP -- quality at heur, cost below it on 4 of 5

_pb1 (BOOTSTRAP-EXACT): pass 1 runs on rollouts exactly as the heuristic ladder runs it (guarded by the ladder's own
overrun ceiling, anytime commit), the ladder's own bootstrap check `c1 x 6.0 <= 1.1 x remaining` decides pass 2 on
that REAL cost (exact parity there), and pricing starts at pass 2 and admits passes 3+ only. Pass 1 is the cheapest
pass, so this costs little, and it removes the SHALLOWER pass-1/pass-2 divergences that carried every loss.
_plc (LEAFLESS CAP): a pass whose leafless tree alone outgrows the admit threshold cannot fit (its price is at least
that tree), so the leafless pass stops at the threshold and the pass is refused -- a sound cut, absent at an
unlimited budget (thr = the budget's own threshold). Refused passes' leafless trees were the biggest single overhead:
Hydras 3.69M units of leafless trees on refused passes (2.83M of it past the threshold) vs heur's 3.96M superseded.
Every leafless pass also carries the ladder's overrun ceiling now (S6 fix: an unguarded _pb1 pass 1 ran 814k units
on Snow where the ladder aborts at 450k).

S6 (`mtg_b8`, 89b225bb7), g_pb1_plc vs heur, train seeds:

| deck | units | thread-CPU | dturn | better/worse |
|---|---|---|---|---|
| Hydras (1000) | 0.917x | 0.987x (S5 0.922x) | 0.0000 | 3/3 |
| Alchemist (400) | 0.837x | 0.686x | 0.0000 | 1/1 |
| EDF (400) | 0.999x | 0.934x | 0.0000 | 0/0 |
| Snow (400) | 0.981x | 1.124x | +0.0025 | 0/1 |
| Midrange Burn (400) | 1.007x | 1.054x (S5 0.983x) | -0.0050 | 2/0 |

Thread-CPU per job is NOISY in a 32-worker pool (same-digest Snow jobs at 0.99x units read 1.84x CPU; a
40-game 8-thread probe of the same jobs read 0.95x CPU at 0.96x units). Snow's 1.12-1.20x CPU at <=1.0x units is
nevertheless systematic across S3-S6 -- suspected: the reservoir copies GameStates (up to 64 + ~64 ln(n/64) per pass,
not charged in units) and allocation contention under 32 workers. S7 (Snow only, MTG_P17_LEDGER) localizes it
(`ledcpu.py S7/err.log`). Snow's one loser (game 364): heur K=3 on pass 3 = 14.1k against thr 14.2k; our price
21.4k (1.5x high: the warm-vs-cold pre-pay factor) refused.

## Iterations 7-8 (S7, S8; P18c): the excess is refused deeper passes, and refuse-exact at pass 3 buys quality

- S7 (Snow): the "52k ns/u" W rate was an accounting artefact (sample units missing from its denominator; ~39k ns/u).
  Sampler GameState copies are ~225/game, negligible. Most of the measured excess was REFUSED deeper passes (their
  leafless tree plus samples, refused afterwards): priced_out 0.82M units, capped 0.64M, "thr0_post" 0.59M.
- Every S6 loser (Hydras 21, 976; Alchemist 15; Snow 364) is a gate K=2 with pass 3 refused where heur's own
  c2^2/c1 admits pass 3. Mechanism: price = l + n x mean(sampled rollout) assumes each leaf costs a full rollout,
  but the real pass finds an improving line early and its incumbent cut makes later rollouts nearly free (Snow 364
  T2: priced 87.6k, heur's real 16.5k). With 64 samples of ~1,000 leaves a rare improving leaf is usually missed,
  so this is a sampling-coverage error, not a constant in the formula.
- `_prx2` (refuse-exact at pass 3: when pass 3 is refused, pass 2 runs on rollouts, which is the commit anyway, and
  heur's real c2^2/c1 decides pass 3) brings quality to net <= 0 on all five train decks (S8).
- S8 thread-CPU vs heur for `g_pb1_plc_prr_prx2`: 1.029 / 0.762 / 0.920 / 1.167 / 1.051x (Hydras / Alchemist / EDF /
  Snow / MB). The Snow and MB figures are biased; see Iteration 9.

## Iteration 9 (P18d): the "per-unit CPU gap" was the batch scheduler, not the gate

- The P1 perf pair (Snow, 240 games, heur and gate as two concurrent 16-thread batches): gate 0.990x heur thread-CPU,
  0.981x units, per-unit +0.9%, and flat, near-identical symbol profiles (no hash-map, malloc or GameState-copy
  growth). The same 240 games in S8: heur 603k cpu-ms, gate 640k, for byte-identical units.
- The S8 excess sits in a few heavy games whose work is IDENTICAL in both arms (Snow gi 144: 1,003,088 units in heur
  and every gate arm): heur 41 s, the five gate arms 62-73 s. Re-run in isolation (6 identical copies, 6 threads),
  in a 5-game chunk (per-job state), and as 16 heur + 16 gate copies at 32 threads with the S8 trace on: gate == heur
  every time (26.5 vs 26.4 s; 57.0 vs 57.0 s at 32 threads). So neither the gate's code, its working set,
  identical-copy contention, per-job state nor the trace instrumentation explains it.
- Mechanism: `BatchRunner`'s LPT sort orders the work list by (weight, depth, budget, PROFILE PATH, job). Every arm
  shares depth and budget, so each deck's arms run as contiguous blocks in alphabetical order of their arm dirs:
  `g_...` arms first, `heur` LAST, and Snow (alphabetically last deck) last overall. S8 completion lines: snow_heur
  1512-1561 of 1562. heur Snow therefore ran in the batch's draining tail, where SMT siblings are idle and a thread
  does the same work in about half the CPU time (game 144: 26 s alone vs 57 s at 32 busy threads). "Arms innermost"
  in the manifest was undone by the runner. That is why the gap was systematic on Snow (and MB, the deck before it)
  in S3-S8 and shrank in the lighter S7 pool.
- Fix for measurement: give each job `"weight": N - index`, so the runner keeps manifest order (arms innermost in
  time), with `MTG_BATCH_PROFILE_CACHE=30` so the interleaved profiles stay resident. S9 is the first screen measured
  that way. Every earlier thread-CPU comparison is biased against the gate on the last deck(s) of the sort.
- `_pfr<10F>` FREE REFUSAL (with `_pb1` + `_prx2`): pricing pass 2 serves only pass 3's check, so before pass 2's
  leafless tree is built, heur's own extrapolation on the one real cost known (c2 ~ c1 x growth, c3 ~ c2^2/c1) asks
  whether pass 3 could fit what pass 2 leaves. When the prediction exceeds F x the admit line, the gate prices
  nothing and hands the decision to heur's ladder from pass 2 (its real c2^2/c1 decides pass 3). The refusal is free
  and its verdict is heur's own on real costs. Inert at an unlimited budget.

### S9 (train seeds as S8; 15,600 games, one pooled batch, 32 workers, weight-ordered, no trace; `build/scratch/p18d/S9`, binary a37c3fe2c)
thread-CPU vs heur, units vs heur, dturn (better/worse games):
| arm | Hydras | Alchemist | EDF | Snow | Midrange Burn |
|---|---|---|---|---|---|
| g_pb1_plc | 0.908x u0.917 0 (3/3) | 0.801x u0.837 0 (1/1) | 0.907x u0.999 0 | 1.011x u0.981 +0.0025 (0/1) | 1.006x u1.007 -0.0050 (2/0) |
| _prr_prx2 | 0.990x u1.006 -0.0020 (3/1) | 0.851x u0.885 0 (1/1) | 0.926x u1.027 0 | 1.052x u1.028 0 | 1.052x u1.056 -0.0050 (2/0) |
| _prr_prx2_pfr10 | 0.956x u0.975 -0.0020 (2/0) | 0.871x u0.916 -0.0025 (1/0) | 0.993x u1.013 0 | 1.016x u1.009 0 | 1.001x u1.025 -0.0025 (1/0) |
| _prr_prx2_pfr20 | 0.981x u0.993 -0.0020 (2/0) | 0.847x u0.899 -0.0025 (1/0) | 0.935x u1.002 0 | 1.017x u0.989 0 | 0.988x u1.019 -0.0050 (2/0) |
| _prr_prx2_ppr15_pfr10 | 0.992x u0.997 -0.0020 (2/0) | 0.893x u0.935 0 | 0.999x u1.023 0 | 1.050x u1.015 0 | 0.968x u1.012 0 |
Units of g_pb1_plc and _prr_prx2 are identical to S8 (the new knob is inert when absent). The scheduler fix alone
moves Snow g_pb1_plc 1.119x -> 1.011x and _prr_prx2 1.167x -> 1.052x. `_pfr20` has NO worse game on any of the five
decks and is below heur's CPU on four; Snow reads 1.017x at 0.989x units. S10 (Snow + MB, 2,000 games/arm on fresh
seeds with a heur A/A arm for the CPU noise floor) checks whether that is real.

### S10 / S11 / D1 (P18d)
- S10 (Snow + MB, 2,000 games/arm, fresh seeds 725000/726000, weight-ordered, with an A/A copy of heur):
  heurB 0.998x / 0.983x CPU (the noise floor), `_pfr20` Snow 1.012x CPU at 1.012x units, dt 0 (2/2); MB 0.971x,
  dt -0.0025 (6/1). `_pfr10` Snow 1.017x / MB 1.020x.
- S11 (S9's seeds again, binary 2b93bb0df): `_pfr20` reproduces S9's units exactly and reads CPU 0.847 / 0.942 /
  0.981 / 0.975 / 0.991x (Alchemist / EDF / Hydras / MB / Snow) -- Snow 1.017x in S9 on the same games, so the
  batch-to-batch CPU noise on 400 Snow games is about +-2.5%. `_pfx` (the free refusal at every k >= 3, routed exact)
  does not help: `_pfr20_pfx` 0.877 / 0.949 / 0.973 / 0.970 / 1.003x; `_pfr10_pfx` 0.894 / 0.997 / 0.990 / 0.975 /
  1.012x. No arm has a worse game in S11.
- D1 (Snow, 400 games, traced, `_pfr20` vs heur): heur 21.12M units = 20.01M committing + 1.12M superseded (5.3%).
  Gate 21.08M = 18.16M committing + 0.75M superseded + 1.68M leafless + 0.50M sampled. Refused priced passes still
  cost ~1.1M (capped 0.53M; priced_out 0.56M), and admitted passes' pricing ~1.0M. Snow's whole superseded share
  is 5.3%, so the gate's margin there is the refused-pass waste: a free refusal that caught all of it would put
  Snow at ~0.95x units. The heur rollout share per pass is ~0.9+ on every deck (Snow 0.98/0.92/0.92), so this is
  not the leafless tree outweighing the rollouts.
- Held-out A/B H1 launched on `g_pb1_plc_prr_prx2_pfr20` (see the checkpoint below).

### H1 (held-out A/B, 33,600 games, DONE): `_pfr20` fails the bar
Pooled CPU 0.938x over 17 decks; quality better on the five screen decks (Hydras -0.0011 14/5, Alchemist -0.0070
8/1, EDF -0.0030, MB -0.0020, Snow 0) and level on 10 of the 12 broader decks, but Landfall +0.0325 (3/16) and CPU
above heur on Melira 1.165x, Snow 1.023x, Bruna 1.003x. Landfall's losses are two mechanisms at the T1 decision:
`_prr`'s cap halving the room for a pass that has no leaves (the tree IS the pass), and the cold over-price at pass 4
where the refusal is not routed exact. Details and the next arm: `logs/hydras-research/P18d-handoff.md`.

## Checkpoint (P18d, 2026-11-13 ~09:20 UTC)
- Candidate: `g_pb1_plc_prr_prx2_pfr20` (bootstrap-exact pass 1, leafless cap, real-remaining cap, refuse-exact at
  pass 3, free refusal of pass 3 at F=2 on heur's own extrapolation from c1).
- H1 (DONE, see above) held-out A/B (heur vs candidate; Hydras 8,000/arm seeds 780000+, Alchemist/EDF/Snow/MB 1,000/arm seeds
  790000/800000/810000/820000, the 12 broader decks 400/arm seeds 830000+; 33,600 games; weight-ordered; binary
  `build/scratch/p18d/mtg_d2` = 2b93bb0df): `build/scratch/p18d/H1`, DONE marker `H1/DONE`. Read with
  `python3 build/scratch/p18d/s9sum.py build/scratch/p18d/H1 heur g_pb1_plc_prr_prx2_pfr20`.
- Every thread-CPU screen must weight-order its manifest (`"weight": N - index`), or the runner's profile-path sort
  puts heur last within each deck and the last deck's heur in the batch tail.
- Open: Snow sits at CPU parity (0.991-1.017x over three batches); the remaining lever is the refused-pass waste
  (~5% of Snow's work) and the double walk of the committing pass's interior. The cold over-price (a rare improving
  leaf the 64 samples miss) is a sampling-coverage error; `_prx2`/`_pfr` sidestep it by deciding pass 3 on real costs.

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

## USER ruling 2026-11-13 (after P18d): continuous sampling, not a hand-off
"I would rather it doesn't do exactly that. My preferred vision would be that it uses less rollouts when more certain
and more when less certain. So, it would be closer to the heuristic rollout, but would still bank savings."
So `_prx2` / `_pfr` (hand the refusal to heur's real-cost check) are NOT the design. The sample count must grow
continuously with the verdict's uncertainty, but never up to the whole pass. Next: make the
price's uncertainty honest (the 2-SE stop under-states it on heavy-tailed passes; no fixed sample cap), then re-screen.
Follow-up, VERBATIM: "There shouldn't be cases where it is so uncertain it just runs the ladder." / "It should at least
cut out a significant chunk of it." So the sample count has a hard ceiling at a general fraction of the pass's
estimated rollout cost, and at the ceiling the gate decides anyway (recommended: lean admit, bounded by the overrun
ceiling, since wrong refusals are what cost quality).

## Iteration 10 (P18e): uncertainty-scaled CLUSTER sampling, no hand-off (`_pus`, `_pbk`, `_prr2`)

Built under the USER's 2026-11-13 rulings (more rollouts when less certain, never the whole ladder, decide at a
ceiling). Commit aa3209ae0 (+ the cluster rewrite after it); screening knobs in the arm key, default off. `_prx2` /
`_pfr` are NOT used.
- **Rollout cost profiles.** The outermost rollout records the units spent at the head of each simulated turn
  (`p18::t_prof`), so a sampled rollout can be re-costed at any tighter cut without running it again.
- **Visit-order pricing (the incumbent effect).** Every rolled leaf keeps its visit position. The price is taken in
  visit order: each leaf is costed at the cut the real pass would hand it (the best win among the rolled leaves
  BEFORE it), read off its profile. A leaf is rolled at the cut it gets from the leaves rolled so far; later samples
  can only tighten that, so its profile always covers the cut it is priced at.
- **Cluster sampling in visit order.** Leaves after the first are cut into consecutive blocks; block 0 (the PV's
  neighbourhood) is always rolled, later blocks are reservoir-sampled WHOLE and rolled in order. Siblings share
  rollout paths through the rollout table and an early incumbent cuts its neighbours, so a block prices each leaf
  with the leaves that precede it -- which individually sampled leaves cannot (they are each priced cold). Block
  size ~ 1/64 of the pass's expected leaves (leaf-count growth of the passes below), capped at `_pbk<N>` (8), >= 1.
- **Honest interval, uncertainty-scaled count.** The block totals give the SE (with the CV floor 0.5 as the heavy-tail
  prior, finite-population corrected). Blocks are drawn until `P + 2 se <= thr` (admit) or `P - 2 se > thr`
  (refuse), so a verdict far from the threshold costs 2 blocks and a close one more.
- **Hard ceiling.** Never more than `_pus<pct>` (25%) of the pass's leaves (at least 3 blocks), and never more than
  the 256-leaf reservoir (a memory bound). At the ceiling the gate decides anyway: `_pul` admits, otherwise the point
  price decides.
- **`_prr2`.** The real-remaining cap `l <= a R/(1+a)` is armed only at the pass's first leaf; before it the cap is
  the admit line `a R`. NOT done: committing a no-leaf leafless pass directly. A leafless pass whose subtrees are
  answered from the game-line cache (or the leafed entries it reads) offers no leaves for them, so `n == 0` does not
  prove the rollout pass would roll nothing out; committing it would not be exact.
- A first version (T1) priced individually sampled leaves and stated the refusal side with an "unseen improver"
  bound (an improving leaf at the expected first position of an unobserved event cutting every later leaf one
  turn). That bound was not credible: on short horizons one turn is most of a rollout, so it collapsed to the bare
  tree, 35-40% of Snow's priced passes hit the ceiling, and lean-admit there cost Snow 1.39-1.54x CPU. Dropped.

### T1 / T2 (traced subsets, 100 games/deck, Hydras 200; train seeds = the first games of S12)
| arm | Hyd cpu | Alc | EDF | Snow | MB | Landfall | Melira | ceiling hits / priced pass |
|---|---|---|---|---|---|---|---|---|
| T1 `pus25` (single leaves, point) | 0.986 | 0.724 | 0.782 | 1.080 | 0.991 | 0.899 +0.01 (1/2) | 0.662 +0.02 (0/2) | Snow 0.35, Alc 0.20, MB 0.11 |
| T1 `pus25_pul` | 1.094 | 0.798 | 0.802 | 1.535 | 1.004 | 0.933 +0.01 | 0.673 +0.02 | same |
| T2 `pus25_pbk8` (clusters, point) | 0.932 | 0.680 +0.01 (0/1) | 0.878 | 1.041 | 0.926 | 0.907 +0.01 (1/2) | 0.666 +0.02 (0/2) | Snow 0.057, Alc 0.057, EDF 0.041, MB 0.021, Hyd 0.022, Landfall 0.005, Melira 0 |
| T2 `pus25_pbk16` | 0.938 | 0.686 | 0.877 | 1.028 | 0.937 | 0.918 | 0.661 | about the same |
| T2 `pus25_pbk8_pul` | 0.924 | 0.678 | 0.885 | 1.035 | 0.933 | 0.904 | 0.662 | about the same |
(traced, so CPU is indicative only.) Cluster sampling cut the ceiling-hit rate about 6x, and with it most of the
lean-admit cost. Every remaining loss is a T1 SHALLOWER decision, and the two inspected are not sampling errors:
Melira 728014 T1 (heur's pass 3 cost 26.8k on an 18k budget, admitted on c2^2/c1 = 12.3k; the gate's leafless
pass 3 hit the `_prr` cap at 9.3k, since its interior alone is ~18k and the commit re-walks it) and Landfall
727004-class decisions (over-priced lower passes shrink the deeper pass's `rem0 - priced` threshold).

### S12 (train seeds; 18,400 games, one pooled batch, 32 workers, weight-ordered, untraced; `build/scratch/p18e/S12`, binary `mtg_e2` = aa3209ae0)
Hydras 1000 @710000, Alchemist/EDF/Snow/MB 400 @720000-723000, Landfall 1000 @727000, Melira 1000 @728000.
thread-CPU vs heur, units, dturn (better/worse):
| arm | Hydras | Alchemist | EDF | Snow | MB | Landfall | Melira |
|---|---|---|---|---|---|---|---|
| `g_pb1_plc_prr2_pus25_pbk8` | 0.983 u0.957 -0.0010 (3/2) | 0.813 u0.851 0 (1/1) | 0.929 u1.020 0 | 1.002 u0.982 +0.0025 (0/1) | 0.920 u1.028 -0.0050 (2/0) | 0.728 u0.819 **+0.0140 (13/27)** | 0.746 u0.813 **+0.0060 (10/16)** |
| `..._pbk8_pul` | 0.981 -0.0010 (3/2) | 0.811 0 (1/1) | 0.928 0 | 1.004 +0.0025 (0/1) | 0.951 -0.0050 (2/0) | 0.726 **+0.0140 (12/26)** | 0.748 **+0.0060 (10/16)** |
| `g_pb1_plc_pus25_pbk8_pact` | 0.995 u0.969 -0.0010 (3/2) | 0.793 0 (1/1) | 0.915 +0.0025 (0/1) | 1.018 u0.998 0 | 0.992 u1.121 -0.0025 (1/0) | 0.857 u0.954 **+0.0050 (18/23)** | 0.811 u0.885 **+0.0040 (10/14)** |
No arm passes: Landfall and Melira (the two held-out failures of H1) stay net worse, every loss T1-shallower class.
Lean-admit at the ceiling changes almost nothing now (the ceiling is rarely reached). `_pact` (the deeper pass's
threshold on the decision's REAL remaining budget instead of `rem0 - sum of priced lower passes`, and no `_prr`
cap) cuts Landfall's loss by two thirds at a CPU cost (Landfall 0.73 -> 0.86x, MB 0.92 -> 0.99x, Snow 1.00 -> 1.02x).

### Why Landfall / Melira lose: the DOUBLE WALK, not the sampling (traced replay L2 of all 37 `_pact` losers)
Every Landfall loser inspected is a T1 pass the heuristic ladder commits and the gate refuses on its REAL remaining
budget, and the gap is the leafless walk: 727043 heur pass 4 = 13,268 (interior ~7.9k, leaves 5.3k) on a 18k budget,
heur's total spend 14.1k. The gate walks pass 4 leafless (7,934 real units), so its admit line falls to
1.1 x (18k - 8.8k) = 10.8k against a commit price 15.7k (the commit re-walks the same ~7.9k interior: measured, the
committing pass's interior equals the leafless l on every committed pass inspected -- the in-window proofs it
shares do not cover no-win interior). On interior-heavy passes the gate pays the interior twice; no sampling or
interval change can recover that. 23 of 23 Landfall `_pact` losers are this shape (T1, pass 4 or 5).

### S13 (S12 seeds; 13,800 games, one pooled batch; `build/scratch/p18e/S13`, binary `mtg_e3` = 6b0be9766)
`_pnr`: the admit line and the leafless cap read the heuristic ladder's remaining (rem0 less the priced passes below),
with no clamp to the decision's real remaining (overspend bounded by the overrun ceiling, like heur's).
| arm | Hydras | Alchemist | EDF | Snow | MB | Landfall | Melira |
|---|---|---|---|---|---|---|---|
| `g_pb1_plc_pus25_pbk8_pnr` | 0.984 u0.969 -0.0020 (4/2) | 0.755 u0.797 **+0.0050 (1/3)** | 0.847 u0.938 0 | 0.889 u0.854 +0.0025 (0/1) | 0.915 u1.039 -0.0025 (2/1) | **1.268** u1.277 -0.0010 (17/16) | **1.051** u1.114 -0.0020 (10/8) |
| `g_pb1_plc_pus12_pbk8_pnr` | 0.974 -0.0020 (4/2) | 0.754 +0.0050 (1/3) | 0.837 0 | 0.880 +0.0025 (0/1) | 0.899 -0.0025 (2/1) | 1.269 -0.0010 | 1.056 -0.0020 |
Dropping the clamp recovers Landfall and Melira quality (net better) but pays the double walk in CPU there
(1.27x / 1.05x); Snow drops to 0.88-0.89x. The clamp (S12) and no clamp (S13) bracket the answer: the next arm,
`_pcr`, credits back only the committing candidate's own leafless walk (the one duplicated cost), so a decision
overspends by at most the interior it walks twice.
