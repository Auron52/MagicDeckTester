# Worldfire integration regression audit (2026-11-22)

The integrated LTB, Worldfire cantrip, tutor-width, and free-turn changes were rebased onto the shared Merfolk gate and built together. The serial unit suite passed 3,948/3,948. One pooled `regression.sh --both` run completed 632 cases in 21m15s of batch time; its 608 scenarios passed. The strict viewer sweep replayed 940 references: 0 play drift, 0 board divergence, 0 enum gap, 0 contract failure. `check_references_safe.py` also passed before the run (112 worktrees).

All 632 case rows were compared with the pre-rebase integration run using played count, mean, digest, and search units. Exactly 16 rows differ, all in Merfolk or Goblins after the shared Merfolk gate's generic scaled-growth search fix. Every Worldfire row and every other deck's row matches the pre-rebase measurement. The 133 changed keys and the searched-game audit below are relative to the rebased shared ground truth.

## Ground-truth attribution

The tier has 133 changed keys: 68 smoke and 65 regression. The Worldfire keys move with the cantrip and free-turn rules (the nine solo case means improve by a sum of 9.223 turns across their distinct rows; both 2HG rows improve). Cyclone, Shadowborn, Sea Creatures, Prevent Damage, Elementals, and Extra Turns move under the free-turn rule. Hinata, Goblins, Melira, Soldiers, and Creature Giving move under the tutor-width removal and merit rules. Worldfire mass exile's LTB routing had already been shown byte-identical on its isolated branch.

The global bounceland opening change accounts for 60 changed keys whose common game is smoke gi436 or regression gi985 (43 and 17 keys respectively). Forty-eight keys outside the named rule/tutor decks contain only that one changed game. Creature Burn additionally has smoke gi519 and gi894 and regression gi39, all same-score digest changes; an isolated four-game run with `MTG_OPP_BOUNCE_OPENING=0` reproduced the old committed win turns **and digests exactly** for gi436, gi519, gi894, and gi39. Maze's End regression gi168 is the previously traced T5→T6 Burgeoning/Manabond response to a Karoo opponent receiving a basic in its opening hand. These are opponent-model changes, not unexplained search behavior.

The automatic per-game audit found 10 searched-depth slower smoke games and 18 searched-depth slower regression games. At 4×/16× budget, 20 recover to the old turn or better. The eight that persist are:

| Case | Old→new | Attribution |
|---|---:|---|
| Cyclone smoke d3 gi40; regression d3 gi27 | T4→T5 each | `MTG_FREE_TURNS=0` reproduces T4; free-turn search changes the line. Cyclone's case means improve overall. |
| Shadowborn 2HG smoke d3 gi47 | T5→T6 | `MTG_FREE_TURNS=0` reproduces T5; free-turn rule changes the line. |
| Prevent Damage 2HG regression d5 gi24 | T7→T8 | `MTG_FREE_TURNS=0` reproduces T7; free-turn rule changes the line. |
| Prevent Damage regression d5 s2002 gi36, s3003 gi5 and gi45 | T5→T6 each | `MTG_FREE_TURNS=0` reproduces T5 in all three; free-turn rule changes the line. |
| Maze's End regression d5 gi168 | T5→T6 | The bounceland opening, as above; `MTG_FREE_TURNS=0` alone still gives T6. |

The free-turn rule is the user's chosen mode, including its fairness underestimation and changed search objective. Prevent Damage loses a small amount in these tier rows (sum of case-mean deltas +0.024 solo, +0.013 2HG); Cyclone improves by -0.558 and Shadowborn by -0.101 across their rows. The per-game losses are recorded rather than hidden by those averages. No unexplained changed key remains.

After this audit, `regression.sh --both --accept` promoted the run's measured results without rerunning cases. `check_gt_logs.py` reports 1,409 consistent keys, zero stale or missing. Overnight ground truth was left untouched.
