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
