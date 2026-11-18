# Jumbo Cactuar: hasted fresh untappers in the land-Aura ceiling (`MTG_AURA_CEIL_HASTE_UNTAPPER`), 2026-11-18

Status: **default ON** (passed the adoption bar; GT not accepted). See "Decision" at the end.

## The defect (mana-clean gate claude-regate-20261118, Jumbo ENUM-GAP 301)

The plan menu's land-Aura scalar ceiling (`EnumeratePlansCore`, the `ef-aura-scalar` pre-filter that runs ahead of the
sequential walk `SubsetPayableSequential`) bounds a subset's supply by

    pool + aura_bonus * (1 + ready untappers on the battlefield)

It did not count an untapper that a haste-Equipment equip in the same subset unlocks: a hand Voyaging Satyr / Arbor Elf
with Lightning Greaves or Chariot of Victory equipped onto it, a GSZ/Finale-tutored copy, or a summoning-sick one. That
untapper re-taps the Aura'd Forest, so it brings its own re-tap yield AND one more Aura bonus. Both terms were missing,
so the ceiling sat below the real supply and refused the plan at every budget.

Trace (`MTG_DBG_MULTI=<turn>`, which names the gate that refused each multi-cast subset): s30118474 T3 m1, board
Forest#10 + Overgrowth, Arbor Elf, Forest#11; hand Greaves, Overgrowth, Tooth and Nail, Satyr.
`[chain] t3 pool=7 ... ef-aura-scalar: Lightning Greaves + Overgrowth + Tooth and Nail + Voyaging Satyr + Lightning Greaves`.
The ceiling was 7 + 2 x 2 = 11 against 14. The real line is 14 of 14 (Forest#10 3 -> Overgrowth, then Forest#10's
re-taps by the Elf and the Greaves-hasted Satyr at 5 each, plus Forest#11). The same gate refused s30116935 T3,
s30117637 T4, s30117177 T4 and s30118307 T4 (all `ef-aura-scalar`).

Of the gate's 301 Jumbo ENUM-GAP flags, 185 have this shape (equip + fresh untapper + land Aura in the plan).

## The change

`AuraCeilHasteUntapperOn()` (TurnSolver.cpp, heurarm slot `AURA_CEIL_HASTE_UNTAPPER`, read per call). For each distinct
host of a same-subset haste-unlock equip (`haste_unlock[j] > 0`) that is an untap-target-land source
(`UnlockHostUntapperDef`: Arbor Elf / Voyaging Satyr, on the battlefield, in hand or the tutored library copy), the
ceiling adds that host's re-tap yield (`haste_unlock[j]`, the flat pool's own credit) and one more Aura bonus. It is
still an upper bound (every untapper is assumed to re-tap the host carrying every new Aura); the sequential walk is the
arbiter, so this can only admit plans the walk then prices. Over-admission is a cost, never a lost line.

Reach: the only shipping deck with an untap-target-land source, a haste Equipment and a land Aura is Jumbo Cactuar
(cards.json: `equip_grants_haste` = Lightning Greaves, Chariot of Victory, Skateboard; untappers = Arbor Elf, Voyaging
Satyr). Bruna has Greaves + Wild Growth but no untapper, so the added terms are zero there by construction.

Unit test: test/unit/test_jumbo_cactuar.cpp, "Jumbo Cactuar enumerates a Greaves-hasted fresh Satyr re-tapping a new
Overgrowth for Tooth and Nail" (the s30118474 board; the plan is on the menu with the lever on, absent with it off).

## Measurement

Repros (`MTG_MANA_ORACLE=3`, flags at the named turn), off -> on:

| repro | gate | off | on |
|---|---|---|---|
| s30118474 T3 (x20) | ef-aura-scalar | ENUM-GAP 20 | 0 (menu 237 -> 489 plans over the game's 5 decisions) |
| s30116935 T3 (x14) | ef-aura-scalar | ENUM-GAP 20 | 0 |
| s30117637 T4 (x14) | ef-aura-scalar | ENUM-GAP 20 | 0 |
| s30117177 T4 (x13) | ef-aura-scalar | ENUM-GAP 55, ORDER 5 | ENUM-GAP 0, ORDER 5 |
| s30118307 T4 (x13) | ef-aura-scalar | ENUM-GAP 20 | 0 |
| s30119376 T5 (x19) | ef-walk-fail | ENUM-GAP 19 | 19 (other gate, untouched) |

No repro gained an APPLY-DROP or APPLY-DROP-ORDER.

Gate slice (oracle level 2, gi 115000-116999, 2000 games; flags / distinct decisions / games):

| arm | ENUM-GAP | APPLY-DROP | APPLY-DROP-ORDER | ORACLE-GAP | PHANTOM | DOMINATED-CREATURE |
|---|---|---|---|---|---|---|
| off | 90 / 59 / 33 | 7 / 3 / 3 | 92 / 32 / 24 | 45 / 18 / 13 | 4735 / 854 / 368 | 66 / 55 / 38 |
| on | 43 / 42 / 26 | 7 / 3 / 3 | 92 / 32 / 24 | 42 / 15 / 11 | 4681 / 851 / 365 | 62 / 51 / 37 |

ENUM-GAP of this family (equip + untapper + Aura): 52 -> 14. The residual 14 are mostly `ef-walk-fail` (below).

Held-out paired A/B, one pooled `mtg --batch` (32 workers, saturated), play settings (d6 b20, value_play), seed =
30000000 + gi, game_index = gi, Jumbo gi 640000-644999:

| deck | pairs | faster | slower | net | digest-only | mean on / off | units on/off | cpu on/off |
|---|---|---|---|---|---|---|---|---|
| Jumbo Cactuar | 5000 | 7 | 1 | **-6** | 95 | 4.0808 / 4.0820 | x1.003 | x1.026 |

Faster: 640387, 641791, 641895, 642459, 642765, 643238 (4 -> 3), 642718 (5 -> 4). Slower: 641075 (4 -> 5).

Slower-game verdict: 641075 at `--depth 8 --budget-ms 0` (one pooled batch, both arms): off T4, **on T4** -- it
recovers (units 37,312 vs 39,494). The play-setting loss is budget spread at d6 b20, not a lost line. The menu grows
where the line exists (s30118474 T3: 237 -> 489 plans), but units over the 5,000 pairs rose only x1.003 (cpu x1.026).

Plan-signature fold risk: no repro gained an APPLY-DROP / APPLY-DROP-ORDER, and the slice's APPLY-DROP and ORDER counts
are unchanged, so no admitted plan stands in for a realisation it cannot reach.

## What remains (not changed here)

1. **`ef-walk-fail`** (the T5 Surrak family s30119376 x19, and most of the 14 residual Aura-family flags): the
   sequential walk prices ONE order (CastOrderRank plus the payable-order retry), and that order does not reach the line
   (e.g. Surrak before both Satyrs, Satyr re-taps of the Wild-Growth/Overgrowth Forest).
2. **APPLY-DROP-ORDER (gate 291; slice 92, unchanged by this lever)** is a DIFFERENT mechanism in the apply: the
   payable-order search (`PayableCastOrderCoreImpl`, ManaPayment.cpp) admits an order only inside a producer domain
   (treasure makers, rituals, domain wideners, land Auras, sac fodder) and its walk does not model a haste-unlock
   equip. So the order "cast Chariot (or GSZ -> Arbor Elf) first, equip, re-tap" is never considered, and the apply's
   rank order drops GSZ (152 of the gate's 291) or Chariot (73). It shares the haste-unlock-untapper shape with the
   ENUM-GAP family but not its code. Fix shape: an unlock-piece producer domain plus `ApplyManaUnlockEquips`
   semantics inside the order walk.

## Decision

**Default ON.** It passes the adoption bar: Jumbo net -6 on 5,000 held-out pairs (7 faster, 1 slower), the one slower
game recovers at d8 b0, and no other deck can reach the change. Lever-off smoke (`MTG_AURA_CEIL_HASTE_UNTAPPER=0`, build/Release): ALL PASS, 231 passed, configs changed 0. Units 3666/3666. Smoke/regression ground
truth is NOT accepted here (the parent owns GT acceptance): Jumbo's smoke/regression rows may move with the lever on.
`MTG_AURA_CEIL_HASTE_UNTAPPER=0` restores the old ceiling.

Scratch: build/scratch/claude_jumbo_enum/ (ab/, slow/, slice.sh, cls.py, chain_*.txt).
