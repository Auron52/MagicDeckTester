# Overnight rebaseline audit — 2026-07-11 (value-leaf fallback + phase-1-2 catch-up)

The overnight GT was doubly stale (pre-value-leaf-adoption **and** pre several phase-1-2 play
changes — proven by 35 **d0** win→loss, which the search-only value-leaf cannot cause). It was fully
rebaselined on `phase-1-2-deck-analyzer` alongside the value-leaf depth-aware fallback. This note records
the two findings the audit surfaced that are **not** closed, so they aren't lost.

Isolation method: value-OFF (`MTG_VALUE_MODEL=0`, byte-identical to the pre-value-leaf heuristic) vs
value-ON, both run as `--batch` over the committed overnight manifest (no viewer tests), diffed per game
against the old GT. Drivers: `scripts/valueleaf_fallback_ab.py`, `scripts/valueleaf_calibrate_trust.py`.

## 1. BURN overnight regression — phase-1-2's, for the other agent

The burn mulligan-profile + engine changes on phase-1-2 make burn **+0.0679 mean LP WORSE** on the
overnight seeds (loss=9 avg win turn): old-profile GT mean **4.318** → new **4.386**. Win rate is flat
(~99.4%), but the deck wins **~0.068 turns later** on average — every config shows far more *later* than
*earlier* games (e.g. `burn_overnight_d3_s7007`: 133 later vs 47 earlier). **This agent's value-leaf is
inert on burn** (value-on LP ≈ value-off LP to 4 dp), so it is entirely the phase-1-2 burn change. It was
likely missed because it was validated on the non-overnight seeds only (the overnight seeds 4004/5005/6006/
7007 are disjoint). The 21 "burn win→loss vs old GT" in the audit are just the tip; the real signal is the
broad LP slowdown. **Owner: the agent who adopted the burn profile.** The overnight GT now bakes this in
(a faithful snapshot of current behavior) — re-examine + fix the burn regression, then re-accept burn's
overnight GT.

## 2. ANTILIFE +0.0028 — value-leaf remaining-budget escalation (this agent's follow-up)

The depth-aware fallback escalates an unverified value-leaf line to the heuristic on the **remaining** shared
budget. The value-leaf pass spends part of the node budget first, so the heuristic escalation's start-gate
can't always **finish the depth level it needs** — on 4 antilife d5 knife-edge games (T5–T6 wins) it commits
a hair shallower and the win tips to a loss. Net over all decks the change is **mean dLP = +0.0000**
(antilife +0.0028, burn/knights/slivers/th better-or-tie, hinata byte-identical), so it shipped as-is.

**Deferred fix:** give the escalation a **fresh/full budget** (as the pre-fallback hybrid's redo did) instead
of the remaining budget — the escalation is the quality path, so starving it is counterproductive. Trade-off:
it "double-spends" budget on escalated decisions (value-leaf pass + full heuristic pass), but the value-leaf
pass is cheap (free leaves). Expected to recover the antilife knife-edge wins with no downside at generous
budgets. Requires a smoke/regression + overnight re-rebaseline. See `FullSearchLineHybrid` in
`src/ai/TurnSolver.cpp` (the `budget` arg passed to the escalation `FullSearchLine`; change to a fresh
`SearchBudget::FromVirtualMs(budget_ms)`).

## Not blocking (recorded)

- **fd-diverge severe:** 2 games (seeds 6225/6303, realized T6 vs predicted T4) are **pre-existing** — they
  appear identically in the value-OFF arm, so they are a phase-1-2 rollout-optimism, not the value-leaf.
  61–62 off-by-one fd-diverge (the known minor optimism) + these 2 severe; `nonconv=0` on both arms.
  Left for the other agent (does not block value-leaf adoption).
