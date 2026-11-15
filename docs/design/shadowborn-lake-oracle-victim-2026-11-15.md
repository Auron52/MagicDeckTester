# Shadowborn Lake sacrifice oracle coverage

The latest Shadowborn mana-clean block had one `ORACLE-GAP` on seed 30,077,370,
turn seven main two. Reaper from the Abyss costs `{3}{B}{B}{B}`. The engine
cracked Lake of the Dead #16, which first tapped Swamp #50 and then sacrificed
it, and also tapped Swamp #55. The oracle grouped the two identical Swamps
into one source class and always chose the first physical copy, #50, for its
one explicit Swamp tap. Once Lake sacrificed #50, that script failed, so the
oracle incorrectly claimed it could not reproduce the engine's board.

`BuildSources` now keeps potential land-sacrifice victims by physical card
number while a crackable on-demand land is present. This lets the payment
table choose Swamp #55 as the explicit tap and leave #50 for Lake's sacrifice.
The exact payment is still verified by the real tap and payer; the change
only adds candidate scripts. The focused seed now reports zero oracle gaps,
with the same play digest and work units. A unit fixture pins the two-Lake,
two-Swamp Reaper payment; the full Release suite passes 3,351 cases and
3,614,042 assertions.

Only Shadowborn's shipping deck contains Lake of the Dead. On 1,000 fresh
paired Shadowborn games (seeds 9,300,001–9,301,000), the old PGO binary and
new Release binary had identical win turns, play digests, and 17,399,131
search units. Their wall times are not comparable across compiler modes. The
ten old issue seeds were then replayed on both binaries: all ten play digests
and units matched, the old `ORACLE-GAP` disappeared, and the 15 `APPLY-DROP`
plus five `APPLY-DROP-ORDER` reports remained.

Scoped Shadowborn/Shadowborn2HG smoke and regression finished all 11 cells,
561 scenarios, and 864 strict viewer references with zero viewer drift or
gaps. One regression cell had a same-turn game-75 fingerprint difference from
committed GT. The *old* PGO binary reproduced the new binary's exact 150-game
fingerprint and game-75 digest; the Lake change did not cause this drift. The
current game wins legally on turn five after Diabolic Intent finds Thrumming
Stone and Shadowborn Apostle activates. The filtered regression GT accept
promoted only that one current-source digest; `check_gt_logs.py` reports all
1,252 files consistent. No game became slower.

Raw probes and paired data: `build/scratch/gods_counter_mask_ab/shadow_*`.
The remaining Shadowborn payment and order issues need separate repairs and
a fresh 5,000-game mana-clean block after those fixes.
