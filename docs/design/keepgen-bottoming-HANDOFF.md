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

## 5. WHY a peek-nullified lookahead still beats an ungated table

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

**`fast` is what creates it.** Surveying all 22 committed raws, only those four decks have floor
cells and they are exactly the four built with `--gen-mulligan fast`. All 18 `complete` decks
(R=40, `sub_target:40`) refine every sub-cell and have none. So **`fast` is not merely "less R"** —
it buys its 22–38 % saving by leaving the *worst* cells unrefined, which the recipe study does not
mention. Weigh that when choosing a recipe. Melira was generated `fast`.

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
