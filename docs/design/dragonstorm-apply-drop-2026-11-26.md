# Dragonstorm APPLY-DROP: the off-colour sources (2026-11-26)

Mana-clean gate `claude-queue-20261125a` (src `773d4e440d5c`, Dragonstorm games 87000-158999, seed 30000000 + gi):
Dragonstorm had 1,137 issues, 728 of them APPLY-DROP. Branch `wip/ds-apply`. Scripts and numbers: `logs/ds-apply/`.

## The cluster

The 728 flags are 146 (seed, turn) events in 144 games. The dropped cast is almost always one of the deck's two
off-colour closers: Karrthus, Tyrant of Jund `{4}{B}{R}{G}` (77 events) and Dragonlord Kolaghan `{4}{B}{R}` (48). Every
one of the 146 events has Lotus Bloom or Unclaimed Territory on the board, and those (plus Apex of Power's ten of one
colour) are the deck's only `{B}` / `{G}` sources. 112 events are declared drops (the closer is in the plan) and 34
are "need-later" (the closer is beyond an Apex of Power breakpoint and not named by the plan).

The line-keep rung (`LineReducerKeepHold`, crit mode) cannot reach these: its gate counts only lands, rocks and ready
dorks, so a Bloom, a Territory and a floating unit are all invisible to it, and its exact judge (the complete
backtracker) does not model an on-demand Bloom crack at all. The drops come from two of the on-demand payer's own
rules instead.

1. **The visible-closer lump rule cracked a Bloom early on a comparison that reads an uncracked Bloom as no supply.**
   `MTG_ONDEMAND_VISIBLE_LUMP` (Dragonstorm only) cracks a lump for a colour a later cast owes when the early crack's
   board has more of some other owed colour than the ordinary payment's. It subtracts the lump's own wild supply from
   the ordinary board (an uncracked Bloom "cannot replace two distinct Territory colours"), so trading a Bloom for one
   extra `{R}` reads as a gain. s30152662 T5 `{Seething Song, Irencrag Feat, Karrthus}`: `{B}` and seven `{R}` float
   (the first Bloom was cracked for Song); Irencrag's `{1}{R}{R}{R}` is paid by the float alone, but the rule cracked
   the second Bloom for `{B}` (the generic then ate a `{B}` and one more `{R}` stayed), and Karrthus's `{G}` had no
   source.
2. **The leftover swap was too narrow.** `MTG_LEFTOVER_LINE_SWAP` re-attributes a generic pip the float drain paid
   with a colour the line still owes to a leftover unit of another colour (same taps, same float total). It moved only
   units the payment PRODUCED net of the float, and judged the rest without the float of the ritual being paid for.
   s30087280 T4 `{Desperate Ritual, Scourge of Valkas, Irencrag Feat, Dragonlord Kolaghan}`: `{B}{R}{R}` floating;
   Irencrag drained the `{R}{R}` into its red pips and the `{B}` into its `{1}`, then tapped Sandstone Needle for
   `{R}{R}` and floated one. That `{R}` was not "produced" (the float's `{R}` went 2 -> 1), and without Irencrag's seven
   `{R}` Kolaghan's `{R}` read short too, so the swap was refused and Kolaghan lost its `{B}`.

## The change (two levers, DEFAULT ON, each `=0` byte-identical)

- `MTG_ONDEMAND_VISIBLE_LUMP_EXACT`: the early crack is refused when it STRANDS the later bill (the named rest's pips,
  a visible closer's pips past an opener, the rest's generic): the ordinary payment's board pays it and the early
  crack's does not, both judged exactly: `LumpAwareBillPayable` tries every crack of the lumps left (uncracked, or
  cracked for each colour the bill has a pip of) with the complete backtracker, plus the float of the ritual this
  payment is for. In every other case the colour comparison stands as before. (The first version, 893647990, also
  refused the crack when BOTH boards paid the bill; see "The slower game" below for why that was wrong.)
- `MTG_LEFTOVER_SWAP_WIDE`: the swap may move any leftover unit (one that floated before the payment as well as one it
  produced: both were in the pool it spent from, CR 601.2h), and its strand test credits the ritual's float.

Both are mana-payment choices (the standing exemption), in the shared payer: executor, rollout and the payability
walks move together, at every budget. The USER's ruling for this deck (*"spend the counters if they are required for
the line"*) is not engaged: neither lever spends a counter the greedy would not.

Unit tests: `test/unit/test_dragonstorm_line_keep.cpp` (both repro boards, lever on and off, plus a no-strand control).

## Measurements

### Gate, first version (893647990): Dragonstorm slice of the mana-clean gate, MTG_MANA_ORACLE=2

`logs/ds-apply/gate_report.py <gate_out>`; 72,000 games (gi 87000-158999) per arm, the leave-one-out arms on the first
18,109 (`loo`). `AD-decl` = APPLY-DROP flags whose dropped cast is in the plan, `AD-undecl` = need-later ("not
declared"); `ev` = distinct (seed, turn) events.

| arm | gi | AD-decl flags | AD-undecl flags | events decl | events undecl | turns |
|---|---|---:|---:|---:|---:|---:|
| off | all | 415 | 299 | 105 | 47 | 313,645 |
| on  | all | 288 | 363 | 62 | 56 | 313,643 |
| off | loo | 112 | 70 | 33 | 12 | 78,970 |
| on  | loo | 66 | 86 | 17 | 15 | 78,969 |
| no_leftover_swap_wide | loo | 81 | 86 | 21 | 15 | 78,969 |
| no_ondemand_visible_lump_exact | loo | 97 | 70 | 29 | 12 | 78,970 |

Declared events 105 -> 62 (-41%; 44 fixed, 1 new). Leave-one-out: the lump lever fixes 12 of the slice's 16, the swap
4. The 9 new need-later events (all from the lump lever) are in games whose play diverged on an EARLIER turn (a
search choice, e.g. s30106654 casts Song + Lathliss on T3 in one arm only; s30094928 differs from T1): new boards,
not a worse payment of a shared board. Both arms win those three repro games on the same turn. APPLY-DROP-ORDER 68 =
68, ORACLE-GAP 5 = 5, ENUM-GAP 33 -> 41. Units -0.036%.

### Play A/B, first version: every suite deck, seed 5130000 + gi, game_index = gi

`logs/ds-apply/ab_report.py`; 63 decks x (400 std + 200 2HG) per arm, Dragonstorm 3000 + 3000 per arm; 86,400 games.
Every deck but Dragonstorm: net 0, 0 slower, 0 faster (units differ only in Niv and Sea, by <0.01%: the wide swap).
Dragonstorm std: net 0 (0 / 0), units -0.004%. Dragonstorm 2HG: net +1 (1 slower, 0 faster), units +0.054%.

### The slower game (2HG s5131993 gi1993: 5 -> 6, persisted at d8 b0)

Leave-one-out: the lump lever alone. With the first T1/T2 choices forced to the off arm's, the lever arm still won T6:
the divergence was T4 combat (off pumped Scourge of Valkas for 9, the lever arm for 5; the pump's banked-source hold
is searched, MTG_FB_HOLD_SEARCHED). A trace of the rule in that search showed the T5 line {Rite of Flame, Seething
Song, Dragonlord Kolaghan} with `later={4}{B}{R}`, `ord_pays=1 -> crack=0`: the ordinary payment of Song (tap Sandstone
Needle) and the early one (crack Lotus Bloom for {B}) BOTH pay Kolaghan, and the lever refused the crack. But the
crack also keeps the Needle's {R}{R} for firebreathing ({R} per pump), which the Bloom's leftover {B} cannot pay, so
the line without the Needles' pumps no longer killed on T5 and the T4 search held them. The payability test is the
right reason to REFUSE a crack (s30152662: the crack strands Karrthus), not a reason to refuse one that strands
nothing. Narrowed: refuse only when the ordinary board pays and the early one does not. On the narrowed binary the
game is byte-identical to the off arm at play settings and at d8 b0 (5 = 5). Unit test: `s5131993 2HG T5 -- both
boards pay, the early crack keeps the Needle`.

The no-strand control unit test asserted identical taps in both arms; the wide swap legitimately re-attributes which
unit floats (one more land untapped with the levers on). It now asserts both arms pay and leave the same mana, the
levers never tapping more.

### Narrowed lump lever (e07c7849c): gate and held-out A/B

Gate, same slice, the narrowed `on` arm (`on2`) against the first gate's `off` (the `=0` path is unchanged; three
`off` blocks re-run on the new binary are digest-identical):

| arm | AD-decl flags | AD-undecl flags | events decl | events undecl | ENUM-GAP | turns |
|---|---:|---:|---:|---:|---:|---:|
| off | 415 | 299 | 105 | 47 | 33 | 313,645 |
| on (893647990) | 288 | 363 | 62 | 56 | 41 | 313,643 |
| on2 (e07c7849c) | 296 | 304 | 64 | 48 | 33 | 313,643 |

Declared events 105 -> 64 (42 fixed, 1 new); need-later 47 -> 48 (0 fixed, 1 new); ENUM-GAP back to 33.

Held-out play A/B, Dragonstorm only (the lump rule is Dragonstorm-only and the swap's other-deck effect measured nil
above), seed base 5160000, 3000 std + 3000 2HG per arm: net 0, 0 slower, 0 faster in both formats; units -0.00% /
+0.00%. The first A/B's slower game (2HG s5131993) is byte-identical to the off arm at play settings and d8 b0. Full
units on e07c7849c: 4205 / 4205.

### Third lever: `MTG_LINE_FLOAT_RITUAL_NET` (ca4ee7d53, landed as part of land-1128)

The LINE_FLOAT_TAP_FIRST rung's spare-source colour test nets the paying ritual's own float out of `later[]` (the
float pays part of the later bill, so a source the rung keeps for that part is not spare). Mana-payment choice in the
shared payer, default ON, `=0` byte-identical; unit test on/off in `test_dragonstorm_line_keep.cpp`.

Gate (same slice, `on3`): declared events 62, need-later 48, ENUM-GAP 33, AD flags 291 / 306 (on2: 64 / 48 / 33,
296 / 304).

Held-out play A/B of ALL THREE levers off vs on (`mk_ab.py --n 400 --hg 200 --wf 3000 --base 5190000`, every suite
deck, 86,400 games): **43,200 pairs, net 0, 0 slower, 0 faster in every shape**; Dragonstorm std 3000 / 2HG 3000 pairs
0 / 0; units -0.00% total (no shape moves more than +0.02%); cpu +0.03% (wall, noise). No slower game, so no d8 b0
replay is owed.

### Round 2 (ds-apply2): `MTG_CCO_RESERVE_JOINT` and `MTG_LINE_NC_LEFT` (2026-11-27)

Two more payer judgements in the shared payer (executor and rollout together, every budget), each DEFAULT ON with `=0`
byte-identical (six event repros: the `=0` game logs equal the base binary's f5f442801, `runId` aside).

**(a) `MTG_CCO_RESERVE_JOINT` -- two Unclaimed Territories.** The greedy ladder's CCO line reserve (`cco_hold`,
ManaPayment.cpp) releases a colored_creature_only land when, PER COLOUR the line is short of, the OTHER untapped
creature-only lands make that colour (`MTG_CCO_RESERVE_OTHER`). Each other land was counted toward every colour it
makes, but one tap makes one unit: with Karrthus owing {B} and {G} and two Territories, each read the other as covering
both, neither was held, the CCO hand-demand rung tapped one early and Desperate Ritual's generic took its {C}
(s30096797 T5). The release now needs the others to cover the shortfall JOINTLY (Hall's condition over every subset of
the held colours; boards with more than 16 other lands keep the per-colour test). It only adds holds; the backtracker
still runs on any greedy failure. A unit fixture must carry the game's HAND (the hand-demand rung reads it).

**(c) `MTG_LINE_NC_LEFT` -- the line-keep rung's stale noncreature total.** s30129376 T4 {Ruby Medallion, Seething Song,
Karrthus}: Ruby is paid with Lotus Bloom ({B} floats). At Song's payment the floating {B} and Mountain#29's {R} paid
Song's {1}{R}, leaving Karrthus's {B} and {G} to the one Territory, although Sandstone Needle's {R}{R} pays Song and
keeps the {B}. (Round 1's handoff placed the loss at Ruby's payment and wrote a leftover-swap lever,
`MTG_LEFTOVER_SWAP_EXACT`, for it; the debug trace shows the engine's Ruby payment leaves `fl B1` and Song's drains it,
and no unit floats after Song for a swap to move. That lever is DELETED.) The line-keep rung (`LineReducerKeepHold`)
judges the rest of the line in creature mode -- a creature-only land makes a colour -- only when
`g_line_noncreature_mv` is 0 or this noncreature payment's mana value reaches it. That value is the line's WHOLE
noncreature bill (Ruby 2 + Song 3 = 5) and is never counted down, so at Song's payment (mana value 2 after Ruby)
Karrthus was judged as a noncreature bill (Territory {C} only), unpayable however Song was paid, and the rung kept
nothing. With the lever both apply worlds count the line's noncreature casts down at the cast site
(`g_line_noncreature_left`, `LineNoncreatureLeftScope` + `NoteLineNoncreatureCast`, beside the
`MTG_SERVANT_HASTE_KEEP` count), and the rest is creature-only once none is left. It only turns the rest's mode from
noncreature to creature. Payability walks bind no count (every `LineUnpaidCostScope` resets it to -1) and keep the old
test. (The LINE_FLOAT_TAP_FIRST rung does not catch this board either: a depletion land is never "spare" there.)

**Event repros** (oracle on the event turn, APPLY-DROP flags with both levers off / both on / CCO_JOINT only / NC_LEFT
only; "off" is the base binary f5f442801, except s30103016 and s30126750: the new binary at `=0`. The two agree wherever
both were run):

| seed:turn | off | on | joint only | nc_left only | fixed by |
|---|---:|---:|---:|---:|---|
| 30096797:5 | 2 | 0 | 0 | 2 | CCO_RESERVE_JOINT |
| 30112157:6 | 4 | 0 | 0 | 4 | CCO_RESERVE_JOINT |
| 30142602:6 | 6 | 0 | 0 | 6 | CCO_RESERVE_JOINT |
| 30155973:5 | 2 | 0 | 0 | 2 | CCO_RESERVE_JOINT |
| 30089733:5 | 4 | 0 | 0 | 4 | CCO_RESERVE_JOINT |
| 30103016:7 | 5 | 0 | 0 | 5 | CCO_RESERVE_JOINT |
| 30129376:4 | 1 | 0 | 1 | 0 | LINE_NC_LEFT |
| 30126750:5 | 2 | 0 | 2 | 0 | LINE_NC_LEFT |

Units: full suite 4314 / 4314; the line-keep cases 8 / 8, both new tests pass on (pays) and off (fails).

**Gate** (`logs/ds-apply2/mk_gate2.py`: the Dragonstorm slice of claude-queue-20261125a, gi 87000-158999, 72,000 games
per arm, MTG_MANA_ORACLE=2; leave-one-out arms on gi < 105000):

| arm | slice | AD-decl flags | AD-undecl flags | events decl | events undecl | ENUM-GAP | ORACLE-GAP | turns |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| off | all | 284 | 314 | 59 | 48 | 27 | 0 | 313,583 |
| on | all | 270 | 323 | 50 | 49 | 27 | 1 | 313,583 |
| off | loo | 62 | 72 | 14 | 13 | 5 | 0 | 78,957 |
| on | loo | 51 | 72 | 10 | 13 | 5 | 0 | 78,957 |
| CCO_JOINT off | loo | 62 | 72 | 14 | 13 | 5 | 0 | 78,957 |
| NC_LEFT off | loo | 51 | 72 | 10 | 13 | 5 | 0 | 78,957 |

Off -> on: 10 declared Karrthus events fixed (all ten in the table above, plus 30103016 T8 and 30142602 T7), 1 declared
and 1 need-later Karrthus event new (s30145787 T6, s30155815 T6), both in blocks whose play moved (5 of 362 block
digests differ). The one ORACLE-GAP (s30123403 T7) is in a block with an identical digest: the oracle's own probes
run under the levers, so its coverage moved, not the engine's play. Turns 313,583 both arms; cpu +0.1% (wall).

**Held-out play A/B** (`mk_ab.py --n 400 --hg 200 --wf 3000 --base 5220000`, every suite deck std + 2HG, Dragonstorm
3000 + 3000, levers all off vs all on, 87,600 games): **43,800 pairs, net 0, 0 slower, 0 faster in every one of the
128 shapes**; units -0.00% total (largest move boars 2HG -0.01%); cpu +0.09% (wall, per-shape noise within +-2.7%). No
slower game, so no d8 b0 replay is owed. Heartbeat 32/32 throughout.

Verdict: both levers ADOPT, default ON (payer-ordering fixes; play-neutral on the held-out sample, no CPU cost).
Remaining Dragonstorm APPLY-DROP: Kolaghan declared (144 flags) and the need-later Karrthus cluster (270-279), unchanged
by these levers; round-1 items d (Apex-line Kolaghan overkill-only) and e (need-later past Apex) are still open.

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
```
(seed  turn    Kolaghan-declared flags off -> on, all APPLY-DROP flags, win turn)
30092109 4      KolD   5 ->   5  allAD   5 ->   5  win 4.0000 -> 4.0000
30093026 4      KolD   1 ->   1  allAD   1 ->   1  win 4.0000 -> 4.0000
30102129 4      KolD   3 ->   3  allAD   3 ->   3  win 4.0000 -> 4.0000
30115282 5      KolD   9 ->   9  allAD   9 ->   9  win 5.0000 -> 5.0000
30118865 7      KolD   2 ->   0  allAD   2 ->   0  win 7.0000 -> 7.0000
30120119 6      KolD  25 ->   0  allAD  25 ->   0  win 8.0000 -> 8.0000
30127991 4      KolD   4 ->   4  allAD   4 ->   4  win 4.0000 -> 4.0000
30129637 5      KolD   1 ->   0  allAD   1 ->   0  win 5.0000 -> 5.0000
30130531 6      KolD   8 ->   8  allAD   8 ->   8  win 6.0000 -> 6.0000
30131013 6      KolD   5 ->   5  allAD   5 ->   5  win 6.0000 -> 6.0000
30132752 4      KolD   8 ->   8  allAD   8 ->   8  win 4.0000 -> 4.0000
30133545 5      KolD  15 ->  15  allAD  15 ->  15  win 5.0000 -> 5.0000
30134453 6      KolD   4 ->   4  allAD   4 ->   4  win 6.0000 -> 6.0000
30135813 6      KolD   2 ->   2  allAD   2 ->   2  win 6.0000 -> 6.0000
30140711 5      KolD   1 ->   1  allAD   1 ->   1  win 5.0000 -> 5.0000
30141736 6      KolD   3 ->   3  allAD  19 ->  19  win 6.0000 -> 6.0000
30142598 5      KolD   7 ->   0  allAD   7 ->   0  win 5.0000 -> 5.0000
30142750 7      KolD   4 ->   4  allAD   4 ->   4  win 7.0000 -> 7.0000
30145461 4      KolD   4 ->   4  allAD   4 ->   4  win 5.0000 -> 5.0000
30149079 4      KolD   1 ->   1  allAD   1 ->   1  win 4.0000 -> 4.0000
30152803 4      KolD  14 ->  14  allAD  14 ->  14  win 4.0000 -> 4.0000
30155333 5      KolD  10 ->   0  allAD  10 ->   0  win 5.0000 -> 4.0000
30157929 4      KolD   9 ->   9  allAD   9 ->   9  win 4.0000 -> 4.0000
total KolD 145 -> 100, all AD 161 -> 116
```

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

### Round 4 (ds-apply4): `MTG_LINE_SEQ_REST` and `MTG_UNSEEN_SCARCE_RITUAL_NET` (2026-11-27)

Target: round 3's open Kolaghan-declared shapes, re-measured on the round-4 base (land-1129 42fe79749: round 3 + SG50 +
K NOM2, whose search moved several events). The 18 still-open events of round 3's `on` arm replay at play settings on
this base with 123 Kolaghan-declared flags (single games, the oracle on the event turn, `MTG_DS2_PAYDBG` trace plus
`MTG_UNSEEN_SCARCE_TRACE`; `logs/ds-apply4/dbgrun.sh`, `blost.py`, `evcmp.py`).

**The hypothesis in the brief was wrong for the two-Apex shape.** The oracle's rollback names the failing decision: in
s30093026 T4 ({Pyretic Ritual, Seething Song, Apex of Power, Apex of Power, Dragonlord Kolaghan}) the engine's first six
payments are fine and the seventh is a breakpoint CONTINUATION's Seething Song {2}{R}, paid from the float alone
(B1 R14, Mercadian Bazaar and the Mountain tapped). No lump is involved, so `LumpAwareBillPayableOpener` is never asked.
The line opens a breakpoint, so the drain is the unseen-keep-scarce order (`MTG_UNSEEN_KEEP_SCARCE`): with no untapped
source of either colour BOTH {B} and {R} are "scarce" (keep mask 12); red's spare is its float beyond the named later
red (13 - 12 = 1), and the second generic unit came off the keep colours in hand order, {B} first. Song's own five red
pay those later red pips, so red had six to spare.

**M1 (`MTG_UNSEEN_SCARCE_RITUAL_NET`, heurarm `UNSEEN_SCARCE_RITUAL_NET`, DEFAULT ON, `=0` byte-identical).** In the
unseen-keep-scarce drain the later pips are netted by the output of the RITUAL this payment is for before a scarce
colour's spare units are computed (the `MTG_LINE_FLOAT_RITUAL_NET` argument, applied to the drain): it resolves before
the rest. A reorder of floating units only; the same total is spent. Sound dominance in that payment: the kept {B} plus
the red left and the ritual's float cover every later pip the plain drain covered.

**M2 (`MTG_LINE_SEQ_REST`, heurarm `LINE_SEQ_REST`, DEFAULT ON, `=0` byte-identical): the rest of the line walked in
order.** The line hold is one summed bill, so no payer judgement could credit what the rest's own casts add. Both apply
worlds now bind the plan's casts in order (`LineSeqScope` beside the hold, ApplyPlanDirect and AIEngine::TakeTurn;
`NoteLineSeqCast` at both cast sites before the payment; every `LineUnpaidCostScope` binds none, so continuations and
payability walks never read it, in either world). `LineSeqRestPays` walks the rest after the cast being paid: each
cast at its live cost (`EffectiveSpellCost` on the walk's board, so a Ruby Medallion earlier in the line reduces it),
paid by the complete backtracker over every crack of the lumps left, keeping where it can the units of colours the later
casts owe and none of them makes; then its output (a ritual's float, an Apex of Power's ten in the plan's chosen colour
when cast from hand, an artifact onto the battlefield). It is a WITNESS: true only by exhibiting a legal sequence of
payments; anything it does not model (an alternative / kicked / evoked cast, an unknown card) ends it false. It is used
only when the sequence is in step with the payment (the stepped cast is the spell being paid; the rest's pips equal the
hold's; its generic is within the hold's reducer credit) and the hold holds casts only. Two readers:
- the round-3 lump keep-owed rung's judge: the plain crack strands when neither the summed judge nor the walk pays the
  rest; the kept payment is taken when either does;
- the OWED FLOAT KEEP (`KeepOwedFloatPay`, at the top of TapForCostShared, outermost autonomous payments): units of a
  colour floating that the rest owes and none of its casts makes are set aside for the payment when the plain payment
  (tried on a copy) strands the rest by both judges and the kept payment pays the bill and the walk pays the rest after
  it (CR 601.2h). s30152803 T4 {Mountain, Ruby Medallion, Seething Song x2, Dragonlord Kolaghan, Irencrag Feat,
  Dragonstorm}: the first Song's {1}{R} spent the Bloom's floating {B} on the {1} with a Mountain untapped (the float
  rung reads it as non-spare: the rest owes red beyond the paying Song's five); kept, both Mountains pay and the walk
  (Song, Kolaghan, Irencrag's seven, Dragonstorm) pays the rest. The same rule covers the storage-land and Sandstone
  Needle variants of the shape (the {B} spent while Mercadian Bazaar or a Needle stayed untapped): "spend the counters
  if they are required for the line" (USER).
Both replace only a payment that provably strands the named line (no witness, summed or ordered) by one with a witness,
so neither can strand a line the old judgement kept. A payment choice in the shared payer, executor and rollout
together, at every budget.

Unit tests (`test_dragonstorm_line_keep.cpp`): `s30152803 T4 -- the first Song taps both Mountains and the Bloom's {B}
floats to Kolaghan` (`LINE_SEQ_REST` on: the line pays; off: Kolaghan fails) and `s30093026 T4 -- Song's own red nets the
later red, the scarce drain keeps the {B}` (`UNSEEN_SCARCE_RITUAL_NET` on: {B} floats; off: spent). Line-keep 11 / 11;
full suite 4333 / 4333.

**Event repros** (both levers `=0` vs default, the same binary; with the levers `=0` all 18 game logs equal the round-4
base's, `logs/ds-apply4/logcmp_base_off.txt`):
```
(seed  turn    Kolaghan-declared flags off -> on, all APPLY-DROP flags, win turn)
30092109 4      KolD   5 ->   5  allAD   5 ->   5  win 4.0000 -> 4.0000
30093026 4      KolD   1 ->   0  allAD   1 ->   0  win 4.0000 -> 4.0000
30102129 4      KolD   3 ->   3  allAD   3 ->   3  win 4.0000 -> 4.0000
30115282 5      KolD   9 ->   0  allAD   9 ->   0  win 5.0000 -> 5.0000
30127991 4      KolD   4 ->   0  allAD   4 ->   0  win 4.0000 -> 4.0000
30130531 6      KolD   8 ->   0  allAD   8 ->   0  win 6.0000 -> 6.0000
30131013 6      KolD   2 ->   0  allAD   2 ->   0  win 6.0000 -> 6.0000
30132752 4      KolD   8 ->   0  allAD   8 ->   0  win 4.0000 -> 4.0000
30133545 5      KolD  15 ->  18  allAD  15 ->  18  win 5.0000 -> 5.0000
30134453 6      KolD   4 ->   0  allAD   4 ->   0  win 6.0000 -> 6.0000
30135813 6      KolD   2 ->   2  allAD   2 ->   2  win 6.0000 -> 6.0000
30140711 5      KolD   1 ->   1  allAD   1 ->   1  win 5.0000 -> 5.0000
30141736 6      KolD   3 ->   3  allAD  19 ->  19  win 6.0000 -> 6.0000
30142750 7      KolD   4 ->   4  allAD   4 ->   4  win 7.0000 -> 7.0000
30145461 4      KolD  30 ->  36  allAD  30 ->  36  win 5.0000 -> 5.0000
30149079 4      KolD   1 ->   1  allAD   1 ->   1  win 4.0000 -> 4.0000
30152803 4      KolD  14 ->   0  allAD  14 ->   0  win 4.0000 -> 4.0000
30157929 4      KolD   9 ->   9  allAD   9 ->   9  win 4.0000 -> 4.0000
total KolD 123 -> 82, all AD 139 -> 98
```
Eight events fixed (s30093026 two-Apex, M1; s30115282 Territory; s30127991; s30130531 one-Apex; s30131013 two-Apex;
s30132752 / s30152803 the later-Song float, M2; s30134453). Two count more flags with the same failing decision on both
arms (s30133545 decision #5 "keep-floating {B}", 15 -> 18; s30145461 decision #11, 30 -> 36): the oracle examines more
plans on the on arm; neither is a new mechanism. Four final game logs move (30130531, 30134453, 30145461, 30152803);
every win turn is unchanged.

**Gate** (`logs/ds-apply4/run_gate.sh`, one pooled oracle batch, `MTG_MANA_ORACLE=2`, Dragonstorm play settings: off /
on 72,000 games each on gi 87000-158999, the two leave-one-out arms 18,109 games each on gi < 105000;
`logs/ds-apply4/gate_attrib.txt` from `attrib.py`, which keys a flag on the LAST dropped cast as round 3 did):
```
arm                          slice  games   turns  ADdecl ADundc ADordr ENUM CAPdec KolD  evD  evU  cpu_s
off                          all    72000  313541    259    321     51   25    228  123   47   50  84043
on                           all    72000  313540    211    318     35   25    248   82   37   50  86636
off                          loo    18109   78944     51     76     14    5      0    9   10   14  19966
on                           loo    18109   78943     50     76     14    5      0    8    9   14  20524
no_line_seq_rest (M1 only)   loo    18109   78944     50     76     14    5      0    8    9   14  19893
no_unseen_scarce_ritual_net  loo    18109   78943     51     76     14    5      0    9   10   14  20497
```
Kolaghan-declared 123 -> 82 (-33%), all declared drops 259 -> 211, ORDER 51 -> 35, Kolaghan-declared events 47 -> 37;
the undeclared (need-later) class is unchanged (321 -> 318), as expected: neither lever reads an undeclared cast. One game
is a turn faster (block g101129, LINE_SEQ_REST: the leave-one-out arm without it does not move); no game is slower.
The leave-one-out slice holds only one of the events (s30093026, fixed by M1 alone), so the per-lever split comes from
the 18 event repros instead (`logs/ds-apply4/events_loo_*.txt`, the same binary, one lever `=0`):
```
                         KolD   events fixed
both =0 (off)             123   -
M2 only (RITUAL_NET =0)    80   s30115282 s30127991 s30130531 s30132752 s30152803
M1 only (SEQ_REST =0)     125   s30093026 s30131013 s30134453 (and s30133545 15->18, s30145461 30->36)
both on                    82   all eight
```
So M2 (`LINE_SEQ_REST`) carries five events and M1 (`UNSEEN_SCARCE_RITUAL_NET`) three; with M2 on, M1 still fixes its
three (80 -> 82 is those -7 flags plus the +9 the oracle counts in the two events that keep failing at the same decision).
CAPPED-DECISION 228 -> 248 is three event turns, all games that win on the same turn on both arms: s30137529 T7 98 -> 106,
s30112005 T6 40 -> 41, and s30145787 T6 (0 -> 11). It counts the oracle's per-decision enumeration cap being reached,
i.e. how many plans the AUDIT examined at a capped decision; the engine's play there is unchanged (same win turn), so it is
analyzer work, not a play defect. The gate's +3.1% cpu includes the oracle; the A/B below is the play-CPU measure.

**Held-out A/B** (`logs/ds-apply4/run_ab.sh`, `mk_ab.py --n 400 --hg 200 --wf 3000 --base 5410000`: every deck at
play settings, Standard 400 / 2HG 200 pairs, Dragonstorm 3,000 + 3,000; 128 shapes, 43,800 pairs, no oracle;
`logs/ds-apply4/ab_report.txt`): **net 0 on every shape -- not one game is slower or faster** (win turn), units
-0.00% overall (Dragonstorm +0.03% / -0.00%, Extra Turns -0.09% / -0.12%, everything else 0). So the 8 events fixed are
payments inside lines the search already won on the same turn (as the gate's turn totals also say, 313541 -> 313540).
There is no slower game, so nothing to replay at d8 b0 and no clairvoyance test to run.

**CPU, and the cut.** The A/B measured Dragonstorm at **+4.1% / +4.2% CPU** (240 blocks, 172 slower, median +4.0%;
other shapes within the +-2-4% cpu noise of a 200-400-game shape, total +0.59%). Not oracle cost: the A/B has no
oracle. The leave-one-out gate slice puts it on `LINE_SEQ_REST` (19,893 s with it off vs 20,497 s with it alone).
A call-graph profile located it in `KeepOwedFloatPay`: every entry paid the plain bill TWICE (a quiet trial on a board
copy, then the caller's real payment after the -1), then ran the summed judge (more board copies, one per lump crack)
and, when that failed, the walk. Stage counters over 100 games (temporary build): 206,552 entries, 76% with a float
that pays the rest after any payment, 90% judged fine, 0 taken -- the keep acts rarely, but its test ran on every
ritual-chain payment. Commit 777cba077 (exact):
- THE FLOAT ALONE PAYS THE REST: a payment spends at most its mana value from any one floating colour, so when the float
  less that still covers the rest's every pip and its total, the summed judge would say "payable" -> -1 at once
  (157,830 of 206,552 entries; 0 cases where the judge disagreed);
- IN PLACE when the reveal pause is a no-op (search, rollouts): the plain payment is made once, on the real board; only
  the original is copied, for the kept retry; a float that covers the rest after it skips the judge;
- `LineSeqPayOn`: no copy for a pass 0 with nothing to keep; the winning scratch board is moved.
Measured in user instructions (`perf stat -e instructions:u`, insensitive to the box's load; `logs/ds-apply4/cpu_cut.txt`):
```
Dragonstorm play, 200 games    levers off       levers on        on/off
before the cut (3bd740edd)     179.69 G         187.86 G         +4.55%
after the cut  (777cba077)     179.70 G         182.62 G         +1.62%
45 other decks x 25 games      1147.40 G        1148.30 G        +0.08%   (after the cut)
```
So 64% of the Dragonstorm overhead is gone, and the other decks pay +0.08% (the sequence scope per apply). The
residual +1.6% on Dragonstorm is the walk (~0.75% of samples, about half from the round-3 lump rung's `rest_ok`, half
from the keep's after-a-failed-judge check), the summed judge on the 24% of entries the float does not cover (~0.4%),
and the board copy for those (~0.15%). Play is unchanged by the cut: every play digest equals the pre-cut binary's on
the 6,000 Dragonstorm A/B games and 200 paired games; 118 non-Dragonstorm o0 blocks equal the A/B's OFF arm in digest
and units. The only moves are what the old discarded trial left in the thread's payment caches: search units in 6 of
248 Dragonstorm blocks (<0.2%), and Extra Turns o0, whose digest the old trial had moved at the same win turns, now
equal to the off arm. Unit tests 4333 / 4333.

**Verdict: adopt both levers, DEFAULT ON (as committed).** Both are sound payment choices (each replaces only a payment
that provably strands the named line by one with a witness; CR 601.2h), present at every budget, executor and rollout
in lockstep, and inert to findability (they choose which mana pays, never which line is searched). The A/B moves no
game at play settings on any deck (net 0, 0 slower, 0 faster), so the adoption bar's "average at play settings" is
met at exactly neutral, and the gate shows the correctness gain: Kolaghan-declared drops 123 -> 82, declared drops
259 -> 211, ORDER 51 -> 35, eight events fixed (M2 five, M1 three), one game a turn faster, none slower. CPU after the
cut: Dragonstorm +1.6% instructions, every other deck +0.08%. The USER's "cut CPU" stands against that +1.6%; what is
left is listed above (the walk and the judge on the 24% the float misses) and is the next CPU item if it is wanted.

**Second CPU cut (exact, 9ef05496e).** The walk spends a board its caller throws away instead of copying one
(`LineSeqRestPaysOn`), and runs its last payment attempt on the walk board itself; the lever's trial and kept boards,
the walk wrapper and `LumpAwareBillPayable`'s per-combo board come from pooled scratch boards; the two cheap `-1` exits
run before the rest-of-line loop. Same payments, boards and backtracker calls in the same order: 1,124 jobs (18,500
games: the 6,000 Dragonstorm A/B games both arms, 118 other-deck blocks, the off arm against the base) give 0 digest and
0 units diffs; units 4333 / 4333. Dragonstorm levers on vs off **+1.62% -> +0.96%** user instructions (200 games); the
45 other decks **+0.078% -> +0.045%** (of which +0.017% on the off arm is code layout, not executed work: every digest
equal; not Worldfire, the one lump deck of the 45). What is left: the summed judge 0.34%, the two walks ~0.43%, board
copies 0.18%. A verdict memo would remove most of it (73% of trials and 91% of walks repeat a key) but is not exact
(the payment backtracker's mana cache is path dependent), so it is a play change with its own gate and A/B
(`logs/ds-apply4/cpu_cut.txt`). Measured on `wip/ds-memo` (2026-11-28, `logs/ds-memo/report.md`): **not worth it**. Exact-board verification
costs more than the route (+0.81% to +1.75%); a payment-board digest key on the keep's verdict saves 0.15% against its
own off arm but only 0.06% against the shipped binary (play-identical on 200 games, 0 verify mismatches); memoising the
rest verdict buys nothing. Not landed.
GT: the cut moves no play digest relative to 3bd740edd on Dragonstorm, but the levers themselves are a default-ON play
change, so smoke / regression GT is accepted by the parent under the landing binary (not here).
Still open in the cluster: s30133545 (decision #5 keep-floating {B}, probably a breakpoint continuation's payment,
which binds no sequence by design), s30145461 (Territory, decision #11), and s30092109, s30102129, s30135813,
s30140711, s30141736, s30142750, s30149079, s30157929 unchanged. Extending the walk into continuations needs the
sequence recorded with the record (`Action::rec_line_hold`) so executor and rollout stay in lockstep.
