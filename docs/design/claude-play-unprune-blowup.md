# The reference sweep OOM: `--claude-play` opens EVERY prune gate, and one of them is a 512x fan-out

**Status:** root-caused and fixed (the SacColor demand filter is no longer lifted by an unprune gate).
**Symptom:** WSL OOM during an overnight run, killing the user's session.

## What was measured

Per-replay peak RSS across all 208 reference games, one `mtg` at a time
(`viewer_protocol_check.py --emit-resolved` with `/usr/bin/time -f %M`, each child under
`ulimit -v 12000000`):

| deck | peak RSS | replays over 100 MB |
|---|---|---|
| **Mirrorwing_Dragon** | **10,318 MB** (bad_alloc at the 12 GB cap -- the true peak is unbounded) | 7 of 278 |
| Dragonstorm | 22 MB | 0 of 463 |
| every other deck | <= 11 MB | 0 |

Four Mirrorwing references account for all of it: `claude_s26_gi25` (unbounded),
`claude_s16_gi15` (5,617 MB), `claude_s7_gi6` (2,651 MB), `claude_s4_gi3` (669 MB).

`test/regression.sh` ran this stage at `VPC_THREADS=$THREADS`, i.e. `nproc` = 24 concurrent
engine subprocesses. Three of those four references were committed in `02e3fbc` (2026-08-17,
"refs(mirrorwing): commit 12 hand-played reference games") -- which is what made a
long-standing engine degeneracy start OOMing the box.

**The engine batch was never involved.** A FiveColour d5 batch peaks at 45 MB per thread with
`peak_entries=36208` in the transposition table. Only the reference-replay path blows up.

## The mechanism

`gdb -ex "catch throw"` on the failing replay put the allocation in the plan enumerator:

```
operator new (PoolAllocator.cpp:102)
  std::vector<Action>::_M_realloc_insert
    EnumeratePlans(...)::<lambda(sel)>          TurnSolver.cpp:12997  <- plan.actions.push_back
      EnumeratePlanPositions(cands=45, groups=9, ind=0, mana_bound=49)
        EnumeratePlans                          TurnSolver.cpp:13212
          EnumeratePlansWithLand -> TurnSolver::EnumerateMainPlans   <- the claude-play hook
            AIEngine::TakeTurn -> GameEngine::MainPhase
```

State at the throw: turn 5, hand EMPTY, 24 permanents, 45 candidate actions -- **all of them
`Action::Kind::SacForMana`** -- in 9 groups of 5. Nine untapped Treasures, each fanning out to one
action per candidate float colour, each Treasure its own odometer group:

```
product = (1 + 5)^9 = 10,077,696 plans, each materialised with its own vector<Action>
```

Two independent gates had to be open for that number to happen, and `--claude-play` opens both.
`src/main.cpp:4207` does `setenv("MTG_UNPRUNED", "1", 1)` for the whole session, and
`DecisionUnpruned(g)` returns true for **every** gate when that is set:

1. **`SacColor`** -- `ChosenFloatColorCandidates` (TurnSolver.cpp:2926) used
   `if (open_all || demand[c] > 0)`, so an open gate offered all five colours instead of the two
   the deck's cards actually have pips for. This is the 512x term: `6^9` vs `3^9`.
2. **`ComboLine`** -- the board-lethal short-circuit (TurnSolver.cpp:13101, opted into by
   `MirrorwingProvider::UseLethalShortCircuit`) is disabled under that gate. It would have skipped
   the odometer outright: the probe measured `pending_atk=97` against `opp_life=18`.

Measured contribution of each, on `claude_s26_gi25`, by closing one gate at a time:

| gates open | product at the T5 frame | peak RSS | wall |
|---|---|---|---|
| both (today) | 10,077,696 | >2.6 GB, unbounded | 8.3 s |
| SacColor closed | 19,683 | 81 MB | 0.08 s |
| ComboLine closed | 10,077,696 (cut fires, odometer skipped) | 10 MB | 0.02 s |
| both closed | 19,683 (cut fires) | 10 MB | 0.02 s |

## The fix, and why it is this one and not the other

Only the `SacColor` half is fixed:

```cpp
for (int c = 0; c < 5; ++c) { if (demand[c] > 0) { idx.push_back(c); } }   // was: open_all || demand[c] > 0
```

The demand filter is **not a heuristic**. `demand[]` counts coloured pips across the active
player's hand, library, graveyard and battlefield, so a colour with zero demand cannot be spent on
anything the deck contains -- floating it is a dead branch, not an option the search or a human is
being denied. What `open_all` legitimately lifts is the line above it, the provider's
`RestrictSacColorsToHasteAndRed` collapse (DragonstormProvider only), which *does* drop real
options. The two were joined; splitting them is the whole change.

**`ComboLine` is deliberately left open in claude-play.** Closing it would show a human exactly one
plan ("attack, you already win") on any lethal board, which is a real narrowing of the menu -- and
it would make any reference that recorded a different pick on a lethal board unrepairable. The
viewer's contract is to offer every legal line; the fix has to come from not enumerating dead ones.

Normal play never sets any unprune gate, so `open_all` is false there and this path is unchanged --
byte-identical GT, confirmed by smoke.

One consequence worth knowing before you sweep gates: **`MTG_UNPRUNE=saccolor` is now a no-op for
every deck whose provider does not set `RestrictSacColorsToHasteAndRed`** (i.e. everything except
Dragonstorm), because that hook is the only thing the gate still lifts. That is the correct
reading of the gate -- it names a heuristic, and a deck without the heuristic has nothing to open --
but a gate probe will still report it as "queried" for any deck with a sac source.

## Residual risk, and the harness guard

The fan-out is still `(1 + |colors|)^N` in the number of untapped fungible sac sources. Mirrorwing
at 9 Treasures is now 19,683 plans / 81 MB; a board with ~15 would be back in the GBs.
`DecisionProvider::FungibleSacSourceCap` (64) bounds the source COUNT, not the product, so it is
far too loose to be the memory bound -- at 64 sources the product is astronomically large either
way. A product-aware bound is the real cure and is **not done**; the honest reason the box is safe
today is that the 512x term is gone.

Because of that residual, `test/viewer_protocol_check.py` sets `RLIMIT_AS` on each replay child.
A pathological board now fails ONE reference with a `bad_alloc` the checker reports, instead of
taking the machine (and the user's session) down with it.

## Reusable lessons

* **A blanket "open every gate" is not free.** `MTG_UNPRUNED=1` is written as a widening for
  correctness ("let the human pick any legal line"), but the gates it opens include cost cuts and
  dead-branch filters, and their product is combinatorial. Prefer naming gates (`MTG_UNPRUNE=<list>`)
  over the global switch when the goal is a specific widening.
* **Attribute memory before theorising.** The batch was suspected first (transposition table,
  plan cap); both were measured and exonerated -- `MTG_TT_CAP=200000` did not move the number and
  neither did `MTG_PLAY_PLANS_CAP`. Per-invocation `/usr/bin/time -f %M` over one deck at a time
  found the deck in one pass, and `catch throw` under a `ulimit -v` named the exact allocation site.
* **A new REFERENCE can be the trigger for an old engine defect.** Nothing in `src/` changed
  between the last clean overnight and the OOM; twelve hand-played games did.
