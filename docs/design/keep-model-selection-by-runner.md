# Keep-model selection must use the real runner, not held-out regret

Self-contained finding + the harness that acts on it. Relevant to anyone training keep-models
(incl. on a second machine). No external notes required.

## The bug: we selected keep-models on a proxy that doesn't transfer

The analyzer's keep-model trainer ([`src/analyzer/KeepModelTrainer.cpp`](../../src/analyzer/KeepModelTrainer.cpp))
optimizes and **selects** models by **held-out mean turn-regret** against per-hand `kv` labels
(the accuracy bar: `always-keep / always-mull / deep-tree / static / oracle`). That number is a
per-hand proxy — "does the model's keep/mull agree with the `kv`-oracle on each sampled hand in
isolation." It is **not** how a keep policy accrues value in a played-out game.

Note also that the trainer's "policy-simulated baseline" (`baseline_mode=policy`, `simulate_V`)
is **not** a real game sim — it is backward induction over the *same* `kv` table
(`keep ? kv[p][m] : V[p][m+1]`), used only as the label *baseline*, never as the model selector.
So everything the trainer optimizes lives in the `kv`-label world. The real-game A/B was always a
separate, manual, after-the-fact check that the shipped model was never selected to satisfy.

## Evidence it doesn't transfer (slivers, 2026-07-02)

Held-out mean regret (turns), same test split, `MTG_KEEP_ROLLOUTS` = label averaging:

| labels | always-keep | always-mull | deep-tree | static | oracle |
|---|---|---|---|---|---|
| R=1 (1 rollout, noisy) | 0.285 | 0.311 | 0.136 | 0.131 | 0 |
| R=8 (blind-averaged)   | 0.195 | 0.284 | **0.032** | **0.060** | 0 |

Two findings:
1. **Label noise was huge.** R=1 → R=8 dropped every regret ~50–75%. Every keep-model we had
   trained used R=1 (the default) — i.e. single-rollout-noise-dominated labels.
2. **At clean labels the learned tree "beats" static on held-out regret** (0.032/0.046 shippable
   vs static 0.060) — but the **in-game A/B did not follow**:

In-game A/B, clean-label (R=8) tree vs static, 1000 games × seeds 4004–7007:

| depth | mean Δ win-turn (keep − static) | read |
|---|---|---|
| d0 | **+0.026** (all 4 seeds worse) | keep clearly worse |
| d3/d5 | ≈ +0.001 (mixed) | wash |
| overall | **+0.009** (keep worse) | ≈ the pre-clean-label result |

So a ~23% held-out-regret "win" produced a **wash-to-worse** in-game. Held-out regret — even
perfectly de-noised — is not a faithful proxy for in-game win-turn. (Why: small keep diffs are
masked/recovered by search at d3/d5; `kv` labels are depth-5 rollouts, mispricing d0; and a model
fit to minimize the metric has home-field advantage over a fixed static policy on that metric.)

## Consequences

- **Do NOT do a blanket R≥8 retrain sweep expecting held-out gains to transfer** — they don't.
  Clean labels fix the metric, not the game.
- **Validate every keep-model by an in-game A/B**, never by held-out regret. This includes
  re-checking the decks already adopted (antilife/burn/hinata) with the honest selector.
- The expressive/per-card model idea is only worth pursuing *after* selection is honest, and must
  itself be validated in-game.

## The fix: select by the real runner (`test/keepmodel_select.sh`)

Path B makes the trainer a candidate **generator** and the real runner the **selector/gate**:

1. One `MTG_KEEP_SPLIT=both` fit builds the (expensive) blind-averaged `kv` table **once** and
   emits every form from it — gini-tree (primary `.keepmodel.profile.json`) + `regret` / `score`
   / `hybrid` side files.
2. A/B each candidate **and** the committed static with `./build/Release/mtg` (same engine that
   plays real games — no train/serve mismatch), across d0/d3/d5 × seeds 4004–7007.
3. Rank by **mean in-game win-turn**; adopt the best **only if it beats static**, else keep static.

```
KM_DECK=decks/slivers_vial.txt KM_ROLLOUTS=8 KM_HANDS=4000 bash test/keepmodel_select.sh
```

Output + per-form ranking land in `logs/keepmodel_select/<stem>/REPORT.txt`. This is the honest
objective; it will confirm-or-refute adoption per deck without the proxy in the loop.
