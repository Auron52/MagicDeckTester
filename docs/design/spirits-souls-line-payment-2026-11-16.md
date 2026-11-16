# Spirits: finish Souls flashback and the following creature

The fresh Spirits gate on games 10,000–14,999 found 61 `APPLY-DROP`
flags in three games. Fifty-eight involved Lingering Souls flashback
`{1}{B}` and White Orchid Phantom `{W}{W}` with Azorius Chancery and a
white creature-only land. The other three involved Souls and Millicent
`{W}{U}`. The oracle found paying continuations in all cases; the engine's
shared payer spent a source or floated color the later cast needed.

`MTG_SPIRITS_SOULS_LINE_HOLD` is default on and `=0` restores the old payer.
For the named Spirits `{1}{B}` payment, one held attempt uses the line's
remaining pips and the visible sources:

* With two later white pips, hold the creature-only white land and use
  Chancery's spare blue unit for Souls' generic pip.
* With a later white and blue pip, hold Chancery and use an ordinary land
  for Souls' generic pip.
* If Chancery is already tapped and both white and blue are floating,
  keep those units for Millicent and let an untapped black source pay
  Souls. The scarce-source mask is released for this attempt because the
  already-floating blue covers its later pip.

Every held attempt restores the board on failure and then follows the
ordinary payer. The change uses only the named line and visible mana
sources; it does not add or search plans.

Exact replays of gate seeds 30,014,133, 30,010,478, and 30,013,710 now
have zero `APPLY-DROP` or `ORACLE-GAP` flags. With the switch off they
restore 45, 13, and 3 drops. The full Release unit suite passes 3,368
cases. On 2,000 fresh paired Spirits games (seeds 30,015,000–30,016,999),
both arms averaged 4.6230 loss-penalized turns; every win turn and play
digest matched, and total work units were equal. The paired logs are under
`build/scratch/spirits_souls_hold_ab/`. Spirits and two-headed Spirits
combined smoke/regression passed all ten cells with no changed game or
digest. All 561 scenarios passed. Strict viewer replay of 864 references
reported zero play drift, board divergence, or enumeration gaps; both
suboptimal references remained beaten. The next fresh gate is pending.
