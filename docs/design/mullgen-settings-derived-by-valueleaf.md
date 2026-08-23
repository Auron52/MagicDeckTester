# Derive the mulligan-gen setting in the value-leaf pipeline (proposed)

**User proposal (2026-08-15):** add a step near the END of the value-leaf run that fills the play
profile's mulligan-GENERATION setting in automatically, "based on both performance and what we
consider a reliable setting" — because setting it by hand "is trickier and unnecessary in the grand
scheme of things."

Status: **IMPLEMENTED 2026-08-15** in `scripts/attic/valueleaf_table_to_metadata.py` (the phase-D
deriver that already ships `value_trust_depth` / `value_fallback_crossover` / `escalation_cap`), with
`--no-mullgen` to opt out.

Validated by driving the REAL `write_deck` with synthetic tables (no matrix log exists on this box --
they are per-run and gitignored), covering all four cases: trusted-at-play-depth drops the override;
untrusted derives the converged depth and PRESERVES an existing budget; an absent budget stays absent;
and a truncated ladder under `--allow-partial` changes nothing. **Still owed: one real value-leaf run
end-to-end**, since the synthetic check exercises the rule and not the log-parsing path into it.

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

---

## 2026-08-23: the step was wired, and inert for the decks that needed it most

The step shipped as **phase F** of `scripts/valueleaf.sh` (`c2eee2a1`, 2026-08-15) and, per the
disproof in `mullgen-setting-is-a-trust-question.md`, delegates to the MEASURED deriver
(`derive_mullgen_setting.py`) rather than reading the setting off the depth table. That much worked.

It nonetheless produced nothing for **KittyEquipment**, whose value leaf was adopted a week later
(`d33dfe75`, 2026-08-22) and which then generated mulligan labels at the built-in **d5/b20** default
— 4.44x more expensive than the measured d3/b3 — until it was found by hand while scoping an
overnight run.

Two defects compounded, and neither was a missing feature:

1. **The deriver refused the case.** It exited on `value_play.target_depth/budget_ms unset --
   nothing to reference against`. But a deck with no `value_play` is not a deck with no play policy:
   `ResolvePlaySettings` falls through to `MulliganProfile::BuiltinDefaultPlay()` (d5/b20), which is
   a perfectly good reference — and is exactly what a human typed by hand for Knights and Mirrorwing
   ("against the deck's real play policy d5/b20 = BuiltinDefaultPlay, since this deck ships no
   explicit value_play block"). So the tool refused precisely the decks with the most to gain,
   because *inheriting the default* is the condition that makes generation expensive in the first
   place. It now resolves that default itself, parsing it out of `src/ai/MulliganProfile.h` rather
   than keeping a Python copy that could drift.

2. **Phase F swallowed the exit code.** `mullgen_finalize.py` reported the failure correctly
   (rc=1), but the call site is `python3 ... | tee -a "$VLQ/driver.log"`, and a pipeline's status is
   `tee`'s. `mark F_mullgen` then ran unconditionally, so the phase was recorded done and the run
   reported success. Phase F now reads `PIPESTATUS[0]`, names the failing decks, and refuses to mark
   itself — so a re-run retries instead of skipping.

**The lesson for the "Validate before wiring in" list above.** Every case on that list was
validated, and the doc still notes what was owed: *"Still owed: one real value-leaf run end-to-end,
since the synthetic check exercises the rule and not the log-parsing path into it."* That is exactly
the gap this fell into. The three listed cases all assume a deck that HAS a `value_play` block; the
case that broke — a deck with none at all — was never on the list, and a synthetic fixture will
always have whatever fields the fixture author thought to include. A step that writes config for
every future deck needs at least one test against a deck in its **shipped** state, not a
constructed one.
