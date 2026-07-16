# Hinata mulligan-profile generation with the escalation confidence-gate

**Status:** ready to run — threshold decided (T=0.70), gate config frozen, awaiting the
user to kick off the multi-week regen on the secondary machine.

**Purpose.** Hinata's exhaustive mulligan-profile generation is a multi-week job on the
slow secondary machine. The escalation confidence-gate (`MTG_ESCALATION_GATE`) is used
here as an **env-only generation accelerator** — it skips escalations predicted to be
no-ops, cutting wall-time. The *shipped* engine stays gate-off (no adoption, no GT
rebaseline); the gate touches generation only.

Companions: `escalation-and-rollout-cost.md` (the gate as lever #2, and why it's
deck-dependent), `hinata-profile-generation.md` / `exhaustive-keep-policy.md` (the
generation stage itself), `.claude/skills/mulligan-profile.md` (the handoff/merge
protocol and Rule 0).

## Why the gate fires during generation (and thus the speedup transfers)

`ExhaustiveKeep` rolls out at **d5 / budget20** (`ExhaustiveKeep.cpp:192`), and the
value-leaf hybrid escalates unverified lines during that search. So the gate — which
skips predicted-no-op escalations — fires during generation, and its wall-time saving
transfers directly to the job.

## The threshold decision: T=0.70

Measured on the **actual generation config** (Hinata d5/budget20, held-out seed 5005,
300g; `logs/eval/hinata_gate_ab.out`):

| T    | won/300 | LP     | skip% (of escalations) | wall  | vs OFF |
|------|---------|--------|------------------------|-------|--------|
| OFF  | 266     | 6.1133 | 0%                     | 79.8s | —      |
| 0.95 | 266     | 6.1133 | 4.2%                   | 77.5s | −2.9%  |
| 0.90 | 266     | 6.1167 | 16.1%                  | 74.5s | −6.6%  |
| 0.70 | 265     | 6.1233 | 77.7%                  | 65.2s | −18.3% |
| 0.50 | 265     | 6.1267 | 96.8%                  | 57.7s | −27.7% |

(The wall reduction, not the skip fraction, is what maps to job time: 0.70 ≈ −18%,
0.50 ≈ −28%.)

**Decision-safety (the reason a skip is acceptable for keep/bottom decisions).** Keep and
bottom are *ranking* comparisons (argmax/argmin over hands), not absolute EV. A gate that
degrades EV *uniformly* preserves rankings; only *combo-correlated* degradation corrupts
them. Stratified combo-bias check (gate-off vs gate-on, 1200 fresh-seed Hinata d5 games,
win-turn stratified):

| stratum    | T=0.70 Δ / newly-lost | T=0.50 Δ / newly-lost |
|------------|-----------------------|-----------------------|
| combo(≤4)  | **+0.0000 / 0**       | **+0.0000 / 0**       |
| 5          | +0.0144 / 0           | +0.0144 / 0           |
| 6          | +0.0191 / 1           | +0.0219 / 1           |
| slow(7-8)  | +0.0611 / 8           | +0.0873 / 12          |
| off-loss   | −0.0070 / 0           | −0.0070 / 0           |
| aggregate  | ~+0.022t, 9/1200      | +0.0275t, 13/1200     |

**Mechanism:** fast combo wins are *verified* (they win inside the search horizon) → no
escalation fires on them → the gate structurally *cannot* touch combo-hand rankings. All
degradation lands in slow/grindy games, in the "slightly slower win turn" direction —
which is monotonic with how the ranking already treats those hands, so it *reinforces*
rather than inverts the keep/bottom order.

**Why T=0.70 and not T=0.50:**
- **The knee.** 0.90→0.70 buys the big skip jump; 0.70→0.50 buys only the last −9.4pp of
  wall (7.5s) while pushing skip to 96.8% — escalation is then *effectively off*
  (near-pure value-leaf), losing the hybrid safety net for decision-critical artifacts.
- **Equal on the cases that matter.** combo(≤4) and stratum-5 are identical at both
  thresholds; T=0.50 only adds slow-tail degradation (+0.061→+0.087) and 4 more
  newly-lost per 1200 — for ~1.5 extra days on a multi-week job.
- **T=0.90** is the guaranteed-lossless floor (won unchanged over 6 seeds/1800g,
  dWins=−1; `logs/eval/hinata_t90_moreseeds.out`) if zero quality risk is wanted, but it
  only saves ~6.6%.

Landing spot: **T=0.70** for the run; **T=0.90** as the fallback if the confounded A/B
backstop (below) ever shows a flipped bottom decision.

## The frozen gate config

- **Weights:** `decks/Hinata2/Hinata2.escgate.json` (committed; a copy of the trained
  `logs/eval/hinata_escgate.json`). Trained by `scripts/esc_train_gate.py` on
  `MTG_ESCALATION_DUMP` rows from seeds 1001–4004 (8160 rows, 79% no-op, in-sample
  acc 0.789). Feature order = `[committed, gap, turn, est_wt] + 46 midgame feats`.
- **Invocation:** `env MTG_ESCALATION_GATE=decks/Hinata2/Hinata2.escgate.json \
  MTG_ESCALATION_GATE_T=0.70 <generation command>`.
- **CAVEAT — env not in the commit fingerprint.** The gate is env-only; it is **not**
  captured by `MTG_COMMIT`, which the raw sidecar stamps for cross-machine pooling. So the
  commit fingerprint alone will NOT catch a machine running with a different gate config
  (or gate-off). The **determinism handshake below is the only cross-machine mismatch
  catch** — do not skip it.

## Regeneration plan (secondary machine)

1. **Freeze the commit.** Both machines build from the exact same commit containing the
   gate infra (the `feat(escalation): confidence-gate…` commit). The raw sidecar stamps
   `MTG_COMMIT` and merge parity-rejects a mismatch.
2. **Ship the frozen gate config.** Both machines use the committed
   `decks/Hinata2/Hinata2.escgate.json` and `MTG_ESCALATION_GATE_T=0.70` verbatim.
3. **Determinism handshake FIRST — before weeks of compute.** On each machine run a small
   fixed batch (Hinata 50g, fixed seed, d5/budget20) with the gate on, and diff the
   verification/`V` dump byte-for-byte across machines. **No byte-match → STOP** (the
   sidecars won't pool). This is the only catch for the env-not-in-fingerprint gap above.
4. **Discard the gate-off work and restart from the same seed_base.** Gate-on rollouts are
   *not* byte-identical to gate-off (the gate skips escalations that sometimes move
   `win_turn`), so gate-on and gate-off sidecars cannot pool, and mixing the two policies
   *within one profile* corrupts cross-bucket keep/bottom comparability. Clean restart,
   gate on from bucket zero. (The in-flight gate-off job was <1 day in when this was
   decided — cheap to discard.) Seed allocation per the mulligan-profile handoff protocol
   (disjoint seed_base per machine).
5. **Confounded A/B backstop (mandatory).** After generation, run the confounded A/B
   (`MTG_CONFOUND_BOTTOM`, `KM_MODE=bottom`) on the finished profile to confirm the gate's
   rollout degradation did not flip bottom decisions. If it did, fall back to T=0.90 and
   regenerate. Optional gold standard: a low-R gate-off-vs-on *decision* diff (slow on
   Hinata).

## Provenance of the numbers

- `logs/eval/hinata_gate_ab.out` — the T-sweep wall/skip/won table (seed 5005, 300g).
- `logs/eval/hinata_gate_val.out` — validation on seeds 6006/7007.
- `logs/eval/hinata_t90_moreseeds.out` — T=0.90 losslessness over 6 seeds/1800g.
- `logs/eval/hinata_biascheck.out` (T=0.70) and `hinata_biascheck50.out` (T=0.50) — the
  stratified combo-bias checks.
  (These `logs/` files are gitignored; the tables above reproduce their content.)
