# Treasure Hunt APPLY-DROP: the crit gate over-counted filter lands (2026-11-26)

Mana-clean gate `claude-queue-20261125a` (src tree `773d4e440d5c`, Treasure Hunt gi 100000-199999, seed 30000000 + gi):
Treasure Hunt had 4,440 APPLY-DROP (4,143 at rollback depth 1), 242 APPLY-DROP-ORDER, 365 ENUM-GAP. Branch
`wip/th-apply`. Scripts: `logs/th-apply/` (mk_gate.py, run_gate.sh, gate_report.py, mk_ab.py, run_ab.sh, ab_report.py,
from `logs/worldfire-ad/`).

## Most of it was already fixed by the Worldfire rungs

The gate predates `MTG_LINE_KEEP_CRIT_MULTI` (`docs/design/worldfire-apply-drop-2026-11-25.md`). On the current tree
(origin 9e566000b) the top signature, s30101992 T4 `{Cascade Bluffs + Land's Edge + Treasure Hunt}` (x104), and
s30105794 T5 (x64) flag nothing: the multi-source gate passes Land's Edge's `{1}{R}{R}` there and the rung keeps
Thundering Falls / Temple of Epiphany for Treasure Hunt. A baseline re-gate of gi 100000-105999 on that tree:
APPLY-DROP 146 (the gate had about 266 per 6,000), 71 of them undeclared, APPLY-DROP-ORDER 9, ENUM-GAP 23.

## The remaining declared cluster: filter lands counted as colour

`LineKeepCritGate` (ManaPayment.cpp) counts each untapped source's colours as supply. A hybrid-fed filter land
(Cascade Bluffs, `is_filter`: "{U/R}, {T}: Add {U}{U}, {U}{R}, or {R}{R}") counted one unit of `{U}` and one of
`{R}`. But it makes colour only from a coloured unit of ANOTHER source, and unfed it makes `{C}`. A bill that spends
every unconditional source of the owed colour therefore read as "plenty left" whenever Bluffs were in play, and the
rung never tried it.

* s30133719 T8 `{Bluffs + Land's Edge + Treasure Hunt}` (x40 in the gate): Thundering Falls, Temple of Epiphany,
  Cascade Bluffs, two Reliquary Towers, land drop Cascade Bluffs. The greedy paid Land's Edge with Falls + Temple +
  a Tower, and two unfed Bluffs plus a Tower could not make Treasure Hunt's `{U}`. The gate saw four `{U}` sources.
* s30101328 T4 `{Island + Treasure Hunt + Treasure Hunt}`: Steam Vents, two Bluffs, land drop Island. The second
  Treasure Hunt had no `{U}`.
* s30100258 T4 `{Ferrous Lake + Treasure Hunt + Land's Edge}`: Bluffs, Sandstone Needle, Reliquary Tower, land drop
  Ferrous Lake. The greedy paid Treasure Hunt with the Needle's `{R}{R}` (one fed into the Bluffs), and Land's Edge
  had one `{R}` left.
* s30100350 T5 `{Bluffs + Land's Edge + Treasure Hunt}`.

Not a modelling gap (the filter land is modelled right, and the exact judge already knew the strand), and not a
USER-owned policy: it is a payment the greedy makes that provably drops a cast of the declared line while another
payment of the same sources casts it.

## The change: `MTG_LINE_KEEP_CRIT_FILTER` (DEFAULT ON; `=0` byte-identical)

After its existing tests, the gate asks the multi-source take once more with every untapped filter land's colours
left out of the supply: the bill's non-c pips take the largest UNCONDITIONAL c sources, and the gate passes when what
is left of c without the filters falls short of the rest's c plus the bill's own. The supply only shrinks, so this
passes a superset of the payments the gate passed before. The judge (scratch trial, witness, hold-out repair) is
unchanged and decides every payment. It is the shared payer, so the executor, the rollout and the walks move
together, at every budget. A payment choice (the standing mana-payment exemption), not a search-window decision.

Decks with a filter land in the suite: Treasure Hunt, Hinata, Unpredictable Cyclone, Niv-Mizzet Combo (Cascade
Bluffs), Morph (Fire-Lit Thicket), Sea Creatures (Flooded Grove). Every other deck is byte-identical by construction.

Unit test: `test/unit/test_line_keep_all.cpp`, "Line keep (crit, filter lands)": the s30133719 board, lever on
(Treasure Hunt pays) and off (it does not), plus an Island control where both arms tap the same sources.

## Not this mechanism (left out)

* Undeclared drops (71 of the 146 baseline): `{Treasure Hunt}` alone, Land's Edge drawn by Treasure Hunt and cast in
  the continuation. Not a payment of the declared line.
* `{Treasure Hunt + Throes of Chaos}` dropped Throes (19 of 146): the continuation after Treasure Hunt's reveal casts
  the cards the reveal found (a second Treasure Hunt, then Land's Edge), and the declared Throes no longer pays.
  A continuation re-plan with new information, not a payment strand.

## Measurements

(filled in below)
