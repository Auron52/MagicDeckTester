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
