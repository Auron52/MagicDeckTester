# Dragons ORACLE-GAP (Haven of the Spirit Dragon): report

(Written by the parent from the agent's final message, 2026-11-26.)

**Result:** ORACLE-GAP 3,747 -> 0 on the same 100,000 gate games (Dragons gi 96000-195999, oracle level 2); play with the oracle OFF byte-identical. Analyzer-side fix only; no new MTG_* flag.

## Mechanism (three families)
1. **The table paid Haven's {2} with the Haven itself (3,433 flags, every `gy-return` plan).** Haven's ability costs "{2}, {T}, Sacrifice". The engine holds the Haven back while paying the {2}; the oracle's payment table still offered the Haven as a source, so its only outcome ("Haven tapped for mana, rebuy does nothing") matched neither the engine's line nor any legal one.
2. **A failed rebuy was never reported as a dropped action.** When the {2} could not be paid, or the Haven was already tapped, `ApplyGraveyardReturnAbility` silently did nothing. Also hid 76 flags from Lathliss breakpoint continuations. Same defect the Lavamancer ping had before `NoteGraveyardDamageDrop`.
3. **A mana rock's entered-this-turn marker was treated as identity (238 flags).** Dragon Tempest + Mind Stone beside a Mind Stone already in play: two end boards differed only in which stone was left untapped, which no rule reads.

## Fix (`8b1dd8706`)
- Both gy-return sites record the held source; the oracle stores it with that payment step and leaves it out of the payment table. Play never reads it.
- `ApplyGraveyardReturnAbility` returns whether it fired; both sites call a new `NoteGraveyardReturnDrop` when it did not (reports only when the oracle, plan dump or `MTG_ACT_DROP_AUDIT` listens).
- A mana rock that cannot animate is treated like a plain land for the entered-this-turn marker (`AgeIsInert`).
- 3 new unit tests in `test_mana_oracle.cpp`. Agent's full suite 4,201/4,204; the 3 failures (Windswept Heath / Blood Sun card checks, merged mulligan buckets) also failed on its base binary (parent note: the land-1126b build passed 4,206/4,206, so these look environment-specific to the agent's worktree; recheck at landing).

## Gate (base `9e566000b` vs fix, oracle level 2)

| class | before | after |
|---|---|---|
| ORACLE-GAP | 3,747 | **0** |
| APPLY-DROP | 1,127 | 1,136 |
| CONTINUATION-DIFFERS | 468 | 187 |
| DROP-COVERED | 3,858 | 3,970 |
| PHANTOM (disclosed) | 99,380 | 103,802 |

Base matched the original gate exactly. APPLY-DROP: 10 newly visible (were hiding as ORACLE-GAP: a breakpoint continuation's rebuy unpaid though an order pays both; s30128278 T5, s30137584 T6, Lathliss plans), 1 gone. Extra PHANTOMs are rebuys only the Haven itself could pay.

## Identity (oracle off)
Every job digest and work units byte-identical: Dragons 20,000 std + 10,000 2HG; Kitty Equipment (Cid) and Landfall (Haunted Fengraf) 5,000 std + 2,500 2HG each (same code reached).

## Oracle CPU
Applies 386,903,370 -> 386,919,744 (+0.004%). Batch CPU 25,088 -> 25,046 s (shared box: noise).

## Open engine follow-ups (play changes, not done; repros in the design note)
- **APPLY-DROP x262 T3 m1 `{land=Gruul Turf}`, Bolt dropped:** Gruul Turf's bounce leaves {R} floating; the Mind Stone dig pays its {1} from that {R} instead of Haven's {C}; the continuation's Bolt then has no red. Bolt is undeclared in the plan, so the payer cannot see it at the dig. Needs a play change + A/B.
- **The menu still offers a rebuy only the Haven itself could pay** (a dead branch): a sound collapse, but it changes play, so it needs GT.
- The 10 newly visible continuation rebuy drops.

Files: `docs/design/dragons-oracle-gap-haven-2026-11-26.md`, `logs/dragons-ogap/gate_report.txt`, `logs/dragons-ogap/ident_report.txt`; raw output `build/scratch/dragons-ogap/{gate_base,gate_new,ident_base,ident_new}`.
