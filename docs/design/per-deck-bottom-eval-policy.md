# Per-deck bottoming-eval policy — fleet rollout (DEFERRED)

Status: **deferred** (recorded 2026-09-06). Melira Pod ships the policy; the fleet does not.
Owner of the go/no-go per deck: the user (each adoption moves GT).

## What exists (shipped 2026-09-06, commits 286b3948 + 8d113e92)

The clairvoyant bottoming rollouts in `AIEngine::HandleMulligan` (both the `MTG_BOTTOM_LEGAL`
removal-subset table and the per-candidate step loop) can run **two-stage**:

1. Stage 1 scores every legal removal subset / candidate with a **cheap** full-game rollout
   (`bottom_eval_depth` / `bottom_eval_budget_ms` override the play settings inside
   `BottomEvalScope` only).
2. Stage 2 re-rolls the **K cheap-best** (`bottom_eval_topk`) at the deck's REAL play settings,
   overwriting their scores. Cheap scores triage; searched scores decide.

Carrier: the deck profile's `mulligan` block (`bottom_eval_depth`, `bottom_eval_budget_ms`,
`bottom_eval_topk`) — parse + save round-trip in `MulliganProfileIO.h`, emitted only when set.
Env twins `MTG_BOTTOM_EVAL_DEPTH/_BUDGET/_TOPK` override the profile **only when explicitly
set** (A/B hatch; an env set to the "off" value forces legacy behaviour on a deck that ships a
policy). Everything unset ⇒ byte-identical legacy rollouts (verified: smoke 72/72
configs-changed 0, three builds).

## Measured result on Melira Pod (the adopter)

- Baseline d3 ×50 games serial: 613 s, avg 4.9600.
- `depth0` alone: 245 s but avg 5.0200 (3 mull games +1 turn — greedy trials misrank).
- `depth0 + topk5`: **405 s, avg 4.9600, all 50 win turns identical** to full fidelity.
- Worst game (s1033 "gi32", a mull-2 keep → 21 subset playouts): 289 s → ~15 s.
- 299-ref viewer gate profile-driven: 0 play-drift, 0 enum-gap, 0 mull-drift.

## Why the fleet is NOT flipped

Forcing `depth0+topk5` on every deck (env, smoke A/B 2026-09-06) moved **6 smoke cases** —
fluctuator play changes at same score, 12 cases slower by loss-penalized score. Bottoming
choices are deck-quality-sensitive; the two-stage refine that is exactly win-turn-neutral on
Melira is not automatically neutral elsewhere. So this is a per-deck adoption, never a default.

## The prize (why this doc exists)

Decks **without** an exhaustive keep/bottom table pay full-game rollouts per candidate per
bottom step at play settings. Measured shares of total runtime:

- FiveColour: **90.4%** of runtime is bottoming (callgrind, 2026-08-10 — the
  `MTG_BOTTOM_ROLLOUTS` comment in AIEngine.cpp).
- Fluctuator: **~92%** (units A/B 2026-09-06: 9.1 → 0.69 s/game at d3 b20 with rollouts off;
  rollouts earn −0.08 turns vs the blind heuristic, so plain "off" is not free).
- Melira (before adoption): ~55% of the d3 suite-set cost.

A per-deck `depth0 + topk K` policy plausibly keeps most of the −0.08t quality at a fraction
of the cost. Decks WITH exhaustive tables short-circuit this code entirely and are unaffected.

## Per-deck rollout recipe (when picked up)

1. Baseline: deck's suite-config game set with `--log-dir`, record per-game win turns.
2. Arm: same set with `MTG_BOTTOM_EVAL_DEPTH=0 MTG_BOTTOM_EVAL_TOPK=K` for K in {3,5,7}.
3. Adopt the smallest K that is **win-turn-identical per game** (Melira standard). If no K
   reaches identity, surface the (avg delta, speedup) trade to the user instead of adopting.
4. Ship in the deck's profile.json `mulligan` block; re-run the deck's suite cases + refs;
   GT re-accept for that deck's keys (bottoming changes = play changes for mull games).
5. Note: `bottom_eval_budget_ms` exists for a depth>0 cheap stage, but Melira measured d1 as
   no cheaper than d3 (the FullSearchLine ladder dominates at any depth>0); depth 0 (greedy,
   budget-irrelevant) is the useful stage-1 setting until someone measures otherwise.

## Related pending item

`test/viewer_protocol_check.py`'s "shuffle-dead" class mislabels board-divergent replays
(user 2026-09-06: the category shouldn't exist; Fluctuator has no shuffle effects at all).
`Fluctuator/claude_s10_gi9` is the standing example — predates the 2026-09-06 perf work.
Rename/split the class + bisect the divergence when picked up.
