# Landfall late Myriad greedy repair (2026-11-10)

The final six-deck smoke at `12b43b665` changed three Landfall depth-0 games
from T8 wins to unwon by the horizon: gi31, gi86 and gi624. The full regression
changed gi201 the same way. The committed GT and final-run `.wins` digests were
reproduced by their respective snapshot binaries before comparing turn traces.

All four games agreed through the T7 draw. At T7 the baseline cast Omnath,
Avenger or Phylath; the final run instead cracked Myriad Landscape and left the
payoff in hand. The T8 draw difference follows that shuffle and is not the
cause. `MTG_SOLVE_PROJ_TRACE=7` showed why: the generic greedy creature score
for these non-haste payoffs is zero at T7 (its six-turn attack estimate is
exhausted), while each Myriad crack scores one point. With two Myriads, the
activation-only subset scores two; the cast-plus-crack subset ties it and loses
the mask tie-break.

`LandfallProvider::ComboCardValue` now gives the three token-making payoffs a
score one point above the number of Myriads on the board, only in the real
depth-0 runner at T7 or later. It leaves the score alone when another
token-making landfall watcher is already on the battlefield: cracking Myriad
is then itself the payoff. The first, broader floor caused a searched d3
slowdown and a depth-0 slowdown on such watched boards; both disappeared with
these guards. Searched play is outside the depth-0 runner and remains unchanged.

Validation with the isolated Release build based on `12b43b665`:

| Row | Final before fix | Fixed | Per-game comparison |
| --- | --- | --- | --- |
| Landfall smoke d0, 1,000 | 7.4430 | 7.3620 | 81 faster, 0 slower, 96 same-score digest changes |
| Landfall regression d0, 1,000 | 7.5000 | 7.3970 | 103 faster, 0 slower, 93 same-score digest changes |
| Landfall smoke d3/d5 | 6.0400 / 5.7300 | same | `.wins` byte-identical |
| Landfall regression d3 s2002 / d5 s3003 | 5.8800 / 5.7067 | same | `.wins` byte-identical |

The four lost wins now finish T8. These are targeted Release checks, not a
replacement for a fresh full regression after integration.
