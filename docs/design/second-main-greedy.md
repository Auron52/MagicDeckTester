# The second main inside the search was greedy — searched variant built, DEFAULT OFF

**Status:** implemented and gated (`MTG_SEARCH_SECOND_MAIN=1`), **default OFF** on measurement.
Off is byte-identical to the shipped engine (smoke 30/30, 0 configs changed), so nothing here
perturbs ground truth. Session ran out of budget before the decisive deck was measured; the exact
resume command is at the bottom.

**(Updated 2026-09-03: the global lever is still correctly default OFF, but the "what to do
next" list is done/overtaken** — the decisive deck (FiveColour) was measured and adopted
per-deck as `MTG_5C_SSM` (2026-08-21), with KittyEquipment and Anti-Lifegain following; and the
breakpoint-continuation greedy in item 4 was deleted by ebfb5f74.)

**User bar (2026-08-09):** *"we can't afford to have second main be greedy … I want no greedy steps
except attack decisions"*, later widened: *"(and mana allocation is also okay)"*. So the target state
is: every PLAN choice searched; attack declaration and mana payment may stay heuristic.

## The defect

Three sites played the post-combat main with the depth-0 greedy `Solve(state, false)`, marked
*"(speed). The real game searches it."*

| site | what it decides |
|---|---|
| `SolveWithLookahead` candidate loop | **this turn's** second main, per pre-combat candidate |
| deferred-wave loop | the same, for wave-N variants |
| `SimulateToEndImpl` | every **future** turn's second main in the rollout |

The first is the one that matters. The candidate being scored there IS the pre-combat choice, and
the value of *passing* the pre-combat main is exactly the second main it buys — so scoring that
second main with the greedy asked the heuristic to answer the question the search exists to answer.
If the greedy did not find the post-combat cast, holding mana looked worthless and the search cast
pre-combat instead. Reported on FiveColour (attack with vigilant Faeburrow Elder, THEN spend its
mana on Unite the Coalition), but the shape is deck-agnostic — it is the same reason the full-search
path `FSLineTail` has always enumerated the second main properly.

## The change

All three sites now route through one `SolveSecondMainInSearch` (one function, so they cannot
drift). `depth <= 0` keeps the greedy: at depth 0 there is no search at all — `SolveWithLookahead`
itself returns `Solve()` there — so every d0 case is byte-identical by construction. The shared
`SearchBudget` is threaded through, so it spends from the same per-decision allowance.

## Measurement — it is NOT budget dilution

Budget dilution was the obvious hypothesis (a fidelity gain paid for out of a fixed budget looks
negative at the gate). **It is wrong here.** Dilution shrinks as the budget grows; this does not:

| budget | summed Δ vs greedy (positive = searched WORSE) | cost |
|---|---|---|
| x1 (gate) | **+0.0240** | 0.91–1.08x |
| x4 | **+0.0174** | 0.98–1.15x |
| x16 | **+0.0173** | 0.96–1.12x |

Per-case (seed 1001): antilife d3 **+0.0040 at all three budgets** (250 games × 0.0040 = exactly one
game, one turn — and that game, gi139, **diverges at a turn-3 fetch**, i.e. variance by the harness's
own classification rule, not a like-for-like slowdown); antilife d5 and hinata d5 **0.0000** (digest
differs, score does not); hinata d3 +0.0200 → +0.0134 → +0.0133.

**Eight of ten decks are byte-identical** — including burn (spectacle) and Goblins (Lackey), which
both take the second-main path. That is the most informative number here: the suite has no deck
whose post-combat main carries a decision worth searching, which is also why the ~1.1x cost is so
low. So this measurement mostly says *the suite cannot see this change*, not *the change is bad*.

## What to do next (in order)

1. **Measure FiveColour** — the deck the request came from, and the only one built around attacking
   with a vigilant mana source and then spending it. It is NOT in `test/regression_cases.sh`, which
   is why none of the above touches it. This run was in flight when the session ended:

   ```bash
   # 5 held-out seeds x 250 games, d3/b10 -- same manifest shape as the cross-agent control
   (echo "=== GREEDY ==="; MTG_SEARCH_SECOND_MAIN=0 build/Release/mtg --batch /tmp/fc_sm2.json \
      2>&1 | grep -E "^fc_" | sort;
    echo "=== SEARCHED ==="; MTG_SEARCH_SECOND_MAIN=1 build/Release/mtg --batch /tmp/fc_sm2.json \
      2>&1 | grep -E "^fc_" | sort)
   ```
   Manifest: seeds 2002/3003/5005/6006/7007, `decks/FiveColour/FiveColour.cod` + its profile,
   250 games, depth 3, budget_ms 10. Rebuild it if `/tmp` is gone.

2. **If FiveColour gains, replace the implementation before adopting.** It currently calls the full
   `SolveWithLookahead`, which drags the whole multi-pass escalation, wave and non-convergence
   machinery in to answer a one-phase question. The lean form is what `FSLineTail` already does:
   `EnumeratePlans(state, false)`, push the empty plan (casting nothing must always be legal —
   otherwise a tap-out line reads as a no-win), `MoveOrderPlans`, then one `SimulateToEnd` per
   candidate and take the minimum win turn. Cheaper and a closer match to the real game.

3. **Add FiveColour to the regression suite.** It is the deck with the most active development and
   the only one with no ground truth, so every FiveColour change is currently unmeasured by default.
   This should land before the mulligan profile is generated (generation is commit-bound).

4. The remaining greedy plan-steps, if the "no greedy steps" bar is to be met in full, are the
   post-breakpoint continuation re-solves (`TurnSolver.cpp` ~7833/7870/8093/8605/8693 and the
   executor mirror in `AIEngine.cpp`). Some are already searched under `MTG_BP_SEARCH`
   (`bp_searched_plan`); the rest are not. Not started.

## Gotcha for whoever runs the A/B

A batch manifest may not pin `depth` for a deck whose profile has an enabled `value_play` block —
the binary errors with *"value_play depth is ENABLED for this deck"*. Mirror `test/regression.sh`:
DROP the `depth` key for the d5 case (the block owns it) and pass `"depth"` + `"ignore_play_profile":
true` for d0/d3. FiveColour has no such lock, so its manifest may pin depth directly.
