# Rolling out from mid-turn: `GameEngine::PlayOutFrom`

2026-07-31. Infrastructure note. Read this before making any **new** decision searched by rolling
the game out from the decision point.

## The trap

`GameEngine::PlayOut` opens a **fresh turn**: `RunTurn` starts with `++state.turn_number`. That is
correct for the callers it was written for — the mulligan keep/bottom rollouts evaluate a state
captured *between* turns (turn 0), so "play from here" and "start the next turn" are the same thing.

It is wrong for every state captured *during* a turn. The rest of that turn — the remaining cleanup
sheds, the draw step, the main phase, combat — is silently skipped, and the rollout scores a state
the real game can never reach. Nothing errors; you just get a confident number about a fiction.

The first instance cost a real win. `AIEngine::ChooseDiscard`'s searched pass captured its trial
*inside* the cleanup step, one shed done and ten to go, and rolled out from there. Those ten sheds
never happened, so the rollout played the next turn holding all eighteen cards. On Treasure Hunt the
surplus is **Land's Edge ammunition** (2 damage per land discarded), so every candidate scored a
phantom turn-3 kill and the ranking came down to which discard best exploited damage that did not
exist. It pitched the deck's third Treasure Hunt and turned a T4 win into T8
(`th_smoke_d3_s1001 gi61`; see `searched-cleanup-discard.md`).

## The primitive

```cpp
enum class GameEngine::ResumeAt { NewTurn = 0, Draw, Main1, Combat, Main2, End, Cleanup };
int  GameEngine::PlayOutFrom(GameState&, int max_turns, ResumeAt from);
int  AIEngine::RolloutWinTurnFrom(GameState trial, int max_turns, ResumeAt from, int* lands_out = nullptr);
```

`RunTurnFrom` runs the same steps in the same order with the same early-outs; `from` only chooses
where to enter. `NewTurn` is exactly the old behaviour, so `PlayOut` / `RolloutWinTurn` are unchanged
and every existing caller is byte-identical.

## The rule

**Name the step you are standing on.** A rollout launched from a decision point must resume at that
point's step, not at the next turn:

| decision | step it happens in | resume |
|---|---|---|
| mulligan keep / bottom | between turns | `NewTurn` (the default) |
| Aether Vial charge | upkeep, *before* the draw | `Draw` |
| cleanup discard | cleanup | `Cleanup` |

Resuming at `Cleanup` also gets the tail policy right for free: the remaining sheds run through the
real `CleanupStep`, and `m_in_rollout` gates the searched pass off inside the rollout, so the
candidate under test is the **first** shed and the rest follow the heuristic. That is the same
"branch on the first divergence, default for the tail" shape the plan search's breakpoints use — and
it is exact, rather than a hand-rolled imitation of the cleanup loop.

## What it does not fix

A resume point makes the *label* legal. It does not make the label match the game that will actually
be played — if the executor deviates from what the committed line assumed, the line must be dropped
too. See the second half of `searched-cleanup-discard.md`.
