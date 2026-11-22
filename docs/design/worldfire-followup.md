# Worldfire keep-order follow-up (2026-11-22)

## 1b: larger paired holdout

The integrated binary (Worldfire cantrip rules, tutor width, and free turns default ON) ran 500 new paired games at fixed d5/b20, 250 at seeds 9300001+gi and 250 at 9400001+gi. Each option changed one runtime lever against the adopted baseline. The table gives its mean credited win turn and faster/slower game count relative to the baseline within each block; lower is better.

| Option | Block A mean, faster/slower | Block H mean, faster/slower |
|---|---:|---:|
| Adopted baseline | 4.240, — | 4.272, — |
| Aggressive dig with spare blue | 4.240, 0/0 | 4.272, 0/0 |
| Bazaar cutoff after turn 1 | 4.244, 0/1 | 4.272, 0/0 |
| Keep dig in aggressive Preordain | 4.248, 0/2 | 4.272, 1/1 |
| Keep finisher in aggressive Preordain | 4.232, 2/0 | 4.276, 0/1 |

No option is consistently better. The adopted defaults stay in place, following the user's preference to keep the designed rule when differences do not separate on a larger set. The paired manifest and per-game results are in `build/scratch/worldfire/cantrip/ab4/` in the primary build volume.

## 1c: independent experiments

Four temporary, default-off arms in `WorldfireKeepRank` were built and tested independently: (1) dig slot 2 above the next land when three mana are already on the board; (2) the third dig above further land drops when both pieces are missing; (3) make one fewer future land a quota when both pieces are missing and at least two board mana exist; (4) cap tutors retained for combo, finisher, and acceleration at two. The current baseline already retains a third dig as a quota when both pieces are missing, but ranks it below quota lands. The focused Worldfire unit tests passed 1,708/1,708 assertions with the arms present.

The paired d5/b20 batch used 500 new games in two 250-game blocks, seeds 9500001+gi and 9600001+gi. The table shows mean credited win turn and faster/slower counts against the baseline in each block.

| Option | Block A mean, faster/slower | Block H mean, faster/slower |
|---|---:|---:|
| Adopted baseline | 4.412, — | 4.260, — |
| Dig slot 2 above next land | 4.412, 0/0 | 4.260, 0/0 |
| Dig slot 3 above further lands | 4.408, 1/0 | 4.260, 0/0 |
| Skimp one future land | 4.412, 1/1 | 4.260, 0/0 |
| Cap kept tutors at two | 4.412, 0/0 | 4.260, 0/0 |

No change repeats as a gain in the held-out block. The baseline stays; the temporary levers were removed. The paired manifest and per-game results are in `build/scratch/worldfire/cantrip/ab5/`.
