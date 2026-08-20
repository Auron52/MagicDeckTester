# The executor pays for an Equip whose host is not on the battlefield

**Status:** open, NOT fixed. Found 2026-08-20 on KittyEquipment; deck-independent (it is in the
shared executor path, so any deck with Equipment can hit it).
**Frequency:** 2 of 1,270 logged equips (0.16%), in 2 of 600 games — and identical in all three
lever arms measured, so it is pre-existing and not lever-induced.
**Cost when it fires:** the equip cost, paid for nothing, plus a game log that claims an attach that
never happened.

## The signature

In a game JSON the ability string reads `equip -> #<number>` instead of `equip -> <host name>`.
`AIEngine.cpp`'s `bf_name()` falls back to `"#" + number` exactly when no permanent with that number
is on the battlefield, so the numeric form IS the bug, and it is greppable:

```
grep -l 'equip -> #' logs/<dir>/*.json
```

## Worked case

`seed 500001` game 559, turn 2 main 1. Card #48 is **Puresteel Paladin, still in hand** (it is in the
opening hand and never cast — the battlefield holds only two Plains, Bonesplitter and Sol Ring, with
no creature at all). The logged action is `ABILITY Bonesplitter "equip -> #48"`.

Puresteel Paladin costs {W}{W} and was not castable that turn: one Plains had been spent on Sol Ring,
leaving one white source. So the plan carried an equip onto a host it never put onto the battlefield.

## Why it costs mana

`AIEngine.cpp` (Equip branch):

```cpp
ManaPool avail = AvailableManaPool(state);
if (!EquipmentAttachedTo(...) && TapForCost(state, EquipActionCostNow(...), avail, false))
{
    ApplyEquip(state, state.active_player_index, a.sac_source_id, a.sac_victim_id);
    if (m_logger) { m_logger->LogAbility(..., "equip -> " + bf_name(a.sac_victim_id)); }
}
```

`TapForCost` runs — and pays — before `ApplyEquip` is called. `ApplyEquip` then finds `host_ok ==
false` (no controlled creature with that number) and returns without attaching. Nothing rolls the
payment back. In the worked case metalcraft was off (Puresteel Paladin being precisely the card still
in hand), so Bonesplitter's equip {1} cost a real Plains. Under metalcraft the equip is {0} and the
waste is only the log entry.

The log is emitted unconditionally after `TapForCost` succeeds, so it records an attach that
`ApplyEquip` declined — which is why the board snapshot afterwards shows nothing attached.

## Proposed fix (two lines, but it forces a GT rebaseline)

Guard the payment on the host actually being there, and log only on a real attach:

1. before `TapForCost`, require a controlled creature (or animated permanent) with
   `a.sac_victim_id` on the battlefield — the same predicate `ApplyEquip` already applies;
2. move the `LogAbility` call to after a *successful* attach.

**Why this is not done unilaterally.** `GameLogger::LogAbility` folds into the PLAY DIGEST
(`FoldStr("A"); FoldInt(...); FoldStr(ability);` runs before the `m_digest_only` early-out), so
change (2) alone moves every digest that contains an equip — KittyEquipment, FiveColour (Greaves),
Mirrorwing — and change (1) alters mana spent, so it moves play. Either way the full three-tier
ground truth needs rebaselining for a defect that fires in 1 game in 300.

**And it should be measured, not assumed.** This repo has three separate cases on record where an
obviously-correct mana-accuracy fix measurably LOST (see the measure-before-fixing lesson: pessimism
in the projection acts as a tempo prior). This one looks different in kind — it is the EXECUTOR
wasting mana on a proven no-op, not a projection heuristic — but the expected effect is far below
measurement resolution at this frequency, so a fix would be unverifiable rather than verified.

## Open question worth more than the fix

How does an Equip action naming a hand-side host survive into an executed plan at all? The
enumerator has co-selection machinery for "cast the creature, then equip it" (see the `hand-side:
co-select` note in `KembaLoopKind`). The worked case looks like a co-selected pair whose CAST half
was dropped — the host was never castable that turn — leaving the equip half orphaned. If that is
right, the same orphaning could drop other co-selected pairs' first halves, and the equip is just the
visible one because it logs a resolvable-looking action. That is the thing to chase, not the
two-line guard.
