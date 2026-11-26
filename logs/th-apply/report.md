# Treasure Hunt APPLY-DROP: MTG_LINE_KEEP_CRIT_FILTER

(Written by the parent from agent a5ba7aadb3c2881f0's final message.)

Commits on wip/th-apply (pushed): `ac40cb59a` (lever), `fdab5b10c` (Niv test), `e1096e6b7` (converters added), `29d4d68ae` + `d3d9edccc` (doc, results). Design doc: `docs/design/treasure-hunt-apply-drop-2026-11-26.md`; scripts and tables in `logs/th-apply/`.

## Result
- New default-ON lever `MTG_LINE_KEEP_CRIT_FILTER`. 20,000-game gate re-run: APPLY-DROP 582 -> 326, declared drops 325 -> 69. Play A/B net -1, 0 slower, CPU +0.048% (instruction count). GT not accepted.
- Most of the original 4,440 was already gone: the gate binary predated the Worldfire `MTG_LINE_KEEP_CRIT_MULTI` rung. On 9e566000b the top two signatures (s30101992 T4 x104, s30105794 T5 x64) flag nothing; a fresh 6,000-game baseline has 146 APPLY-DROP (~266 at the old rate), 71 undeclared.

## Mechanism
`LineKeepCritGate` (`ManaPayment.cpp`) counted a fed source as plain supply of its colours: filter lands (Cascade Bluffs) and `{1}`-fed converters (Ferrous Lake, Izzet Signet, Capital City, Arcum's Astrolabe). Such a source makes colour only by spending a unit from another source, which the bill can spend first; unfed, Cascade Bluffs makes `{C}`. So a payment that used up every unconditional source of the colour the rest of the line needs looked like "plenty left" and the rung never tried it.
- Example s30133719 T8 `{Cascade Bluffs + Land's Edge + Treasure Hunt}`: the greedy paid Land's Edge with Thundering Falls + Temple of Epiphany + Reliquary Tower, leaving two unfed Bluffs and a Tower, no `{U}` for Treasure Hunt.
- The same over-count causes the `{Treasure Hunt + Treasure Hunt}` and `{Treasure Hunt + Land's Edge}` drops, and Ferrous Lake's in s30106408 T4.

## Fix
After its existing tests, the gate reruns the multi-source check with the fed sources' colours left out. Supply only shrinks, so it only sends more payments to the existing exact judge, which still decides each one. `=0` is byte-identical. A payment choice, at every budget, deciding nothing in the search window. Twelve suite decks have a fed source and can move; every other deck is byte-identical by construction.
- Unit tests 4,203/4,203; two new board tests in `test_line_keep_all.cpp` (lever on and off, one with an Island control). The Niv hole-1 demo in `test_niv_payment_holes.cpp` pins this lever off (it catches that strand on its own); a new block asserts the catch.

## Gate re-run (Treasure Hunt gi 100000-119999, `MTG_MANA_ORACLE=2`, one pooled batch)

| arm | APPLY-DROP | undeclared | APPLY-DROP-ORDER | ENUM-GAP |
|---|---|---|---|---|
| off | 582 | 257 | 62 | 76 |
| filter lands only | 405 | 257 | 62 | 76 |
| on (filter lands + converters) | 326 | 257 | 62 | 76 |

Single-game repros: s30133719 8->0, s30101328 3->0, s30100350 8->0, s30104784 3->0, s30106408 13->5, s30100258 6->5 (the remaining 5s are undeclared drops).

## Residual classes (left alone; in the design doc)
- **Throes continuation (42 of the 69 declared):** after Treasure Hunt's reveal the engine casts what it found, so the declared Throes no longer pays. A re-plan on new information; in s30100391 the engine's line wins and the oracle's does not.
- **Undeclared drops (257):** the oracle applies menu entries real play doesn't take (s30106563: real game casts Treasure Hunt then Land's Edge, wins T4).
- **ENUM-GAP:** the second Treasure Hunt pays only with a land played after the first resolves, which the menu's mana check doesn't see.

## CPU and play A/B
Instruction counts over the 12 affected decks (200 games at play settings): 768.010 G -> 768.378 G, **+0.048%**, all 200 digest-identical.

Play A/B: one pooled batch, 60,200 games, seed 5130000 + gi with game_index gi, heartbeat 32/32.

| shape | pairs | net | slower | faster | units |
|---|---|---|---|---|---|
| Treasure Hunt std | 10,000 | 0 | 0 | 0 | 0.00% |
| Treasure Hunt 2HG | 4,000 | 0 | 0 | 0 | 0.00% |
| Hinata std | 600 | -1 | 0 | 1 | -0.12% |
| other 21 shapes | 15,500 | 0 | 0 | 0 | 0.00% (Hinata 2HG +0.02%) |
| **total** | **30,100** | **-1** | **0** | **1** | **-0.01%** |

No slower games, so no d8 b0 replays. Digests moved in 15 of 1,204 chunk pairs (Treasure Hunt 11, Hinata 3, Morph 1), so smoke/regression GT may move at landing.

Agent ended at 294K tokens.
