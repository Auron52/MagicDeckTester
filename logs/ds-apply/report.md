# Dragonstorm APPLY-DROP: report

(Written by the parent from agent a1c3892629109da61's final message, 2026-11-26; agent 1 was a453ba081daa246e4.)

**Landable: 20cd98179** on `wip/ds-apply` (two validated levers). A third lever, ca4ee7d53, is committed but UNVALIDATED until its all-deck A/B (`build/scratch/ds-apply/ab3_out`, DONE marker there) reports. Handoff: `logs/ds-apply/handoff.md`.

## Mechanism
Every Dragonstorm APPLY-DROP event has Lotus Bloom or Unclaimed Territory on the board; the dropped cast is almost always Karrthus or Dragonlord Kolaghan, whose {B}/{G} only those sources make. Two rules in the shared payer:
- **Visible-closer lump rule:** cracked a Bloom early on a colour comparison that counted an uncracked Bloom as no supply.
- **Leftover swap:** could move only units the payment produced, and judged the rest of the line without the paying ritual's float.

## Levers (all default ON, `=0` byte-identical)
- **`MTG_LEFTOVER_SWAP_WIDE`** (893647990): the swap may move any leftover unit and credits the ritual's float.
- **`MTG_ONDEMAND_VISIBLE_LUMP_EXACT`**, narrowed in e07c7849c: refuses an early Bloom crack only when the crack strands the later bill (ordinary payment pays it, early crack does not).
  - The first version also refused when both paid; that made 2HG s5131993 gi1993 slower (5 -> 6), persisting at d8 b0. Cause: paying Seething Song's generic with the Bloom's {B} keeps Sandstone Needle's {R}{R} for Scourge of Valkas's firebreathing; a leftover {B} can't pay it, so the T4 pump/hold choice held the Needles and lost the T5 kill. On the narrowed binary the game is byte-identical to off, at play settings and at d8 b0.
- **`MTG_LINE_FLOAT_RITUAL_NET`** (ca4ee7d53, UNVALIDATED): the line-float rung no longer counts a later pip the paying ritual's own output covers when deciding whether a source is spare (s30096957: Song's {1} drained Karrthus's {B}). Shared rung, so its A/B covers every deck.

## Units
e07c7849c 4205/4205; ca4ee7d53 4206/4206 (5/5 Dragonstorm cases with each lever on and off). The no-strand control test now asserts both arms pay the line and leave the same mana (the swap legitimately changes which unit floats).

## Gate (Dragonstorm slice of claude-queue-20261125a, 72,000 games, `MTG_MANA_ORACLE=2`)

| arm | declared events | need-later events | ENUM-GAP | turns |
|---|---:|---:|---:|---:|
| off | 105 | 47 | 33 | 313,645 |
| first version (893647990) | 62 | 56 | 41 | 313,643 |
| narrowed (e07c7849c) | 64 | 48 | 33 | 313,643 |
| + ritual net (ca4ee7d53) | 62 | 48 | 33 | 313,647 |

Narrowed vs off: 42 declared events fixed, 1 new. Ritual net vs narrowed: 2 more fixed, 0 new. Leave-one-out on the first version (18k games): lump lever 12 of 16 events, swap 4.

## Play A/B (seed = base + gi, game_index = gi)

| run | shape | pairs/arm | net | slower | faster | units | CPU (wall) |
|---|---|---:|---:|---:|---:|---:|---|
| 893647990, base 5130000 | 62 other decks std | 400 each | 0 | 0 | 0 | 0.00% | ±1% |
| | 62 other decks 2HG | 200 each | 0 | 0 | 0 | 0.00% | ±1% |
| | Dragonstorm std | 3000 | 0 | 0 | 0 | -0.004% | +0.27% |
| | Dragonstorm 2HG | 3000 | +1 | 1 | 0 | +0.054% | +0.60% |
| e07c7849c, held-out base 5160000 | Dragonstorm std | 3000 | 0 | 0 | 0 | +0.00% | +0.06% |
| | Dragonstorm 2HG | 3000 | 0 | 0 | 0 | -0.00% | -0.56% |

- The narrowing changed only the Dragonstorm-only lump rule, so the first A/B still covers the swap's effect on other decks.
- The only slower game (2HG s5131993) is fixed on e07c7849c and not slower held-out.
- CPU is wall on a shared box (noise); units are the deterministic cost figure; no instruction count.
- The levers fix declared drops in the gate but buy no measurable play gain; the search usually finds another line.

## Open
- ca4ee7d53's all-deck held-out A/B (three levers off vs on, base 5190000, 86,400 games, ~1 h; last stage of chain3, holds the box lock). Then `ab_report.py --list-slower` and d8 b0 replays of every slower game before landing it.
- No USER decisions.

Agent 1 ended at 396K (stopped by the parent); agent 2 at 243K.
