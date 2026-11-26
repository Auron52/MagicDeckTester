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
