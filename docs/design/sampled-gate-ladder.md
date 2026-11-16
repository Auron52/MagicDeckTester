# Sampled-gate ladder (P18, 2026-11-13): choosing the committing depth without superseded rollouts

Status: IMPLEMENTED, opt-in. `value_play.sampled_gate=2` is the measured fast shape; `=1` preserves the raw
research sampler and the default is off. The mode beats the bare heuristic depth-five / 20 ms ladder on Angels,
Critter Lifegain, and Minotaur in paired held-out games. Their current shipped play settings remain faster, so
none of those deck sidecars changes. Follows `single-ladder-skip-leaves.md` (P16) and
`logs/hydras-research/P17-audit.md` (P17).

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
interval change can recover that. 19 of the 23 Landfall `_pact` losers (10 of 14 Melira) have a T1 decision
committed shallower than heur; the 8 Landfall ones inspected by hand are all this shape (pass 4 or 5).

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

### S14 (S12 seeds; 13,800 games; `build/scratch/p18e/S14`, binary `mtg_e4` = 09aeac874): `_pcr` == `_pnr`
`g_pb1_plc_pus25_pbk8_pcr` reproduces S13's `_pnr` units on every deck (Hydras 0.969, Alc 0.797, EDF 0.938, Snow
0.854, MB 1.039, Landfall 1.277, Melira 1.114) and its dturn; CPU 0.986 / 0.754 / 0.848 / 0.890 / 0.949 / 1.266 /
1.051. So the real-remaining clamp bit ONLY through the pass's own leafless walk: crediting that walk back is the
same as dropping the clamp. That confirms the double walk as the whole mechanism, and it means the CPU Landfall and
Melira pay under `_pnr` IS the duplicated interior. Unit tests (sampled_gate, Leafless reads leafed, rollout_from,
fit_gate; 9 cases, 120 assertions) pass on 09aeac874.

## Checkpoint (P18e, 2026-11-13 ~11:40 UTC) -- no arm passes the train screen
- Sampling is no longer the problem: cluster sampling in visit order brings ceiling hits to 0-6% of priced passes and
  skips 23-86% of heur's superseded rollout units (Alc 23% is the weak deck: the refused pass K+1 is priced too).
- Clamped (`_prr2` / `_pact`): CPU below heur on 6 of 7 decks (Snow 1.00), quality net worse on Landfall/Melira.
  Unclamped (`_pnr` == `_pcr`): quality net <= 0 except Alchemist +0.0050 (1/3), CPU 0.75-0.99x on five decks but
  1.27x Landfall, 1.05x Melira. The difference between the two is exactly the double-walked interior.
- Next (in order): (1) remove the double walk for the COMMITTING pass -- the leafless walk of pass K is needed to price
  K, but its interior is then walked again with rollouts; candidates: run the committing pass's rollouts INSIDE a
  re-entry of the leafless tree's no-win frontier (only nodes whose leafless answer depended on a leaf need
  re-search), or start the commit as a rollout pass and price the NEXT pass from its own leaves; (2) Alchemist's
  `_pnr` losers (3) -- not yet inspected; (3) then the held-out A/B on fresh seeds (mkH.py shape), and a second
  independent batch for CPU.

## Iteration 11 (P18f): the double walk is NOT leaf-free, and it is not the largest overhead

### `_plf` leaf-free answers (exact, measured worthless)
Built the sound part of "re-enter only the leafless tree's leaf-dependent frontier": a node whose COMPLETE search
under cutoff c reached no depth-0 leaf and read no memo answer proves "no win <= c" for every leaf kind, so it is
stored in the game line cache under a leaf-free tag and read by any node (rollout / value / no leaf) at cutoff <= c
(`gamelc::LeafFreeOn`, MTG_P18_LEAFFREE or arm key `_plf`, default off).
- Exactness: V1 (140 games, all 7 decks, MTG_GAME_LINE_CACHE_VERIFY=1, every hit re-searched): 46 leaf-free hits
  re-searched, 0 MATERIAL. U1 (280 paired games, `_pnr` vs `_pnr_plf`): 280/280 digests byte-identical.
- Effect: 475 leaf-free stores / 411 hits against ~20k ordinary game-line-cache stores; units ratio 1.000 on every
  deck. The committing pass's interior is leaf-BEARING: almost every interior subtree of pass K reaches a leaf, so no
  completed leafless proof can stand in for its rollout answer. Removing the double walk exactly needs the leaves
  themselves (a deferred-leaf replay of the leafless tree: record the leaf frontier in visit order, roll it with the
  incumbent cut, reconstruct the line), not a cache read. Kept default-off as a sound, inert fold.

### Where `_pnr`'s extra cost really goes (T3: traced, 100 games/deck, Landfall/Melira 200; heur vs `g_pb1_plc_pus25_pbk8_pnr`, binary p18f/mtg_f1 = 42ccc2e52 without _phx)
`build/scratch/p18f/T3`; tools `p18f/dec.py` (p16 pass records) and `p18f/p18rec.py` ([p18] priced-pass records).
Units (p16 decision records; the gate's sampled rollouts are in the [p18] `su` column, not here):
| deck | heur all | heur superseded | gate all | gate leafless walks | double walk (commit interior <= its leafless l) | refused passes' leafless l | admitted passes' l |
|---|---|---|---|---|---|---|---|
| Landfall | 4.24M | 1.62M | 5.60M (1.32x) | 3.63M | 0.76M | **1.64M** (183 passes) | 1.68M |
| Melira | 5.85M | 1.17M | 6.01M (1.03x) | 3.78M | 0.86M | **1.43M** (172) | 1.92M |
| Hydras | 1.46M | 0.49M | 1.29M | 0.70M | 0.15M | 0.10M | 0.53M |
| Snow | 4.85M | 0.30M | 3.76M | 0.70M | 0.12M | 0.12M | 0.53M |
- heur's commit pass on Landfall / Melira is 93% / 94% INTERIOR (leaf units 0.19M of 2.62M, 0.29M of 4.68M): the
  rollouts are cheap there, so the pool the gate can save is small, and each overhead the gate adds is interior.
- The refused passes' leafless walks are the LARGEST overhead on both decks (each refused pass walks its interior to
  the admit line before the cap trips; heur refuses on an extrapolation that costs nothing). The double walk is the
  second. Even a perfect, exact double-walk removal leaves Landfall at ~1.14x units (5.60M - 0.76M vs 4.24M);
  Melira would reach ~0.88x.
- Superseded LEAF units in heur (the most "heur minus unnecessary rollouts" can save, T2): Landfall 12%, Melira 3%,
  Snow 5%, Alc 11%, EDF 14%, Hydras 20%, MB 27% of heur's units (x ~5 in CPU, since a rollout unit costs ~5x).

### `_phx`: heur's own free refusal, on prices
Before pass k >= 3 is walked, `c_{k-1}^2 / c_{k-2}` on the PRICED costs is checked against `gate_alpha x (rem0 -
priced)` -- heur's start gate with prices standing in for its real superseded costs. A pass heur would not start is
refused at no cost; only passes heur would start are walked and priced (the price can still refuse them). This is not
a hand-off: nothing runs on rollouts, the gate still commits its priced K. It is the shape the USER asked for ("search
approximately the same as the heuristic ladder ... except ... not running unnecessary leaves").

### S15 (S12 seeds; 18,400 games, one pooled weight-ordered batch, 32 workers; `build/scratch/p18f/S15`, binary 42ccc2e52)
thread-CPU vs heur, units, dturn (better/worse):
| arm | Hydras | Alchemist | EDF | Snow | MB | Landfall | Melira |
|---|---|---|---|---|---|---|---|
| `g_pb1_plc_pus25_pbk8_pnr` (S13 repeat) | 0.984 u0.969 -0.0020 (4/2) | 0.755 u0.797 +0.0050 (1/3) | 0.844 u0.938 0 | 0.897 u0.854 +0.0025 (0/1) | 0.929 u1.039 -0.0025 (2/1) | 1.263 u1.277 -0.0010 (17/16) | 1.042 u1.114 -0.0020 (10/8) |
| `..._pnr_phx` | **0.884** u0.878 +0.0020 (2/4) | 0.838 u0.835 +0.0050 (0/2) | 0.916 u0.991 0 | 0.914 u0.903 +0.0025 (0/1) | 0.855 u0.909 +0.0075 (0/3) | **0.913** u0.943 **+0.0140 (11/25)** | **0.864** u0.918 +0.0070 (4/11) |
| `g_pb1_plc_pus12_pbk8_pnr_phx` | 0.877 +0.0020 (2/4) | 0.838 +0.0050 (0/2) | 0.914 0 | 0.917 +0.0025 (0/1) | 0.846 +0.0075 (0/3) | 0.912 +0.0140 (11/25) | 0.865 +0.0070 (4/11) |
`_phx` puts thread-CPU below heur on all seven decks for the first time (0.85-0.92x), Landfall from 1.26x to 0.91x
and Melira from 1.04x to 0.86x -- confirming the refused-pass walks as the dominant overhead -- but it gives back
quality everywhere (Landfall +0.0140, MB +0.0075, Melira +0.0070). Its refusals bite where heur's do not: the
extrapolation runs on PRICES, and a price is cold (each pass priced on its own, without the rollout-table reuse
heur's real passes get across the ladder) and the admit line reads `rem0 - priced`, so a high price both inflates the
estimate and shrinks the room. S16 measures a margin (`_phx<10F>`: refuse only above F x the line).

### S16 (S12 seeds; 18,400 games, one pooled batch; `build/scratch/p18f/S16`, binary 6989ab158): the margin
| arm | Hydras | Alchemist | EDF | Snow | MB | Landfall | Melira |
|---|---|---|---|---|---|---|---|
| `..._pnr_phx15` | 0.901 u0.881 +0.0010 (3/4) | 0.832 +0.0075 (0/3) | 0.913 0 | 0.922 +0.0025 (0/1) | 0.873 +0.0075 (0/3) | 0.964 u1.003 +0.0100 (13/23) | 0.926 u0.978 +0.0060 (3/9) |
| `..._pnr_phx20` | 0.914 +0.0010 (3/4) | 0.822 +0.0075 (0/3) | 0.913 0 | 0.918 +0.0025 (0/1) | 0.874 +0.0075 (0/3) | 1.003 u1.038 +0.0080 (13/21) | 0.963 u1.021 +0.0020 (5/7) |
| `..._pnr_phx30` | 0.964 0 (3/3) | 0.854 +0.0075 (0/3) | 0.909 0 | 0.923 +0.0025 (0/1) | 0.904 +0.0025 (0/1) | 1.096 u1.105 +0.0010 (14/15) | 0.997 u1.055 +0.0020 (6/8) |
The margin trades the refused walks back for quality continuously (Landfall 0.91x/+0.014 -> 0.96x/+0.010 ->
1.00x/+0.008 -> 1.10x/+0.001; `_pnr` is 1.26x/-0.001). No F passes: wherever Landfall's quality is recovered its CPU
is back above heur, and Alchemist (+0.0075, 0/3 at every F, +0.0050 under `_pnr`) and MB (+0.0025..0.0075) lose games
`_pnr` does not -- so part of `_phx`'s loss is not the margin at all (inspect those few games first: they are cheap).

## Checkpoint (P18f, 2026-11-13 ~12:25 UTC) -- no arm passes the train screen
- The double walk is real but leaf-bearing: no completed leafless proof can answer the committing pass's interior
  (`_plf`, exact and inert). An exact removal needs a deferred-leaf replay of the leafless tree (record the leaf
  frontier and the holes the leafless walk answered from caches; roll the frontier in visit order with the incumbent
  cut; re-search the holes with rollouts). Even perfect, it leaves Landfall ~1.14x units under `_pnr`.
- The LARGEST overhead on interior-heavy decks is the refused pass's leafless walk (Landfall 1.64M, Melira 1.43M
  units in T3) -- heur refuses for free. `_phx` (heur's own start gate, on prices) removes it and puts CPU below heur
  on all seven decks (0.85-0.92x), but at a quality cost that a margin buys back only with the CPU.
- Why prices refuse where heur's real costs admit is the open question; the leading hypothesis is that a price is
  cold (heur's real pass k reuses pass k-1's rollouts through the rollout table) and that `rem0 - priced` charges
  those cold prices against the room. Test: in the same decisions, compare `p18_est[k]` with heur's real `c_k`
  (`_pverify` prices on a scratch meter and then runs heur, pairing each price with the real cost).

## P18g restart (2026-11-16): cold prices confirmed; three further train screens fail

The restart used the existing `p18-ladder` worktree and the same held train seeds as S15/S16. All arms were paired in
one weighted, 32-worker batch per screen. The baseline's per-game `.wins` matched the earlier trace on every shared
chunk; no shipped deck was changed. Artifacts are `build/scratch/p18f/S17` through `S19` in the primary checkout.
These are small diagnostic screens (Hydras/Landfall/Melira 200 games each; Alchemist/EDF/Snow/Midrange Burn 100 each),
not adoption samples. All CPU ratios below are pooled thread CPU for each arm, against heur on the same games.

**Cold-price evidence.** In T3's paired first-turn traces, the gate's price exceeds heur's measured pass cost on
177/185 Landfall depth-four decisions (median 1.35x, p90 1.99x); Hydras depth three: 80/99 (median 1.70x).
The price is the cold pass this shape would pay, while heur's pass reads earlier rollouts from its cache. Thus
`_phx` can refuse a pass heur would start even when the sample's own statistical error is small.

| screen / arm | Landfall: delta, CPU | Hydras: delta, CPU | other failure |
|---|---:|---:|---|
| S17 no pre-refusal (`_pnr`) | +0.000, 1.423x | +0.000, 1.120x | Alchemist +0.010, Melira +0.005 |
| S17 `_phx` from depth 3 | +0.010, 1.007x | +0.010, 0.884x | Alchemist/Melira/MB +0.010 |
| S17 `_phx_phd4` | +0.010, 1.011x | +0.005, 0.952x | Alchemist +0.010 |
| S17 `_phx_phd5` | +0.005, 1.168x | +0.000, 1.122x | Alchemist +0.010, Melira +0.005 |
| S18 exact pass 2 (`_pb2`, no pre-refusal) | +0.015, 1.356x | +0.000, 0.941x | Alchemist +0.010, Melira +0.005 |
| S18 `_pb2_phx_phd4` | +0.030, 0.931x | +0.005, 0.879x | Alchemist +0.010 |
| S18 `_pb2_phx_phd5` | +0.030, 1.076x | +0.000, 0.949x | Alchemist +0.010 |
| S19 minimum 10% samples (`_pmi10`) | +0.000, 1.472x | +0.000, 1.131x | Alchemist +0.010, Melira +0.005 |
| S19 minimum 20% (`_pmi20`) | -0.005, 1.573x | +0.000, 1.160x | Alchemist +0.010, Melira +0.005 |
| S19 `_pmi10_phx_phd5` | +0.005, 1.193x | +0.000, 1.139x | Alchemist +0.010, Melira +0.005 |

S17 varied the earliest pass at which the free pre-refusal can fire. S18 ran pass two on rollouts exactly, using its
measured cost to gate pass three and warm the cache before sampling deeper passes. S19 required 10% or 20% of an
admitted pass's leaves to be sampled before the confidence test could stop; refusals still stop early. `_phd`, `_pb2`
and `_pmi` are default-off experimental arm-key selectors. None clears the fleet bar; no held-out adoption A/B was
started.

**Four Landfall loss traces (S18 T4).** At seed 727123, heur's pass four cost 15,556 units and found T5. The gate's
leafless pass four cost 8,040; its sampled price was 22,668 against a 17,556-unit admit line, so it committed pass
three and won T6. Seed 727004: heur admitted pass five and won T6; the gate's leafless pass five alone cost 10,917
against a 10,875-unit admit line and was refused, then it won T7. Seed 727039: `_phx_phd4` refused pass five before
walking it and won T7; heur ran pass five and won T6. Seed 727144: `_phx_phd4` refused pass five, while heur's pass
four found T5 and the gate's pass four found T6. All four reproduce the batch outcomes in single-game traces.

The exact second pass and minimum sample share improved the cache context, but they did not fix the later cold-price
and duplicated-interior cost at an acceptable price. The next candidate must avoid paying both the leafless walk and
the rollout walk of the committing pass, or derive a warm-cost decision without refusing the depths heur actually
enters. A further threshold sweep of `_phx` on the same cold prices is unlikely to fix both quality and cost.

### S20: spend the warm-up quota only on shallow passes (2026-11-16)

`_pmd<N>` limits `_pmi20` to passes at or below N; the default remains unlimited, so all earlier arms are unchanged.
Same seven decks, seeds, 4,000 paired games, 32-worker weighted batch. The heur baseline's completed `.wins` were
identical to S17. All three variants still fail the cost-and-quality bar:

| arm | Landfall delta / CPU | Hydras delta / CPU | other loss |
|---|---:|---:|---|
| `_pmi20_pmd2` | +0.000 / 1.423x | +0.000 / 1.145x | Alchemist +0.010, Melira +0.005 |
| `_pmi20_pmd3` | -0.005 / 1.515x | +0.000 / 1.153x | Alchemist +0.010, Melira +0.005 |
| `_pmi20_pmd3_phx_phd5` | +0.005 / 1.186x | +0.000 / 1.174x | Alchemist +0.010, Melira +0.005 |

The useful warm-up is at depth three: limiting the quota to depth two loses the Landfall gain, while extending it to
depth three retains that gain at most of the original cost. Depth-five pre-refusal still trades the gain away. The
latest screen rules out a simple sample-quota adjustment as the general solution.

### Adaptive rollout share (P18h, 2026-11-16)

The mode now has a screen-only `_pa1<pct>` route. Pass one is already exact under `_pb1`. Its measured ratio of
rollout units to total units tells us whether a leafless traversal is likely to cost more than the leaves it would
avoid. If that ratio is below the threshold, the mode evaluates every leaf from pass two onward, carrying forward
pass one's real cost. This is an exact continuation of the ordinary ladder; it does not pay for a sampled pass and
then fall back. `_paf<pct>` makes the same decision after exact pass two under `_pb2`, for a later route.

The `_pa1101` all-exact control matched the heuristic ladder's **work units and win turns exactly** on 20 paired
games from each of seven decks (`build/scratch/p18f/T5`, 280 games). S22 then compared the prior P18d candidate
`g_pb1_plc_prr_prx2_pfr20` with `_pa193`, `_pa195`, and `_pa197` on the seven P18 train decks, 5,000 games total,
32 weighted workers, same seeds (`build/scratch/p18f/S22`, binary `mtg_adaptive1`). Results vs heur:

| arm | pooled CPU | Hydras | Alchemist | EDF | Snow | Midrange Burn | Landfall | Melira |
|---|---:|---:|---:|---:|---:|---:|---:|
| old | 0.891x | 0.961x / -0.005 | 0.728x / 0 | 0.806x / 0 | 0.994x / 0 | 0.950x / 0 | 0.991x / +0.025 | 0.909x / +0.005 |
| `_pa193` | 0.925x | 0.956x / 0 | 0.815x / 0 | 0.867x / 0 | 0.990x / 0 | 0.931x / 0 | 0.954x / +0.005 | 0.989x / 0 |
| `_pa195` | **0.936x** | 0.963x / 0 | 0.823x / 0 | 0.871x / 0 | 0.994x / 0 | 0.950x / 0 | 0.988x / 0 | 1.002x / 0 |
| `_pa197` | 0.933x | 0.958x / 0 | 0.823x / 0 | 0.875x / 0 | 0.986x / 0 | 0.930x / 0 | 0.991x / 0 | 0.997x / 0 |

Each deck cell is thread-CPU ratio / mean win-turn delta. `_pa195` and `_pa197` have no quality loss on any train
deck. Landfall and Melira's work units are exactly the ordinary ladder's at both thresholds, which removes the
known losing branch but yields no work saving there. The small CPU differences on those two are measurement noise.
`_pa195` retains more sampled work on Hydras and has 0.970x pooled work units (vs heur), so it is the first held-out
candidate. H2 is running 33,600 games on fresh seeds 1,000,000+, across the same 17 decks as H1; the threshold is
fixed before H2's results are read (`build/scratch/p18f/H2`). No adoption decision is justified from S22 alone.

### H2 held-out: adaptive pass-one route is insufficient

H2 completed on the planned 33,600 games. `_pa195` has lower CPU on most decks, but fails the per-deck quality bar:
Alchemist +0.0020 (1 better / 3 worse), EDF +0.0030 (0/3), Hydras +0.0004 (3/6). EDF's CPU is also 1.056x even though
its work units are 0.969x. Landfall and Melira are exact parity in work units and win turns, as intended; the broad
decks have no quality loss (Angels improves by one game). Exact figures: `python3 build/scratch/p18d/s9sum.py
build/scratch/p18f/H2 heur g_pb1_plc_prr_prx2_pfr20_pa195`.

The twelve H2 worse games were replayed one game per job with `MTG_P16_TRACE=1` (`build/scratch/p18f/T6`). The
causes differ: Alchemist 480/563 and EDF 25 search deeper than heur on T1, while Hydras 1338/2570/7557 stop
shallower than heur after a cold sampled price. A deeper pass is not automatically better here: it changes the
line chosen under the finite budget and can lose a turn. T7 replayed those same seeds with an exact pass two:
`_pb2` recovers all three EDF losses, Alchemist 818, and Hydras 7953, but five Hydras losses remain. Adding
`_phx` recovers Alchemist 480/563 and Hydras 7487 but makes Hydras 7953 worse again. The next test makes pass three
exact as well (`_pb3`, screen-only), then samples deeper passes; T8 runs those twelve diagnostic games first.

T8 (`_pb3`) recovers ten of the twelve H2 losses without `_phx`; adding `_phx` reintroduces Hydras 7953. The two
remaining losses are Hydras 2570 and 7487. T9's decision traces show different causes. In 2570, depth four's sampled
price is 22,988 +/- 3,996 against a 14,931 admit line: its two-standard-error lower bound misses by only 65 units,
so a slightly wider interval may force more samples and admit. In 7487, depth five's leafless walk costs 8,378 with
no leaf reached, then the rollout pass at the same depth consumes additional work and aborts to the previous line;
the ordinary ladder runs that pass once and commits it. `_pfd` runs the final depth directly on rollouts, without the
extra leafless walk. T10 tests `_pb3_pfd` with z=2,3,4 on the twelve losing seeds before a broader screen.

T10: `_pfd` alone does not recover 7487; the first-wave final-depth pass completes, but the second compound-wave
search commits only depth four. With the ordinary ladder it reaches depth five and verifies the T5 line. The first
wave's skipped depth-four rollouts are useful cache warming for that second wave. Increasing z from 2 to 4 still
refuses Hydras 2570 after all 64 available sample leaves: its point price is 21,476 against a 14,931 line, but
the 4-SE interval includes the line. `_pua` leans admit at the sample ceiling only when that interval overlaps;
T13 recovers 2570 at z=4. With `_pb3_pfd_pua_pz40`, eleven of twelve H2 worse seeds now match heur; 7487 remains.

S23 (the seven-deck train screen, same 4,000 games as S22) is a cost failure despite zero net quality delta on all
seven. Unconditional exact pass three plus `_pfd` costs 1.027x CPU on Alchemist, 1.004x on EDF and 0.969x on Hydras;
adding `_pua_pz40` costs 1.030x, 0.991x and 1.057x respectively. The exact warm-up needs to be conditional.
`_pb3r20` runs pass three exactly only when the exact c2/c1 cost ratio is at most 2.0; otherwise it samples at that
depth. This is measured within the decision (not a deck label): Hydras' shallow-refusal losses have ratios 0.7-1.8,
while Alchemist's over-deep losses have ratios about 4. `_phx` prevents those over-deep continuations using the
priced start gate. T14 and S24 test that combination, with `_pfd` retained.

S24: conditional pass-three at c2/c1 <= 2.0 saves 4-5% CPU on Hydras and 3% on EDF, but Alchemist seed 15 still
wins one turn later (Alchemist +0.010 on 100 games). `_phx` adds a Hydras loss without fixing seed 15. T15 shows
why seed 15 is subtle: both arms search depth three and choose the same T1 plan, but sampling before the exact
fallback changes the rollout cache. On T2, the sampled arm's exact depth-two pass costs 1,178 vs heur's 251, so
its cost-ratio gate refuses depth three; heur's depth-three pass finds the T5 line. This is an across-turn cache
effect, not simply a T1 depth mismatch.

S25 widened the exact-pass-three band to c2/c1 <= 3.0 or 3.5. Both restore parity on all seven train decks, and
Hydras remains below heur CPU (0.971x at 3.0); Alchemist rises to 1.032x CPU and 1.039x work units. The extra
Alchemist work is concentrated in a few chunks, some with high c2/c1 where an exact terminal pass should be
cheaper than a leafless walk plus sampling. `_pag<10R>` continues the ordinary ladder after exact pass two when
c2/c1 >= R, before any sampled pass. S26 screens R=3.0/3.5/4.0 with the exact-pass-three band at 3.0.

S26's high-growth exact route is a cost failure: Alchemist stays 1.046-1.052x CPU and 1.040-1.041x work units at
R=3.0/3.5/4.0, with no quality loss. The excess therefore comes after an exact pass three in the lower-growth
decisions. T17 traced the 100 Alchemist train games with `_pb3r30_pfd`: the largest extra decision is seed 720044
T1, 44,043 units vs heur's 3,998. Heur's H4 costs 752 with no rollout leaf and warms H5 to 135; the sampled mode's
W4 also costs 752 but uses a separate leafless line cache, so its H5 runs cold for 40,180 units. This repeats in
several Alchemist seeds. `_pzn` handles exactly this zero-leaf case: after W4 offers no sample, it runs H4 exactly
and lets the real cost gate H5. In T18, seed 720044 falls to 4,031 units: W4 752, H4 only 33 with the warmed
interior, H5 135. Same win turn as heur.

S27: `_pb3r30_pfd_pzn` cuts Alchemist to 1.015x work units / 1.022x CPU from 1.039x / 1.031x; EDF is 0.994x
units / 0.968x CPU, Hydras 0.963x / 0.950x. Quality is level on all seven train decks (Midrange Burn improves
one game). At `_pb3r20`, Alchemist CPU is 0.998x but seed 15 loses a turn again. The remaining Alchemist overhead
usually has an exact depth-three rollout predicting a win only two turns beyond its horizon. `_pwn<N>` routes such
near wins to exact depth four BEFORE the leafless walk; S28 tests N=1/2/3 with `_pb3r30_pfd_pzn`.

S28: near-win exact continuation keeps all seven decks quality-neutral (Midrange Burn one better) and cuts
Alchemist's 1.015x work units to 1.003x at `_pwn3`, but Hydras' saving falls from 0.963x to 0.996x units. At
`_pwn1` Hydras remains 0.961x but Alchemist is 1.015x. The remaining Alchemist cost includes over-deep pass five
when exact pass three predicted a win far beyond its horizon (seed 720032: p3 predicts T9, p18 prices pass four
5,743 units, then pays 19,656 for H5; heur commits H4 at 6,923). `_phx` can refuse that depth-five pass before
walking it. Its earlier Hydras loss at seed 710163 was a FALSE depth-four pre-refusal on a cold pass-three price
(15,920 vs 12,166 line, while heur's actual pass-three cost admits depth four). `_phw<N>` retains `_phx`'s free
refusal after an exact pass three only when that pass predicted a win more than N turns beyond its horizon; a
near-win decision skips the risky pre-refusal at depth five. S29 screens `_phx_phw3` with `_pwn1/2/3`.

S29 is quality-neutral on all seven train decks (Midrange Burn improves one game). At `_pwn2`, CPU/work units vs
heur are: Hydras 0.950/0.958x, Alchemist 1.014/1.015x, EDF 0.985/0.994x, Snow 1.002/0.997x, Midrange Burn
0.999/0.992x; Landfall/Melira remain exact parity in units. `_pwn3` moves Alchemist to 1.008/1.004x but gives back
most Hydras savings (0.991/0.994x). These are near parity on the troublesome decks, so no adoption is justified
from the 100-200-game train counts. H3 runs both `_pwn2` and `_pwn3` against one heur baseline on fresh seeds
1,400,000+ across 17 decks (50,400 games total); the decision will use per-deck quality and paired CPU, not the
individual diagnostic seed outcomes.

H3 rejects both arms despite broad CPU savings. On 17 decks (8,000 Hydras; 1,000 each Alchemist, EDF, Snow, and
Midrange Burn; 400 each for the other twelve), `_pwn2` loses 10 and wins 2 Hydras games, plus one each on Alchemist,
EDF, and Giants. `_pwn3` loses five Hydras games and one each on Alchemist, EDF, and Giants, with no compensating
wins. The common `_pwn3` failures are Alchemist 614, EDF 722, Giants 310, Hydras 7609/2160/3927/2988/4229.
T20 replays these games with per-decision traces. The first four types include false refusal of depth four or five
after exact pass three; others run the same first-turn depth as heur but the sampled pass four changes the warmed
cache and makes the next turn shallow. `_pwn4` (route an exact pass-three win within four turns beyond its horizon
to the ordinary ladder from pass four) restores the heuristic win turn on all eight T21 replays; `_pwn5` and
`_pwn6` do too. Some repaired Hydras games cost exactly heur's units, as expected for a route that chooses exact
rollouts there. H3b now measures `_pwn4` and `_pwn5` against H3's heuristic baseline on the full H3 seed set;
targeted repair alone does not establish the generic cost or quality bar.

H3b completed on those same fresh 17-deck seeds. `_pwn4` still loses one Alchemist and one Hydras game (no wins);
`_pwn5` is win-turn identical in all 16,800 paired games. Its pooled work units are 0.99688x and thread CPU is
0.99647x heuristic. The gain is modest: Angels 0.954x units, Critter 0.967x, Dragons 0.985x, Hydras 0.993x,
while Alchemist and EDF cost 1.006x and 1.003x units. Several decks have unchanged units. The CPU comparison
uses separate pooled batches on the same binary and seeds, so a paired fresh run on the integrated engine is
needed before adoption. The `_pwn5` quality result on H3 is encouraging but one seed set is insufficient to
show that the mode generically beats the ladder.

H4 is the fresh confirmation on the integrated 2026-11-16 engine: 16,800 paired games on seeds 2,000,000+,
depth five / 20 ms. `_pwn5` still saves units overall but fails quality: Alchemist is +0.0020 turns (one faster,
three slower among 1,000), Hydras +0.0001 (one slower among 8,000). Other 15 decks are quality-neutral. T22
traces show Alchemist 846 and 465 had exact pass-three no-wins: the sampled fourth pass either pre-refused a
depth heuristic reached or changed next-turn cache warming. Alchemist 82 and Hydras 5963 used sampled pass three
because the exact c2/c1 growth sat just above 3.0, then deviated. `_pne` is a new screen-only route: after an
exact pass-three no-win, continue the ordinary ladder; test it with `_pb3r35` and `_pb3r40` before another
held-out run. The earlier H3 quality parity therefore did not justify adoption.

H4b: `_pb3r35_pne` (all other suffixes as H4) is quality-neutral on 16 decks and improves one Alchemist game on
H4's seeds; `_pb3r40_pne` is quality-identical on all 17. R35 uses 0.99770x pooled units; R40 uses 0.99861x.
Their CPU ratios against H4's heuristic batch were 1.012x/1.014x, but the arms ran in a separate batch and even
decks with exactly identical work show +1-2% CPU, so that comparison does not isolate the mode.

H5 is the fresh PAIRED 17-deck check of R35+pne on seeds 2,400,000+: 16 decks are quality-neutral, but Midrange
Burn loses one game of 1,000 (gi137, T5 -> T6). Pooled work units 0.99924x, CPU 0.99854x, too narrow and not a
quality pass. T24 shows its second-main T1 c2/c1 = 2002/571 = 3.506, just above the 3.5 exact-pass-three band;
the sampled pass-three price pre-refuses depth four, while heur runs H4 and finds the T5 line. R36 fixes gi137
in T25. H5b tests R36 on every H5 seed before an independent fresh paired confirmation.

H5b R36 repairs Midrange Burn and matches heuristic wins on all 17 decks on H5's seeds. It uses 0.99937x pooled
work units; its separate-batch CPU ratio is confounded by run conditions. H6 is the independent paired check on
seeds 2,800,000+ (16,800 games per full arm). R36 matches heuristic wins on every deck, uses 0.99885x units but
1.00100x thread CPU overall, so it is NOT a generic CPU win. The faster R30+pwn3 shape, tested on five decks in
the same batch, matches all 2,000 game wins and uses 0.974x units / 0.964x thread CPU. H7 therefore expands those
five to 4,000 fresh games each on seeds 3,200,000+.

H7: Angels 0.955x CPU / 0.962x units, Critter 0.945x / 0.950x, Minotaur 0.977x / 0.976x, and Vampires
0.995x / 0.997x, all with 0 changed win turns in 4,000 paired games each. Dragons 0.977x / 0.982x but one
slower game, so it is excluded. The three strong quality-neutral decks (Angels, Critter, Minotaur) pool to
0.963x CPU / 0.967x units. `sampled_gate=2` now encodes exactly this measured fast shape; `sampled_gate=1`
retains the raw research prototype. Before adoption, compare production mode 2 against the diagnostic arm for
byte identity and against each deck's ACTUAL live setting (several already have single-pass ladders, value leaves,
or a different depth/budget). A win over a bare heuristic d5/b20 arm does not establish a win over those settings.

H8 identity: an arm-name parser overlap made diagnostic suffix `_pzn` also match `_pz`, implicitly setting the
uncertainty multiplier to zero. The parser now requires a numeric character after numeric tags, and mode 2 sets
`z=0` explicitly. With an explicit `_pz0` in the diagnostic arm, mode 2 and the diagnostic shape match all 300
game wins, per-game digests, job digests, and charged units. The old H7 diagnostic arm also matches the corrected
diagnostic arm on those same 300 games. The identity outputs are in `build/scratch/p18f/H8id2` (ignored scratch).

H9 is a fresh paired confirmation of production mode 2 against the bare heuristic ladder, depth five / 20 ms,
1,000 games per deck on seeds 3,400,000+. Mode 2 is win-turn identical in all 3,000 games. Thread CPU / units:
Angels 0.969x / 0.971x, Critter 0.933x / 0.935x, Minotaur 0.965x / 0.968x. Together with H7, that is 5,000
held-out paired games per deck with no changed win turns and a repeatable CPU saving of about 3-7%.

H10 compares mode 2 at each deck's CURRENT depth/budget against its actual shipped play setting, 1,000 fresh
paired games per deck on seeds 3,500,000+. Mode 2 is slower: CPU 2.457x Angels, 2.812x Critter, 2.104x
Minotaur; Minotaur also loses two games. This does not merit changing the shipped sidecars. The general
17-deck conservative shape R36 is CPU-neutral (H6); the three-deck result establishes a useful opt-in mode,
not a fleet-wide replacement for the heuristic ladder or for already tuned play settings.

Integration verification on the 2026-11-16 shared parent `e4f2b7c71`: all 3,181 unit tests pass. The smoke
batch ran 211 jobs / 67,265 games and reported 17 ground-truth mismatches. Those 17 jobs, plus an unchanged
Hinata canary, were rerun using the parent commit's binary with the exact same deck/profile inputs; every
per-game log, job digest, and charged-unit count matched the mode-2 binary. The smoke mismatches are already
present on the shared parent (which carries Sunday play changes pending ground-truth review). No smoke
ground truth was rebaselined for P18.

After rebasing onto remote `de7aa49fc` (123 intervening commits), H11 reran the production mode against bare
heuristic on 17 decks, 400 fresh paired games per deck and 1,000 each for Angels, Critter, and Minotaur, seeds
3,600,000+. The three confirmed decks again matched every win turn: CPU / units Angels 0.954x / 0.958x,
Critter 0.939x / 0.950x, Minotaur 0.979x / 0.982x. Giants was 0.903x / 0.927x with 0/0 changed games in
400, but mode 2 at Giants' shipped depth six / 20 ms cost 1.394x CPU and 1.294x units against its live
single-pass setting in H12's 1,000 fresh paired games (one faster game). The wider H11 mode lost one game each
on Alchemist, Dragons, and Midrange Burn. No deck sidecar is enabled for mode 2; the result remains an opt-in
alternative for a deck whose live shape is still the bare heuristic ladder.

The rebased branch passed all 3,365 unit tests / 3,614,308 assertions. Its smoke run completed 211 jobs /
67,265 games and reported 55 ground-truth mismatches. An exact `de7aa49fc` parent binary replayed all 55
changed smoke jobs plus two unchanged canaries using the integrated branch's deck and profile inputs. With the
same working directory, all 57 jobs matched the integrated binary byte-for-byte in per-game logs, job digests,
and charged units. (Three deck families needed their replay launched from the same directory; otherwise
relative-path inputs differed even at greedy depth zero.) Thus P18 adds no smoke drift; the 55 mismatches are
already present on its parent with those deck inputs. The branch does not rebaseline ground truth.


## Full repository deck screen on the 2026-11-16 shared engine

At shared parent `8cd0bbf23` plus the P18 option, F2 compared `sampled_gate=2` with the bare heuristic
ladder at depth 5 / 20 ms / max turn 8. It used 400 paired games per top-level deck, seeds
5,000,000 + 10,000 × deck index (44,800 simulated games total), one pooled 32-worker batch. Both arms
used the same deck and mulligan profile; the diagnostic directories omitted each deck's existing
`.value.json` on both arms. Therefore these numbers measure the option against the bare ladder,
not against each deck's shipped value leaf or play shape. CPU ratio is charged thread CPU; work ratio
is deterministic charged units. A ratio below 1 is faster. Earlier/later is the number of games whose
win turn changed. An unwon game scores turn 9 in the delta.

| Deck | Paired games | CPU ratio | Work ratio | Earlier / later | Mean turn delta |
|---|---:|---:|---:|---:|---:|
| Accomplished Alchemist | 400 | 0.993x | 0.998x | 0 / 0 | +0.0000 |
| Angels | 400 | 0.932x | 0.951x | 0 / 0 | +0.0000 |
| Anti-Lifegain | 400 | 1.011x | 1.050x | 0 / 0 | +0.0000 |
| Auras | 400 | 1.007x | 1.018x | 0 / 0 | +0.0000 |
| Boars | 400 | 0.980x | 0.984x | 0 / 0 | +0.0000 |
| Breaching Dragonstorm | 400 | 0.947x | 1.000x | 0 / 0 | +0.0000 |
| Bruna | 400 | 0.994x | 0.997x | 0 / 0 | +0.0000 |
| Burn | 400 | 0.985x | 1.006x | 0 / 0 | +0.0000 |
| Colossus Hammer | 400 | 0.955x | 0.969x | 0 / 0 | +0.0000 |
| Creature Giving | 400 | 0.984x | 0.988x | 0 / 0 | +0.0000 |
| Critter Lifegain | 400 | 0.917x | 0.932x | 0 / 0 | +0.0000 |
| Dragons | 400 | 0.990x | 0.985x | 0 / 0 | +0.0000 |
| Dragonstorm | 400 | 0.937x | 0.937x | 0 / 1 | +0.0025 |
| Eldrazi Displacer Flicker | 400 | 1.001x | 1.010x | 0 / 0 | +0.0000 |
| Extra Turns | 400 | 0.999x | 1.000x | 0 / 0 | +0.0000 |
| Five Colour | 400 | 0.981x | 1.005x | 0 / 0 | +0.0000 |
| Fluctuator | 400 | 0.991x | 1.006x | 0 / 0 | +0.0000 |
| Fungus | 400 | 1.004x | 1.030x | 0 / 0 | +0.0000 |
| Galadriel | 400 | 0.990x | 0.985x | 0 / 0 | +0.0000 |
| Giants | 400 | 0.963x | 0.960x | 0 / 0 | +0.0000 |
| Goblins | 400 | 0.990x | 0.996x | 0 / 0 | +0.0000 |
| Gods | 400 | 0.988x | 0.956x | 1 / 6 | +0.0125 |
| Hinata | 400 | 0.989x | 1.000x | 0 / 0 | +0.0000 |
| Hydras | 400 | 0.988x | 0.999x | 0 / 0 | +0.0000 |
| Jumbo Cactuar | 400 | 1.002x | 0.986x | 0 / 0 | +0.0000 |
| Kithkin | 400 | 0.981x | 0.993x | 0 / 1 | +0.0025 |
| Kitty Equipment | 400 | 0.991x | 0.989x | 0 / 0 | +0.0000 |
| Knights | 400 | 0.946x | 0.977x | 0 / 0 | +0.0000 |
| Landfall | 400 | 1.002x | 1.000x | 0 / 0 | +0.0000 |
| Landfall Elementals | 400 | 0.992x | 0.996x | 0 / 0 | +0.0000 |
| Lifegain Heliod Combo | 400 | 0.964x | 0.977x | 0 / 0 | +0.0000 |
| Martyr Lifegain | 400 | 0.995x | 0.996x | 0 / 0 | +0.0000 |
| Melira Pod | 400 | 1.001x | 1.000x | 0 / 0 | +0.0000 |
| Merfolk | 400 | 0.990x | 0.999x | 0 / 0 | +0.0000 |
| Merfolk Turns | 400 | 1.011x | 0.995x | 0 / 1 | +0.0025 |
| Midrange Burn | 400 | 0.984x | 0.992x | 0 / 0 | +0.0000 |
| Mill | 400 | 0.983x | 0.987x | 0 / 0 | +0.0000 |
| Minotaur | 400 | 0.969x | 0.981x | 0 / 0 | +0.0000 |
| Mirrorwing Dragon | 400 | 0.999x | 1.008x | 0 / 0 | +0.0000 |
| Modern RW Burn | 400 | 0.990x | 1.003x | 0 / 0 | +0.0000 |
| Morph | 400 | 0.991x | 1.006x | 0 / 0 | +0.0000 |
| NivMizzetCombo | 400 | 0.999x | 0.999x | 0 / 0 | +0.0000 |
| Pirates | 400 | 0.981x | 0.988x | 0 / 0 | +0.0000 |
| Prevent Damage | 400 | 0.997x | 0.997x | 0 / 0 | +0.0000 |
| Red Elementals | 400 | 0.997x | 1.017x | 0 / 0 | +0.0000 |
| Selesnya Lifegain | 400 | 0.983x | 0.983x | 0 / 0 | +0.0000 |
| Shadowborn Apostle | 400 | 0.981x | 0.983x | 0 / 0 | +0.0000 |
| Slivers | 400 | 0.980x | 0.985x | 0 / 0 | +0.0000 |
| Snow | 400 | 0.999x | 1.001x | 0 / 0 | +0.0000 |
| Soldiers | 400 | 0.971x | 0.986x | 0 / 0 | +0.0000 |
| Spirits | 400 | 0.992x | 0.992x | 0 / 0 | +0.0000 |
| Stompy Surprise | 400 | 0.980x | 1.001x | 0 / 0 | +0.0000 |
| Treasure Hunt | 400 | 0.994x | 0.996x | 0 / 0 | +0.0000 |
| Unpredictable Cyclone | 400 | 0.998x | 0.998x | 0 / 0 | +0.0000 |
| Vampires | 400 | 0.989x | 1.001x | 0 / 0 | +0.0000 |
| White Knights | 400 | 0.947x | 0.974x | 0 / 0 | +0.0000 |

The default-on mode is not suitable as a fleet replacement: Dragonstorm 1 later; Gods 1 earlier,
6 later; Kithkin 1 later; Merfolk Turns 1 later. All other decks had identical win turns in F2.
Only nine top-level decks lack a value sidecar: Boars, Hydras, Martyr Lifegain, Merfolk Turns, Mill,
Morph, Red Elementals, Shadowborn Apostle, and Spirits. The best deterministic work savings among
these were Boars 1.6% and Mill 1.3%, too small to adopt from 400 games alone. The 100-game F1
screen at seeds 4,000,000 + 10,000 × deck index was used only to identify candidates.

### Conservative repair screen and Minotaur cost

F3 tested a stricter diagnostic gate (`_pb3r36_pne_pwn5_paf97`) at the same depth 5 / 20 ms / turn 8
settings on all 56 decks, using 100 fresh paired games per deck, seeds 6,000,000 + 10,000 × deck
index. It changed no win turns, including Gods and Kithkin, but generally erased the useful work
savings: Angels used 0.989x bare-ladder work, Critter Lifegain 0.995x, Gods 0.999x, and Mill 1.001x.
Minotaur used 0.957x. This remains a diagnostic variant, not a production default or proof of
win-turn equivalence beyond the sampled games.

A separate 100-game Minotaur trace of the production gate found 181 decisions. Only 26 reached
leafless walks, 43 reached priced walks, and 46 sampled rollouts cost 1,737 charged units out of
705,688 total (0.25%). Leafless walks cost 17,109 units (2.4%). Bare ladder cost 718,168 units
on those games. Reducing the rollout count alone cannot materially improve Minotaur runtime;
few decisions enter the priced part of the gate at all.

### Comparison with configured play shapes

F4 used 400 fresh paired games per deck, seeds 7,000,000 + 10,000 × deck index, at depth 5 /
20 ms / turn 8. The baseline loaded the deck's actual sidecar, if present; mode 2 and the
conservative diagnostic variant replaced that sidecar. These ratios therefore answer the
adoption question for the selected decks. CPU and deterministic work ratios are relative to
the configured baseline; lower is better.

| Deck | Mode 2 CPU | Mode 2 work | Mode 2 earlier / later | Conservative work |
|---|---:|---:|---:|---:|
| Boars | 0.973x | 0.983x | 0 / 0 | 1.000x |
| Mill | 0.984x | 0.990x | 0 / 0 | 1.000x |
| White Knights | 2.812x | 3.741x | 0 / 0 | 3.847x |
| Colossus Hammer | 1.339x | 1.450x | 0 / 1 | 1.497x |
| Galadriel | 1.530x | 1.518x | 0 / 0 | 1.547x |
| Pirates | 1.781x | 1.908x | 0 / 0 | 1.924x |
| Hydras | 0.984x | 0.986x | 0 / 0 | 1.003x |

The sidecar-backed decks do not benefit from replacing their configured play shape. Boars,
Mill, and Hydras still run the bare ladder, and mode 2 saves only 1–2% deterministic work.
A larger fresh paired sample is needed before considering adoption on the bare-ladder decks.
