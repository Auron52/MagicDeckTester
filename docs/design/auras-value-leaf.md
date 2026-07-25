# Auras value-leaf model — GENERATED + ADOPTED (provisional)

2026-07-24. Built the learned value-leaf for the **Auras** (Bogles hexproof-aura aggro) deck — the
last suite-adjacent deck without one. Generated late, on a frozen commit, **after** play was
validated: `ref_bench` showed the shipped search at human parity/better on the 16 hand-played
`references/Auras/` games (no hand it falls short on in shipped/clairvoyant config). Follows the
canonical value-leaf pipeline (`learned-d0-policy.md`, "Leaf VALUE model") and the aggro precedent
(burn/slivers/knights).

## Archetype expectation (met)

Auras is **board-driven aggro** — the win is decided by the visible board + hand (hexproof creature +
stacked auras), not library uncertainty. Per `learned-d0-policy.md`, the value leaf is therefore
**inert on quality** for this archetype (like burn/slivers) and its payoff is a **search SPEED win**,
not a win-turn improvement. Confirmed below.

## Pipeline

1. **Value rows** (`MTG_DUMP_VALUE_ROWS`, position-labeled = de-clairvoyed searched win turn, K=8):
   `--games 2500 --seed 8008 --depth 3` → **11,141 rows** (`logs/eval/auras_value.rows`; sorted →
   `auras_value.sorted.rows` for reproducible training since the dump is multi-threaded).
2. **Train** regression GBDT (`train_eval_gbdt.py --regression`, 120 trees, depth 4, min-leaf 20):
   train RMSE 1.16 → **0.69 turns** → `decks/Auras/Auras.value.json` `eval_model`.
3. **Depth matrix** (`valueleaf_depth_matrix.py --incremental`, 50-game batches, resumable):
   `--hdepths 1 2 3 --vdepths 1 2 3 4 5 --seeds 8008 9009 --games 400`. PROVISIONAL (2-seed/400g,
   hdepths capped at 3 — the heuristic converges by d3 for aggro; deep unbounded H is expensive and
   uninformative here). Log: `logs/eval/valueleaf_depth_auras.txt`.

   | | d1 | d2 | d3 | d4 | d5 |
   |---|---|---|---|---|---|
   | heuristic LP | 4.4863 | 4.4800 | 4.4700 | — | — |
   | value-leaf LP | 4.6275 | 4.5288 | 4.4938 | 4.4725 | 4.4712 |
   | leaf cost | 1.7ms | 5.3ms | 23ms | 75ms | 272ms |

   V5 − H_conv(=H3=4.470) = **+0.0012 < tol 0.002** → the leaf ties heuristic quality at d5.
4. **Gate** (`valueleaf_table_to_metadata.py`): `value_trust_depth=5`, `value_no_fallback=False`,
   `value_fallback_crossover` (c→take@hc: 1→1 2→1 3→1 4→3 5→3). Identical profile shape to slivers
   (the closest archetype twin, also trust=5). Trust=5 is within 2-seed noise of UNSET; the adoption
   A/B (not the matrix) is the real gate.

## Adoption A/B — PASS (quality-neutral + faster)

- **Quality** (shipped regime d5/budget-20, 400g/seed, value OFF vs ON): Δ(on−off) = **−0.0075
  (s2002), −0.0050 (s3003), 0.0000 (s7007); mean −0.0042t** — neutral-to-slightly-better, none worse.
- **Speed** (d5 unbounded, 40g): heuristic-rollout leaf 30.5s → value leaf 4.45s = **~6.9× faster**.

Shipped `value_play`: `{target_depth:5, budget_ms:20, enabled:true, regime:"light",
escalation_cap:5}`. **Adopted default-on** — Auras is NOT in the regression suite, so there is no GT
to rebaseline; the leaf is validated neutral. The byte-identical escape hatch is
`--ignore-play-profile` (`MTG_VALUE_MODEL=0` alone does not override an enabled `value_play` block).

## Binary-freshness correction (important)

The first pass ran on a binary built **before** commit `f408f64` (same-turn creature→aura
enumeration, −0.02t on Auras). The whole pipeline was **regenerated on a `build.sh`-fresh binary**
that includes it; the depth matrix and adoption A/B came out **essentially identical** (H3=4.4700,
V5=4.4712, V5−H_conv=+0.0012; A/B mean −0.0042t; ~6.8× unbounded) — so the value-leaf result is
robust to that change (which affects specific aura plays, not the aggregate depth-matrix LP). The
committed model is the fresh-binary one. NB: on the fresh binary the value-rows dump exposed a
**slow same-turn-aura search on some complex Auras boards** (one dump game's single decision ran
K=8 `EnumerateEarliestWins` for >100s) — a perf item worth a look, unrelated to the value-leaf.

## Caveats to revisit

- PROVISIONAL 2-seed/400g table, hdepths capped at 3 → trust=5 is within noise of UNSET (escalate-
  everywhere). If a future run wants precision, extend to hdepths 1–5 + more seeds (cheap value arm).
- Not independently reference-validated for the *leaf* lines (the references validated the heuristic
  search play); the A/B neutrality is the adoption evidence.
- Regenerate: rows via the Phase-1 dump above; model via `train_eval_gbdt.py --regression`; table via
  the incremental matrix + `valueleaf_table_to_metadata.py --decks auras`.
