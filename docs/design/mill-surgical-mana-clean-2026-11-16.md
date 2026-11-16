# Mill: Surgical payment choice in the first mana-clean gate

The first complete 5,000-game PGO gate for Mill (`codex-boars-mill-red-20261116`,
seeds 30,000,000–30,004,999) reported 208 `ENUM-GAP` and 152 `ORACLE-GAP`
flags. Every issue plan contains Surgical Extraction. Oracle-on/off play was
byte-identical. Red Elementals in the same gate was clean; Boars was not.

Mill normally offers Surgical only as a life-paid {B/P} cast while life allows.
`SubsetPhyrexianDominated` assumed every life-paid action had a mana-paid twin
in its candidate group. It could therefore discard the only Surgical action
in a selected line. The guard now retains a life-paid action when the provider
explicitly says it has no mana-paid twin. This is a prerequisite for the
dominance argument, not a new payment policy.

On exact seed 30,000,242, turning off Phyrexian dominance removed its miss;
the new guard also leaves that seed clean. On seed 30,000,033 the miss count
fell from three to two. A 500-game Release sample on the first gate block fell
from 18 to 12 `ENUM-GAP` flags, with zero oracle gaps. The sampled mean moved
from 5.116 turns on the original PGO binary to 5.108 on the corrected Release
binary; that cross-build comparison is descriptive, not a paired performance
claim. The full Release unit suite passed 3,362 cases / 3,614,418 assertions.

The remaining sampled gaps all pair Visions of Beyond with Surgical. Some
oracle lines pay Surgical with black mana and preserve two life; Mill's normal
menu offers only the life-paid cast. Others reach the same visible board but
leave a different blue/black land untapped. `MTG_UNPRUNE=surgicalcost` restores
both payment choices: on seed 30,000,033 it removes all three original misses,
but on the 500-game corrected sample it leaves eight `ENUM-GAP` flags, raises
oracle work from 176,005 to 248,050 ms, and changes the mean from 5.108 to
5.110 turns. Its 86 `DOMINATED-LIFE` lines are disclosed by the oracle, not
counted as issue flags. Opening this searched cost choice does not clear Mill;
the resource-comparison and breakpoint cases need separate investigation.

The 152 original `ORACLE-GAP` flags cluster around a turn-six fetch + Visions
+ Surgical line, including seed 30,003,370. The oracle's nearest outcome can
tap a different land or retain an extra floating black mana after Visions'
draw continuation. This is an observer completeness question, not evidence of
a dropped cast. No clean verdict is claimed for Mill.
