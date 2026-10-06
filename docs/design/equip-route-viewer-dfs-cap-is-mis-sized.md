# The equip route's VIEWER node cap costs 13 minutes per decision, and it stalls the regression tier (2026-10-06)

Status: OPEN, measured, not fixed. **It makes `test/regression.sh --regression` unable to finish**,
so the reference-reproducibility gate is currently un-runnable on the kitty decks. Found while
running that tier for the go-off cut's scope fix; it is NOT caused by that fix (see below).

## The measurement

One spawn of the reference-reproducibility sweep's replay chain -- `kittyv2 claude_s8_gi7`, the step
the sweep re-enters per decision point:

```
build/Release/mtg decks/KittyEquipment/v2-puresteel-hammer/KittyEquipment.cod --claude-play \
  --seed 8 --game-index 7 --max-turns 8 --depth 0 --profile <...> \
  --choices 16,-1,-1,25,-1,-1,545 --force-mulligan 0: --cast-order <...> ...
```
run under the sweep's own environment (`MTG_PLAY_PLANS_CAP=0`,
`MTG_VIEWER_PLAN_CAP_POSITIONS=8192`):

| arm | wall |
|---|---|
| origin tip c7487fb9 (no equip route) | **0.12 s** |
| ours, `MTG_EQUIP_ROUTE=0` | **0.07 s** |
| ours, `MTG_EQUIP_ROUTE_VIEWER_NODES=1` | 0.44 s |
| ours, `MTG_EQUIP_ROUTE_VIEWER_NODES=64` | 4.0 s |
| ours, `MTG_EQUIP_ROUTE_VIEWER_NODES=256` | 14.5 s |
| ours, `MTG_EQUIP_ROUTE_VIEWER_NODES=1024` | 51.8 s |
| ours, `MTG_EQUIP_ROUTE_VIEWER_NODES=4096` | 196.2 s |
| ours, DEFAULT (`MTG_EQUIP_ROUTE_VIEWER_NODES=16384`) | **~780 s (13 min)** |

So the route accounts for all of it, and the cost is LINEAR IN THE NODE CAP across four doublings
(each 4x of nodes buys 3.6-3.8x of wall, 64 -> 4096). It is the viewer-regime DFS, not the
certificate playout: with the cap at 1 the whole step is back under half a second. Note also that
even 64 nodes is 4 s against upstream's 0.12 s -- 33x -- so no re-sizing makes this step free.

Note the trap in reproducing this: `MTG_EQUIP_ROUTE_VIEWER_NODES=0` is a NO-OP. `ViewerDfsNodes()`
clamps a non-positive value back to the 16,384 default (unlike `RolloutDfsNodes()`, whose `<= 0` is
an honest off switch that `RunSearch` checks). An arm set to 0 measures the default.

## Why the cap is the wrong shape, not merely the wrong number

`ViewerDfsNodes()` bounds the walk in LINKS. A link is "cast one more card and re-enumerate", so its
cost is the cost of a plan enumeration on the board at that point -- which on this deck, under the
viewer's uncapped plan enumeration (`MTG_PLAY_PLANS_CAP=0`), is tens of milliseconds. The same 16,384
links are milliseconds on a narrow board and a quarter of an hour on a wide equipment board, so a
link budget does not bound what it is there to bound. The comment at the regime selection states the
intent plainly -- *"The viewer's menu (false in every rollout and autonomous game): a click must come
back"* -- and 13 minutes is not a click coming back.

Two candidate fixes, both unmeasured:

1. **Bound the viewer walk by WORK, not links** -- the same `ChargeDecisionUnits` /
   `gamework` accounting the in-window walk already uses, with a wall-independent unit ceiling. Then
   the cap means the same thing on every board, which is what a cap is for. This is the principled
   one.
2. **Re-size the constant.** 1,024 is still 52 s here and even 64 is 4 s, so no value makes this
   step cheap -- the responsive range is 16-64 and it is 30-50x upstream even there. That trades
   away menu completeness (a chain the walk no longer reaches is a "COMBO OFF" option the human is
   not offered) for responsiveness, and that trade is the USER'S to make -- `.claude/skills/coding-conventions.md` and the no-greedy doctrine both treat the viewer's
   enumeration completeness as a promise, and `test/scenarios/kittyv2_valve_covers_every_affordable_cast.json`
   exists to hold it. So this file does not change the default.

## What it blocks right now

`test/regression.sh --regression` runs `test/viewer_protocol_check.py --strict --threads $(nproc)`
over the whole reference corpus, and that sweep re-spawns the engine PER DECISION POINT of each
reference. At ~13 min per spawn, one kittyv2 reference is hours and the 22 kitty references are
unbounded in practice. On 2026-10-06 the tier reached that step in 8 minutes and was still inside it
40 minutes later, having advanced one reference; the tier's budget is under 45 minutes total.

The batch half of the tier is unaffected -- it had already written `test/results/regression.env` --
so a run can be stopped there and still accepted. That is what was done for commit 4e20c501, with
the gate recorded as UN-RUN rather than passed.

**Until this is fixed, `VPC_SKIP=1` is the way to run the regression tier**, and any tier run that
way must say the reference gate did not run.

## It is not the go-off cut's scope fix

The cut is disabled under `HumanPlayActive()` by construction (that guard is what keeps the collapse
out of a human menu -- see `docs/design/equip-route-cut-poisons-a-frame-it-does-not-win.md`), and a
route trace of the straggling spawn shows `regime=viewer` and **zero** `CUT` lines. The cost is in
the 15 route commits that are unpushed on `phase-1-2-deck-analyzer`, not in anything added on
2026-10-06, and it has gone unmeasured because the reference sweep runs in the regression tier only
and that tier had not been run since the route redesign landed.
