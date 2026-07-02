# Deferred: claude-play decision parity (mulligan/bottoming + show-the-AI's-choice)

Self-contained deferred ideas (2026-07-02, user's). Improvements to the `--claude-play` oracle so a
human-driven game matches the game the search actually plays, and so the human can compare each of
their choices to what the AI would do. Do when convenient.

## Motivation / bug found

While verifying gi627 we found the claude-play mulligan **bottoms a different card than the
autonomous engine** (`Sandstone Needle` vs `Lonely Sandbar`). Isolated cause: the bottoming is a
clairvoyant rollout, and human-play sets `MTG_UNPRUNED` + `MTG_HUMAN_PLAY`, EITHER of which changes
that rollout (verified: each flag alone flips the keep; `MTG_PONDER_SEARCH` does not).

- `MTG_UNPRUNED` un-gates `ShouldCastDrawEngine` etc. — by design a "change every rollout" knob.
- `MTG_HUMAN_PLAY` changes `ApplyPlanDirect` (breakpoint/no-auto-resolve semantics), which leaks
  into the bottoming rollout. **This one looks like a bug**: bottoming is an ENGINE decision
  (claude-play leaves mulligan/combat/cleanup on the engine), so it should be invariant to the
  human-play apply-semantics flag — the bottoming rollout should null `s_human_play` the way search
  already nulls the external chooser / draw log (`RevealLogPause`).

Workaround today: `--force-mulligan "<count>:<n1,n2,...>"` reconstructs a reference's exact opening
hand (e.g. `3:8,28,38` reproduces gi627's base hand). But it should not be necessary for parity.

## Three items

1. **Make claude-play bottoming match the real search (d5).** Run the mulligan-bottoming rollout
   with the SAME behavior the autonomous d5 game uses — at minimum null `MTG_HUMAN_PLAY` inside the
   bottoming rollout so human-play doesn't perturb it. Decide whether `MTG_UNPRUNED` should also be
   excluded from bottoming (probably yes — the human isn't choosing the bottom, the engine is, so it
   should use the engine's normal gated rollout). Goal: a claude-play game opens with the same hand
   the search would keep, so it's the SAME game.
2. **Let the human choose mulligans and bottoming.** Surface mulligan keep/mulligan and London
   bottoming as claude-play decision points (new decision `type`s in the stateless-replay protocol,
   sharing the `--choices` stream), so the oracle covers those decisions too instead of deferring
   them to the engine. Complements #1: #1 makes the DEFAULT faithful; this lets the human override.
3. **Show what the AI (d5) would do at each choice — including bottoming and mulligans.** For every
   emitted decision, also report the engine's own pick (the search's chosen plan index / keep /
   bottom) alongside the legal options, so the human sees "AI would do X" next to their choice. Makes
   claude-play a direct A/B-against-the-search viewer, not just a driver. The decision JSON already
   carries a per-type `heuristic_default` for vial_charge; generalize that to a `search_choice`
   (or `ai_default`) field on every decision, computed by running the normal engine decision for the
   replayed state before overlaying the human's `--choices`.

## Touch points

- Mulligan/bottoming: `AIEngine::HandleMulligan` + the bottoming rollout it calls; the `s_human_play`
  static in `TurnSolver.cpp` (null it for the bottoming rollout).
- Protocol / decision emission: `RunClaudePlay` and `WriteDecisionJson` in `src/main.cpp`
  (the `<<<CLAUDE_DECISION>>>` blocks, `--choices`/`--force-mulligan` plumbing).
- See `.claude/skills/claude-play.md` (stateless-replay protocol, `--force-mulligan`).
