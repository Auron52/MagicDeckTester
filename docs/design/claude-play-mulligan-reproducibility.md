# Claude-play: player-controlled mulligan + reference reproducibility

Self-contained.

## STATUS (2026-07-02) — record + replay IMPLEMENTED; player-controlled mulligan still deferred

The reproducibility half is shipped: the engine RECORDS the mulligan it made and can REPLAY a
recorded one, so a saved reference reconstructs its exact opening hand on any engine version.

- **Record:** `AIEngine::HandleMulligan`/`BottomCards` track `LastMulliganCount()` +
  `LastBottomedNumbers()`; `RunClaudePlay` writes `"mulligan": { "count": N, "bottom": [card #s] }`
  into the reference trace and the `CLAUDE_RESULT` block. Card numbers are now stamped in claude-play
  (`AssignCardNumbers`, previously goldfish-`--log-dir`-only), and the decision hand JSON carries a
  `"num"` per card.
- **Replay:** `--force-mulligan "<count>:<n1,n2,...>"` makes `HandleMulligan` keep at exactly `count`
  and `BottomCards` bottom exactly those numbers, ignoring the keep/bottoming heuristics. Inert when
  unset (goldfish GT byte-identical, verified).
- **Existing references patched** by `test/patch_reference_mulligans.py` (derives `count = (7 or 8) -
  |hand|` accounting for the on-the-draw turn-1 draw, then searches the size-`count` bottom set that
  reproduces the recorded hand). `test/viewer_protocol_check.py` now forces the recorded mulligan, so
  the former `mull-drift` class is reproducible (burn s11 went mull-drift -> ok).

Still deferred: **player-controlled** mulligan (the human/agent CHOOSING keep/mull + bottoming in the
`--choices` stream, vs the engine deciding). The record/replay format above is the substrate for it.
The remaining design below is that player-control layer.

---

## Problem

A saved `references/<deck>/claude_s<seed>_gi<gi>.json` game records the player's
**main-phase** choices (the `--choices` plan-index stream) but **not** the mulligan
decisions — mulligan is engine-decided (the profile heuristic), and the claude-play
player has no control over it. So when the mulligan profile or logic changes, the engine
opens a **different hand**, the whole game diverges at the root, and the recorded choices
target a game that no longer occurs. The reference is dead and **cannot be re-recorded as
the same line**, because the player can't steer the engine back to the old hand.

`test/viewer_protocol_check.py` already isolates this cause: it compares the reference's
decision-0 opening hand to the current engine's and reports **`mull-drift`** (hand changed)
separately from **`play-drift`** (same hand, line diverged). `mull-drift` is deliberately
NOT a `--strict` failure today, precisely because it isn't reproducible.

## Proposal

Make the **mulligan a player-controlled decision** in `--claude-play`, carried in the same
stateless decision/`--choices` stream as main-phase picks, and **record it in the reference**:

1. **New decision type** `"mulligan"` emitted at game start (and after each mulligan): the
   current hand + "keep or mulligan"; on the final keep at N mulligans, the **bottoming**
   choice (which N cards to put back, London-style). Player replies in the `--choices` stream
   (same protocol as `main_phase` / `vial_charge`).
2. **Reference records** the mulligan sub-stream (mull count + the exact bottomed cards)
   alongside the play choices.
3. **Replay reconstructs** the opening hand deterministically from the recorded mulligan
   decisions — independent of the engine's mulligan heuristic. Same (deck, seed, gi, choices)
   determinism the protocol already guarantees, now extended through the mulligan.

## Payoff

- **References stay valid across mulligan-logic changes** — a `mull-drift` reference becomes
  reproducible (the recorded mulligan rebuilds the hand), so `viewer_protocol_check.py --strict`
  can cover it and the user's saved games keep their value.
- **Player mulligan control** (a wanted feature) falls out of the same decision stream.
- Distinguishes a genuine play regression (same hand, worse line) from a mere hand change —
  the former is a bug signal, the latter is not.

## Notes / touch points

- Protocol + emitters live in the `--claude-play` path (`main.cpp` branch) and the decision
  JSON schema; the GUI bridge (`tools/play/server.js`) forwards decisions verbatim, so a new
  decision type flows through without UI changes (per tools/play/README.md).
- Mulligan replay must be deterministic and lockstep with normal setup so a replayed hand is
  byte-identical to a fresh keep of the same decisions.
- Once shipped, re-save the drifted references and flip the relevant cases to `--strict`.
