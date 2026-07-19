# Failed-cast atomic rollback (deferred — correct but GT-churning)

**Status:** CONFIRMED real leak; fix is CORRECT but NOT byte-identical (changes hinata GT) —
deferred to a focused session with a GT rebaseline. User asked for it (2026-07-19): "it should
always go back to before the cast when you do the wrong order."

## The leak (confirmed by code reading)

`TurnSolver::TapForCostDirectOnce` and its mirror `AIEngine::TapForCostOnce` pay a cost greedily
(tapping sources, decrementing storage/depletion counters, taking pain-land life loss / Grove
opponent-lifegain). On **total failure** (greedy + backtracker + filter-retry all fail) they restore
`bf_greedy_fail` / `life_greedy_fail` — the **greedy's partial-tap end-state** — NOT the pre-payment
snapshot:

```cpp
// Total failure: restore the greedy's exact end-state to match prior behaviour.
state.battlefield        = bf_greedy_fail;   // <-- PARTIAL taps preserved
state.players[active].life = life_greedy_fail;
state.floating_mana      = reserve_pre;
return false;
```

So a **failed cast leaves side effects**: tapped lands, spent storage/depletion counters, pain-land
life loss. The comment ("match prior behaviour") shows it was a deliberate byte-identity choice when
the backtracker was added — i.e. the leak is long-standing. In normal play the search picks payable
orderings so casts rarely fail; the leak surfaces on ill-ordered plans (Dragonstorm: a spell listed
before its funding source — see `dragonstorm-plan-execution-fidelity-bug.md`).

## The fix (correct, but changes GT)

Restore the FULL pre-payment snapshot on total failure, in BOTH mirror sites (lockstep):
- capture `opp_pre = players[1-active].life` and `oll_pre = opponent_lost_life_this_turn` alongside
  the existing `bf_pre` / `life_pre`;
- delete the `bf_greedy_fail` / `life_greedy_fail` captures;
- on total failure set `battlefield = bf_pre; players[active].life = life_pre;
  players[1-active].life = opp_pre; opponent_lost_life_this_turn = oll_pre; floating_mana = reserve_pre;`.

**Why deferred:** implemented + built clean, but **smoke regressed 14/18** — `hinata` play changed
(2 games win a turn LATER, 24 play-changed at same win turn; d0 greedy also changed). So GT decks
DO hit a failed payment during play and the committed ground truth encodes the leaky end-state.
Reverted to keep GT green.

## What a focused session must do

1. **Root-cause hinata's failed payment:** why does the greedy/search attempt a cast it cannot pay
   (planner committing an unpayable cast? CanPay-vs-executor approximation)? Is the atomic-rollback
   end-state genuinely correct there, and WHY does it come out a turn LATER (was the leak
   accidentally beneficial, or is there a second bug the leak was masking)?
2. Decide: accept the correct behaviour + **rebaseline GT** (per-game audit first — never `--accept`
   on aggregate), or find a narrower gating that fixes only the ill-ordered-plan case.
3. Re-validate: the Dragonstorm ill-ordered reproduction leaves NO tapped lands after a failed cast,
   plus the rebaselined smoke/regression.
