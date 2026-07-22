# Dragonstorm payoff-prune (Hook 29) — hold rituals until a payoff

**Status: ADOPTED — default ON for Dragonstorm (opt-out `MTG_UNPRUNE=payoffprune` / `MTG_UNPRUNED`),
2026-07-22.** Shipped on the BASELINE (fake-red) model; smoke + regression GT rebaselined. The Unclaimed
`colored_creature_only` fix that surfaced this is **DEFERRED** (held for user game/log review) — the prune
is validated as a win *independent* of it (see "Shipping-model measurement" below). See also
[ritual-payoff-guard-stormgate](../../) (the committed d0/leaf surplus guard) and
[hinata-spasm-gate-rootcause.md](hinata-spasm-gate-rootcause.md) (the Hinata analog).

## The problem

Faithfully restricting Unclaimed Territory (`colored_creature_only`, see
[unclaimed-territory-restricted-mana.md](unclaimed-territory-restricted-mana.md)) removes the *fake red*
the old model let it tap for rituals. That is correct, but it surfaced a **+0.03–0.08 turn "regression"**
at d3/d5 that is NOT an honest cost:

- Per-game budget escalation on the d5 slower games: ~half (gi91/130/166, and gi79) **recover to the
  baseline win turn when given 4×–16× search budget** — the fast line still exists, the fixed-budget
  search just runs out of nodes before reaching it.
- Why starved? The restricted model opens up *more* one-turn ritual-accelerant branches, and the search
  burns its budget enumerating/​rolling them out instead of the payoff lines.

The greedy/leaf ritual guard already committed (surplus test in `Solve::consider`) shapes the leaf but
does NOT prune the **search branch list** (`EnumeratePlans`), so the wasteful accelerant branches are
still expanded.

## The rule (user's spec)

> Allow lines that cast a **Dragon, Dragonstorm, or Apex**, and prune the other one-turn accelerant lines.

A mana ritual is a ONE-TURN accelerant: its float empties at end of turn (mechanically identical to
Hinata's Reality Spasm untap — extra mana for exactly that turn) and storm count does not carry across
turns. So a plan that casts a ritual (`ritual_float > 0`) but no **payoff** —
- a Dragon (`Card::IsCreature()` — every creature in the deck is a Dragon),
- Dragonstorm (`tutor_to_battlefield`), or
- Apex of Power (`impulse_exile > 0`)
— burns the ritual for nothing: Dragonstorm has no cantrip/dig to sink the extra mana into. Prune it.

A ritual-only subset deals no damage, so it can never be lethal → pruning never drops a winning line.

## Why it helps here but was ~neutral on Hinata

The **same idea** as the Hinata spasm gate. There the final (soft) form was essentially *neutral*
(+0.019 ≈ noise) with no perf payoff, because Hinata's ritual IS a useful accelerant — its float powers a
bigger **cantrip/dig** turn, advancing toward the combo. Dragonstorm has **no such mana sink**: a ritual
with no same-turn payoff is pure waste. So the strict "no payoff → prune" form, too aggressive for Hinata,
is exactly right here — and it converts Hinata's *neutral* into a real *win*.

## Implementation

- `DecisionProvider::PrunesAcceleratorWithoutPayoff()` (Hook 29) — base `false`; `DragonstormProvider`
  overrides `true`. Scopes the prune to Dragonstorm (Hinata/others untouched even flag-on).
- `SubsetWastesAccelerant(cands, sel)` (TurnSolver.cpp) — `has_ritual && !has_payoff` as above.
- Applied at BOTH `Solve::consider` (leaf) and `eval_and_push` (the `EnumeratePlans` search branch list —
  where the freed budget comes from), gated by `DragonPayoffPruneEnabled()` (`MTG_DRAGON_PAYOFF_PRUNE`,
  read once). Default off → both callsites short-circuit before touching the helper → byte-identical.

## Measurement (regression/train seeds 2002/3003 + smoke 1001)

One binary, Unclaimed-fix cards; conditions: **A** = baseline cards + prune off (== committed GT),
**C** = fix cards + prune off (the "regression"), **B** = fix cards + prune on (proposal).

| case | A: GT | C: fix, no prune | B: fix + prune | C−A | **B−A** |
|------|-------|------------------|----------------|-----|---------|
| d0 s2002 | 7.159 | 7.157 | 7.113 | −0.002 | **−0.046** |
| d3 s2002 | 4.843 | 4.897 | 4.750 | +0.053 | **−0.093** |
| d3 s3003 | 4.830 | 4.857 | 4.753 | +0.027 | **−0.077** |
| d5 s2002 | 4.716 | 4.792 | 4.684 | +0.076 | **−0.032** |
| d5 s3003 | 4.784 | 4.852 | 4.740 | +0.068 | **−0.044** |
| **mean** | 5.922 | 5.949 (**+0.028**) | **5.867 (−0.055)** | | |

- **Quality:** B is faster than the committed GT on **every** case (mean −0.055) — the prune erases the
  Unclaimed regression (+0.028) and improves past it. Per-game gi91/130/166 recover to baseline; gi79
  recovers to T3 (which even 16× budget did not reach — focusing beats brute-forcing).
- **Perf (deterministic rollout-stats, 100g d5 s2002, single-thread):** rollout calls **2.17M → 1.26M
  (−42%)**, turn_steps 4.26M → 2.28M (−46%), interior nodes 228K → 193K (−15%). A real perf WIN (fewer
  branches → fewer rollouts), same shape as Hinata's 2.7×. NOTE: multi-threaded *wall* times looked
  slower — a CPU-contention artifact (concurrent polling); trust rollout-stats, not wall time.
- **Byte-identical (flag off):** all 5 A-condition digests match committed GT exactly.
- **No leak (flag on):** Hinata smoke d0/d3/d5 byte-identical to GT — cleanly Dragonstorm-scoped.

## Shipping-model measurement (BASELINE / fake-red, Unclaimed held) — what was actually adopted

Since Unclaimed is deferred, the prune ships on the current fake-red model. Prune OFF (== committed GT)
vs ON, cards.json as committed (no Unclaimed flags). OFF reproduces GT **exactly on all 8 cases**:

| case | OFF (= GT) | ON (prune) | ON−OFF | worse/better |
|------|-----------|------------|--------|--------------|
| smoke d0 s1001 | 7.271 | 7.257 | −0.014 | 27/40 |
| smoke d3 s1001 | 4.793 | 4.740 | −0.053 | 3/8 |
| smoke d5 s1001 | 4.800 | 4.800 | +0.000 | 2/2 |
| regr d0 s2002 | 7.159 | 7.114 | −0.045 | 29/48 |
| regr d3 s2002 | 4.843 | 4.700 | −0.143 | 1/36 |
| regr d3 s3003 | 4.830 | 4.713 | −0.117 | 1/31 |
| regr d5 s2002 | 4.716 | 4.652 | −0.064 | 3/18 |
| regr d5 s3003 | 4.784 | 4.700 | −0.084 | 1/19 |
| **mean** | 6.251 | **6.197** | **−0.055** | |

Faster on 7/8 (smoke d5 neutral), mean −0.055 — matching the fix-model and held-out results. So the win is
independent of the Unclaimed model. Worse-game counts are tiny (1–3 at d3/d5) vs 18–48 better.

## Held-out validation (overnight seeds 4004–7007) — GENERALISES

`scripts/dragon_payoffprune_heldout.sh` (C vs B on the unseen overnight sweep, both fix cards). Every one
of the 12 cases is faster with the prune; the held-out mean (−0.051) matches the train result (−0.055):

| depth (4 seeds) | C: no prune | B: prune | B−C |
|-----------------|-------------|----------|-----|
| d0 | 7.181 | 7.152 | −0.029 |
| d3 | 4.893 | 4.770 | **−0.123** |
| d5 | 4.843 | 4.761 | −0.083 |
| **mean** | 6.522 | 6.470 | **−0.051** |

Biggest win at d3 (tightest budget headroom → worst starvation). No case regresses. The win generalises.

## Adoption (DONE, 2026-07-22) — prune only; Unclaimed deferred

Per the user's call ("adopt prune, hold Unclaimed — revisit Unclaimed after reviewing games/logs"):

1. **Default on for Dragonstorm**, gated by `!DecisionUnpruned(UnprunedGate::PayoffPrune)` — the provider
   (`DragonstormProvider::PrunesAcceleratorWithoutPayoff`) scopes it; `MTG_UNPRUNE=payoffprune` (or global
   `MTG_UNPRUNED`) reverts to the full branch set for the standing audit. No per-flag env.
2. **Unclaimed `colored_creature_only` flags NOT adopted** — reverted to the fake-red model. The prune is a
   win independent of it (see shipping-model measurement). Unclaimed adoption is deferred pending the
   user's game/log review; the engine wiring stays committed-inert and its proof is preserved in
   [unclaimed-territory-restricted-mana.md](unclaimed-territory-restricted-mana.md).
3. Smoke + regression GT rebaselined (only the 3 Dragonstorm smoke + 5 regression cases change; every
   other deck byte-identical). Overnight GT now stale (deferred).
4. Committed: `DecisionProvider.h`, `DecisionProviders.{h,cpp}`, `TurnSolver.cpp`, this doc, and the A/B
   scripts (`scripts/dragon_payoffprune_ab.sh`, `dragon_payoffprune_heldout.sh`,
   `dragon_prune_baseline_ab.sh`). cards.json NOT touched.
