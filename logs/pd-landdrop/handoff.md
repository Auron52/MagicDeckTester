# pd-landdrop status (2026-11-21 04:55 UTC, second agent)

Branch `wip/pd-hold-landdrop`, rebased onto origin/phase-1-2-deck-analyzer (src 9cf45118). Fix commit f98cfb65f
(src/ai/ManaPayment.cpp, src/ai/HeuristicArm.h, test/unit/test_prevent_damage.cpp): no later-hand red hold while the
live plan traits name no mana cast and the line opens nothing unseen. Lever MTG_PD_CASTLESS_LINE_NO_HOLD (heurarm slot),
default ON, =0 control. TEMP selector / diag removed. WIP commits 5b8c18a6e, 931c85509, 8eff1a997 carry only logs/.

Mechanism confirmed: the real T7 lifelink payment runs with plan traits mana_casts=0 and no line cost bound (lu=0),
so variants 1/2 (keyed on the line cost) never fired there; variant "castless plan" does.

chain3 (Release, rebased tree): all 3804 unit cases pass; mana-clean seeds 0 APPLY-DROP on/off (byte-identical);
gi47 case on = T8 (off unwon); gi47 d8 b0 on = T8, 362K units (off unwon, 2.87M units).

Running: A/B `logs/pd-landdrop/run_ab.sh` (box lock; out build/scratch/claude_pd_landdrop/ab/, DONE marker ab/DONE;
compare with `python3 logs/pd-landdrop/cmp_ab.py build/scratch/claude_pd_landdrop/ab`), then
`logs/pd-landdrop/run_tiers.sh` (smoke+regression, MTG_PGO=0, tiers.status / tiers.log / tiers.DONE).
Remaining: d8 b0 every slower A/B game; audit tiers; accept smoke+regression GT here if net <= 0 per deck; check_gt_logs.
