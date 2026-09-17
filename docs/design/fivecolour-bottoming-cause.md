# FiveColour's bottoming table loses the confounded A/B — state of the investigation

**Status 2026-09-17: EXPLAINED — ONE cause, and it is not the sampling budget.** The bottoming argmin
selects for cells the generator's own rollout model flatters (an optimizer's curse against the
simulator, ~0.08t per disagreement game) — a bias no amount of re-generation can remove, because a
fresh re-measurement inherits it. Against a genuinely blind bottomer the table is 0.064t/game FASTER.
A second, real but minor defect (argmin-only sub-refinement, ~0.01t) is fixed in the generator.
See §7, which supersedes the 2026-09-15 status below.

> **CORRECTION 2026-09-17.** This status previously claimed a *second* cause: that
> `MTG_CONFOUND_BOTTOM` fails to blind the lookahead, so the A/B the table loses still rewards a peek
> worth 6× its blind value. **That is wrong and is now measured to be wrong** (§7i). Reshuffling
> *before* the decision as well as after leaves the veto's value unchanged, so mode 1 already removes
> the entire peek. The 6× gap is the same model bias as the one cause above, seen on the lookahead's
> selected set instead of the table's. Consequence: **the confounded A/B is a fair blind-vs-blind test
> and remains this repo's valid adoption gate for bottoming** — every deck that passed it still passes.
> The cost of the correction is that FiveColour's loss is fair too, rather than an artefact of the test.

**Status 2026-09-15 (superseded): the cause is UNEXPLAINED. Six hypotheses tested, five refuted, one in flight.**
This document is the running record so nobody re-tests a dead one. It supersedes the "still live"
item in [keepgen-bottoming-HANDOFF.md](keepgen-bottoming-HANDOFF.md) §5.

The adoption question is **already settled and is independent of the cause** — see §4. This is an
explanation hunt, not a blocker.

---

## 1. The fact to be explained

FiveColour's 20-day generation (frozen `2f7822a2`, 55.66 M rollouts) produced:

```
keep      -0.125000t   16/16 seeds     decisively GOOD
bottoming +0.018375t    0/16 seeds     loses to a BLINDED lookahead
```

Any correct explanation must survive that **asymmetry on one deck with one set of sub-tables**.
Most candidate mechanisms (noise, drift, pool quality) should damage both halves; they don't.

There is a structural reason keep is protected and bottoming is not, and it is worth holding onto:
keep is a coarse threshold, `V(hand)` vs `Dopt`, additionally **clipped** by
`min(KeepVal, Dopt[m+1])` — the same clipping that made the keep-side argmin curse provably inert
([keep-argmin-winners-curse.md](keep-argmin-winners-curse.md) §7). Bottoming is a **fine argmin among
near-tie subcompositions of the same hand**. So bottoming is the exposed half by construction, and
any explanation only needs to corrupt *rank order inside a narrow band*.

## 2. Why "it's just noise" is not available

`MTG_CONFOUND_BOTTOM` reshuffles the library after the bottoming decision **including the bottomed
cards** (`AIEngine.cpp:682`), and the comment states the intent: *"mirrors how the exhaustive V
labels were built (fresh continuations)"*.

Count it through: deck 60, draw 7, library 53; bottom `m`, hand `7-m`, library `53+m`; confound then
shuffles all of it. Generation's `V(comp, H)` for `H = 7-m` sits on a library of `60-H = 53+m`. The
distributions **match**. The table is therefore tested in its own training distribution, against a
lookahead whose clairvoyance has been nullified.

In that setting a table averaging R=14–30 rollouts per cell should beat a lookahead taking **one**
rollout per candidate. The user made this objection from the start — *"testing N versions is less
noisy than 1"*, *"this has never failed in the past"* — and it is correct. **So the defect is BIAS,
not variance.** Every hypothesis below is a candidate bias.

## 3. Hypotheses tested

### 3a. Starved sub-tables — REFUTED (prior work)
The Dragons/Mirrorwing cause (`confounded-bottoming-gate-failures.md`). FiveColour's sub-cells are
trimodal 2/18/30, not starved at 1; its artifact check reports `min_rollouts=2 sub_target=2` and
passes legitimately. Goblins is explicitly listed there as a PASS at the same adaptive floor of 2.

### 3b. Winner's curse in the bottoming argmin — REFUTED by control
Mine. Killed by **Melira Pod**: same `fast`/R30 recipe, worse on every metric the theory used (45.4 %
of cell-sides at floor R=2 vs 41.2 %, mean R 13.58 vs 13.84, m=1 no-refined fallback 8.2 % vs 6.3 %)
— and it **passes** at −0.091t, 16/16. The supporting arithmetic only appeared to fit because the
lookahead's regret `L` was solved for from the very A/B being explained.
Full record: [keepgen-bottoming-winners-curse.md](keepgen-bottoming-winners-curse.md).

### 3c. Adaptive refinement makes the candidate pool adversarial — REFUTED by control
Floor cells are measurably worse *and* noisier (dV +1.19…+1.63, corr(cnt,V) −0.67…−0.72 on all four
adaptive decks). Real structure, but it does not predict failure: **Goblins has the worst pool of any
deck (floor 58.2 %, dV +1.628) and passes; Dragons had no floor cells at all and failed.**

### 3d. Play drift — REFUTED by direct causal test
The labels were fit at `2f7822a2`; every A/B ran ~230 commits later. Play *has* moved
(digest `b79a1414` → `8fca5f52`, and the gen-time value sidecar reproduces the new digest, so the
engine moved, not the sidecar). But rebuilding `2f7822a2` in a worktree and re-running the confounded
A/B **there**, with the original profile, on the same 16 bottom-block seeds:

```
on 2f7822a2 (the engine it was FIT TO):  +0.0165t   (2/16)
on HEAD     (recorded):                  +0.018375t (0/16)
```

Indistinguishable. **The table was never good; it is not stale.** The Mirrorwing precedent (shipped
with a known-stale digest, still passed at −0.0918) held exactly as written.

### 3e. Fetchland bucket abstraction — REFUTED by control + domain knowledge
FiveColour's discovery merges exactly one bucket: five fetchlands reaching five different colour
pairs (`Misty Rainforest`, `Scalding Tarn`, `Verdant Catacombs`, `Windswept Heath`,
`Wooded Foothills`), 26 of 27 buckets being singletons. `BottomCards` resolves within-bucket identity
by **hand order** — `AIEngine.cpp`: *"Members of a bucket are equivalent by construction... picking
the first is deterministic"* — and `EquivalenceDiscovery.cpp` admits the merge is *"an APPROXIMATION,
not an identity... a deck whose fetches reach genuinely different colour sets loses that
distinction."* It looked compelling, and it is wrong:

* **Anti-Lifegain merges FOUR fetchlands** (Wooded Foothills, Windswept Heath, Marsh Flats,
  Bloodstained Mire — R/G, W/G, W/B, B/R) **and passes.** Creature Giving merges two. A heterogeneous
  fetchland merge is not remotely unique to FiveColour.
* USER, on the game rather than the code: *"Humans seeing them literally do not worry about which
  fetchland they have... It's super rare to be messed up by getting the 'wrong' fetchland."*

Note the earlier partial refutation in the HANDOFF (§5 *Eliminated 3*) tested only whether the *gate
deferred more* on fetchland-heavy hands — i.e. the table's **confidence**, not its **correctness** —
so it did not close this. The Anti-Lifegain control does.

### 3f. Labeller-depth bias — IN FLIGHT (`logs/fc_repolicy/depthtest.sh`)
FiveColour's own `value.json` says it outright: *"Mulligan GENERATION runs **d2/b1**, not the shipped
**d6/b20**."* The V labels describe how each hand plays **for a depth-2 player**; the A/B plays at
depth 6. Confounding nullifies the lookahead's library peek but **not its depth fidelity** — it rolls
out at real play settings. Two estimators of *different quantities*, which is the one circumstance
where "N beats 1" legitimately fails.

**The durable finding here, independent of the outcome:** that labeller setting was approved on a
200-hand battery reporting rho 0.9891 and *"100 % pairwise agreement"* — **on the KEEP decision**. Per
§1, keep is exactly the half that is robust to a monotone-ish distortion. **The bottoming axis was
never tested.** Whatever this run says, labeller-depth approval should include a bottoming-rank check.

**Not claimed: that the depth gap is deck-specific.** It is not — burn 5, Anti-Lifegain / Auras /
Creature Giving / Dragonstorm / Hinata2 4, FiveColour 4 — and Anti-Lifegain passes. If the test is
positive, the amplifier (how much a deck's hand *ranking* moves between d2 and d6) is a **separate
question that must be measured**, not assumed. That assumption is the exact error made in 3b and 3e.

The run carries a **self-refutation arm**: `look_play` vs `look_label` measures whether
bottoming-eval depth buys the lookahead anything at all on this deck. **If that is flat (<0.005t) the
instrument has no resolution and the hypothesis is unsupported regardless of the other comparison.**
Read it first.

## 4. The adoption question is settled and does not depend on any of this

**ADOPTED 2026-09-15 (`53e0ff57`, pushed).** The shrunk table is installed at
`decks/FiveColour/FiveColour.keepmodel.exhaustive.profile.json(.gz)`; the generated (`plain`) table is
kept beside it as `...profile.DISABLED.json`. GT rebaseline for the three tiers is **still owed** —
it runs after the labeller-depth diagnostic frees the box (`logs/fc_repolicy/adopt_finish.sh`), on the
binary rebased onto `origin/909e781d`, which brought 63 changed `src` files.


Per [no-lookahead-bottoming.md](no-lookahead-bottoming.md) §2 the confounded A/B is a **diagnostic,
not a gate**: neither "ship bottoming off" nor "defer to lookahead" is available, so losing to a
lookahead we will not ship cannot veto adoption. Measured directly — 3 arms, one manifest, 32 fresh
seeds × 1000 games, `logs/fc_repolicy/decide.sh`:

```
       arm    avg win turn
    noprof          4.8760     no sidecar + real lookahead bottoming
     plain          4.8506     generated table (byte-identical rebuild)
    shrink          4.8459     shrunk table

Q1  shrink vs noprof   -0.0301t   31/32 seeds   mean/se  -8.70   -> SHIP
Q2  shrink vs plain    -0.0047t   26/32 seeds   mean/se  -5.61   -> ship SHRINK
```

**Q2 independently replicates Melira's −0.0044t** on a different deck and different seeds; `D_opt` is
identical between those two arms, so it isolates the bottoming targets.

**A prediction that failed, recorded because it matters methodologically.** Composing the two
separately-measured halves (keep −0.125t + bottoming) predicted ≈ −0.111t for Q1. The measured
combined effect is **−0.0301t, ~4× smaller**. Back out the arithmetic and the table's bottoming is
losing ≈ +0.095t to the *real, peeking* lookahead — consistent with the HANDOFF's unconfounded
+0.061t and far worse than the confounded +0.0165t, as it should be, since blinding is what weakens
the lookahead. **Do not compose halves on this deck; measure the combination.**

So FiveColour is a deck whose keep table is excellent and whose bottoming table is genuinely poor,
shipping net-positive *despite* that rather than because everything works.

Incidental but real: the `noprof` arm took ~5.5 h against ~1.5 h per table arm, because lookahead
bottoming is ~90 % of this deck's runtime. Shipping the profile is roughly a **3× wall-clock win**
independent of win-turn, and this deck is in the regression suite.

## 5. Ideas NOT yet tested (for whoever picks this up)

* **Objective mismatch.** The table minimises expected win turn of the kept hand. The lookahead
  rolls out a full game. If loss-penalisation or variance is handled differently between `V` and the
  A/B's scoring, the argmin could be optimising a subtly different target.
* **Cross-pd conditioning** (HANDOFF §7 tool `keepraw_crosspd_prior.py`): rho 0.885 at size 6 — each
  pd is strong evidence about the other and is currently unused. Predicted −39 % bottoming regret.
  Not implemented in C++.
* **Direct argmin audit.** Sample real bottoming decisions; re-evaluate every candidate with many
  fresh rollouts **at play depth**; compare the table's argmin to that ground truth. This measures
  the bias directly instead of inferring it, and would have settled 3b/3f without either A/B.
  ~15k rollouts ≈ 2 h on 12 cores.

## 6. Method notes earned the hard way

* **Find the control before building the fix.** 3b, 3e both died to a deck sitting in the repo that
  cost minutes to check. Twice.
* **A model fitted to one number using a parameter solved for from that number has explained
  nothing.** Ask whether the account could have come out differently.
* **Refuting a corollary is not refuting the hypothesis** (3e's confidence test vs correctness).
* **Test a staleness claim against the old binary** when that is cheap, rather than reasoning from
  fingerprints (3d's screen would have supported the wrong conclusion; the causal test settled it).
* **Build the kill-switch into the experiment** — a calibration arm that can declare the instrument
  blind (3f).

---

## 7. 2026-09-16 — the direct audit, and the cause

**Status: EXPLAINED (mechanism), fix built, FiveColour re-refinement in progress.** §3b/§3c below were
closed by "refuted by control"; that inference was wrong (see 7e). This section supersedes §5's
"ideas not yet tested" and the status line at the top.

### 7a. Instrument

The §5 *direct argmin audit*, built on real decisions rather than sampled hands:

1. **Three arms on one confounded seed** (1004004, 1000 games, `MTG_CONFOUND_BOTTOM=1`, per-game
   JSON logs, `logs/fc_audit/arms.sh`): the shrunk table (`MTG_EXHAUSTIVE_BOTTOM=1`), the pure
   heuristic (`MTG_EXHAUSTIVE_BOTTOM=0 MTG_BOTTOM_ROLLOUTS=0`), and the lookahead. Keep is the shipped
   table in every arm, and the shuffles before bottoming do not depend on the decision, so **every game
   sees identical hands at every mulligan level in all three arms** (verified: 0 of 1000 differ) — the
   arms differ only in which cards leave the hand. A fourth arm ran the *plain* (unshrunk) table.
2. **The raw sidecar** (`decks/FiveColour/FiveColour.keepmodel.exhaustive.raw.json`): per-cell sum /
   sumsq / count, i.e. what the table *believed* about every candidate and how many rollouts backed it.
3. **Fresh blind rollouts** of every size-6 candidate of every m=1 disagreement hand with the comp
   scorer (`MTG_SCORE_COMPS`, now served from the on-disk table so it costs 0.02 GB instead of 5 GB), at
   the labeller's depth (d2/b3, R=64) and at play depth (d6/b20, R=24, `MTG_SCORE_PD` one side per
   cell). `logs/fc_audit/{analyze,regret}.py`.

### 7b. What the arms showed

* **58.9 % of games mulligan** (352 at m=1, 186 at m=2, 46 at m=3). Bottoming decides most games.
* Win turn on the seed: **table 4.8700, heuristic 4.9340, lookahead 4.8640** (r1 had 4.869 / 4.863
  for table / lookahead on this seed). So *heuristic ≪ table ≈ lookahead*.
* **Under the confound the "lookahead" is the heuristic with a rare veto.** Win turns are integers,
  so most candidates tie at the single rollout's best value and `HeuristicBottomPick` decides among
  them: heuristic and lookahead disagree on only **17 %** of m=1 decisions (33 % at m=2). When the veto
  fires it is worth **+0.42 turns per game** (60 games × 0.42 ≈ the whole heuristic→lookahead gap).
* **The table disagrees with the lookahead on ~72 % of decisions.** Structurally: the table bottoms a
  *spell* 72 % of the time (Nicol Bolas, Progenitus, Hellkite, Mana Cannons, Archangel …); the
  heuristic/lookahead bottom a *land* ~60 % of the time (the excess-land rule fires at 3+ lands).
  Paired on disagreement games the table beats the pure heuristic clearly (−0.083/game at m=1,
  −0.243 at m=2) and ties the veto-corrected one within one seed's noise.
* Shrink vs plain differ on only 5 % of m=1 picks: the shrinkage is not where the behaviour is.

### 7c. What the raw sidecar showed — the signature

On the 274 m=1 disagreement hands, **the table's pick is an R=30 cell 274/274 times**, and its
believed margin over the lookahead's pick depends on how well-sampled the *alternative* was:

| lookahead's pick sampled at | n | table's believed margin (raw V) |
|---|---|---|
| R=2 — never refined | 45 | **−0.818t** (V 4.96 vs 5.78) |
| R=18 | 13 | −0.490t |
| R=30 | 216 | −0.185t |

Cells one card apart from the same 7-card hand do not truly differ by 0.8 turns. The pooled
per-rollout sd of this table is **~1.0t**, so a two-rollout estimate is ±0.7t; the never-refined
cells are exactly the ones whose two rollouts came out high. Meanwhile 41 % of the table's believed
margins are inside one standard error of zero, 71 % inside two.

Population view (table + raw, all hands kept at m=1, hypergeometric-weighted): **42 % of kept-hand
mass has a never-refined candidate; 16–24 % has one whose lower bound (z=1) undercuts the chosen
target's upper bound.** Those contenders are only **~29k distinct cell-sides**.

### 7d. Truth (fresh rollouts)

**At the labeller's depth (d2/b3, R=64 fresh rollouts per candidate, 274 m=1 disagreement hands,
`logs/fc_audit/regret_d2.txt`) the table's picks are the better ones, by a wide margin:**

| policy | regret vs truth argmin (per disagreement game) | 95 % |
|---|---|---|
| table | **+0.041t** | [+0.031, +0.051] |
| lookahead (heuristic + 1-sample veto) | +0.163t | [+0.143, +0.185] |
| heuristic alone | +0.183t | [+0.163, +0.203] |

V(table pick) − V(lookahead pick) = **−0.122t** (table better); the table's pick *is* the truth argmin
on 62 % of games, the heuristic's on 15 %. Mean candidate spread 0.89t, scorer se 0.06t per cell.

**The §7c signature is real but small.** Split by how well-sampled the lookahead's pick was in the raw:

| lookahead's pick sampled at | n | truth V(table)−V(look) | believed (raw) | regret table / look |
|---|---|---|---|---|
| R=2 — never refined | 45 | −0.068t | −0.818t | +0.088 / +0.156 |
| R=18 | 13 | −0.214t | −0.490t | +0.034 / +0.248 |
| R=30 | 216 | −0.128t | −0.185t | +0.032 / +0.159 |

So the never-refined alternatives *were* grossly over-estimated by the raw (believed 0.82t worse, truly
0.07t worse) — that is the winner's curse on the losers' two rollouts, exactly as §7e describes — and
the table's regret on those hands is nearly 3× its regret elsewhere (0.088 vs 0.032). But the table's
pick is still the better one on average even there. Weighted over the 274 hands the screening defect
costs about **0.01t per disagreement game**, i.e. ~0.003t per game overall: an order of magnitude too
small for the +0.018t confounded loss.

**Play depth says the same thing** (d6/b20, R=24, one side per cell, `regret_both.txt`): table regret
+0.056t, lookahead +0.186t, heuristic +0.202t; V(table) − V(lookahead) = −0.130t. So the ranking is
not an artifact of the labeller's depth — §3f is closed for good, and so is the "the labels value
bottoming a spell where play does not" reading this section carried for an hour.

### 7d-bis. The join that actually settles it: prediction vs the same games

Fresh rollouts and real games were then compared **per game**, on the very decisions the arms made
(`predicted_vs_realized.py`, `calibrate.py`, `level_check.py`):

| pair (m=1 disagreements) | n | scorer @ d6 predicts | the games deliver | gap |
|---|---|---|---|---|
| table − heuristic | 266 | −0.150t | −0.083t (se 0.031) | +0.067 |
| table − lookahead | 251 | −0.142t | **+0.020t** (se 0.028) | +0.162 |
| lookahead − heuristic (veto only) | 60 | −0.070t | **−0.417t** (se 0.083) | −0.347 |

And the levels, which is where the mechanism is visible (same 283 m=1 games, scorer @ d6):

| arm's kept comps | scorer V | realized win turn | V − realized |
|---|---|---|---|
| chosen by the **table** (argmin of the model) | 4.7465 | 4.8233 | **−0.077** |
| chosen by the **heuristic** (a domain rule, no model) | 4.8871 | 4.9011 | −0.014 |
| chosen by the **lookahead** (selected using the real game) | 4.8722 | 4.8057 | **+0.067** |

The model is *calibrated* on cells nobody selected with it (the heuristic's, −0.014t) and biased in
opposite directions on the two selected sets. That is the shape of an **optimizer's curse against the
simulator itself**, and it is a different defect from §7e:

* Each cell's rollout value carries a per-cell systematic error b(cell) — the gap between the
  generator's rollout model and the shipped game — on top of sampling noise.
* The table's target is `argmin` over a hand's removals, so it selects for **low b as well as low true
  value**: it lands on the cells its own simulator flatters. Measured at 0.077t.
* A fresh, high-R re-measurement **cannot detect this**, because it uses the same simulator: the scorer
  averages away the noise and reproduces the bias. That is exactly why §7d's fresh rollouts "confirmed"
  the table while the games did not.
* More rollouts — raising R, or racing the contenders (§7f) — converge to the *biased* value. The
  sampling half of the curse is worth ~0.01t per disagreement game (§7d); the model-bias half is ~0.08t.
  **No amount of generation fixes the larger half.**
* The lookahead's veto shows the mirror image: it selects on the real game, so it lands on cells the
  model is pessimistic about (+0.067t) — and the confound does not take that away. Its vetoed picks are
  worth **0.417t** in real games while a blind evaluation of the same hands prices them at 0.070t. Six
  times. ~~`MTG_CONFOUND_BOTTOM` reshuffles the library after the decision, which destroys the ORDER
  the lookahead peeked at but not the order-independent part of what its rollout learned, so the
  confounded A/B is still not a blind-vs-blind test.~~ **WRONG — retracted 2026-09-17, see §7i.** The
  6× gap is not residual sight. Mode 1's reshuffle happens before a single card is drawn, and blinding
  the lookahead *harder* (reshuffling before its evaluation rollouts too) leaves the veto's value
  unchanged. What survives the confound is order-INDEPENDENT hand quality, which is a legitimate blind
  signal, not a peek — and the 6× is this same model bias measured on the lookahead's selected set.
  The confounded A/B **is** blind-vs-blind, so "the table loses it by +0.018t" is a fair result.

**Against a genuinely blind opponent the table wins.** On the audit seed the pure heuristic (no
rollouts at all, nothing to confound) finishes at 4.9340 and the table at 4.8700 — the table is
**0.064t per game better**, while costing ~nothing: lookahead bottoming is 90.4 % of this deck's
runtime with no table.

### 7e. The mechanism (real; measured in 7d as a minor contributor)

`compute_sub_wave_tasks` (the adaptive sub-refine) marks **only the current argmin** of each needed
hand for more rollouts. The generator's own comment names the consequence — *"a true-argmin cell
noisily-high at the floor would never be marked"* — and `RunAdaptiveBottomRegretSim` models it as the
"winner's-curse-of-omission". The fast recipe (`adaptive_bottom`, floor R=2, cap R=30) runs a
two-rollout screening between the removals of a hand; the loser is never looked at again, and the
`bottom_floor` filter then bars it from the argmin for good (the plain table reaches the same pick
95 % of the time through the loser's inflated estimate). With ±0.7t of floor noise and removals that
are genuinely near ties, that screening is close to a coin flip, so the shipped argmin is
systematically "whichever near-tie candidate had the luckier first two rollouts".

Why the *controls* did not refute this: the regret is ∝ P(the true best loses the screening), which
depends on the spread between a hand's removals relative to the floor noise. Goblins and Melira have
larger spreads (their good removal is obvious) and pass; FiveColour's fifteen singleton lands, four
mana creatures and a top end of 6–10-drops make most removals near ties. A control that *passes* shows
the effect is small there, not that the mechanism is absent. §3b/§3c drew the stronger conclusion.

Why keep is immune: the keep decision reads `min(KeepVal, Dopt)`, and the min of noisy estimates is
biased *downward*, so an under-sampled good cell only ever makes a hand look *more* keepable — the
"curse-SAFE by construction" the code relies on. Bottoming needs the argmin's **identity**, and that
is precisely what a coin-flip screening corrupts. Same table, one half exposed.

Depth (§3f) is a minor component at most: the killed calibration arm had 9 paired seeds, label-depth
lookahead **+0.0034t** worse than play-depth (8/9 seeds) — a fifth of the deficit.

### 7f. The fix

1. **Generator** — race the contenders. `compute_sub_wave_tasks` now also marks every other
   subcomposition of a needed hand with P(V_c < V_arg) > flip_eps under the two cells' shrunk standard
   errors, until the cap or the race separates them. Same flip_eps as the keep gate. Refined contenders
   also clear the `bottom_floor` filter, which turns that filter from an exclusion of the unlucky into a
   guard against the merely unsampled.
2. **FiveColour** — re-refine on the frozen commit. The gen's per-cell journal survived
   (`logs/FiveColour_gen/journal.BACKUP.20260911`, 3,955,796 terminal size-7 records = every size-7
   cell-side frozen), so the patched generator at `2f7822a2` (worktree `/tmp/fc-gen-wt`, same
   play_digest `b79a1414`, d2/b1/t8) resumes it in a scratch deck folder (`logs/fc_contend/deck/`):
   nothing is re-rolled except the contenders. Then MTG_KEEP_MERGE with shrink + floor 2 → candidate
   profile → `versus` A/B against the shipped table and the confounded A/B against the lookahead.
   **NOT RUN, deliberately (2026-09-16).** The burn tests above show the re-refinement route works —
   an argmin-only journal resumed by the racing binary runs its waves and the resulting table wins its
   A/B. But 7d-bis prices this deck's *sampling* defect at ~0.01t against a ~0.08t model bias, so a
   10–28 h FiveColour re-refinement would buy a better table on the generator's own objective and
   would not move the confounded A/B it was started to fix. The journal is preserved
   (`logs/FiveColour_gen/journal.BACKUP.20260911`, 535 MB) and the scratch deck folder
   (`logs/fc_contend/deck/`) is ready, so the run remains one command away if the test is fixed first
   (7h) and the table is then judged on its merits.
3. **Resume path** — the first re-refinement attempt ran **0 waves, 0 rollouts** and wrote the shipped
   table back out. `sub_refine_step()` was only called from the floor branch of the producer loop;
   a journal resume that restores the REFS record enters the loop already in the refine phase, so the
   sub-refine was never stepped. That was harmless under the argmin-only rule (refs are fixed only after
   the sub-refine has converged, so a same-binary resume has nothing left to mark) and fatal for
   re-refining an argmin-only journal under the racing rule. Fix: the refine branch now runs the
   change-detect classification and `sub_refine_step()` too, and the refine exit additionally requires
   `sub_converged`. Fresh runs never reach the new call (refine is never true before convergence) and
   same-binary resumes converge on the first step, so neither path moves. Also learnt: the completion
   path deletes the journal, so a no-op resume *consumes* the journal — keep the backup.
4. **Generator tests (burn, K=10, 10,945 size-7 cells, fast recipe at d1/b3, 12 threads;
   `logs/gen_test/`).** Old binary = `0225469c` (argmin-only), new = HEAD with racing + the resume fix.
   * *Fresh old vs fresh new:* structural check `SAME-COUNT-DIFFERENT-VALUE: 0` over 37,706 cell-sides
     (a rollout is a pure function of its seed; nothing moved a value). Racing spent 319,540 sub-table
     rollouts against 209,016 (+53 %), total +27 % (524,777 vs 413,564), 4 waves vs 5; wall 82 vs 67 min.
     Size-6 cell-sides left at the R=2 floor: 3,953 → 1,840 (mean R 15.1 → 21.7); size-5 1,748 → 661.
     Decisions: 1.2 % of keep flags moved (Dopt shifted with the sub-tables), and **21–24 % of m=1/m=2
     bottoming targets changed** — burn's removals are near ties, so the shipped argmin was largely
     "whichever candidate the floor liked", and the racing re-decides most of those. Whether the
     re-decided targets are better is the `versus` A/B's question (below).
   * *Resume exactness (new binary killed at the refine transition, resumed by the same binary):* the
     resume reloads 37,706 cell-sides, reports "resuming refine (0s)", and runs **0 waves** — the
     reloaded sub-tables yield no marks, which is the invariant the fix must not break. Against the
     uninterrupted run: every sub-table cell-side identical (H=6..1 differ on 0 cells), 260 of 21,890
     size-7 cell-sides differ in COUNT only (the documented in-flight schedule drift),
     same-count-different-value 0, and the two profiles differ by **1 keep flag in 153,230 and 0
     bottoming targets**.
   * *Re-refinement (OLD binary killed at the refine transition, resumed by the NEW one)* — the
     FiveColour scenario: the resume runs **4 waves / 79,236 extra rollouts** where the old binary
     would have run none. Against the old table it adds samples to 4,573 cell-sides and removes them
     from 7, same-count-different-value 0. It does NOT reproduce a fresh racing run (5.9 % fewer
     rollouts than `burn_new`): the journal's refs — Dopt and the pooled vg — were fixed by the old
     run, so a re-refinement recovers most of the racing, not all of it.
   * *In-game A/B, old table vs new, both in the shipping condition* (16 seeds × 1000 games/arm,
     `KM_MODE=versus`): **−0.0077t, new wins on 15 of 16 seeds** (4.3516 → 4.3439; sd 0.0066, se
     0.0017, mean/se −4.6). The racing is worth a real, if small, improvement in actual play on a deck
     where it re-decides ~21 % of the bottoming targets.
   * *Confounded bottoming (blind table vs lookahead), both tables, same 16 seeds:* old
     **−0.0425t** (mean/se −12.80), new **−0.0483t** (mean/se −13.56). Both beat the lookahead
     decisively; racing widens the margin by **−0.0058t**, which agrees with the −0.0077t the
     `versus` A/B measured directly. So the racing's value shows up on the gate axis too, and burn's
     bottoming table is not close to the boundary on either binary.

### 7h. What to do about it (2026-09-16)

1. **Do not re-generate to fix the A/B.** The re-refinement described in 7f addresses ~0.01t of a
   ~0.09t problem. It is still worth having (it re-decides 21–24 % of burn's bottoming targets and
   removes a real winner's-curse), but it is not a remedy for the confounded loss and must not be sold
   as one.
2. ~~**Fix the test before re-judging the table.** Either blind the lookahead arm properly (reshuffle
   *before* its evaluation rollouts, not after the decision) or make the blind **heuristic** the control
   arm. `KM_MODE=bottom` currently pits blind-table against peeking-lookahead and calls the table's loss
   a defect.~~ **DONE and the premise was wrong (§7i).** The lookahead arm was blinded properly —
   `MTG_CONFOUND_BOTTOM=2` reshuffles before its evaluation rollouts — and the veto's value did not
   move. `KM_MODE=bottom` is already blind-vs-blind and needs no fix. Reporting the blind **heuristic**
   as a second control is still worth doing, because it is the arm that shows the table's *positive*
   value (0.064t/game on FiveColour) where the lookahead comparison shows only parity.
3. **Attack the bias, not the variance.** The lever that matters is the gap between the generator's
   rollout and shipped play, and a selection correction on the argmin (an optimizer's-curse / empirical-
   Bayes shrink toward the hand's mean, which `MTG_KEEP_BOTTOM_SHRINK` already does in a mild form).
   Both can be explored on the EXISTING raw with `test/keep_reconstruct_ab.sh` — no rollouts at all.
4. **Generalise the check.** Any deck whose table was validated by "fresh rollouts agree with the
   table" has been validated by the biased instrument. The level check in 7d-bis (model V vs realized
   win turn, split by which policy chose the cell) is cheap and is the honest calibration test.

### 7g. Method notes added

* **A fresh re-measurement with the same simulator is not an independent check.** It averages away the
  noise and reproduces the bias, so it will confirm a pick that the selection biased into existence.
  The only independent instrument is the shipped game. Six hypotheses were tested against rollouts
  before anyone joined the prediction to the outcome of the very same games — which took one script
  and no compute, and settled it immediately.
* **Calibrate on cells nobody selected.** The heuristic's picks were the control that made the bias
  legible: the model is right on them and wrong, in opposite directions, on the two selected sets.
* **"Passes with a worse pool" refutes only a linear story.** A mechanism whose effect scales with a
  deck-specific ratio (spread/noise) needs that ratio measured on the failing deck, not a pass elsewhere.
* **Read the arm you are losing to.** The confounded lookahead is not a one-sample argmin; it is a
  domain heuristic with a rare veto. Two hours of logs showed that; two weeks of theory did not.
* **The raw sidecar is an instrument.** Believed margin vs the alternative's R gave the signature
  before a single fresh rollout ran.

### 7i. 2026-09-17 — is the confounded A/B actually blind? (it is)

§7d-bis asserted a second cause: that `MTG_CONFOUND_BOTTOM` leaves the lookahead some residual sight,
because its vetoed picks realise 0.417t while a blind scorer prices the same hands at 0.070t. That
assertion does not survive the obvious test, and it mattered enough to run — the confounded A/B is the
adoption gate for **every** deck's bottoming in this repo, so "the gate leaks" would have put Hinata,
Auras, Melira, Dragonstorm and burn all in question.

The argument against it is structural. Mode 1 reshuffles the whole remaining library *after* the
decision but *before* a single card is drawn, so the order the lookahead peeked at is gone by the time
it matters. What its rollout can still carry forward is the **order-independent** part of what it
learned — how good the hand is in general — and that is a legitimate blind signal, not a peek.

**The test.** `MTG_CONFOUND_BOTTOM=2` (new, `AIEngine::HandleMulligan`) reshuffles *before* the
decision as well, so the lookahead's evaluation rollouts run against an order independent of the one
the playout will deal. Modes 0/1 cannot reach the new code, so every prior measurement is unchanged.
Three bottoming policies × three modes, one binary, 1000 games on the audit seed (`logs/fc_confound2/`).

Two structural checks first, because they must hold or the experiment is meaningless:

| check | result |
|---|---|
| m0 vs m1 lookahead picks — must be IDENTICAL (both decide pre-reshuffle) | 589/589 (100 %) |
| m2 vs m1 lookahead picks — must DIVERGE (m2 decides on a decorrelated order) | 458/589 (77.8 %) |

And the answer, the m=1 veto (lookahead − heuristic on the games where they disagree):

| confound mode | veto value | se |
|---|---|---|
| 0 — none (full clairvoyance) | **−1.017t** | 0.017 |
| 1 — shipped gate (reshuffle after the decision) | **−0.417t** | 0.083 |
| 2 — reshuffle before AND after | **−0.492t** | 0.074 |

**Mode 2 does not collapse the veto** — it is if anything marginally larger, about 0.7 se away from
mode 1. Mode 1 already removes the entire peek (0.60t of the 1.02t that clairvoyance is worth). So:

* **There is no leak, and `KM_MODE=bottom` is a fair blind-vs-blind test.** Every bottoming adoption
  that passed it stands. The gate needs no repair.
* **The 6× gap is the ONE cause, seen from the other side.** The blind scorer under-prices the
  lookahead's picks for the same reason it over-prices the table's: a per-cell model bias b(cell),
  and the two policies select on opposite sides of it.
* **FiveColour's +0.018t loss is therefore a fair result, not an artefact.** It is also a small one:
  on the audit seed the table is 4.8700 against the lookahead's 4.8640 and the blind heuristic's
  4.9340, so the table is at rough parity with lookahead bottoming while costing ~nothing, against a
  bottomer that is 90.4 % of this deck's runtime.

### 7j. 2026-09-17 — the shrink is already in the shipped FiveColour table

§7h item 3 proposed an optimizer's-curse shrink on the argmin as a zero-rollout lever, noting that
`MTG_KEEP_BOTTOM_SHRINK` "already does in a mild form". Worth stating plainly, because an agent
re-derived the opposite from the code and wasted a night on it: **generation does pass
`refine = nullptr` (`ExhaustiveKeep.cpp:1176`), so a table built by the generation path has no
shrink — but FiveColour's shipped table was not built that way.** It came from a merge, and
`logs/FiveColour_gen/adopt_chain.sh` (lines 69–76) ran that merge with `MTG_KEEP_BOTTOM_SHRINK=1` and
`MTG_MERGE_BOTTOM_FLOOR=2`. Reproducing that recipe from the existing raw gives a profile whose play
is **identical to the shipped table on all 1000 audit-seed games**; dropping only the shrink changes
56 of 589 mulliganed games' bottoming. So the shrink is in, and re-applying it buys nothing.

What it was worth, measured the other way round — the no-shrink rebuild as baseline against the
shipped table, 16 seeds × 1000 games, both in the shipping condition
(`logs/fc_shrink/ab_shrink/`):

```
noshrink 4.8501   shrink 4.8456   delta -0.0046t   12/16 seeds
sd 0.0038  se 0.0009  mean/se -4.86   (min -0.0100, median -0.0040, max +0.0010)
```

**−0.0046t, and it is already banked.** That lands almost exactly on the −0.0044t (mean/se −4.70)
the shrink measured on Melira Pod, which is worth noting on its own: the effect size is stable across
two decks with very different curves, so it behaves like a property of the correction rather than of
a deck. It is also the right order of magnitude for what §7d-bis predicts — the shrink attacks the
**sampling** half of the curse (~0.01t), and the model-bias half (~0.08t) is untouched by any
re-policy. There is no free win left on this axis for FiveColour.

Traps this walked into, all of which cost a run each:

* **A merge of this deck needs ~10 GB.** Peak RSS 9.8–10.0 GB, against a default `MTG_RSS_CAP_GB` of
  3/4 of RAM (8.02 GB on a 10.7 GB box). Both first-attempt merges aborted on the cap and wrote
  nothing. The write path is already streamed (the Creature-Giving OOM fix); the peak is the load and
  the size-7 keep/`bottom_keep` maps.
* **`MTG_EXHAUSTIVE_PROFILE` pointing at a MISSING file is not an error** — `AttachExhaustiveSidecar`
  falls through and the deck plays with whatever else resolves. The two arms then "measured" 4.9790,
  *worse than the blind heuristic*, which is the tell. Guard the path's existence in the harness.
* **`D_opt` does not discriminate shrink from no-shrink.** Both rebuilds print
  D_opt(draw)=4.77341 / D_opt(play)=5.01141, matching the original merge log, because the keep
  decision ranks on raw `V` and only the bottoming target ranks on the shrunk `Z`. (This is a second
  reason not to read `D_opt` as a quality metric — cf. `mulligan-reconstruct-lower-r.md`.)
* **Comparing arm MEANS is not a validity check; compare DECISIONS.** The no-shrink control and the
  shipped table both average exactly 4.8700 on 1000 games while differing on 56 of them — the effects
  cancel in the mean. A "the control reproduces the baseline" gate that reads the mean will pass on a
  profile that plays differently in one game in eighteen.
