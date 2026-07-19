# Magma Opus: divide surfacing + unmodeled "tap two" / Treasure clauses (issue #9)

Status: **DEFERRED — engine card-modeling + reproduction needed; moves Hinata GT.**

## Problem (as reported)

Seed 5 Game 4 (Hinata): the user "cannot divide Magma Opus damage to opponent's creatures, nor select
the lands," which "prevented me from casting Magma Opus even though there were plenty of targets."

## What the model has vs. omits

`cards.json` — **Magma Opus** `{6}{U}{R}` Instant, `template:"direct_damage"`,
`{ damage:4, targeting:"any", cast_draw:2, discount_max_targets:6, discount_targets_permanents:true,
damage_divided:true }`. Real oracle: "deals 4 damage divided among any number of targets. **Tap two
target permanents.** Create a 4/4 Elemental. Draw two cards. `{U/R}{U/R}, Discard: create a Treasure`."

The model's own note flags the omissions: "**STILL omitted** … the 4/4 Elemental token, tap-two (inert
vs a passive opponent), and the `{U/R}{U/R}`-discard-for-Treasure ramp mode." So:

- **"select the lands" (tap two permanents)** is literally not modeled → **no engine decision is
  emitted**, so the viewer has nothing to render. Matches that half of the complaint exactly.
- **Divide IS implemented and uncapped** — engine `WriteDivideDecisionJson`
  ([src/main.cpp:788](../../src/main.cpp#L788)) + viewer `wireDivideBoard`/`commitDivide`
  ([tools/play/index.html:1142](../../tools/play/index.html#L1142)). Magma Opus is flagged
  `damage_divided:true`, and `CollectDamageTargets(players_only=false)` includes opponent creatures.
  So dividing 4 among opponent creatures is *representable* — **when the divide sub-decision actually
  reaches the human**. The divide panel only opens during real cast execution; if the Magma Opus cast
  is folded into a committed combo plan (choices supplied) or the cast is never reached, the panel
  never opens — the likely reason "cannot divide … prevented casting."

## Fix (deferred, staged)

1. **Reproduce** `--seed 5 --game-index 4`, step to the turn-4 Magma Opus draw, and dump the emitted
   `main_phase` plans + whether a `divide` sub-decision fires on the cast. Pin down whether the blocker
   is (a) the plan enumerator not offering the cast at that mana, or (b) the `divide` sub-decision not
   surfacing interactively.
2. **Surface the `divide` sub-decision for interactive casts** so the human can split the 4 among
   opponent creatures (not only the autonomous all-to-face default).
3. **Model the omitted clauses** (per `.claude/skills/mtg-rules.md`, conservative-then-validated):
   add a "tap two target permanents" decision (new decision `type` + viewer panel reusing the
   target-selection UI) and, if wanted, the 4/4 token and `{U/R}{U/R}` Treasure alt-mode.
4. Engine changes move **Hinata GT** (Magma Opus is a Hinata 1-of) → rebaseline per
   `.claude/skills/regression-testing.md` and regen the Hinata profile on a frozen commit; disclose in
   the Stage-6a note.

## Verification

Cast Magma Opus interactively in the repro: divide 4 among opponent creatures, choose two permanents to
tap, confirm the 4/4 token + draw-two land. Regression-suite audit before `--accept`.
