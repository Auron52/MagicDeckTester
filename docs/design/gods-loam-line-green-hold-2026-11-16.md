# Gods Loam line green payment

The fresh pooled mana-clean gate `codex-mana-queue-20261115` found two
`APPLY-DROP` games on original Careful Study → Life from the Loam plans:
seeds 30,037,134 (turn 5) and 30,037,273 (turn 4). The land targets are
chosen when Loam is cast, after Study resolves. The plan and the targets
were available; the Study payment spent the green-capable Tendo Ice Bridge
or Vivid Creek to avoid Cephalid Coliseum's one life, then could not pay
Loam's {1}{G}. The oracle paid Study's {U} with Coliseum, leaving the green
source for Loam.

`MTG_GODS_LOAM_GREEN_HOLD` makes the Gods provider invoke the shared
payment hold-up when Loam is in hand and the current named line still owes
green. The hold-up changes a source only if the current and later bills are
jointly payable and the ordinary payment strands the later bill. It does
not add a post-breakpoint plan or change how Loam targets are selected.
The switch remains default off after the held-out result; `=1` enables the
trial for focused oracle replays.

The focused two-game oracle replay had two `APPLY-DROP` flags with the
switch off and zero with it on. The full Release unit suite passed 3,353
cases and 3,614,055 assertions. A fresh 1,000-game paired Release pilot
(seeds 9,300,001–9,301,000) had 986 wins in each arm. The on arm was two
turn-games faster (two games won one turn sooner, none slower), averaging
5.559 versus 5.561; it used 50,898,680 versus 51,856,891 search units
(1.85% fewer). Raw data are in `build/scratch/gods_loam_hold_pilot_*.{out,err}`.

A fresh 5,000-game paired PGO check (seeds 9,301,001–9,306,000) rejected
the candidate: 4,901 wins in each arm, 5.5758 average turns off versus
5.5764 on. Two games slowed and none improved; game index 3,185 won on
turn 6 off and turn 8 on, and index 3,364 won on turn 5 off and turn 6 on.
The on arm used 263,337,248 search units versus 263,327,766 off, but
38,527,000 recorded game milliseconds versus 34,366,846 off (12.1% more;
the median matched job ratio was 1.118). Raw data are in
`build/scratch/gods_loam_hold_held_*.{out,err}`.

The two regressions reproduced as individual PGO logs in
`build/scratch/gods_loam_regress/`. In both, the on arm chose a turn-one
Careful Study; the off arm took a faster Faithless Looting or land-building
line. The extra payable Study → Loam continuation changed the bounded
search's earlier choice. The result is a genuine play-quality tradeoff for
the current search, not an oracle false positive. Gods remains beta with
the two Study → Loam `APPLY-DROP` cases outstanding. A narrower or more
efficient continuation fix is still needed; no ground truth was updated.
