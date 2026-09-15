# HANDOFF — bottoming estimator work: what is settled, what is refuted, what to change

Written 2026-09-15 for the agent taking over. **No shipped artifact was changed by this
investigation** — FiveColour and Melira are exactly as committed. Everything below is measurement
plus one adoptable result. You make the final changes.

Companions: [no-lookahead-bottoming.md](no-lookahead-bottoming.md) (the constraint),
[keep-argmin-winners-curse.md](keep-argmin-winners-curse.md) (a lead that was **refuted** — read its
§7), [keepgen-bottoming-winners-curse.md](keepgen-bottoming-winners-curse.md) (the original fix).

---

## 1. The constraint you are working under

**USER DIRECTIVE 2026-09-14:** *"I don't want to use lookahead bottoming for anything"* — *"The point
of the profile is to replace both keep and lookahead."*

A shipped profile must answer **every** bottoming slot it covers. An empty `bottom_keep` row makes
`DecideBottom` return false and `AIEngine::BottomCards` fall through to the lookahead bottomer. That
is prohibited regardless of how it measures. This overrides any recommendation to defer.

Practical form: **`MTG_KEEP_BOTTOM_GATE_K=0` on every build.** Note `MTG_KEEP_BOTTOM_REFINE=1`
*alone* defaults `gate_k` to **1.0**, so the flag's own default is non-compliant.

---

## 2. THE ONE ADOPTABLE RESULT — compliant shrinkage beats what we ship

Generation ships a **plain, unshrunk** bottoming table (`BuildPolicyFromTables` at
`ExhaustiveKeep.cpp:1198` passes no refine argument; only the merge path at `:4909` does). Melira's
shipped profile is exactly that. Rebuilt with shrinkage on and the gate off, then A/B'd in the
shipping condition, 32 fresh seeds x 1000 games, **both arms zero-deferral**:

```
Melira Pod:  shipped(plain)  vs  k=0 + shrink
overall delta -0.0044t   (new BEATS old)
spread min -0.0160  median -0.0040  max +0.0080   sd 0.0053  se 0.0009  mean/se -4.70
```

* No regeneration, no rollouts — a **~4 minute offline re-policy** of the committed raw.
* Fully compliant with §1: zero deferred slots on both arms (verified; the rebuilt profile is
  byte-identical in *size* to the shipped one, 616,281,656 bytes — only target values moved).
* No confounding needed here, and that matters: confounding is required only when the arms differ in
  how much lookahead they use. These two don't use it at all.

**Offline predicts this is general, including for `complete` decks that have no floor cells:**

| deck | recipe | plain | +shrink | +shrink +cross-pd |
|---|---|---|---|---|
| FiveColour | fast R30 | 0.0593 | 0.0451 (−24 %) | 0.0363 (−39 %) |
| Melira Pod | fast R30 | 0.0611 | 0.0486 (−20 %) | — |
| Goblins | fast R30 | 0.0734 | 0.0583 (−21 %) | 0.0441 (−40 %) |
| Creature Giving | fast R30 | 0.0439 | 0.0350 (−20 %) | 0.0277 (−37 %) |
| Dragons | complete R40 | 0.0416 | 0.0328 (−21 %) | 0.0213 (−49 %) |
| CritterLifegain | complete R40 | 0.0442 | 0.0369 (−17 %) | 0.0240 (−46 %) |
| Minotaur | complete R40 | 0.0296 | 0.0248 (−16 %) | 0.0163 (−45 %) |

Per-decision *subcomposition-selection* regret, `test/keepraw_crosspd_prior.py`. Only the Melira row
has been confirmed in play.

### Suggested first action

Re-policy every deck with `GATE_K=0 SHRINK=1`, A/B each against its incumbent, adopt the winners.
Recipe in [no-lookahead-bottoming.md](no-lookahead-bottoming.md) §"How to build a compliant profile".
**Per-deck A/B is not optional** — Melira is one deck and −0.0044t is a small effect.

---

## 3. FiveColour is NON-COMPLIANT and needs a decision

Adopted `9feb6bf2` with the winner's-curse gate at k=1.0. Measured on the shipped artifact:

```
K=27 entries=1977898 slots=27690572 deferred=15621158 (56.4%)
```

**56.4 % of its bottoming decisions are made by the lookahead bottomer in real play.**

Also worth knowing: `scripts/mullgen.sh`'s artifact check counts a short `bottom_keep` row as
`malformed_bottom_keep` and **fails the run**. That profile could only ship via the merge path,
around the gate that would have caught it.

**The cost of making it compliant, measured honestly:**

| A/B | delta | reading |
|---|---|---|
| unconfounded `KM_MODE=versus` | +0.0610t | scores lookahead on the library it peeked at — **not usable** |
| **confounded** (`MTG_CONFOUND_BOTTOM=1`, 32 fresh seeds) | **+0.0142t**, mean/se +10.31 | the honest number |

So compliance costs FiveColour ~0.014 turns. Routes to recover it, both compliant:

* **cross-pd conditioning** — predicted to take +0.0142 → ~+0.0104. Not yet implemented in C++.
* **more R** — regret scales **~1/R, not 1/√R**. Pooling one more R30 chunk to R=60 with the
  correction reaches ~+0.003t, near parity. Costs another generation run (the original was 20 days).

A compliant k=0 rebuild of FiveColour exists at `/tmp/fc_k0.profile.json` (not committed, /tmp will
not survive).

---

## 4. WHAT I GOT WRONG — do not rebuild on these

Three claims I made and then disproved. They are in the git history and in earlier commit messages,
so check dates.

### 4a. "The keep argmin is the biggest lead, worth ~3x from a one-line filter" — REFUTED

`KeepVal` (`:307`) takes a plain `std::min` over every subcomposition while `best_sub` (`:624`)
filters to `cnt > bottom_floor`. Regret differs 3x (0.1766 vs 0.0593 t) and that measurement is
correct — but it is **inert**. Implementing the filter and rebuilding `D_opt` by the engine's own
recursion gives **zero change to four decimals** on all seven thresholds and both mulligan rates.

Why: the curse is self-limiting. A floor cell can only win the argmin when *every* subcomp of the
hand is poor, so the corrupted hands are exactly those already worse than `Dopt[m+1]`, and
`min(KeepVal, Dopt[m+1])` clips it on both arms. Measured: of the 2.03 % of m=1 hands where the
filter binds, **100 %** are clipped. `KeepVal`'s argmin *identity* is never shipped; only its
clipped *value* is. Full detail in `keep-argmin-winners-curse.md` §7.

**Generalisable lesson: regret in an estimator only matters where the estimate is PIVOTAL.** Check
pivotality — one cheap offline experiment — before optimising any estimator.

### 4b. "The offline simulator is calibrated" — TOO BROAD

`test/keepraw_hybrid_sim.py` hard-codes `L_LOOK = 0.0185` (line 46), back-solved from FiveColour's
own A/B, and prints it as "vs lookahead" for whatever raw you point it at. On Melira it predicts the
table *loses* by +0.0161; Melira's real confounded bottoming A/B measured **−0.091t**, table winning
16/16 seeds. Sign wrong, magnitude out ~6x.

Because `delta(k) = F·(reg(k) − L)`, the constant **cancels when two gate settings are differenced**
— which is the only way it was used successfully (predicted k=0-vs-k=1 gap 0.0171t vs measured
0.0142t). **Use it to rank table variants; never read its PASS/reject verdicts against lookahead off
FiveColour.**

### 4c. "rho is 0.990, the doc's 0.885 was wrong" — WRONG, the doc was right

Computing rho on structural residuals with `tau_r` in the denominator yields **1.142** — impossible —
which silently hit a 0.99 clamp I had written. Disattenuated on raw cell values it is 0.885 (size 6),
0.887 (5), 0.836 (4), 0.697 (3), 0.551 (2). The tool now computes it that way and warns instead of
clamping silently. Corrected cross-pd gains: 38.7 % (bottoming), 73.2 % (keep, but see 4a — inert).

---

## 5. WHY FiveColour fails the confounded gate — THREE HYPOTHESES TESTED AND ELIMINATED

**This is unexplained. Do not re-tread the three below.** The user's read was right:
*"There isn't a good explanation I can think of for this problem."*

The baseline that makes it a puzzle: `confounded-bottoming-gate-failures.md` root-caused the only
two prior failures (Dragons +0.0641, Mirrorwing +0.1006) to **starved sub-tables at R=1**, repaired
both, and both flipped decisively negative (−0.1176 / −0.0918). Its conclusion — *"no deck with a
properly-sampled sub-table has ever failed this gate"* — was written 2026-09-01, **before FiveColour
existed. FiveColour is the first counterexample.**

### Eliminated 1 — adaptive-floor "adversarial pool"

Refinement targets contenders, so cells left at the R=2 floor are measurably worse AND noisier
(dV +1.19…+1.63, corr(cnt,V) −0.67…−0.72 on all adaptive decks). Attractive, and **wrong as an
explanation of the gate outcomes**:

* **Goblins** has the worst pool of any deck (floor 58.2 %, dV +1.628) and **passes**.
* **Dragons** had *no* floor cells at all (uniform R=40) and **failed**.
* **FiveColour** has the mildest pool of the three adaptive decks and **fails**.

The structure is real and worth knowing (§5b of `keep-argmin-winners-curse.md`), but it does not
predict who fails.

### Eliminated 2 — under-sampled sub-tables (the Dragons/Mirrorwing cause)

FiveColour's sub-cells are trimodal 2/18/30, not starved at 1. `confounded-bottoming-gate-failures.md`
explicitly lists **Goblins at "2 (= adaptive floor, legitimate)" as a PASS**. So an adaptive floor of
2 is not by itself the defect. FiveColour's artifact check reports `min_rollouts=2 sub_target=2` and
passes legitimately.

### Eliminated 3 — fetchland bucket abstraction

FiveColour is the only deck whose largest merge is heterogeneous in a way that matters: **5
fetchlands (13 of 60 physical cards) fetching 5 different colour pairs, on a five-colour deck**,
force-merged by construction. P(>=2 fetchlands in an opening 7) = **47.6 %**, so with bottoming
firing on ~58.6 % of games the table faces a choice it cannot express on ~27.9 % of all games — very
close to the 56.4 % deferral the gate chose. Every other deck's merges are functionally equivalent in
context (Melira's are colour-matched by user ruling; slivers/mana-dorks are near-identical cards).

**Tested against the shipped gated profile and refuted — deferral runs the WRONG WAY:**

```
 fetchlands in hand        slots     deferred   defer%
                  0     20448610     12168701    59.5%
                  1      5617080      2717779    48.4%
                  2      1321040       603665    45.7%
                  5         4704          972    20.7%
                  6          364           18     4.9%
  <=1 fetchland:  57.1% deferred      >=2 fetchlands: 45.2% deferred
```

The gate is **most** confident on fetchland-heavy hands, monotonically. So the +0.0142 t it buys is
not coming from repairing fetchland choices. (Caveat worth keeping: abstraction error would make the
table *confidently wrong*, which a margin-based gate cannot see — so this refutes "abstraction shows
up as low confidence", not abstraction outright. But it does refute the gate's benefit being
fetchland-driven, which was the testable part.)

### What is still live

**Play drift.** FiveColour's raw carries `commit 2f7822a2` / `play_digest b79a141457869ca5`, and every
A/B here ran at HEAD. The labels were fit to play that no longer exists. Another agent was testing
exactly this when this handoff was written — **check that result before spending anything else**, and
note the precedent: the Mirrorwing repair recorded a stale-digest table that still shipped fine, so
drift is not automatically disqualifying.

## 5b. Why a peek-nullified lookahead can beat an ungated table (mechanism, not deck-specific)

Not bucket abstraction — FiveColour's discovery merged almost nothing (K=27 over ~27 distinct cards).
The mechanism is that **adaptive refinement makes the candidate pool adversarial**. Refinement
targets contenders, so cells left at the R=2 floor are *worse as well as noisier*:

```
deck                    K   R     cells  floor%      dV   corr  se(fl)  sigma
Creature Giving        21  30    438322   35.8%  +1.186  -0.67   0.363   3.27
FiveColour             27  30   1317366   41.2%  +1.237  -0.70   0.464   2.66
Goblins                21  30    359376   58.2%  +1.628  -0.69   0.558   2.92
Melira Pod             23  30    600866   45.4%  +1.446  -0.72   0.472   3.06
```

`dV` = meanV(floor) − meanV(refined). A floor cell needs only ~2.7σ to win, and the mistake costs
over a turn. This is the one failure mode that can be **worse than random** — a confounded lookahead
rollout is an unbiased draw; the argmin prefers bad-and-lucky.

**Adaptive bottoming is what creates it** — which `--gen-mulligan fast` selects. **Detect it from the
count distribution, NOT from `R`**: adaptive runs are trimodal (2 / 18 / 30), bottoming-full runs are
uniform at the cap. `sub_target` is absent from most raws (legacy) and is not a usable marker.

```
adaptive:  Creature Giving, FiveColour, Goblins, Melira Pod   (R=30, counts 2/18/30)
           Mirrorwing Dragon v2                               (R=60 POOLED, counts 4/20/32/36/60)
uniform:   everything else (R=40 / 60 / 41 / 22, 100% at cap)
```

**A fixed `cnt <= 2` threshold misses pooled adaptive decks** — pooling sums counts, so two floor
cells become 4. That is how an earlier pass of this survey missed Mirrorwing v2. Detect the floor
empirically.

So `fast` is not merely "less R" — it buys its 22–38 % saving by leaving the *worst* cells unrefined.
Worth weighing when choosing a recipe. **But per §5 this does not predict gate failure**, so do not
treat it as the explanation for FiveColour.

Residual exposure after `best_sub`'s filter: the fallback when *no* candidate is refined —
**8.2 %** of m=1 decisions on Melira, 6.3 % on FiveColour.

---

## 6. Recommended order of work

1. **Re-policy + A/B every deck at `GATE_K=0 SHRINK=1`.** Free, compliant, measured +0.0044t on
   Melira. Adopt per deck on its own A/B.
2. **Decide FiveColour.** It is non-compliant today. Making it compliant costs +0.0142t unless (3)
   lands first.
3. **Implement cross-pd conditioning** in `BuildPolicyFromTables` with a **per-size measured rho**
   (it collapses to 0.55 at size 2 — a global constant is wrong). Predicted −39 % bottoming regret.
4. **Apply the correction at generation time**, not merge-only, so fresh decks stop shipping
   uncorrected tables. Open follow-up #1 in the original doc, now the higher priority of the two.
5. **Then delete the flags.** User: *"once we have settings we are satisfied with we should remove
   the other options. Too many settings is a recipe for problems."* Remove
   `MTG_KEEP_BOTTOM_REFINE` / `MTG_KEEP_BOTTOM_SHRINK` / `MTG_KEEP_BOTTOM_GATE_K`; bake shrinkage on
   with no gate. Consider deriving `MTG_MERGE_BOTTOM_FLOOR` from the raw's `sub_target` — omitting it
   silently produces an unfaithful rebuild, which is a corruption footgun, not a knob.
6. **Fix `mullgen.sh`'s quarantine path.** On a confounded *bottoming* failure it deactivates the
   profile, dropping the deck to **100 %** lookahead — the worst outcome under §1. It should report
   and leave the table live. The *keep* half of that gate is fine.

---

## 7. Tools (all offline, read only the raw sidecar)

| tool | answers |
|---|---|
| `test/keepraw_crosspd_prior.py <raw> [sample] [trials] [floor]` | plain vs shrink vs cross-pd regret; per-size measured rho. `SE_SCALE` simulates a different cap R (se∼1/√R), `SE_CAP` simulates a raised floor |
| `test/keepraw_pool_survey.py [floor]` | cross-deck: is the candidate pool adversarial? floor%, dV, corr |
| `test/keepraw_keepfilter_dopt.py [samples] [floor]` | the §4a refutation: `D_opt` and mull-from-7 under plain vs filtered `KeepVal` |
| `test/keepraw_argmin_noise.py <raw> [sample]` | noise share and `best_sub` fallback rate |
| `test/keepraw_hybrid_sim.py` | gate sweep — **differences only**, see §4b |

`keepraw_hybrid_sim.py` and `keepraw_mullrate.py` now take `KEEPRAW_PATH` / `KEEPCOD_PATH` so they
run on any deck; they used to be hard-wired to FiveColour.

---

## 8. Traps

* **Confound any A/B whose arms differ in lookahead use.** Unconfounded it read +0.0610t where the
  truth was +0.0142t — a 4.3x inflation in the gated profile's favour.
* **`MTG_MERGE_BOTTOM_FLOOR` must match the generation** (`2` for `fast`). Omit it and the rebuild
  silently re-admits floor cells the generation excluded, so it is not a rebuild of the thing you
  meant to rebuild.
* **Verify deferral before shipping** — count `bottom_keep` rows whose length != K. Must be 0.
* **A profile with both `.json` and `.json.gz` present in the deck folder is ambiguous** to sidecar
  resolution. For A/Bs pass explicit `KM_EXH_A`/`KM_EXH_B` paths rather than relying on the folder.
* **Clean up bincaches.** Each A/B arm leaves a `<profile>.bincache` of 1–3.6 GB beside the deck.
* **Regret scales ~1/R, not 1/√R.** Doubling R roughly halves it. Per rollout spent, raising the
  *floor* is ~4x more efficient than raising the *cap*, though the cap goes further in absolute terms.
