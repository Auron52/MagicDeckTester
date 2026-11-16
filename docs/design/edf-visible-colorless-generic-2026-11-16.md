# EDF: spend floating colorless on generic costs when no sink is visible

The EDF deck needs `{C}` for Eldrazi Displacer's blink activation and for
Essence Depleter or Dimensional Infiltrator when finishing the opponent. The
old payer therefore spent flexible `wild` mana before floating `{C}` on every
EDF generic cost. That also kept `{C}` when none of these activation sinks was
visible, even though spending it would leave the more flexible unit available
for a later colored pip.

The adopted payment preference spends untagged floating `{C}` before `wild`
on an EDF generic cost only when no `{C}` activation sink is on the battlefield
or in hand and no finisher is reachable through Living Wish. A cost's own
`{C}` pip and a later `{C}` pip already owed by the line retain their separate
hold. The prepay's kept `{C}` retains its existing behavior. This adds no
searched action and reads no unseen card. `MTG_LEGACY_EDF_WILD_FIRST=1` restores
the old EDF order; the default is the conditional preference.

The focused unit cases check a no-sink payment, Displacer on the battlefield,
Essence Depleter in hand, and a Depleter reachable through Living Wish. Both
the trial-on and trial-off forms passed all four focused cases (21 assertions).

The first 1,000 paired EDF games (seeds 10,600,001–10,601,000) had zero
win-turn changes and 13 changed play digests. After restricting the sink scan
to payments where floating `{C}` and `wild` could actually compete, a fresh
5,000-pair block (seeds 10,610,001–10,615,000) also had zero win-turn changes
and 37 changed play digests. Search work was 47,800,026 old versus 47,798,918
new units (-0.0023%); summed job time was 7,769,578 versus 7,648,790 ms
(runtime noise). Manifests, `.wins`, units, and batch logs are under
`build/scratch/edf_visible_c_ab/`.

The integrated Release binary passed all 3,358 unit cases and 3,614,095
assertions. The four focused cases also pass under the adopted default and
under `MTG_LEGACY_EDF_WILD_FIRST=1`.

The deck-scoped combined smoke/regression batch finished all nine rows and
2,490 games. No game's win turn changed relative to committed ground truth.
Four row digests moved: EDF smoke depth zero, EDF regression depth zero, and
two searched depth-five rows. The strict viewer protocol passed all 864
references with zero play drift, board divergence, or enumeration gaps; both
require-beat references passed. The two searched play changes are extra casts
on already lethal turn four: game 106 in the seed-2002 row adds Wild Growth and
Fertile Ground, and game 51 in the seed-3003 row adds several mana and combo
pieces. A same-binary control with `MTG_LEGACY_EDF_WILD_FIRST=1` reproduced
all nine committed `.wins` files byte for byte, so these changes belong to
the adopted payment preference. The ground-truth ledger was internally
consistent (1,252 entries, no stale or missing entries) before acceptance.
The combined logs and legacy-control files are in `build/scratch/edf_visible_c_ab/`.

The deck-scoped `--both --deck=edf --accept` accepted the four changed `.wins`
files and corresponding row digests. `python3 test/check_gt_logs.py` then
reported 1,252 consistent entries, zero stale, zero missing. A fresh
mana-clean gate remains due.
