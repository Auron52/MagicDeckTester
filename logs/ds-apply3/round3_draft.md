
### Round 3 (ds-apply3): `MTG_ONDEMAND_LUMP_KEEP_OWED` (2026-11-27)

Target: the Kolaghan declared cluster of the round-2 gate (`on` arm: 145 flags, 23 (seed, turn) events, every one with
Lotus Bloom on the board and Dragonlord Kolaghan in the plan). Every event replays at play settings on the round-2
binary with the gate's flag count (single games, oracle on the event turn; `logs/ds-apply3/rep.sh`, `evrun.sh`). In 21
of the 23 the game is still won on the event turn (the search found another line); s30120119 (T6 -> T8) and s30145461
(T4 -> T5) lose a turn.

**Shapes** (by the plan; flags / events):

| shape | flags | events | fixed by the lever |
|---|---:|---:|---:|
| one Apex of Power before Kolaghan | 65 | 9 | 45 flags / 5 events |
| two Apex of Power before Kolaghan | 42 | 8 | 0 |
| no Apex (Ruby / Song / Scourge lines; two with Unclaimed Territory) | 38 | 6 | 0 |

**The mechanism fixed (M1, the lump crack spends the owed colour).** s30120119 T6 {Dwarven Hold, Desperate Ritual,
Seething Song, Apex of Power, Irencrag Feat, Dragonlord Kolaghan}: Desperate (Mountain#31) and Song (Ruby) leave six {R}
floating beside Mountain#26 and Lotus Bloom. Apex's {6}{R}{R}{R} cannot be paid without the Bloom (6 + 1 < 9), so the
on-demand payer's ordinary payment fails, and the lump ladder (step 3) cracks the Bloom in the first colour of
`LumpColourOrder` that pays: {B}, the colour the rest has no other source of. But the float drain then spends all three
{B} on the generic (the six {R} pay the pips and three generic; the bill takes every unit), and Mountain#26 stays
untapped. Apex's ten {R} and Irencrag's seven leave Kolaghan's {B} with nothing. The oracle pays Apex with the Bloom's
{B}{B}, Mountain#26 and the float, and one {B} floats on (trace: `MTG_DS2_PAYDBG`, the round-2 debug snippet with the
`unseen` flag, `logs/ds-apply3/dbg.snippet`). The visible-closer lump rule never sees this board: its ordinary trial
fails, and it stands down on that.

`MTG_ONDEMAND_LUMP_KEEP_OWED` (heurarm `ONDEMAND_LUMP_KEEP_OWED`, DEFAULT ON, `=0` byte-identical): when the lump
ladder's crack pays the bill and that payment STRANDS the rest of the line (`LineRestAfter`'s pips plus its generic),
the bill is paid again with the lump cracked in a colour x the rest owes and the owed x units set aside for the payment
(`OnDemandPayWith`'s new `keep`: CR 601.2h, the player chooses which mana pays), so the other untapped sources pay the
rest and x floats on. The first x, in the lump's colour order, whose payment leaves the rest payable replaces the plain
payment. Both judgements are exact: `LumpAwareBillPayableOpener` = `LumpAwareBillPayable` (every crack of the lumps
left, the complete backtracker) plus the float of the ritual being paid for and, for an impulse-exile spell cast from
hand (Apex of Power), its ten in each colour the rest has a pip of (payable when some colour pays). It only ever
replaces a payment that provably strands the named line by one that does not; it reads only the named line, so
round-1 item e (need-later past Apex) is out of its reach. A payment choice in the shared payer (executor and rollout
together), at every budget.

Unit test: `test_dragonstorm_line_keep.cpp`, `s30120119 T6 -- Apex taps the Mountain and the Bloom's {B} floats to
Kolaghan` (on: the line pays; off: Kolaghan fails). Line-keep cases 9 / 9.

**Event repros** (lever off / on, the same binary; `=0` game logs equal the round-2 binary's on all 23 games):
KOLD_EVENTS

**What is left in the cluster (not fixed, mechanisms named, not measured further):**
- *Two Apexes.* In every two-Apex line the loss is at the first Apex or before it, and the rest of the line holds the
  second Apex ({7}{R}{R}{R}) as one bill; the exact judge credits only the paying Apex's ten, so the rest reads
  unpayable on both boards and the lever stands down. Hypothesis (unverified): crediting the rest's own producers (the
  second Apex's ten, Irencrag's seven) would let the judge see the strand; that credit is optimistic in sequence (each
  producer pays before it resolves), so it needs its own exactness argument.
- *Float drained through a later generic* (s30132752, s30135813, s30152803; 24 flags): Ruby is paid by the Bloom for
  {B} and the {B} floats; the next Seething Song's {1} drains it. The LINE_FLOAT_TAP_FIRST rung's spare test reads the
  Mountains as non-spare (the rest owes {R}, and only the paying ritual's float is netted, not the later Song's five or
  Irencrag's seven; s30152803), or the rung's spares cannot pay the bill's own {R} pip as well as the held generic, and
  the storage tier does not release Mercadian Bazaar because the held generic already keeps the {B} (s30135813).
- *Unclaimed Territory* (s30115282, s30145461, s30149079): the Territory is the {B}, not the Bloom.
- Round-1 item d (Apex-line Kolaghan, "overkill-only"): it IS this cluster's one-Apex shape; five of its nine events
  are fixed. Item e (Karrthus need-later past Apex) is untouched (the lever reads only the named line).

**Gate**:
Dragonstorm slice, 72,000 games per arm, off vs on (lever `=0` vs default):

| | off | on |
|---|---:|---:|
| declared APPLY-DROP flags | 270 | 231 |
| declared events | 50 | 46 |
| Kolaghan declared flags | 144 | 99 |
| Karrthus declared flags | 35 | 41 |
| Karrthus need-later flags | 279 | 274 |
| APPLY-DROP-ORDER | 66 | 58 |
| ENUM-GAP | 27 | 25 |
| turns | 313,583 | 313,580 |

Units -0.019%, CPU +0.045%. One new event (Karrthus s30106357 T5) is in a block whose play moved (10 of 362 blocks
differ). Event repros: Kolaghan declared flags 145 -> 100; five events fixed (s30118865, s30120119, s30129637,
s30142598, s30155333; the last now wins T4 instead of T5); with the lever off all 23 game logs equal the round-2 binary.

**Held-out play A/B**:
every suite deck, standard and 2HG, seed base 5310000 + gi, game_index = gi; 43,800 pairs. Net 0, 0 slower and 0
faster in every shape (Dragonstorm 3,000 + 3,000 pairs included). Units -0.00% overall (Dragonstorm 2HG -0.01%), CPU
-0.05% (noise). No slower game, so no d8 b0 replay was owed.

Verdict: **ADOPT, default ON** (`MTG_ONDEMAND_LUMP_KEEP_OWED`; `=0` byte-identical). It removes 45 Kolaghan declared flags
with no slower game anywhere and no CPU cost. Still open: the two-Apex shape (42 flags), the later-Song float (24) and
the Territory {B} source (3), plus the need-later Karrthus cluster.
