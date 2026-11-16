# Prevent Damage: scripted payment trigger boundary (2026-11-16)

The real-play mana oracle can replay a chosen payment through `RealPayScope`.
When that payment taps a pain land, its damage is recorded as an armed event.
The ordinary payer flushes those events after the outermost successful payment,
before the paid-for spell or ability resolves. The real-play scripted path used
a float-only drain and returned without that flush. It left pain marks on the
lands and postponed Tamanoa gains and Dina/Vito drains until an unrelated event
flushed them. The oracle's audit path already flushed at that boundary.

`ScriptedPay` now flushes armed damage events after a successful real-play
scripted payment. A focused Pyrohemia payment test uses Battlefield Forge,
City of Brass and Ancient Tomb with Tamanoa, Dina and Vito in play. Before the
fix, the scripted path ended at 11 life instead of the ordinary payer's 15,
left the opponent at 20 instead of 13, and left three pain marks unflushed.
After the fix, ordinary, audited and real-play scripted payments agree on
life totals, floating mana, tapped lands and pain marks for opponent life
1, 2, 3 and 20. The focused test passes 186 assertions; the full Release unit
suite passes 3,359 cases and 3,614,281 assertions.

The focused oracle replay of Prevent Damage seed `30089526`, game index
`89526`, turn 5 still reports two `ORACLE-GAP` flags after this fix. Its
payment table does generate the engine's initial Pyrohemia payment (City of
Brass red, Battlefield Forge red, Ancient Tomb colorless) and a subsequent
Reflecting Pool red payment. The gap is in the continuation after that payment,
not fixed by the real-play flush. A trial that flushed damage events in the
payment table's scratch verification left all 57 oracle outcomes and the two
flags unchanged, so that trial was removed.

The combined Prevent Damage smoke/regression gate ran 2,560 games. All nine
solo `.wins` files match the pre-fix same-binary control byte-for-byte. Three
solo rows still differ from ground truth, including one searched turn-6 to
turn-7 game; those differences were already present in the control. The
two-headed regression row has the same average as ground truth and a changed
play digest. Strict viewer replay found zero play drift, board divergence or
enumeration gaps among 864 references, and the require-beat checks passed.

Fresh mana-clean gate results: pending.
