# pd-landdrop status (2026-11-21 04:55 UTC, second agent)

Branch `wip/pd-hold-landdrop`, rebased onto origin/phase-1-2-deck-analyzer (src 9cf45118). Fix commit f98cfb65f
(src/ai/ManaPayment.cpp, src/ai/HeuristicArm.h, test/unit/test_prevent_damage.cpp): no later-hand red hold while the
live plan traits name no mana cast and the line opens nothing unseen. Lever MTG_PD_CASTLESS_LINE_NO_HOLD (heurarm slot),
default ON, =0 control. TEMP selector / diag removed. WIP commits 5b8c18a6e, 931c85509, 8eff1a997 carry only logs/.

Mechanism confirmed: the real T7 lifelink payment runs with plan traits mana_casts=0 and no line cost bound (lu=0),
so variants 1/2 (keyed on the line cost) never fired there; variant "castless plan" does.

chain3 (Release, rebased tree): all 3804 unit cases pass; mana-clean seeds 0 APPLY-DROP on/off (byte-identical);
gi47 case on = T8 (off unwon); gi47 d8 b0 on = T8, 362K units (off unwon, 2.87M units).

Was running: A/B `logs/pd-landdrop/run_ab.sh` (box lock; out build/scratch/claude_pd_landdrop/ab/, DONE marker ab/DONE;
compare with `python3 logs/pd-landdrop/cmp_ab.py build/scratch/claude_pd_landdrop/ab`), then
`logs/pd-landdrop/run_tiers.sh` (smoke+regression, MTG_PGO=0, tiers.status / tiers.log / tiers.DONE).

## DONE 2026-11-21 15:00 UTC
A/B (Release, 32 threads, 9 min, seeds 9,100,001+ PD / 9,200,001+ PD 2HG, lever =0 vs =1): PD 4000 pairs 5.4410 vs
5.4410, net 0, 0 faster / 0 slower, 1 digest changed, units +0.01%; PD 2HG 2000 pairs 5.6550 vs 5.6550, net 0, 0/0,
0 digests changed, units -0.00%. No slower game -> no d8 b0 owed beyond gi47 (recovers T8).
Tiers (smoke+regression, Release, 43 min): ALL PASS, 235 + 375 cases byte-identical, viewer 0 drift -> no GT to accept.
check_gt_logs: 1358 consistent. Overnight GT (s6006 gi47 should go unwon -> T8) left for the periodic overnight run.
