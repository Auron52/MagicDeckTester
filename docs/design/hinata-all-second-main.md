# Hinata: every cast in the second main

**Status: BUILT, MEASURED, DOES NOT PAY AT 20 ms. The idea is mechanically sound and the blocker is
identified.** USER, 2026-08-29: *"I think in Hinata we can actually drop the 1st main ... i.e. Do
everything 2nd main"*, and *"It's still expensive unless we can properly handle breakpoints,
though."* Both halves turned out to be exactly right, the second one decisively.

## Why the classification is correct for this deck

USER: *"Everything can be classified as 2nd main in Hinata. This is because the only attacker is
Hinata itself and there are no pumps (or haste or anything else of that type)."* The
`MainPhase` question is "does this help THIS turn's attack?" and on this 60 nothing does: the only
body that attacks is Hinata herself (summoning-sick the turn she lands), Ornithopter of Paradise is
a 0/2 mana dork, and there is no pump, anthem, haste or haste-granter in the list. So the Main2
dominance argument holds card-for-card.

## Why it was worth trying: the cost side works, completely

From `cantrip-class-affordability.md`, the class (site 3) costs **0.41 plies** of committed depth at
20 ms. The per-site partition says the search cost per turn is the PRODUCT |pre| x |post|, so
collapsing `pre` attacks it multiplicatively where a candidate prune cannot. Measured, 300 games,
d5/20 ms:

| arm | mean committed depth | depth-1 share | `fs_pre` units | class work overhead |
|---|---|---|---|---|
| control | 3.233 | 10.2% | 4,816,363 (30.0%) | -- |
| + class (s3) | **2.819** | **19.7%** | 4,612,621 (28.4%) | 1.49x / 1.51x |
| + class + all-main-2 | **3.463** | **3.0%** | **1,673,907 (8.8%)** | **1.24x / 1.09x** |

The collapse does not merely repay the class's 0.41 plies -- it **overshoots the control by 0.23
plies**, drops depth-1 decisions from 19.7% to 3.0%, cuts first-main interior nodes 2.9x, and takes
the class's per-game work overhead from ~50% down to 9-24% (block total -13%, the cheapest arm
measured). Mechanically the idea did everything it was supposed to.

## Why it still does not pay: main 2 is a second-class search node

`FSLineTail` -- the second main -- **has no deferred breakpoint wave phase.** `FSLineWin` (the first
main) runs `BpWaveWalker` over continuation ranks W.., which is the thing that makes the class
complete ("no rank is unreachable at an unbounded budget"); `FSLineTail` has no such phase at all.
Confirmed two ways: by reading the code (no `BpWavesHere` call between the two function bodies), and
by the counter -- `fs_bp_wave` collapses **1,767,296 -> 5,057** when the casts move to main 2.

So moving every cast into main 2 moves them into a phase where the breakpoint continuation is
truncated to **wave 0, the top W=2 ranks, at any budget** -- silently, and it is exactly the lossy
truncation the USER's standing bar rejects. That is why the arm searches DEEPER and plays WORSE.

**And widening is not the fix at 20 ms.** s3m2 at W = 2 / 4 / 8, paired, 1200 games:

| W | hold | train |
|---|---|---|
| 2 | -0.0175 | -0.0308 |
| 4 | -0.0892 | -0.1083 |
| 8 | -0.1233 | -0.1367 |

Monotone worse. Width buys continuation completeness with depth, and at this budget depth wins --
which also means ADDING the missing wave phase to `FSLineTail` would lose at 20 ms too, since it is
strictly more expensive than raising W.

## The full table, all paired, 1200 games/cell, d5 / 20 ms, vs the control

| arm | hold | t | train | t | per-game work |
|---|---|---|---|---|---|
| s3 -- the class alone | -0.0233 | -2.15 | -0.0292 | -2.68 | 1.49x / 1.51x |
| drop -- MTG_MAIN2_DROP alone | -0.0442 | -4.95 | -0.0475 | -4.85 | 2.16x / 2.20x |
| dropnd -- ...+ MTG_NO_DEFER_DROP | -0.0408 | -4.87 | -0.0367 | -4.01 | 2.13x / 2.15x |
| m2 -- all main 2, no class | -0.0850 | -6.17 | -0.1258 | -9.78 | 1.51x / 1.45x |
| m2nd -- ...+ NO_DEFER_DROP | -0.0900 | -6.45 | -0.1242 | -9.12 | 1.57x / 1.49x |
| **s3m2 -- class + all main 2** | **-0.0175** | -1.32 | **-0.0308** | -2.67 | **1.24x / 1.09x** |
| s3m2 W=4 | -0.0892 | -5.89 | -0.1083 | -7.61 | 2.36x / 2.05x |
| s3m2 W=8 | -0.1233 | -7.78 | -0.1367 | -9.03 | 3.37x / 3.04x |

**Nothing beats the control at 20 ms.** The best arm (s3m2) is indistinguishable from it on hold
(t = -1.32) and a real loss on train (t = -2.67).

Note `m2` WITHOUT the class is a disaster (-0.126) and adding the class recovers most of it
(-0.126 -> -0.031). Moving the cantrips to main 2 makes breakpoint handling MORE important, not
less -- the USER's warning, measured.

## Refuted here, so it is not re-tried

* **The DEFER-THE-DROP tie-break is not the cause.** `MTG_MAIN2_DROP` silently also enables a
  tie-break that prefers HOLDING the land, which contradicts Hinata's USER-reviewed land-first
  doctrine (`LandDropCastOrderRank = 0`) -- a good suspect, and wrong: `MTG_NO_DEFER_DROP` is inert
  to slightly worse on every arm (m2 -0.0850 -> -0.0900 hold, -0.1258 -> -0.1242 train).
* **The learned value leaf is not the cause.** Hinata's sidecar was fit to main-1 play, so the arm
  could have been judged off-distribution. Measured with `MTG_VALUE_MODEL=0` in BOTH arms: the m2
  penalty gets WORSE, -0.0717 / -0.0783 (t -6.41 / -7.11). The value model was masking part of the
  penalty, not creating it.
* **The 2026-08-16 land-drop failure IS closed.** That run's root cause (cantrips classified Main2
  while the drop stayed main-1-only, so the deck made "3 drops in 8 turns") reproduces exactly
  without `MTG_MAIN2_DROP` -- 40 games: control 5.7500, all-main-2 **5.9250**, all-main-2 + drop
  5.7500. The dependency edge is genuinely fixed; it is simply not what is costing the quality now.

## Where this leaves the project

The remaining direction is NOT another prune and NOT more width. It is to make the breakpoint
continuation structurally cheaper rather than wider: today each `(base plan x continuation k)` pair
is a separate top-level Plan that re-derives the shared prefix AND the shared tail, which is why the
class multiplies the whole subtree. The USER named this in the previous session -- *"It doesn't make
much sense to derive part of the turn and then rederive it after the breakpoint"* -- and the
apply-time attempt (`MTG_BP_PARTITION_CANTRIP`) failed because truncation at APPLY time still
derives the tails. The untried layer is the enumeration/search-node one: search the prefix once,
treat the breakpoint as a real search node, and search the continuation once.

If that lands, this doc's arm becomes worth re-measuring immediately: the collapse's depth surplus
(+0.23 plies over the control) is real and is currently being spent on a truncated continuation
search.

**RE-MEASURED 2026-08-29 (same day): the node landed (`MTG_BP_NODE`, `bp-node-partition.md`) and
the all-main-2 arm got WORSE with it** — node+all-main-2 is −0.0483 hold (t −3.42) / −0.0675
train (t −5.25) vs s3m2's −0.0175/−0.0308, sitting exactly on the breadth ladder above. So the
missing-wave-phase attribution in this doc is DEAD — full continuation hosting does not rescue
the collapse. **But the idea is NOT refuted on its merits (USER 2026-08-30: "It should be at
least even" — and the dominance argument above is game-correct, so the residual penalty must be
an ENGINE asymmetry of the m2 host).** The arm is now a detector for that asymmetry; the open
investigation lives in `bp-node-partition.md`.

## Levers this added (all default OFF, byte-identical unset: 150 games, digest 0deb791369e50e31)

| flag | what it does |
|---|---|
| `MTG_HINATA_ALL_MAIN2` | `HinataProvider::ClassifiesMainPhases` + a `MainPhaseOverride` returning Main2 for every card |
| `MTG_MAIN2_DROP` | pre-existing; now also a heurarm slot so it can vary per job in one pooled batch |
