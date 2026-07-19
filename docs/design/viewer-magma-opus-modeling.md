# Magma Opus: divide surfacing + unmodeled "tap two" / Treasure clauses (issue #9)

Status: **ANALYZED — root cause found; card-model change specced (user design); needs GT-tradeoff sign-off.**

## What was confirmed (2026-07-19)

- Magma Opus **casts fine** and the `divide` decision surfaces (reproduced Seed 5 Game 4). The divide's
  `legal_targets` include opponent creatures (`CollectDamageTargets(players_only=false)`), and opponent
  permanents carry a board `idx` (`JsonBattlefield` for both players), so the divide stepper attaches to
  them exactly as it does to your own Hinata (which works). So there is **no separate "can't select
  opponent creatures" wiring bug** — the practical block was reaching the target COUNT.
- **Root cause of "prevented from casting / needed more targets":** Hinata discounts per DISTINCT
  target. Real Magma Opus reaches 6 targets = **4 damage (spread, ≤2 to face) + 2 tapped permanents**.
  The model **omits tap-two**, so the human maxes at the ≤4 divide targets and can't reach the discount
  the cheap `{R}{U}` line needs. Dumping all 4 damage on the face = 1 damage-target (poor spread).
- The model's autonomous discount uses `HinataAvailableTargets = 2 + every permanent`
  ([SpellEffects.h:2420](../../src/core/SpellEffects.h#L2420)) — a free over-count (the "reduction
  without distinct targets" issue). It keeps the SEARCH's Magma cheap, so autonomous play is roughly OK.

## The fix (user design)

1. **Tap-two as a separate target step** — a new decision to choose 2 target permanents to tap; those
   are 2 more DISTINCT targets that earn the discount. (Tap effect itself is ~inert vs a passive
   goldfish opponent; the target COUNT is what matters.)
2. **Distinct-target discount** — Hinata's reduction counts the distinct targets actually chosen
   (divide damage targets ∪ tap targets), not `2 + all permanents`. More faithful.
3. **Autonomous heuristic (goldfishing).** Under the distinct-target discount the search's real choice
   is: *spread* the 4 damage 1-each across cheap **distinct** targets, then tap 2 lands, vs. concentrate
   damage on the opponent face (fewer targets → more mana, more face damage). "The only real decision in
   goldfishing is whether to pay more mana to deal more damage to the opponent" (user). Default to the
   max-discount spread (cheapest cast); concentrate face damage only when the extra points are worth the
   extra mana (lethal / near-lethal). Heuristic tuning (see `.claude/skills/heuristic-optimization.md`)
   on top of the faithful model.

   **Exact cost ladder (user).** The 4 damage-targets are: **the two players (opponent + self) + Hinata
   + one other creature** (yours or the opponent's — *sometimes absent*), 1 damage each; plus **tap 2
   lands**. Magma Opus is `{6}{U}{R}`; Hinata discounts `{1}` per DISTINCT target (cap 6):
   - 4 damage-targets + 2 tap = **6 distinct** → `{U}{R}` (2 mana).
   - no 4th creature → 3 damage-targets (put ≤2 on a face) + 2 tap = **5 distinct** → `{1}{U}{R}` (3 mana).
   Distinctness is load-bearing: a permanent targeted twice earns only one `{1}`.

## The GT tradeoff (needs sign-off before rebaseline)

The Soulfire fix moved GT positively (more reach). Magma is the opposite: switching the autonomous
discount from `2 + all permanents` to **distinct chosen targets** makes Magma Opus *more expensive* for
the search (it can only count what it targets), so it likely casts later / less — a **GT-negative**
move, though a more faithful one. Options:
- **(A) Human-path only:** add the tap-two step + distinct-count for the VIEWER/human cast (like the
  Soulfire chooser is human-only), leaving the autonomous discount as-is → GT-neutral, unblocks the
  human, but keeps the search's over-count.
- **(B) Full faithful:** change the discount everywhere → GT-negative but correct; rebaseline Hinata.

Recommend confirming A vs B with the user. Reference-safe either way (no reference casts Magma —
none replay a `divide`). Read `.claude/skills/mtg-rules.md` before implementing.

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
