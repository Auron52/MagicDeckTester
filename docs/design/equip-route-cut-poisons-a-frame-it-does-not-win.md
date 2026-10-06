# The equip route's go-off cut deleted the line that wins, in a frame it could not win from (2026-10-06)

Status: ROOT-CAUSED AND FIXED (`MTG_EQUIP_ROUTE_CUT_TOPMAIN`, default ON). Found by the post-rebase
ground-truth run, not by a screen, and it blocked the kitty smoke/regression GT re-accept.

## The failure

`kitty_smoke_d3_s1001` game index 237 (kitty **v1**, `decks/KittyEquipment/KittyEquipment.cod`,
d3 b10, per-game seed 1238):

| engine | win turn | units | digest |
|---|---|---|---|
| origin tip c7487fb9 (no equip route) | **T3** | 740 | 9301b8f1ea971125 |
| ours, route ON, before the fix | **T4** | 6,703 | 6a19122b948d4514 |
| ours, `MTG_EQUIP_ROUTE=0` | **T3** | 740 | **9301b8f1ea971125** |
| ours, route ON, AFTER the fix | **T3** | 1,739 | **9301b8f1ea971125** |

The route-off digest was byte-identical to upstream, so the equip route alone accounted for the
regression. It was never a budget problem: T4 persisted at d3 b40/b160/b640/b2560 (units saturate at
~46,950, so the search was running to completion), and at d1, d2, d3 and d5 alike.

## What it actually was

The trace said `t3 LETHAL` 424 times, `EMIT t3 cand#6`, `CUT t3 verified pre=1` six times,
`APPLY real=0` 208 times and `APPLY real=1` **zero** times -- the kill proven over and over and
never played. Three narrowings located it:

* `MTG_EQUIP_ROUTE_SEARCH=0` and `MTG_EQUIP_ROUTE_MEMO=0` both still give T4, so neither the Search
  rung nor the verdict memo.
* `MTG_EQUIP_ROUTE_TOPMAIN=1` gives T3 -- so the harm is in the route's MID-TURN frames. But that
  flag gates the gate, trial and emission together and cannot say which.
* Splitting it: restricting only the CUT to the turn's first main gives T3, and keeping the cut's
  plan ADDITIVELY (verified and flagged, but without deleting the frame's other plans) gives T3 too
  -- the same digest and the same 1,739 units. **The deletion was the defect**, not the detection.

The mechanism, in this game's terms. Kitty's turn 3 wins by casting Colossus Hammer and O-Naginata,
each triggering Puresteel Paladin's draw, and casting the Colossus Hammer the second draw finds.
That makes the turn a BREAKPOINT: the frame that matters is a mid-turn continuation
(`spells_cast_this_turn == 2`, one untapped land, the drawn Hammer in hand). The route proves a kill
there and the cut returns it as the frame's only plan -- which deletes the continuation that actually
wins. Upstream's committed line records it explicitly:

```
phase pre=1 land=Plains + Colossus Hammer + O-Naginata | bp[k0:Colossus Hammer k13:... k13:...] bp_choice=0@0
```

And a base plan carrying a breakpoint **is never scored on its own, only through its children**
(FullSearchLine: *"the pending base plan itself is never scored -- its children were"*). Collapse the
children to one, lose that one, and the whole turn-3 line disappears silently. The commit-the-line
search then reported `win=4` and committed a two-phase T1 line that casts the Hammer on turn one:

```
ours before:  [fd] T1 line win=4 searched_depth=1 verified=0 phases=2
upstream:     [fd] T1 line win=3 searched_depth=3 verified=1 phases=5
```

So the frame was valued on a line that was never played -- the lockstep law, violated in the
direction the law exists to prevent -- but the proximate cause was the cut's *scope*, not its
certificate. The kill it proves is real; the re-verify applies it and checks `PendingAttackDamage`.

## The fix

`EquipRouteCutTopMainOnly()` (`MTG_EQUIP_ROUTE_CUT_TOPMAIN`, default ON, `=0` restores the pre-fix
arm). Both go-off cut sites -- `EnumeratePlans` and its `SolveUncached` twin -- now collapse the
frame **only when `state.spells_cast_this_turn == 0`**, i.e. where the frame's plan IS the turn's
plan and "a verified kill dominates under turn-to-win" is a sound dominance argument. Mid-turn the
route plan is still offered, still re-verified and still flagged `wins_this_turn`, so a frame whose
only win is the chain still reports one; it simply no longer deletes its siblings.

This is the same `spells_cast_this_turn == 0` restriction the Dragonstorm (EDF) go-off cut already
carries, and the restriction `MTG_EQUIP_ROUTE_TOPMAIN`'s own comment argues for on cost grounds.

At the `SolveUncached` site the snapshot restore is skipped when the route was verified mid-turn
(`keep_route`): restoring it there would delete the route from the frame entirely, which is the
opposite error. Nothing downstream re-collapses on `best.wins_this_turn` -- the three sibling cuts
that do all sit ABOVE this one.

## What this does NOT fix: the route cannot be replayed from a recorded continuation

Independently of the cut, `AIEngine`'s `replay_recorded` (the executor's half of a committed
breakpoint continuation) dispatches `PlayLand`, `ActivateVial`, `CastFromHand`,
`CastFromGraveyard`, `SacForMana`, `Suspend`, `DigDraw` and `IsTrailingActivation` kinds --
and **`ComboRoute` is in none of them, nor in `IsTrailingActivation`**. The search's own continuation
apply (`apply_continuation_plan` -> `apply_plan_actions`) *does* handle it, provider-dispatched. So a
committed line whose continuation is the route scores as a win in the search and realises **nothing**
in the executor. It is reachable now that mid-turn frames can keep the route as their best plan.
Tracked separately; the fix is a `ComboRoute` branch in `replay_recorded` mirroring
`ApplyPlanDirect`'s provider dispatch.

## Why it matters more than one game

* **It is the user's standing rule, verbatim:** *"The only excuse for not finding a win should be
  that we didn't have enough budget to find it. Anything else is a definitive bug."* Here the win
  WAS found, repeatedly, and then deleted.
* **It failed the repo's own adoption bar.** `docs/design/no-greedy-in-search-window.md` requires
  that every slower game recover at `--depth 8 --budget-ms 0`. This one recovered at no budget.
* **The route is still net strongly positive**, which is why this was invisible until now: on the
  same run kittyv2 improved by 0.192 (d0), 0.170 (d3) and 0.200 (d5), and kitty v1 d0 by 0.076.
  The aggregate hid a reproducible one-turn loss. A collapse that deletes plans needs a soundness
  argument per FRAME KIND, not per deck average.

## Reproduce

```
build/Release/mtg --batch logs/kittyv2_swaps/probe237.manifest.json --threads 1                      # T3 (fixed)
MTG_EQUIP_ROUTE_CUT_TOPMAIN=0 build/Release/mtg --batch logs/kittyv2_swaps/probe237.manifest.json --threads 1   # T4 (pre-fix)
MTG_FD_TRACE=1 ...        # the committed line per turn: win, searched depth, verified, phases
MTG_EQUIP_ROUTE_TRACE=1 ...  # LETHAL / EMIT / CUT (now prints spells= and collapse=) / APPLY real=
```

`gi=238` in the same GT row is ordinary budget churn by contrast (T5 -> T6 at d3 b10, recovers to T5
at d3 b160, d5 b200 and d8 b400) and needs no fix.
