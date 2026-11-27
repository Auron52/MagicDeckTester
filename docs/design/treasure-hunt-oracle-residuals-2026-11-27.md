# Treasure Hunt mana-oracle residuals: all four classes were oracle false positives (2026-11-27)

These are the residuals left after `MTG_LINE_KEEP_CRIT_FILTER` (`docs/design/treasure-hunt-apply-drop-2026-11-26.md`).
That gate covered Treasure Hunt gi 100000-119999, seed 30000000 + gi, `MTG_MANA_ORACLE=2`, on the lever-on arm:

- ENUM-GAP 76
- APPLY-DROP-ORDER 62
- APPLY-DROP 326, of which 257 were undeclared and 69 declared. 42 of the 69 were the Throes of Chaos continuation.

Branch `wip/th-resid`. Scripts are in `logs/th-resid/`: `mk_gate.py`, `run_gate2.sh`, `gate_report2.py`.

**Result.** None of the four classes is an engine defect. Every change is in the oracle (`src/ai/ManaOracle.cpp`)
and changes classification only. No play changes, so this needs no GT and no play A/B. Each change has its own
lever, read on every call so a unit test can pin it. Each lever is DEFAULT ON, and `=0` gives the old verdicts.

## 1. ENUM-GAP: the provider's strict flood gate, not a mana gate (`MTG_ORACLE_EG_PROVIDER_PRUNE`)

The shape (70 of 76) is `{no land + Treasure Hunt + Treasure Hunt}` on T3 or T4. The other flood-engine pairs
(`+ Throes of Chaos`, `+ retrace:Throes of Chaos`) make up the rest, apart from one case.

Example: s30100161 T3.
- Board: Temple of Epiphany and Saprazzan Skerry. Hand: Land's Edge, two Treasure Hunts, two Islands, Ferrous Lake.
- The pre-land pool is 3, so a mana gate keeps both casts off the menu.
- The oracle's paying realisation:
  - pays the first Treasure Hunt with Skerry's `{U}{U}`;
  - plays Ferrous Lake in that Treasure Hunt's breakpoint;
  - pays the second with Temple feeding Lake.

**Why the engine never offers it.** The plan menu offers Treasure Hunt only with the land drop deferred, which is the
USER's land rule. The line is therefore reachable only as the breakpoint continuation `{land=X + Treasure Hunt}`.
In that continuation the provider's flood-engine gate (`TreasureHuntProvider::ShouldCastDrawEngine`, `THStrictFlood`,
docs/design/treasure-hunt-land-rule-provider.md) refuses the second Treasure Hunt. By then the drop is spent, no
Land's Edge or Reliquary Tower is out, and there is no mana left to cast Land's Edge after it. That is a provider
prune, which restricts options as the no-greedy rule allows, and it is the USER's "don't play a land BEFORE TH/Throes"
rule. It is not a mana gate, and it is not a completeness defect.

**The oracle change.** The ENUM-GAP pass already sets aside a realisation paid by a land the plan did not play when
the plan WITH that land drop is on the menu (`cont_land`). It now also sets one aside when the cast that land paid is
a flood engine and the deck's provider refuses it on the board that payment is asked on. That board is a replay of
the realisation's script up to the payment (`ContLandCastProviderPruned`). The verdict is counted as
`enum-gap rejections: provider=N`.

A unit test with Land's Edge in play checks that the provider's admission still yields ENUM-GAP.

**Left alone.** s30113218 T7 `{land=Cascade Bluffs + Land's Edge + Land's Edge}` (x1) is a real flat-pool
under-count of a filter land. It has no value, though: a second Land's Edge adds nothing, because one Edge discards any
number of lands.

## 2. APPLY-DROP-ORDER: lines that already won (`MTG_ORACLE_DROP_WON`)

There are four seeds, and s30109829 T5 accounts for 41 of the 62 flags.

- Plan: `{land + Treasure Hunt + Throes of Chaos}`.
- The breakpoint continuation of Treasure Hunt casts Land's Edge from hand and throws about 30 lands. The opponent
  goes to -48 and the real game wins T5.
- Throes' `{3}{R}` is then unpaid, and the order Throes > Treasure Hunt pays both.
- 13 of the 41 are the entries whose continuation casts the Edge but fires it after the main. Their boards do not
  show the loss, but other entries of the same menu do.

**The oracle change.** A drop is now DROP-COVERED, with "would-be <class>" in its text, in two cases:
- the plan's own engine line has won (`OpponentHasLost`);
- any menu entry's engine line has won (`MenuHoldsWin`).

No payment can beat a win in this main, and the game's measure is the turn it is won.

## 3. Undeclared APPLY-DROP: the Vampires shape (`MTG_ORACLE_UNDECL_MENU_COVER`)

s30106563 T4 `{no land + Treasure Hunt}`:
- Board: Reliquary Tower, Ferrous Lake and Saprazzan Skerry. Land's Edge is in hand.
- The continuation plays the second Reliquary Tower and casts Land's Edge, which drops. The plan does not name
  Land's Edge.
- The oracle's payment floats `{R}`. Its continuation plays Cascade Bluffs and casts the Edge.
- That board is what `{land=Cascade Bluffs + Treasure Hunt}` reaches under the engine's own line.

The same-action sibling cover never saw it, because it compares only entries with the plan's own actions.

**The oracle change.** For a drop with a lost action the plan did not declare (`LostUndeclared`, the census's `later`),
the oracle now asks one more question. Is every board a full payment reaches also reached by another menu entry's
engine line, with at least the resources? That is the same cover ENUM-GAP's JudgeHit asks of the whole menu. If so, the
verdict is DROP-COVERED. Declared drops are unchanged.

## 4. The Throes of Chaos continuation (42 of the 69 declared)

This is confirmed as a re-plan on new information, not a defect. After Treasure Hunt's reveal the continuation casts
what the reveal found, or the Land's Edge already in hand, and the declared Throes no longer pays. In s30100391 T5 and
s30109829 T5 the engine's line wins. Rule 2 covers these wherever a line on the menu wins.

## Measurements

Gate setup:
- Treasure Hunt gi 100000-119999, seed 30000000 + gi, `MTG_MANA_ORACLE=2`, one 200-job manifest.
- It ran once per binary under the box lock, back to back, because the levers are process-level.
- base = origin `ce045ad8b` (the land-1129 Release build); new = `wip/th-resid` `6a38e62dd`.
- Scripts: `logs/th-resid/run_gate2.sh`, `gate_report2.py`.

| binary | APPLY-DROP | of them undeclared | APPLY-DROP-ORDER | ENUM-GAP | ORACLE-GAP | HIDDEN-INFO |
|---|---|---|---|---|---|---|
| base | 326 | 257 | 62 | 76 | 0 | 8 |
| new | 22 | 18 | 0 | 1 | 0 | 8 |

Where the moved flags went on the new binary:
- DROP-COVERED by a winning line on the menu: 304 would-be APPLY-DROP and 62 would-be APPLY-DROP-ORDER.
- ENUM-GAP provider rejections: `provider=153` realisations, which takes the ENUM-GAP count from 76 to 1.
- The undeclared menu cover fired 0 times in the gate. Every undeclared drop there sat on a menu that already held
  a win, and the win rule is asked first. It is still the right verdict where no line wins: s30106563 T4 with the
  opponent out of reach.

Play is byte-identical: all 200 job digests match between the binaries. Oracle wall rose by 0.14%
(2,953,485 -> 2,957,674 ms). Oracle applies fell by 8,361 of 41.67M.

What is left on the new binary:
- 18 undeclared `{no land + Treasure Hunt}` drops: s30103724 T4 x7, s30112907 T5 x6, s30101394 T5 x5.
- 4 declared `{no land + Treasure Hunt + Treasure Hunt}` drops: s30108799 T4 and s30111609 T4. In s30108799 the
  continuation casts the second Treasure Hunt itself, so the declared cast reads as "dropped".
- 1 ENUM-GAP: s30113218 T7 `{land=Cascade Bluffs + Land's Edge + Land's Edge}`, which is worthless.
