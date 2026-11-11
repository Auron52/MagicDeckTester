# A mana-costed suspend is realised BEFORE the plan's casts (deferred)

Found by the Hydras Claude-play sweep (2026-11-11, seed 50004 T2, flag B). Status: **deferred, USER's call**;
the viewer half (the `drops` label) is fixed, the engine half below is not.

## The defect

Every apply world runs a plan's `Suspend` actions in a PRE-CAST pass, ahead of every hand cast:
`AIEngine` (the executor's main plan and each `extra` continuation: the five `ApplySuspend` sites) and
`ApplyPlanDirect` (TurnSolver.cpp, the `SacForMana / Suspend` pre-pass). That was harmless for Lotus Bloom,
whose suspend costs {0}. Search for Tomorrow ({G}), Profane Tutor ({1}{B}) and Rift Bolt ({R}) have a mana
cost, and the flat pools (Solve's `consider()` and `EnumeratePlans`) price a plan as an unordered bag.

So on two Forests the plan **{suspend Search for Tomorrow, Three Visits}** passes the flat test (2 board + 1 for
the fetched Forest = 3 = {G} + {1}{G}), but the realised order pays the suspend first and Three Visits is
dropped. The payable order (Three Visits, then the suspend off the fetched Forest) cannot be expressed as ONE
plan in either world.

## What it costs

- **A phantom plan.** The search spends a branch on a plan that realises as "suspend only". It is scored by
  what it realises, so it is never preferred over the real line. This is wasted work, not a wrong pick.
- **Hydras: findable at unlimited budget.** Hydras has a searched second main (Garruk's
  `draw_greatest_power`, `DeckUsesSecondMain`). "Three Visits in main 1, suspend in main 2" reaches the same
  end state, because suspend timing within the turn changes nothing.
- **A deck WITHOUT a second main loses the line for that turn.** Landfall (Search for Tomorrow beside the
  untapped land-puts), Shadowborn Apostle (Profane Tutor) and any Rift Bolt list are affected. "Fetch, then
  suspend off the fetched land" can only happen a turn later, and the suspend's arrival slips with it.

## The fix (not done)

Realise a mana-costed suspend AFTER the hand casts, in lockstep, at every site above. Keep {0} suspends
(Lotus Bloom) in the pre-pass so Dragonstorm stays byte-identical. The line-unpaid hold already includes the
suspend's cost, so the casts' payer reserves its colour. Paying the suspend last is weakly dominant for
payability: a suspend puts nothing on the board this turn, so no cast can need it first.

The change touches the executor's continuation paths and the BP-resume routes, and it moves Landfall,
Shadowborn and Rift Bolt ground truth. Per CLAUDE.md it needs a held-out A/B on those decks and a GT accept.
That is a separate change set from the Hydras F1 fixes, so it was left for the USER to schedule.
