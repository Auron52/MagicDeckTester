# Auras exhaustive mulligan profile (R=40) — GENERATED + VALIDATED

2026-07-25. Exhaustive bucketed keep+bottom mulligan profile for the Auras (Bogles) deck, generated
on the frozen commit `a6b4160` (value-leaf adopted) and validated by the in-game A/B before adoption.
Follows `.claude/skills/mulligan-profile.md`.

## Generation

- **Feasibility:** K=**19** buckets (24 cards → 19 classes; lands merge, hexproof 1-drops merge,
  Rancor/Audacity merge, scaling auras stay distinct). **367,321 distinct hands** (size7=252k). R=1 mm6
  full pass ≈ 24 min — keep-rollouts run at d5/b20 with the value leaf attached (`AttachValueSidecar`),
  so the value leaf *accelerates* the gen.
- **Build:** `test/exhaustive_chunked_gen.sh` (`KM_TARGET_R=40 KM_ROUND_R=5`, mm6), 8 adaptive rounds
  (frozen `a6b4160`, seeds 30001000–30001007) → effective **R=40** on the live frontier. Adaptive
  freezing shrank each round (round 1 7231s → round 8 3100s); 388k of 504k size-7 cell-sides confident
  at the end. ~9h wall. Artifacts staged in `logs/Auras_gen/` (pooled.profile.json 167 MB → committed
  gzipped 7.1 MB; raw sidecar committed gzipped 4.7 MB for future pooling).
- **Analyzer keep gap (draw-weighted policy ceiling):** D_opt 4.036 vs D_static 4.467 = **0.431t**;
  static **over-keeps 33%** of hands. Projected keep-regret at R=40 ≈ 0.0006t (converged; R=20 already
  sufficient for keep — the R=25→40 rounds sharpen the R-sensitive **bottoming** argmin).

## Validation A/B (16 seeds × depths 0/3/5, 1000 games, budget-20)

Both gates PASS (negative = exhaustive wins earlier; all 16/16 seeds):

| gate | d0 | d3 | d5 | overall |
|---|---|---|---|---|
| **keep** (exhaustive vs static) | −0.176 | −0.252 | −0.248 | **−0.225t** |
| **bottom** (blind vs lookahead, `MTG_CONFOUND_BOTTOM`) | −0.070 | −0.043 | −0.043 | **−0.052t** |

The real in-game keep win is −0.225t (the 0.431t analyzer figure is the theoretical draw-weighted
ceiling). Confounded bottom confirms blind exhaustive bottoming ≥ clairvoyant lookahead once the peek
is removed. Auto-attach verified active (sidecar present −0.27/−0.38t vs `MTG_EXHAUSTIVE_PROFILE=none`
on the gen seeds).

Harness note: the A/B harness (`test/keepmodel_exhaustive_ab.sh`) needed a fix to tolerate an enabled
`value_play` block (its explicit `--depth` sweep is rejected otherwise) — `--ignore-play-profile` added,
commit `e74536a`.

## Adoption

Committed gzipped sidecars next to the deck (`decks/Auras/Auras.keepmodel.exhaustive.{profile,raw}.json.gz`).
Keep+bottom are presence-gated → active immediately for Auras; byte-identical for all other decks.
Note the GT tradeoff: blind bottoming shifts the *unconfounded* goldfish metric slightly (it rewards the
lookahead peek), an honest, deliberate shift — Auras is not in the regression suite so there is no GT to
rebaseline.
