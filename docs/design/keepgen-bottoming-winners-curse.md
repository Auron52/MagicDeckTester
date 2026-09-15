# The bottoming argmin's "winner's curse" — a REFUTED diagnosis, and what survived it

**Status: REFUTED and REMOVED (2026-09-14).** An earlier revision of this document claimed FiveColour's
failing confounded bottoming A/B was caused by sampling noise in `best_sub`'s argmin, and that a
shrinkage-plus-gate correction fixed it (+0.018375 t → −0.0021 t). **The diagnosis was wrong and the
"fix" was forbidden.** The code was reverted; this document is kept because the measurements it
produced are sound and reusable, and because the failure of the reasoning is itself the lesson.

> **Do not re-implement the gate.** It is closed by policy *and* by evidence — see §3. There is a
> pointer to this document in the comment above `BuildPolicyFromTables` in
> `src/analyzer/ExhaustiveKeep.cpp`.

---

## 1. The symptom that started it

FiveColour's 20-day generation (55.66 M rollouts, frozen `2f7822a2`) produced a keep table that
*won* decisively and a bottoming table that *lost*:

```
keep      -0.125000t  ok      (16/16 seeds, mean/se -23.4)
bottoming +0.018375t  REJECT  (0/16 seeds,  mean/se  +6.09)
```

Per `mulligan-profile.md` the response to a bad confounded bottoming A/B is "raise R or fix the
bottoming heuristic, **not** ship bottoming off" (there is no off switch). Raising R meant
regenerating. The user's constraint: *"I'm not regenerating something that took 3 weeks."*

## 2. The hypothesis, and the measurements behind it (these are still valid)

`BuildPolicyFromTables::best_sub` picks a hand's bottoming target by `argmin` over its
subcompositions' estimated values. Those estimates are rollout means at finite R, so the argmin over
N noisy candidates systematically selects the **luckiest estimate, not the best hand**. The bias is
real at any finite R — it is the mechanism `keepgen-subtable-starvation-detection.md` already names.

Everything in this section is measured from the raw sidecar by the `test/keepraw_*.py` tools and
remains accurate. **What was wrong was the inference from it, not the numbers.**

**Sub-cell precision.** FiveColour's sub-table rollout counts are **trimodal: exactly R = 2, 18 or
30** (floor / one refine step / cap). 41–43 % of size-4…6 cell-sides sit at the floor. Per-rollout
sd is **0.74–0.80 t**, so se ranges 0.52 t (R=2) → 0.14 t (R=30).

**Exposure.** A `--gen-mulligan fast` run has `adaptive_bottom=true`, so `best_sub` receives
`bottom_floor = r0 = 2` and the argmin is already restricted to refined cells. On that refined set:

| depth m | hand size | candidates N | se | tau (true spread) | **noise share** | no-refined fallback |
|---|---|---|---|---|---|---|
| 1 | 6 | 4.2 | 0.200 | 0.296 | **50.0 %** | 6.3 % |
| 2 | 5 | 10.3 | 0.218 | 0.377 | 37.4 % | 1.9 % |
| 3 | 4 | 16.2 | 0.217 | 0.413 | 31.0 % | 0.7 % |

"Noise share" = `mean(se²)/var_observed`. At the depth that dominates play, half the apparent spread
between candidates is sampling noise.

**The arithmetic that appeared to confirm it — and why it proved nothing.** Bottoming runs only when
`mulligan_count > 0` (`AIEngine.cpp:579`), so `delta_overall = f · (regret_table − regret_lookahead)`.
Monte Carlo over the real hypergeometric hand distribution gave `f = 0.586` (mix m1 62 % / m2 30 % /
m3 7 %) and a simulated table regret of 0.0495 t, hence
`0.586 × (0.0495 − 0.0185) = 0.0182` against the measured **+0.0184**.

That agreement is **an artifact of circular reasoning**: the lookahead's regret L = 0.0185 t was
*solved for* from the very A/B being explained. Any table regret whatsoever can be matched by
choosing L to absorb the residual, so the identity had **no power to discriminate** — it could not
have come out any other way. The original revision flagged this as a "caveat"; it was in fact fatal.

Useful byproduct: `keep%_opt` in the gen report is **unweighted over compositions** and is *not* the
mulligan rate. Do not reuse it as one.

## 3. The refutation: a control the hypothesis could not survive

**Melira Pod ships the same `fast`/R30 recipe, is WORSE on every metric the theory relies on, and
PASSES.**

| | FiveColour (**fails** +0.0184) | Melira Pod (**passes** −0.091, 16/16, mean/se −31.4) |
|---|---|---|
| mean sub-table R | 13.84 | 13.58 |
| % of sub cell-sides at floor R=2 | 41.2 % | **45.4 %** |
| m=1 noise share | 50.0 % | 48.4 % |
| m=1 no-refined-candidate fallback | 6.3 % | **8.2 %** |

If argmin sampling noise at this magnitude sank FiveColour, Melira should have sunk harder. It did
not. The user made the same objection from first principles before any of this was measured —
*"I would still expect the confounded bottoming test to succeed because testing N versions is less
noisy than 1"*, and *"this has never failed in the past"* on other decks — and was right.

**A broader survey agrees.** Across all 20 decks' raws, every deck that passes its confounded
bottoming A/B has uniform sub-tables at cap (minR == cap, 0 % at floor): Dragons, Mirrorwing, Auras,
Minotaur, Fluctuator, Dragonstorm, KittyEquipment, StompySurprise, Anti-Lifegain,
BreachingDragonstorm, CritterLifegain at R=40; Knights, burn, slivers_vial at R=60; treasure_hunt 41;
Hinata2 22. Only the four `fast`/adaptive runs have floor cells at all — FiveColour 41.2 %, Melira
45.4 %, Creature Giving 35.8 %, Goblins 58.2 % — and Goblins (adopted 2026-08-08, before the
confounded gate existed) is the only one of those not independently confirmed to pass. **Floor cells
do not predict failure.**

## 4. Why the "fix" was inadmissible regardless of what it measured

The gate emitted a bottoming target only where the winner's margin cleared the top two candidates'
combined noise, and **emitted an empty vector otherwise** — 67.9 % of slots for FiveColour.
`ExhaustiveKeepPolicy::DecideBottom` rejects a slot failing its `size() != K` check and
`AIEngine::BottomCards` falls through to the **lookahead bottomer**. That is:

1. **Against an explicit user directive.** *"Lookahead bottoming should be on nowhere for decks with
   profiles."* Two thirds of a shipped profile's bottoming decisions were being handed back to it.
2. **Against the repo's own artifact check.** `scripts/mullgen.sh:290` counts any row with
   `len(r) != K` as **malformed**; validate failed with
   `ARTIFACT CHECK FAILED: ... malformed_bottom_keep=1977326`. The profile could never legitimately
   have been adopted whatever the A/B said.
3. **Structurally unfalsifiable as an improvement.** As k rises the policy converges to the very arm
   it is measured against, so "it beats the lookahead" degenerates toward "it *is* the lookahead".
   The original revision listed this bounded downside as a *virtue*. It is the opposite: a knob whose
   limit is the control cannot demonstrate that the table is good.

A measured −0.0021 t at k=1.0 therefore established nothing worth keeping.

## 5. What does NOT work on the estimator, measured (so nobody re-tries it)

**Shrinking every candidate toward a common mean recovers ~6 % of the regret (0.0037 of 0.0609 t).**
Arithmetic, not a tuning failure: with equal per-candidate precision, shrinking toward a common mean
is a **monotone transform of the estimates, so the argmin is literally unchanged**. All its leverage
comes from *differences* in precision, and among refined candidates ours differ only R=18 vs R=30.
**Re-weighting the same numbers adds no information.**

**A structural prior carries real signal but did not close the gap.** A quadratic
(pairwise-interaction) fit of the cell mean on bucket counts explains **50–89 %** of true between-cell
variance (size-5 draw: tau 0.689 → 0.224), because a cell borrows strength from every other cell
holding those cards. It recovered 24 % of the (mis-attributed) regret. This measurement stands on its
own and may be worth revisiting for an *in-generation* estimator — but never as a gate.

## 6. What genuinely survived — the durable results

### 6a. The offline rebuild is EXACT

**A 3-week generation can be re-policied in ~20 minutes with zero rollouts.** Verified: with
`MTG_KEEP_MERGE` + `MTG_MERGE_BOTTOM_FLOOR=2` over the single raw sidecar, the rebuilt profile's
`entries` section is **byte-identical** to the generated one and `D_opt` matches to 6 figures
(draw 4.77341 / play 5.01141). **The raw sidecar, not the profile, is the durable asset** — a failed
A/B does not oblige a regeneration if the question is about how the policy is *derived*.

### 6b. Two merge-path bugs, fixed and KEPT

Establishing that control exposed two defects affecting *every* merged profile. Both fixes are
retained; only the gate was reverted.

1. **`t.cnt` was never populated and `t.se` was pushed as `{0,0}`.** Because `best_sub`'s filter
   short-circuits on `bottom_floor < 0`, a plain merge silently rebuilt an **unfiltered** bottoming
   policy — re-admitting exactly the floor-R cells an adaptive-bottom generation deliberately
   excluded. A merged profile was therefore *not* a faithful rebuild of the profile it replaced.
   **Any pooled/merged bottoming profile built before this fix is suspect.**
2. **`ek.play_digest` was dropped by the merge.** That field is the real pooling identity (`commit`
   over-approximates), and `RunKeepMerge` itself uses it to decide whether sidecars may pool. Every
   merged profile shipped without it, so it could not be pooled against or audited later.

### 6c. Measurement tools

Pure-python, read only the raw sidecar, no engine and no rollouts:

| tool | answers |
|---|---|
| `test/keepraw_argmin_noise.py` | se / N / tau and the noise share, filtered vs unfiltered |
| `test/keepraw_shrinkage_sim.py` | what shrink-to-mean can recover (the null result) |
| `test/keepraw_structural_prior.py` | how much signal a structural prior carries |
| `test/keepraw_mullrate.py` | Dopt, mulligan mix, and `f = P(bottoming fires)` |
| `test/keepraw_hybrid_sim.py`, `..._quad.py` | the (now-dead) gate sweep; retained for the machinery |

## 7. Play drift — tested and ELIMINATED (2026-09-15)

What remained specific to FiveColour, after noise was eliminated, was that it is the **only deck
generated by the pre-fix generator**: frozen `2f7822a2`, started 2026-08-21, before the
Dragons/Mirrorwing starvation fix added `sub_target` to the raw meta. It ran 20 days on that frozen
binary and was validated ~230 commits later, so the hypothesis was that its labels describe play
that no longer exists.

**Screen.** Recomputing the rollout-config play digest at the labeller config (d2/b1), in a scratch
deck dir with **no exhaustive profile present** (presence-gating would otherwise change the very
play being fingerprinted):

```
recorded at generation:                          b79a141457869ca5
current binary + CURRENT value sidecar:          8fca5f52d662de5f
current binary + GEN-TIME value sidecar:         8fca5f52d662de5f
```

Rows 2 and 3 agree, so the sidecar edits (`mull_gen_budget_ms` 1→3, `escalation_r` /
`escalation_fresh_frac` added by `54931b3a` / `6a938099`) are **not** the cause — the engine's own
play moved under the labels. But the Mirrorwing precedent says a moved digest is necessary, not
sufficient: that deck shipped with a known-stale `play_digest` and still passed at −0.0918.

**Causal test, which is what settled it.** Rebuilt `2f7822a2` in a worktree and re-ran the confounded
bottoming A/B *there* with the original un-gated profile, on the same 16 bottom-block seeds — the
table measured against the exact engine it was fitted to:

```
on 2f7822a2 (the engine the table was FIT TO):  +0.0165t   (2/16 seeds)
on HEAD     (recorded earlier):                 +0.018375t (0/16 seeds)
```

Statistically indistinguishable. **Drift is real but is not the cause — the table was never good.**

With this, every hypothesis offered for FiveColour's failure has been eliminated: sub-table
starvation, the adaptive "adversarial pool", fetchland bucket abstraction, the winner's curse of §2,
and drift. **The failure is unexplained.** Per `no-lookahead-bottoming.md` §2 that does not block
adoption — the confounded A/B is a diagnostic, not a gate, because neither shipping bottoming off
nor deferring to the lookahead is available.

**Method note.** The screen alone would have supported the drift story. The causal test was
constructed so neither outcome depended on the refuted regret model of §2 — and it is cheap: when a
staleness hypothesis can be tested *against the old binary*, test it there instead of reasoning from
fingerprints.

## 8. Guard blind spots found along the way

* **`scripts/check_keep_subtables.py:57`** requires `rollsub=...sub=N/M` in the monitor line. A
  **continuous** (`--gen-mulligan fast`) run emits `fed=`/`frozen=` instead — `grep -c rollsub=` over
  FiveColour's entire 20-day log returns **0**. The script returns "inconclusive" and **exits 0**,
  and the doc legend reads "0 = healthy or inconclusive". Asked mid-run whether FiveColour had the
  Dragons/Mirrorwing problem, this tool was consulted and its silence reported as a clean bill of
  health. **It structurally could not have answered.** Either teach it the continuous format or make
  "inconclusive" a distinct non-zero exit.
* **`scripts/mullgen.sh:313-315`** falls back to `target = 2` when the raw has no `sub_target`.
  FiveColour's doesn't, so it passes the sampling gate **trivially** rather than on evidence. A
  missing field should be reported as unverifiable, not defaulted.

## 9. Method lessons

* **A model fitted to explain one number, using a parameter solved for from that same number, has not
  explained anything.** Check whether the account could have failed before treating agreement as
  confirmation.
* **Find a control before building the fix.** Melira was sitting in the repo the whole time, cost
  minutes to check, and would have killed the hypothesis before ~300 lines of code, a push, and a
  bad profile reaching origin.
* **A knob whose limit is the control arm cannot prove the treatment works.**
* **A repo guard that rejects your output is evidence about the design, not an obstacle to route
  around.** `mullgen.sh`'s artifact check failed this profile immediately and was correct to.
