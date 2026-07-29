# Rollout/executor lockstep: the bug class, the method, and the open case (2026-07-29)

**The class.** The rollout scores and COMMITS a line that the real executor does not reproduce. This
is not a heuristic disagreement and not search weakness: the search was right, the game just played
something else. It reads as "the search is bad", and **no depth and no budget can fix it**, which is
exactly what makes it expensive to diagnose and easy to misattribute. Three instances were found and
two fixed on 2026-07-28/29; this doc records the method so the next one is cheap.

`MTG_FD_ORACLE` is the detector: it flags any game whose realised win turn is worse than a win the
search had already PROVEN, and prints `[fd-diverge] seed=N realized_win=.. predicted_win=..`.

## Method

1. **Map it.** `MTG_FD_ORACLE=1 mtg <deck> --games 500 --seed 4004` per deck. The per-deck rate is the
   map; a deck at 0 needs no attention.
2. **Reproduce one game.** `--seed <the printed seed> --game-index <seed - base_seed> --games 1`
   (the runner seeds each game at `base_seed + gi`).
3. **Diff the two sides.** `MTG_BP_TRACE=1 MTG_FD_TRACE=1 MTG_NONCONV_TRACE_SEED=<seed>` gives three
   aligned streams:
   - `[fd-pred]` — the rollout replaying its own COMMITTED line (what the search believed)
   - `[traj]` — what the real game actually did
   - `[bp-pay]` — one line per cast from BOTH sides: turn, cost, floating mana, untapped sources
4. **Read the first difference.** The signature tells you the subsystem:

   | first thing that differs | subsystem |
   |---|---|
   | `[bp-pay]` cost/float/untapped | mana payment |
   | `[bp-pay]` identical but `libtop` differs | a tutor / shuffle / random choice |
   | both identical but life or creature counts differ | combat, a continuous effect, or a missing SBA |
   | the two sides cast a DIFFERENT NUMBER of spells | something upstream changed the hand |

Targeted traces exist for the two subsystems that have bitten: `MTG_LP_TRACE` (Light-Paws fetch +
shuffle, tagged `APPLY` vs `EXEC`) and `MTG_DISCARD_TRACE` (Gamble's random discard: seed inputs,
hand size, victim). All are inert unless set.

## Found so far

| # | cause | status |
|---|---|---|
| 1 | rollout paid coloured pips off a `colored_creature_only` land for a non-creature spell | **FIXED** (`6bb2791`) |
| 2 | legend rule not enforced as a state-based action in the executor | **FIXED** (`b71e5e3`) |
| 3 | Gamble's "discard a card at random" depends on hand SIZE and ORDER | **measured, NOT adopted** (below) |

Rate per 500 games after 1 and 2: slivers, burn, TH, Knights, Anti-Lifegain, Dragonstorm, Auras all
**0**. Hinata **2**. (Dragonstorm was 4-5 at the suite gate budgets before #1; Auras was 3 before #2.)

## The open case: Hinata (2 per 500)

`PerformTutor`'s random discard seeds on `ap.hand.size()` and then indexes the hand **as stored**, so
the victim depends on the hand's size and order. Neither is part of the lockstep contract: the
rollout pushes staged (Expressive Iteration / impulse) cards straight into hand, while the executor
merges `Player::staged_cards` at its own breakpoints, so the two legitimately hold the same cards in
a different order.

Measured on seed 4010 (predicted win T6, realised T7):

```
[discard] T6 search_count=2 handsize=7 victim=3(Ponder)  hand=[Island,Mountain,Reflecting Pool,Ponder,Reality Spasm,...]   <- rollout
[discard] T6 search_count=2 handsize=8 victim=2(Island)  hand=[Reality Spasm,Distorting Wake,Island,Mountain,...]          <- executor
```

The rollout discarded the second Ponder, so its committed line never casts it. The executor kept it,
cast it, spent one more `{U}`, and reached Crackle with Power one mana short of the lethal X the
search had proved.

**Choosing over a canonical order was BUILT AND MEASURED, and is NOT adopted.** Seeding on stable
scalars only and picking over ascending `m_number` fixes this game exactly (T7 -> T6, divergence
gone) — but Hinata's rate went **2 -> 4 per 500**. Order-sensitivity is only the last link: note the
hand sizes above, **7 vs 8**. The hands already differ in CONTENT, so any index-based pick lands on a
different card however it is canonicalised; canonicalising alone just reshuffles which games diverge.

**Next step — fix the upstream content divergence first.** The extra card appears during turn 5,
whose second main casts Soulfire Eruption. `SoulfireDig` exiles one card per target and its target
count is `1 + (controller life > 9) + #opponent creatures + searched own targets`, so a single
differing life total or creature count changes how many cards land in hand. Confirm that with a
target-count trace on both sides, fix it, THEN canonicalise the discard, then re-measure. Treat the
canonical-order change as staged work, not as a rejected idea — it is correct, just not sufficient
on its own.

## Why this is worth doing

Every one of these silently caps the search: the engine proves a better line than it plays. #1 was
worth -0.010..-0.033 avg across 8 of 8 held-out Dragonstorm cases with zero regressions; #2 took a
tracked Auras game from T5 to the T4 the search had already found, and stopped the executor
double-counting a legendary's cost discount. Neither was reachable by tuning a heuristic.

## A note on #2: how long the duplicate actually lived

The legend rule applies IMMEDIATELY -- a second copy never gets to do anything. Pre-fix the executor
had exactly two sweep sites: Vial deploys (`AIEngine`) and begin-combat (`GameEngine::CombatPhase`,
before `DeclareAttackers`). So the window ran **from the duplicate entering to the next begin-combat
step**:

| duplicate enters in | window |
|---|---|
| main 1 (measured) | rest of main 1, incl. every breakpoint re-solve in it |
| main 2 | through cleanup, the opponent's whole turn, AND all of the next main 1 |

Measured per phase on the pre-fix binary (Auras seed 4227 gi223): T3 MAIN_1 ends holding both
Light-Paws (#38, #39); T3 COMBAT is back to one. So it is not a permanent extra body -- and it could
never ATTACK, since the sweep precedes `DeclareAttackers`. That is the one outcome the old placement
happened to prevent, and it prevented it by accident of placement, not by design.

Everything else was live for the whole window, which is where the damage is: static abilities,
on-enter and on-cast triggers, and anything counting permanents all saw two. Spirit Link resolved
with two Light-Paws out and fetched twice; two Hinata, Dawn-Crowned double-counted a static cost
discount and let the executor cast a Reality Spasm it could not afford.

Two lessons for this bug class:

1. **State the window you measured, not the impression it gave.** An SBA deferred to a later phase
   looks like a correct engine whenever you inspect the board between turns, which is where most
   inspection happens. "Rest of the main phase" was itself an understatement here -- nothing in the
   code bounded it to one phase.
2. **An SBA that "runs a bit late" is not cosmetic.** Late enough to cover the window in which the
   game does things is the same as not running.

If a legend-rule-off effect is ever added (Mirror Gallery), or a name-changing legend (Sakashima):
no such card exists in `cards.json` today, so enforcement is unconditional. Adding one means gating
EVERY `EnforceLegendRule` site, not just the new one.
