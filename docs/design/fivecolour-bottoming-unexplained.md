# FiveColour's bottoming table loses the confounded A/B — state of the investigation

**Status 2026-09-15: the cause is UNEXPLAINED. Six hypotheses tested, five refuted, one in flight.**
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

RESULTS_PLACEHOLDER

### 7e. The mechanism

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
   REFINE_PLACEHOLDER

### 7g. Method notes added

* **"Passes with a worse pool" refutes only a linear story.** A mechanism whose effect scales with a
  deck-specific ratio (spread/noise) needs that ratio measured on the failing deck, not a pass elsewhere.
* **Read the arm you are losing to.** The confounded lookahead is not a one-sample argmin; it is a
  domain heuristic with a rare veto. Two hours of logs showed that; two weeks of theory did not.
* **The raw sidecar is an instrument.** Believed margin vs the alternative's R gave the signature
  before a single fresh rollout ran.
