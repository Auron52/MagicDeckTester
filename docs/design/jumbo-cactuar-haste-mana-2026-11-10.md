# Jumbo Cactuar: mana from a newly cast hasted creature

The 5,000-game PGO mana-clean run `20261108-095618` reported 4,141 issues, including 1,166 ENUM-GAPs. This change addresses one reproducible ENUM-GAP family; it does not establish a clean gate.

## Reproduction and cause

At seed `30007614`, game index `7614`, turn 5, the player has Surrak and Goreclaw, two Arbor Elves, and a Forest enchanted by Wild Growth. A hand-cast Voyaging Satyr gets haste from Surrak and can untap the enchanted Forest. That mana makes Tooth and Nail payable in the same turn. The ordinary candidate mana bound counts the old board but omits the Satyr's tap. The valid line is pruned before the mana oracle can offer it.

The enumerator now credits the one-tap yield of a selected hand-cast mana creature when a live Surrak and Goreclaw grants it haste, provided the creature itself is payable from the old pool. The same credit widens `ManaPruneBound`; the selection-exact gate stands aside for these states. The existing `MTG_NO_HASTE_DORK_CREDIT=1` switch removes the credit for a causal A/B check.

## Verification

- The exact seed on the changed Release binary has `MISS=0` and no ENUM-GAP. With `MTG_NO_HASTE_DORK_CREDIT=1`, its 23 ENUM-GAP lines return.
- A paired 100-game Release probe, game indices `500000` through `500099`, had identical per-game averages, digests, and work units with the mana oracle on and off. The oracle-on arm still reported 5 ENUM-GAP, 57 APPLY-DROP, 4 APPLY-DROP-ORDER, and 68 DOMINATED-CREATURE lines. It is a diagnostic probe, not a mana-clean stamp.
- `./build.sh release -- --target mtg` and `git diff --check` passed.

Next: integrate the change, build a current-source PGO binary, run a fresh gate, and investigate the remaining Jumbo issue families without accepting a clean verdict from this scoped repair.

## Same-plan Surrak and a newly hasted Arbor Elf

The next old gate exemplar, seed `30006177` / game index `6177` / turn 4,
holds Overgrowth, Tooth and Nail, Surrak and Goreclaw, and Arbor Elf. The
legal line casts the Aura, Surrak, then Elf; Elf's hasted untap of the
enchanted Forest funds Tooth and Nail. The old Aura-only scalar gate treated
the newly cast Elf as unavailable, and the sequential rescue walk did not
place either Surrak or Elf on its scratch battlefield. The deck's cast order
also placed Tooth and Nail and Elf before Surrak.

The rescue now admits a subset containing a same-plan haste watcher and mana
creature to the real sequential payer. That payer places Surrak and the
newly hasted mana creature on its scratch board after paying for each cast,
and the deck order places Surrak before Elf and Tooth and Nail. This is an
optimistic rescue: the actual sequential payment still decides whether the
line is legal. During the replay, that wider walk exposed an older cost bug:
it omitted Tooth and Nail's `{2}` entwine surcharge. The walk now applies
the same surcharge as the real cast path, so it cannot certify that phantom
line.

This is another scoped repair. The fresh 5,000-game PGO mana-clean gate is
still required before a clean verdict, and other issue classes in the first
Jumbo gate remain open.

The exact Release replay has zero counted mana issues: the 38 previous
ENUM-GAP lines are gone, and the temporary PHANTOM and APPLY-DROP-ORDER lines
from the underpriced entwine probe are gone. One DOMINATED-LAND observation
remains disclosed by the oracle, outside the gate's issue classes. With
`MTG_NO_HASTE_DORK_CREDIT=1`, the same binary reports the old 38 ENUM-GAPs.
