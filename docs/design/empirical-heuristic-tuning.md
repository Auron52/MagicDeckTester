# Empirical / AI-authored tuning of decision heuristics

**Status:** deferred idea (not being built yet). Self-contained; an implementing agent
can build from this directly.

## The dividing line: searched vs. heuristic decisions

The engine **searches** the decisions it can afford to during play (the depth/budget rollouts
that pick lines, casts, targets). But many decisions **cannot be searched during play** — they
would explode the branching factor or blow the time budget — so they are resolved by a fast
**heuristic** instead. Examples: *which* of several equivalent mana sources to tap
(`ManaSourceRank`, the scarcity-first greedy), cast ordering, block/attack shortcuts. The tap
order is never enumerated per-decision by the in-play search; it is a hand-written rule.

The organizing principle: **anything we cannot search during play, we must author a heuristic
for — and AI should contribute significantly to authoring those heuristics (both their
*structure* and their *parameters*) and then confirm them with empirical results.** The human's
role shifts from inventing every rule/constant to **reviewing** what the AI + the harness decided.

## Motivation

Today, the heuristic side is **hand-authored guesses** — e.g. `ManaSourceRank` (the scarcity-first
tap order in `DecisionProviders.cpp`) and the `AntiLifegainProvider` Grove drip nudge (`+1` when
no Remedy, `-1` under one). A human notices a modeling gap, proposes a specific number/sign, and
the regression suite confirms it helps. That +1/-1 nudge is exactly the kind of magic constant we
would rather **not** ask a person to invent.

The goal: the user stops having to supply the *ideas*. The structure *and* the values of these
heuristics are **AI-authored and empirically validated**, with the human as a **reviewer** of what
was decided (see "Report-back"), not the source of every tweak. This applies not just to tuning
an existing knob but to **proposing new heuristics** for un-searchable-during-play decisions and
letting the harness score them.

## What "searched" means here

Not an in-play, per-decision search (no MCTS at the table). It is an **offline meta-search over
the parameter space at authoring time**: try different values of a knob, measure how each
performs across a battery of games, keep the winner, bake it in as the new default. The played
game stays exactly as fast/deterministic as today; only the *authoring* step gains a search.

## Objective

Reuse the regression fingerprint the suite already produces per case: `games_won /
avg_win_turn`. For a goldfish deck, win% saturates near the top, so **`avg_win_turn` is usually
the sharper signal** (kill faster). Combine into a scalar per candidate config — e.g. win% as
primary, avg-win-turn as tiebreak — or keep a small Pareto set and let the report-back (below)
choose. Score **per deck**, because the optimal ranks differ by archetype (this is why the tuned
values belong in the archetype providers, not the root — see "Scope by provider").

## The harness already exists

Most of the machinery is in place — this is largely wiring, not new infrastructure:

- **The regression suite is already an A/B harness** (see `.claude/skills/regression-testing.md`):
  run the same seeds under configuration A vs B and diff the fingerprint.
- **Deterministic seeding** makes two configs directly comparable game-for-game.
- **Play digests** (per-case + per-game, `regression_gt.txt` / `.wins`) detect *any* behavior
  delta and localize which games moved (`audit_changed_games.py` + `explain_game.py`).
- **Disjoint seeds across modes** (smoke / regression / overnight) give a natural
  **train / validation split**: tune on one seed set, validate the winner on a held-out set to
  guard against overfitting the tuning seeds.
- **Env-var knob A/B is an established pattern** (`MTG_TAP_LEGACY`, `MTG_EXHAUSTIVE_BOTTOM`,
  `MTG_NO_RESERVE`, …): a heuristic is exposed as a runtime switch so a sweep needs no code edit.
- **Precedent for baking empirically-determined decisions into an artifact:** the exhaustive
  bucketed **mulligan profile** (`.claude/skills/mulligan-profile.md`, `ExhaustiveKeep.*`) already
  computes keep/bottom decisions by rollout battery and stores them in a sidecar. Heuristic-param
  tuning is the same shape applied to decision knobs instead of keep tables.

## Parameterization

To sweep a knob without recompiling per value, expose it as an env var / small config read at
startup (e.g. `MTG_DRIP_RANK_NUDGE=1`, or a `provider_params.json` the providers load). Where a
rebuild-per-config is acceptable (few candidates), a compile-time constant is fine to start.
Candidate first knobs, highest value first:

1. The Grove drip rank nudge (magnitude and sign, per Remedy state) — a tiny, well-isolated space.
2. The base `ManaSourceRank` tiers themselves (mono=10 / dual=20 / tri=30 / rainbow=50 /
   {C}-manland=60 / filter=25) — currently hand-set.
3. Reservation / tap toggles already behind env vars.

## Scope by provider

Tuned values live in the **archetype provider** that owns the decision (e.g.
`AntiLifegainProvider::ManaSourceRank`), because the best value is deck-dependent and the root
`GenericProvider` must stay the neutral default. This mirrors the just-landed refactor that moved
the Grove nudge out of the root into the archetype override.

## Report-back (required)

Even when a config is chosen empirically, **report the decision to the user** so anything "not
quite right" is caught before it sticks: what knob changed, from → to, and the measured effect
(win% and avg-win-turn deltas per case, any per-game `win->loss`, and the digest churn summary).
The human stays in the loop as a **reviewer/veto**, not the idea generator. A tuning run that
finds "no change beats current" should say so rather than silently keeping the default.

## Risks / considerations

- **Overfitting the tuning seeds** → always validate the winner on held-out seeds (the
  overnight seed set is disjoint from smoke/regression); only adopt if it holds there too.
- **Noise** → enough games per candidate that the fingerprint delta exceeds run-to-run variance
  (the suite's per-game digests make "real change vs noise" auditable).
- **Search-space explosion** → start with 1–3 high-value knobs; greedy/coordinate descent or a
  small grid, not a full joint sweep.
- **Objective choice** → win% vs speed can trade off; be explicit about the scalarization, and
  surface it in the report so the user can steer it.
- **GT churn** → an adopted config rebaselines ground truth like any change; fold the rebaseline
  into the adoption step.

## Rollout sketch

1. Pick a knob; expose it as a runtime switch.
2. Sweep values, N games each on the **train** seed set; score by the objective.
3. Validate the top candidate(s) on the **held-out** seed set.
4. **Report** the chosen value + measured deltas to the user for review.
5. On approval, set it as the default and rebaseline GT.
