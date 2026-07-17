# claude-play mulligan latency (separated-out investigation)

**Status:** OPEN observation, not being worked on now. Split out from the deck-onboarding
`land_entry` wiring (2026-07-17) so the two don't get conflated. This is a **performance**
concern, NOT mulligan-profile generation and NOT caused by the `land_entry` change (smoke is
byte-identical).

## Observation

A single `--claude-play` launch spends **~68–82 s before it even returns the first decision**,
and the cost is dominated by the **mulligan keep-evaluation** (the `KeepHand` decision the viewer
shows as the opening Keep/Mulligan modal). Measured 2026-07-17 driving Anti-Lifegain:

- step 0 (mulligan) ≈ 68 s **with no `--profile`** and ≈ 82 s with the exhaustive profile — so the
  bulk is the base keep-rollout eval, not the profile.
- every later step is also ≈ 68 s because the **stateless replay re-runs the mulligan eval from
  scratch on each launch** (the auditor / viewer relaunch the binary per decision), i.e. the whole
  game walk is `O(decisions) × 68 s`.

## Why it matters

1. **Viewer UX:** "New game" appears to hang for over a minute on the opening mulligan modal.
2. **Verification cost:** the Stage-5h viewer auditor and any stateless-replay sweep pay ~68 s
   *per decision per game*, which makes a 10-game sweep take ~an hour and makes in-session
   live-surface checks of a new decision type impractical. (This is why the `land_entry` runtime
   surface-check was deferred to indirect proof: builds clean + smoke byte-identical + self-guard
   maps the type + wiring structurally identical to `replicate`/`retrace`.)

## Leads to investigate (unverified)

- What is the mulligan keep-eval actually doing for ~68 s? Likely full keep-rollouts at the
  claude-play depth/budget for every keep bucket / London size. Profile it (`MTG_BRANCH_STATS`,
  a perf build) and find the hotspot.
- Is the eval redundant with what the viewer needs? The viewer only needs the **AI's keep/mull
  recommendation** (`ai_choice`) to tag the modal — a much cheaper single evaluation than a full
  clairvoyant keep search may be enough for the human-facing hint.
- Can claude-play **cache** the opening keep eval across the stateless replays of the same
  (seed, game-index)? The replay is deterministic, so the mulligan result is identical every
  launch — a memoized decision cache keyed by the choice-prefix would collapse the `O(n) × 68 s`
  replay cost. (This helps every decision type, not just the mulligan.)
- Separately, an env knob to **cap the mulligan eval budget in claude-play** would make
  verification sweeps tractable without changing the shipped autonomous path.

## Non-goals

- Not a correctness bug. Not related to `land_entry`. Not mulligan-profile generation.
