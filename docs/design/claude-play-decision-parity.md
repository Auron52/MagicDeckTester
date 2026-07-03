# claude-play decision parity (mulligan/bottoming + show-the-AI's-choice)

Self-contained ideas (2026-07-02, user's). Improvements to the `--claude-play` oracle so a
human-driven game matches the game the search actually plays, and so the human can compare each of
their choices to what the AI would do.

**Items 1–3 SHIPPED 2026-07-03** for the mulligan/bottom scope, plus the **async deep-search bottoming
hint** (see the "fix"/async sections below). The only remaining piece is a future step: the same async
AI-pick treatment for the OTHER (main-phase / cast-order) decisions — see "Async deep-search AI hint"
below.

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

Workaround (no longer needed for parity): `--force-mulligan "<count>:<n1,n2,...>"` reconstructs a
reference's exact opening hand (e.g. `3:8,28,38` reproduces gi627's base hand). Still useful to pin a
hand across heuristic changes, but item 1's fix makes the DEFAULT bottoming faithful.

## Item 1 fix (SHIPPED 2026-07-03)

The initial diagnosis (flag leak) was correct but INCOMPLETE. Root cause is broader: `--claude-play`
sets `MTG_HUMAN_PLAY` + `MTG_UNPRUNED` process-wide AND installs external choosers on the engine, and
ALL of these leak into the engine's clairvoyant **bottoming/keep rollout** (`AIEngine::RolloutWinTurn`
→ `GameEngine::PlayOut`), which must play autonomously so the kept hand reproduces the real search's
game. Three distinct leaks, all fixed by "play autonomously inside a rollout":

1. **Env-flag leak** (`MTG_HUMAN_PLAY`, `MTG_UNPRUNED`). The scattered `getenv("MTG_HUMAN_PLAY")`
   statics are replaced by `HumanPlayActive()` (in `GameLogger.h`), which returns the env value
   EXCEPT while a `HumanPlaySuppress` guard is live. `RolloutWinTurn` installs that guard (next to its
   existing `RevealLogPause`). `DecisionUnpruned()` likewise returns false inside the guard **when
   human-play is also set** (so a pure autonomous `MTG_UNPRUNED` A/B keeps its bottoming unpruned —
   byte-identical for that use). Non-human-play runs are byte-identical (env is false either way).
2. **Main-phase external chooser leak** (the big one). `TakeTurn`'s external-controller intercept had
   NO `m_in_rollout` guard, so the bottoming rollout's `PlayOut` fired the human chooser and
   `exit(70)`ed — the "first decision" a claude-play game emitted at depth>0 was actually a garbage
   rollout-trial state (a 6-card hand = 7 minus the one candidate being bottom-tested). Dormant at the
   default depth 0 (no rollout) and hidden by `--force-mulligan` (skips the lookahead-bottoming
   branch), which is why it went unnoticed. Fixed: `use_external = m_external_chooser && !m_in_rollout`.
3. **Vial charge external chooser leak** (same class, Vial decks only). `DecideVialCharge` now gates
   its `m_external_vial_chooser` on `!m_in_rollout`.

Verification: at depth 0 AND depth 3, `--claude-play` now opens with the byte-identical hand the
autonomous goldfish keeps at the same depth (th gi627: both keep `{Treasure Hunt, Treasure Hunt,
Sandstone Needle, Saprazzan Skerry}` at d3; slivers gi627 parity too). Autonomous engine is
byte-identical (smoke 18/18 PASS, audit 0 changes; regression clean) — the external choosers are null
in autonomous play and the env flags are unset, so every guard is inert there.

**Depth note:** claude-play bottoms at the `--depth` it is given (default 0). To reproduce a specific
reference game's opening hand, run claude-play at the SAME `--depth` that reference used (e.g. `--depth
3` for the th regression game). The fix guarantees parity at whatever depth is chosen; it does not
auto-pick the depth.

## Three items

1. **[DONE 2026-07-03] Make claude-play bottoming match the real search.** See "Item 1 fix" above.
   Shipped all three: null `MTG_HUMAN_PLAY` in the rollout, exclude `MTG_UNPRUNED` from the rollout
   (only in a human-play session), and — the actual dominant bug — stop the external main/vial
   choosers from firing inside the rollout. A claude-play game now opens with the same hand the search
   would keep at the same `--depth`.
2. **[DONE 2026-07-03] Let the human choose mulligans and bottoming.** Two new claude-play decision
   `type`s — `mulligan` (keep/mulligan, one per London attempt) and `bottom` (one per card to bottom) —
   share the `--choices` stream and fire FIRST, before any turn decision. Wired end-to-end through the
   tools/play viewer (image-based modals). See "Items 2–3 fix" below.
3. **[DONE for mulligan/bottom, 2026-07-03] Show what the AI would do at each choice.** Both new
   decisions carry an `ai_choice` field (mulligan: 1 keep / 0 mulligan from `KeepHand`; bottom: the
   hand index the engine would bottom, plus `win_optimal` flags per card at depth>0). Following the AI
   pick at every mulligan/bottom step reproduces the autonomous kept hand exactly. Extending this to
   the OTHER decision types (main-phase plan, etc.) is a **future step** — see "Deferred" below; most
   other decisions already surface a `heuristic_default`, so cast-order is the main gap.

## Items 2–3 fix (SHIPPED 2026-07-03)

C++ (protocol): two external choosers on `AIEngine` — `m_external_mulligan_chooser` (consulted in
`HandleMulligan`'s keep loop, sees the engine's `KeepHand` result as the AI hint) and
`m_external_bottom_chooser` (consulted in `BottomCards`, sees the engine's bottom pick + the
lookahead win-optimal mask). Both null in autonomous play → byte-identical (smoke 18/18 + regression
30/30 PASS, audit 0 changes), and both skipped under `--force-mulligan`. `RunClaudePlay` installs them
sharing the one `--choices` cursor, and `WriteMulliganDecisionJson`/`WriteBottomDecisionJson` emit the
`<<<CLAUDE_DECISION>>>` blocks (with a minimal life-20/empty board so the viewer renders cleanly
behind the modal). Verified: following `ai_choice` at every step reproduces the goldfish d3 kept hand.

Viewer (tools/play/index.html): `mulligan` and `bottom` added to `SUBDECISIONS`, to the modal set, and
to the render/wire/commit dispatch; new `mulliganPanelHtml` (Keep/Mulligan buttons + AI hint, hand as
art) and `bottomPanelHtml` (click-a-card, mirrors the discard modal, tags the AI pick and ✓-marks the
win-optimal removals when discriminating).

## Async deep-search AI hint (bottoming DONE 2026-07-03; main-phase deferred)

The AI's suggested move is NEVER required for the human to make their own choice, so an expensive AI
pick is computed **asynchronously, in parallel, and must never block the UI**: show the decision
instantly, kick off the deep search, and fill the "AI would do X" hint in when it returns (the human
may well have already moved). This also enables "figure out the AI's next move while the user is still
thinking."

**Bottoming — SHIPPED.** The primary `/api/step` runs at the play depth (0, fast: the depth-0 heuristic
bottom pick, non-discriminating). The `bottom` modal appears immediately showing "AI thinking…", and
the browser fires a PARALLEL `POST /api/ai-hint` that re-runs the same `(deck,seed,gi,choices)` at
`HINT_DEPTH` (default 5) — bottoming happens before turn 1, so that invocation only pays the clairvoyant
bottoming rollout (~1.4 s). It returns the deep `ai_choice` + per-card `win_optimal`; the browser patches
the modal in place (tags the deep pick, ✓-marks the win-optimal removals), or drops the result if the
human already moved on (`decision_index` no longer matches). The shallow depth-0 pick is deliberately
ignored — we want the deep pick. No C++ change was needed: claude-play at `--depth 5` already emits the
deep bottoming `ai_choice`. Touch points: `runAiHint` + `/api/ai-hint` in `tools/play/server.js`;
`fetchBottomHint` + the async-aware `bottomPanelHtml` in `tools/play/index.html`. Mulligan needs no
async hint — `KeepHand` is depth-independent, so its keep pick is already correct at the play depth.

**Main-phase / cast-order — DEFERRED.** Same pattern, but the pick needs a full `SolveWithLookahead` at
the play depth on a copy of the replayed state (under a `HumanPlaySuppress` guard), matched to a shown
plan index — and unlike bottoming, a depth-5 invocation there runs the whole game's play search, not
just a pre-turn rollout, so it is genuinely expensive and wants the async treatment most. Cast-order is
the one decision with no heuristic surfaced today, so it's the highest-value target after the plan index.

## Touch points

- Rollout autonomy (item 1, DONE): `AIEngine::RolloutWinTurn` installs `HumanPlaySuppress`;
  `HumanPlayActive()` / `HumanPlaySuppress` / `g_human_play_suppressed` live in `GameLogger.h`+`.cpp`;
  `DecisionUnpruned()` in `DecisionProviders.cpp`; the `!m_in_rollout` guard on the external chooser
  in `AIEngine::TakeTurn` and on `m_external_vial_chooser` in `AIEngine::DecideVialCharge`.
- Protocol / decision emission (items 2–3, mulligan/bottom DONE): the two choosers on `AIEngine`
  (`SetExternalMulliganChooser`/`SetExternalBottomChooser`, consulted in `HandleMulligan`/`BottomCards`);
  `WriteMulliganDecisionJson`/`WriteBottomDecisionJson` + the chooser lambdas in `RunClaudePlay`
  (`src/main.cpp`). Viewer: `mulligan`/`bottom` in `tools/play/index.html` (SUBDECISIONS, render/wire/
  commit dispatch, `mulliganPanelHtml`/`bottomPanelHtml`).
- Async AI hint (bottoming DONE): `runAiHint` + `POST /api/ai-hint` (`tools/play/server.js`, runs at
  `HINT_DEPTH`); `fetchBottomHint` + async-aware `bottomPanelHtml` (`tools/play/index.html`).
- Deferred (main-phase AI pick, background): `TurnSolver::SolveWithLookahead` for the search pick.
- See `.claude/skills/claude-play.md` (stateless-replay protocol, `--force-mulligan`).
