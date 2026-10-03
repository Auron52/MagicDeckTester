# Matching the user's win turns on KittyEquipment v2 (2026-10-03)

**The ask, in the user's words:** *"We absolutely need those win turns matched at minimum"*, and on
the diagnosis: *"This has nothing to do with the leaf or any of those ranking thing. It is a search
hole if we don't find this line."* A later message authorised the shape of the fix: *"If needed we
could add a 'go off' heuristic to give the search more legs for cases like this ... The idea of the
heuristic is to find a win and otherwise it can just not use it. **It needs to play it out so that
the projection is accurate.**"* That last sentence turned out to be the whole answer, which is worth
saying plainly: the half that moved the win turns is the one the user named, not the one I expected.

The measurement throughout is `scripts/ref_bench.py --deck kittyequipment_v2_puresteel_hammer`: the
14 hand-played references, each replayed with its recorded opening hand forced (`--force-mulligan`)
so **only play varies**, pooled into ONE `mtg --batch`. The deck has no `value_play` sidecar yet, so
every arm runs at the default depth 5 / 20 ms budget.

**Result: 3/14 short -> 1/14 short, pooled average 3.714 -> 3.500, with four games now FASTER than
the human.** All of it from `MTG_EQUIP_ROUTE` + `MTG_EQUIP_ROUTE_SOLVE`, both adopted default ON.

## The hole: the equip-chain go-off was emitted but never selectable

A go-off route needs TWO halves, emission AND selection, and only the first existed. Measured in one
reference game: **7,423 emissions of the route action, 0 plans carrying it, 0 applies.** The action
lands in `independent`, so it ought to be a one-bit odometer position -- but `sel` is seeded with
`auto_sel` (this deck's metalcraft equip {0} puts several there), and
`SubsetHasComboRouteWithOthers` rejects any ComboRoute subset of size >= 2. There is never a subset
of size 1, so the route was rejected at every position. EDF's sibling route works only because of
`TurnSolver::EdfComboOffShortcut`, which is gated behind `MTG_EDF_CO_ROOT` / `MTG_EDF_CO_LOOK` --
**both default OFF**.

The fix is a short-circuit in each of the two enumeration hosts, beside the Dragonstorm storm cut
and the board-lethal cut: pass the route index **alone** (which sidesteps `auto_sel` without touching
a filter EDF also reads), apply it for real on a copy, and keep only that plan when the apply is
lethal.

**The verify predicate is the one real difference from the EDF cut.** EDF's blink damage is a drain
paid on the spot, so `OpponentHasLost` is true at end of main. An equip chain's kill is **combat**
damage, so the test has to be the realised board's own `PendingAttackDamage(copy) >= life` -- the
same quantity the board-lethal sibling already relies on as real, cast-independent damage rather
than an optimistic projection. A route whose apply no-ops (the live board diverged from the trial's)
fails the test and the frame enumerates normally, so no plan can ever carry a win turn its replay
does not reproduce.

### The root cut alone is not enough -- the ROLLOUT host is what moved the win turns

Isolated on `claude_s1_gi0`, with the root (`EnumeratePlans`) cut already in:

| config | win turn | digest | units |
|---|---|---|---|
| shipped (route off) | 4 | `08d32f9bfb345505` | 39,679 |
| root cut only | 4 | `51124837bb379c60` | 10,089 |
| **root + `SolveUncached` cut** | **3** | `826baa754f2d95fe` | 31,739 |

The mechanism is exactly the one the user described. `SolveUncached` is the rollout/greedy host, so
without the cut a playout that is one equip chain from lethal scores the turn as ordinary board
development -- and every frame *above* it then ranks the line that REACHES the chain no higher than
the line that misses it. Matching at the root is not enough; the projection has to agree with what
the executor will do.

Across all 14 references the twin takes 2/14 -> **1/14** and 3.643 -> **3.500**.

## The turn-1 theory, and why it was wrong

All four shortfalls share a striking turn 1. In `claude_s1_gi0`, `claude_s2_gi1`, `claude_s5_gi4` and
`claude_s11_gi10` alike the human played:

```
t1 pre_main: land=Remote Farm; cast: (nothing)
```

A land that enters tapped, a watcher stranded above the available mana, and the free Equipment
deliberately kept for the turn the watcher is on the battlefield. The shipped search instead cast
Bone Saw and Dragonfire Blade on turn 1 **for zero cards** while holding Puresteel and two Srams.

`MTG_FREE_CAST_HOIST` (default ON, adopted 2026-10-02) forces every `{0}` Equipment cast into
`auto_sel`, and `auto_sel` rides every enumerated subset by construction -- so "hold this Equipment"
is not mis-ranked, it is **unenumerable**, at any depth and any budget. `FreeEquipmentCastIsDominant`
lists the Sram / Puresteel draw among the monotone-upward axes that make the cast dominant, and that
is true only of a watcher that is live NOW.

The reasoning is sound and the repair still measured as a dead end. Two shapes were built and both
are default OFF:

| arm | shortfalls | avg | note |
|---|---|---|---|
| `MTG_FREE_CAST_HOIST=0` outright | 4/14 | 3.857 | **worse** -- loses `claude_s11_gi10` |
| carve-out, presence-gated | 3/14 | 3.786 | fixes s1_gi0, loses s11_gi10 AND s2_gi1 |
| carve-out, castability-gated | 3/14 | 3.643 | same two lost |
| hold line (one plan) | 2/14 | 3.643 | **inert**: identical digest to route-only |

* **`MTG_FREE_CAST_HOIST=0` outright is the wrong lever.** The frames with no watcher in hand
  genuinely want the collapse.
* **The carve-out fails on COST, not ranking.** Returning k free-Equipment casts to `groups`
  multiplies the odometer by 2^k in *every* frame holding a watcher -- and on this deck those frames
  include turn 3, the 36,854- to 419,900-plan go-off frames where the kill actually has to be found.
  At a 20 ms budget it bought the holding line by starving the search that has to use it. Narrowing
  the gate to "a watcher that cannot be cast in THIS frame" recovered the average but not the two
  lost games, because turn 3 holds an uncastable Puresteel too.
* **The hold line is reachable but never chosen.** `MTG_FREE_CAST_HOLD_LINE` adds exactly ONE
  constructed plan per frame -- the forced members minus the free-Equipment casts, i.e. play the land
  and pass -- for one evaluated plan instead of 2^k. It demonstrably reaches the search (units 10,089
  -> 10,780 on s1_gi0) and the play digest does not move, on that game or on `claude_s5_gi4`. So the
  turn-1 line is now in the space and the evaluator declines it.
* And once the rollout cut was in, **none of the three changes s1_gi0's play at all** -- same digest
  `826baa754f2d95fe` with the carve-out, the hold line, both, or neither. The turn-1 hold was never
  the thing standing between the search and the human's turn 3.

All three are kept, default OFF, because they are what makes the above a measurement rather than an
assertion.

## Human play and unpruned stay fully open (a defect the scenarios caught)

The cut returns the route as the frame's ONLY plan. That is right for the search -- a verified kill
dominates under turn-to-win -- and **wrong for a menu**: it deletes every other affordable cast from
the human's options. `test/scenarios/kittyv2_valve_covers_every_affordable_cast.json` and its
floating-mana sibling, both built from the user's own rejected T3 frame, went
`legal_not_enumerated` on "cast Colossus Hammer" until both cuts were gated:

```cpp
&& !DecisionUnpruned(UnprunedGate::ComboLine) && !HumanPlayActive()
```

which is how all three sibling cuts already do it. The **emission** site is deliberately not gated,
so the human menu keeps COMBO OFF as an option; it is only the collapse-to-one-plan a menu must
never see.

## Adoption evidence

`bash test/regression.sh --smoke`, accepted:

* **3 configs changed, 107 unchanged.** Every other deck is byte-identical -- the route is gated on
  `EquipRouteEnabled()`, which only the Equipment provider overrides.
* All three kittyv2 configs **better**: d0 5.0330 -> 4.8840 (1000 games), d3 4.3200 -> 4.1700 (100),
  d5 4.2400 -> 4.1000 (50).
* Per-game audit: searched **24 faster / 2 slower**, d0 **139 faster / 0 slower**.
* Both slower games (`kittyv2_smoke_d3_s1001` gi46 and gi75, 4 -> 5) classify as **churn** under
  `test/classify_turn_later.sh smoke` -- each recovers to turn 4 at 4x and at 16x its case budget.
* `MTG_EQUIP_ROUTE=0 MTG_EQUIP_ROUTE_SOLVE=0` reproduces the pre-adoption digest exactly
  (`08d32f9bfb345505`, 39,679 units on s1_gi0), so the cost A/B survives adoption.
* 129/129 scenarios pass; `python3 test/check_gt_logs.py` -> 638 consistent, 0 stale.

It also **removes** work where it fires without needing the extra depth: 39,679 -> 31,739 units on
s1_gi0 (0.80x) while turning that game from turn 4 into turn 3.

## Levers

| flag | default | what it does |
|---|---|---|
| `MTG_EQUIP_ROUTE` | **ON** | emits + selects the verified equip-chain go-off (root cut) |
| `MTG_EQUIP_ROUTE_SOLVE` | **ON** | the same cut in `SolveUncached` -- the rollout projection |
| `MTG_FREE_CAST_HOLD_LINE` | OFF | adds the one "hold the Equipment" plan per frame (inert) |
| `MTG_FREE_CAST_WATCHER_CARVE` | OFF | the rejected 2^k carve-out, kept as the A/B control |
| `MTG_EQUIP_ROUTE_TRACE` | OFF | per-iteration playout / gate-census / EMIT / CUT diagnostics |
| `MTG_EQUIP_ROUTE_MAX_ITER` / `_PROBE` | 24 / 12 | playout bounds; both bound a SOUND detector, so they can only cost a missed win |

## The one remaining shortfall: `claude_s5_gi4`

Human turn 3, search turn 4, and it is **a reachability hole, not a budget one**: at `depth=8
budget=0ms` the digest is unchanged (`15fd465a2b092f19`) and the win is still turn 4. Neither the
hold line nor the carve-out alters that game's play at all.

The route's own trace says where it stops. The gate passes all-true 20 times at turn 3, so the
playout runs -- but it only ever CUTs at **turn 4**, and the turn-3 playouts end `no win: 4 link(s),
2 iter(s)` with the opponent still at 20 life. So the chain is being built and is simply not lethal
on the boards the search reaches, well short of `MaxIter` 24.

What the human did on that turn 3, across four breakpoint segments: cast a SECOND Puresteel Paladin,
cast Colossus Hammer (+10/+10), and then `equip all free (10) -> Puresteel Paladin` -- the whole pile
onto ONE host. Remote Farm taps for `{W}{W}`, which is what pays for a 3-mana Paladin plus a 1-mana
Hammer off three lands.

**Next suspect, and it is not the chain-advance policy.** My first guess was that
`equiproute::Run`'s "advance by the plan that casts the most" strands the mana that buys the Hammer,
but that cannot be it: `MTG_FREE_CAST_HOIST` forces every free Equipment cast into *every* plan, so
cast counts differ only by the NON-free casts and "most casts" already prefers casting the Paladin
and the Hammer. The likelier cause is the **equip-host collapse** -- the auto-equip fold that takes
one creature per frame (`ConsolidatesEquips`, `MTG_EQUIP_ALL_HOSTS` to open it) -- diluting the pile
across hosts instead of stacking all ten onto the Paladin the way the human did. That is a separate
thread from this one and wants its own measurement.

## Still owed

* `claude_s5_gi4`, above.
* The **regression and overnight GT tiers** have not been re-run; only smoke is accepted. kittyv2 is
  the only deck whose play moved, and its regression-tier rows are knowingly stale.
* The **full 442-reference corpus sweep** -- the only thing that can answer "are the user's win turns
  matched on every OTHER deck too". Queued in `docs/design/reference-repair-2026-10-03.md`; it needs
  the box to itself.
* The cost side of the user's adoption bar on a large held-out sample (deck aggregate net <= 0). The
  smoke audit above is the train-seed half of it.
