# Reflecting Pool with a last-counter Gemstone Mine

## Miss and fix

Gods seeds 30031096 and 30031276 reach a turn-three plan that casts Life
from the Loam followed by Faithless Looting or Careful Study. Their only
non-Reflecting land is Gemstone Mine with one mining counter, alongside two
Reflecting Pools. The old payment order spends Mine on Loam, sacrifices it,
and leaves both Pools unable to make mana for the second cast. The oracle
pays Loam with the Pools, then the second spell with Mine. Both `APPLY-DROP`
reproductions disappear with that order; both games still win on turn five.

The shared payer now ranks that last-counter land behind an untapped Pool
when it is the sole non-Reflecting land. A Pool must still be able to pay the
current pip. The rule keeps the land available for a later payment, and the
old order remains available with `MTG_LEGACY_RP_ANCHOR_ORDER=1`. A cheap
precheck avoids the full land-definition scan unless an untapped
last-counter sacrificial land is present.

## Paired play

The Release trial used `MTG_RP_ANCHOR_FIRST=0/1` on the same 5,000 fresh
Gods seeds 8,300,001 onward, with the shipped play profile (depth six,
20 ms). The first 1,000 pairs were the pilot; the remaining 4,000 were an
extension, with each arm run sequentially at 32 threads. The old order
averaged 5.5458 loss-penalized turns, the Pool-first order 5.5452:
**three turn-games faster, three games faster, none slower**. Summed search
units fell from 252,387,769 to 251,993,088 (0.16%). Summed thread time
rose from 46,911,980 to 48,030,020 ms (2.38%) in the unoptimized trial.
The final precheck avoids the full land scan on ordinary payments. On the
1,000 pilot seeds it matched all 20 trial job digests and all search units;
summed thread time was 9,387,199 ms, versus 9,432,572 ms for the trial and
9,211,643 ms for the old payer. Thus the measured pilot still carries a
1.9% runtime cost relative to the old order. The
per-game data and manifests are in `build/scratch/rp_anchor_ab/`.

## Final checks

The final Release rebuild passes the focused seven-assertion test in both
default and legacy modes, and the full suite passes 3,308 cases and
3,612,756 assertions. The optimized default-on implementation has exact
digest parity with the measured trial across all 1,000 pilot games.

The combined Gods/Gods2HG smoke and regression batch completed all eight
cells. Every searched-depth result, including both two-headed cells, matched
ground truth exactly. Only three depth-zero loss lines changed play digest:
smoke game 497 and regression games 60 and 844. The payer lets those lines
cast an additional spell earlier, but all three remain unwon by turn eight.
Each was inspected with `test/explain_game.py`; neither tier has a slower
game. Both targeted tiers were accepted, and `test/check_gt_logs.py` reports
1,231 consistent entries. A fresh Gods mana-clean gate is
still owed after the other known misses are fixed.

## Colour-specific anchor beside other lands (2026-11-15)

The sole-land test was too narrow for Gods seed 30035319. Its turn-five board
has Forest, Cephalid Coliseum, one-counter Gemstone Mine, and Reflecting Pool;
another Coliseum can be played. Magus of the Bazaar's `{1}{U}` spends Mine's
last counter for `{U}`, so the Pool loses its only red-producing land. A later
Faithless Looting flashback drops. The oracle instead pays Magus with Pool and
Coliseum, leaving Mine's red for Looting. Five `APPLY-DROP` flags in that game
vanish with a colour-specific anchor test. The three other focused drop games
are unchanged, and all four per-game play logs are byte-identical.

`MTG_RP_COLOR_ANCHOR` extends the Pool-first preference only when the
last-counter land supplies at least one colour no OTHER non-Pool land offers.
The land need not be the sole non-Pool land. This is still a tap-order rule:
both sources must be able to pay the current pip. The shared payer applies it
in both rollout and execution, default on with `=0` restoring the earlier
sole-land-only order.

On 1,000 fresh paired Gods games (seeds 9,000,001–9,001,000), the prior order
averaged 5.550 turns and the colour-specific order 5.549. One game won a turn
earlier, none won later, and three others changed play digest without changing
win turn; both arms won 981 games by turn eight. Search units fell from
45,728,719 to 45,714,519 (0.03%). Summed thread time rose from 7,132,044 to
7,278,867 ms (2.1%) in sequential Release runs, which is not a paired wall
measurement. The source for both arms was identical; only the default-off
trial flag differed. Data are `build/scratch/gods_color_anchor_ab/`. After the
colour-bitmask optimization, all eight targeted games reproduced their trial
play digests exactly, including the four oracle repros and the four play-changed
games. The full unit suite passes 3,350 cases and 3,614,032 assertions.

The combined Gods/Gods2HG smoke and regression run passed all 561 scenarios
and the strict reference replay (329 ok, 534 repaired, one beaten, zero drift
or gaps). Seven of eight result cells matched committed ground truth. The
eighth, Gods2HG regression play game 0, still wins on turn six: it now casts
Careful Study and Life from the Loam on turn four, then Mystic Retrieval on
turn five, instead of Looting twice on turn four and Study on turn five.
Disabling `MTG_RP_COLOR_ANCHOR` in the same binary reproduces the prior per-game
digest exactly. No game slowed, so the reviewed Gods2HG regression row was
accepted. The new source has no other regression changes in these decks.
