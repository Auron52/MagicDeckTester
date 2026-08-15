# Derive the mulligan-gen setting in the value-leaf pipeline (proposed)

**User proposal (2026-08-15):** add a step near the END of the value-leaf run that fills the play
profile's mulligan-GENERATION setting in automatically, "based on both performance and what we
consider a reliable setting" — because setting it by hand "is trickier and unnecessary in the grand
scheme of things."

Status: DESIGNED, not implemented. The rule below is derivable from data the pipeline already has;
what is missing is one real value-leaf run to validate it against before letting it auto-write config
for every future deck.

## Why this is now safe (and was not, before 2026-08-15)

`mull_gen_depth` / `mull_gen_budget_ms` used to feed equivalence DISCOVERY as well as the label
rollouts, so writing them changed the BUCKETS — and hand count grows as `C(K+6,7)`. Auto-deriving
them then would have silently re-bucketed decks, and a "cheaper" pick could have cost far more
(measured on slivers: K 10 -> 13 at gen d2, 14,117 -> 61,001 distinct hands).

`fix(keepgen): discovery runs under PLAY settings` split those apart. `mull_gen_*` is now a PURE COST
KNOB, which is precisely the property that makes deriving it automatable. **That split is a
prerequisite for this feature, not an unrelated cleanup.**

## Where it attaches

`scripts/valueleaf.sh`, the phase-D/E deriver that already writes `value_play` from the matrix —
the same heredoc that ships `escalation_cap = min(target_depth, max(value_leaf_table.hdepths))`.
That deriver is the precedent: a measured, clamped, matrix-derived `value_play` field. This adds a
second one of exactly the same kind, so it needs no new data and no new phase.

## The rule

**1. Deck has a TRUSTED leaf at its shipped play depth → emit NO `mull_gen_*` (inherit play).**

Condition: `value_leaf_table` present AND `value_trust_depth > 0` AND `value_trust_depth <= target_depth`.

A trusted leaf is exactly the thing that makes generation at play settings cheap *without* giving up
quality — the rollouts stop at the leaf instead of playing on. Overriding such a deck down to a cheap
`d3 b3` throws that away and generates labels under a policy the deck does not use. User, 2026-08-15:
*"we are not planning to run decks that have a fast trusted leaf under d3 b3 ... Slivers and Knights
especially should take advantage of the trusted leaf."*

**2. Otherwise → emit the cheapest RELIABLE depth from the measured ladder.**

Condition: no `value_leaf_table` (matrix never completed), or trust unset/deeper than play.

`mull_gen_depth` = the shallowest measured heuristic depth at which `heuristic_lp` has CONVERGED
(gain over the next rung below the noise floor) — the same convergence quantity `escalation_cap`
already reads. That is the "reliable" half: shallower than convergence is a policy that measurably
misranks hands; deeper buys nothing and costs the whole point of the override.

`mull_gen_budget_ms` = the measured per-decision budget at which that depth's cost is stable
(`value_play.leaf_cost_ms` / `heur_cost_ms` are already recorded per depth), not a hand-picked
constant.

**3. Emit the derivation into the `note` field**, the way the existing hand-set notes do, so the
setting is self-explaining and a later reader can tell a derived value from a human's guess.

## What the rule says about today's decks

| deck | model | trust | leaf table | current `mull_gen` | rule says |
|---|---|---|---|---|---|
| slivers_vial, Knights, Auras, burn | yes | 5 | yes | (inherits play) | **unchanged** — already correct |
| Anti-Lifegain, Dragonstorm, Hinata2, treasure_hunt | yes | — | yes | (inherits play) | unchanged |
| **FiveColour** | yes | 6 | yes | **d3 b3** | **DROP the override** (rule 1) |
| **Goblins** | yes | 6 | yes | **d3 b3** | **DROP the override** (rule 1) |
| Creature Giving | **no model** | — | no | d3 b3 | KEEP (rule 2) — nothing to trust |
| Mirrorwing Dragon | yes | — | **no** | d3 b3 | KEEP (rule 2) — matrix never completed |

Two things fall out of that table:

- **FiveColour and Goblins are the live discrepancies.** Both have a leaf trusted at exactly their
  shipped play depth (6) and yet override generation down to d3/b3, which is the case the user's
  rule says should not exist. Their overrides predate the trust work. Changing them is a measurable
  decision, not a cleanup — left for the user.
- **Mirrorwing's `d3 b3` is a SYMPTOM, not a choice.** It has an `eval_model` but no
  `value_leaf_table` because its matrix phase never finished (see
  `mirrorwing-valueleaf-third-machine-handoff.md`). Completing that matrix would move it to rule 1
  on its own. So this deriver would also make the cost of an unfinished matrix explicit instead of
  leaving it as a hand-set constant nobody re-examines.

## Validate before wiring in

This writes config for every future deck, so it should not ship on reasoning alone:

1. Run it in REPORT-ONLY mode on one deck with a completed matrix (any of slivers/Knights/burn) and
   confirm it derives "no override" — i.e. it reproduces the setting a human already chose.
2. Run it on Creature Giving (no model) and confirm it derives an override rather than crashing on
   the missing table.
3. Only then let it write.

The failure mode to guard: a deck whose matrix is PARTIAL (table present but thin) must fall to
rule 2, not read a truncated ladder as convergence. Prefer "emit nothing and say so" over emitting a
setting derived from an incomplete measurement.
