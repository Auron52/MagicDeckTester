# Dragonstorm smoke gi31: rollout Storm hold

## Reproduction and cause

The committed smoke baseline snapshot (`logs/snapshots/smoke-baseline`, dirty
`4bded6426`) wins Dragonstorm s1001 gi31 on T4. The current pre-fix binary wins on
T5 even at d3 with an unlimited budget. The paired pre-fix trace plays Sandstone
Needle on T1 and keeps both Lotus Blooms in hand until T2; the fixed trace removes
both Blooms from hand on T1 and wins on T4. The trace format does not log the
suspend actions themselves, so the hand state is the evidence for their timing.

This is a search-quality error, not a missing legal plan. The T1 double-suspend
candidate is enumerated. Under the pre-fix d3 rollout it is valued as a T9
continuation, while the land-only candidate is valued as T6. Disabling the
Storm-hold payoff prune makes the double-suspend line win T4. The rollout is too
pessimistic when a storm finisher is in hand: it refuses a ritual used for a fair
Dragon even though the searched continuation benefits from the early Blooms.

The change passes `storm_in_hand=false` to `SubsetWastesAccelerant` only when
`g_rollout_nest>0`, at both the explicit ritual and implicit on-demand Bloom
call sites in `Solve`. The real d0 runner (`g_rollout_nest==0`) keeps the existing
Storm-hold policy. `EnumeratePlans` and the search branch list are unchanged.
Both call sites are guarded by `PrunesAcceleratorWithoutPayoff`, which only
`DragonstormProvider` overrides to true; other decks cannot enter them.

## Paired evidence

All arms use the same deck, profile, seeds, and pre-fix/fixed source except for
this change. The pre-fix binary was saved before rebuilding the isolated branch.
The d3 and d5 smoke comparisons use `ignore_play_profile=true` to set the exact
depth and budget.

| Arm | Games | Pre-fix | Fixed | Per-game score changes | Play changes |
| --- | ---: | --- | --- | --- | ---: |
| d0 s1001 b0 | 1,000 | 4.9620 / `8666da55b491a4d5` | identical | 0 | 0 |
| d3 s1001 b10 | 100 | 4.4100 / `2dab94f2b747087b` | 4.4000 / `75ae295debb59ff9` | gi31 T5→T4 only | 2 |
| d5 s1001 b20 | 150 | 4.4400 / `b9f2486bd505a595` | identical | 0 | 0 |
| 2HG d5 s1001 b20 | 50 | 4.6800 / `98b5986ad8a8e499` | identical | 0 | 0 |

At d3 b0 on the first 32 seeds, the pre-fix average/digest was
4.3125 / `fe251e6873c0ad51`; the fixed result was
4.2812 / `5bb297b76dae7773`. Again only gi31's score improved. The other
play change, gi20, wins on T5 in both arms: the pre-fix line plays Mercadian
Bazaar on T1, Mountain on T2, and Ritual→Ruby Medallion on T3; the fixed line
plays Mountain on T1, Bazaar on T2, and Medallion on T4, then reaches the same
T5 Apex/Dragonstorm kill. This is legal timing variance with no score loss.

`python3 test/repros/check_dragonstorm_rollout_hold_gi31.py build/Release/mtg`
is the focused seeded scenario. It asserts d0 T7 and searched d3 T4 on gi31.
It fails against the saved pre-fix binary (d3 T5) and passes against the fixed
binary. The paired blocks support this narrow search improvement; they are not
a claim about every Dragonstorm seed block or a full regression result.
