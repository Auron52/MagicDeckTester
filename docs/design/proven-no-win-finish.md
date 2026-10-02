# Proven no-win: finish the game (2026-10-02)

Status: **unlimited budgets DONE; budgeted play DEFERRED** (needs the site audit below).

## Rule (USER)

> "There is no need to re-search once we have finished evaluating the whole set of turns our model
> considers ... At that point we are done and can finish." -- "it needs to be fully finished
> searching, not have some lines pruned."

When a real-play full-line decision (`AIEngine::TakeTurn` -> `FullSearchLineHybrid`) is a complete
search through `max_turns` and finds no win, the game cannot be won within the model's horizon.
`GameState::proven_no_win` is set; the rest of that turn plays the empty plan (no FALLBACK
lookahead), and `GameEngine::PlayOutFrom` ends the game as a no-win after the turn. A win that
happens anyway is still counted (the win check runs first) -- and would be a search/executor
divergence worth root-causing.

Companion changes (every budget): the hybrid never escalates an exhaustive commit (above), and
`FullSearchLine` clamps its ladder at `max_turns - turn + 1` on every budget. A
deeper rung re-walks the identical tree under different cache keys (`BuildSimKey` folds the raw
remaining depth); the label path (`EnumerateEarliestWins`) always clamped, play did not.

## What counts as "fully finished" today

All of:
* the decision budget is **unlimited**;
* `TurnSolver::TruncEvents()` did not move during the decision (thread-local);
* the line is not `truncated` (no order-free memo end);
* `turn + searched_depth - 1 >= max_turns` (the rung that deep never reaches a leaf, so no rollout or
  value estimate enters the proof);
* (no beam can be involved: the escalation beam lives only in `FullSearchLineHybrid`'s escalation,
  and an EXHAUSTIVE commit -- `turn + committed - 1 >= max_turns` -- now counts as verified and never
  escalates, on every budget: the escalation re-searched the same tree with a leaf it cannot reach);
* no external chooser (human / claude-play sessions play on).

## Why budgeted play is excluded (the deferred part)

Several budget-gated prunes fire on `Remaining()` before the budget is exhausted and bump no
counter, so under a budget "no truncation event" does not mean "nothing pruned":
* the order-free twin-reuse gate, `twin_units > share x Remaining()` (TurnSolver.cpp ~51289);
* the group-wave tranche `bound_limit` skip (~52890) and its twin (~57901);
* the m2-fix continuation skip when `Exhausted()` (~50785) -- the only `Exhausted()` exit found that
  does not bump `g_fs_trunc_events`;
* the greedy-charge walk's `walk_exhausted` (~28567/28574/28590).

To extend: give every budget- or work-gated skip in the search a single thread-local "incomplete"
bump (or route it through `g_fs_trunc_events`), add a unit test that a budget-starved decision never
sets `proven_no_win`, then drop the `budget.Unlimited()` term and A/B the tiers (the finish changes
digests of lost games and frees nothing in won ones).

## Open: structural truncation events at an unlimited budget

Hinata2 smoke s1001 gi5 at d3 b0: T1-T3 decisions record 54 / 72 / 132 `g_fs_trunc_events` with the
budget unlimited -- some non-budget drop in the search bumps the watermark. Those decisions were not
exhaustive at d3, but at d8 the T1 decision is, and it will (correctly) refuse to count as a proof
while anything was dropped. Identify the bumping sites; each is either a real prune (keep refusing)
or a mis-classified bump.
