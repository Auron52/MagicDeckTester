# Dragonstorm APPLY-DROP round 2 -- agent 4 final report

I couldn't watch Windows CI because `gh` isn't authenticated in this session. The parent should check the run for `wip/ds-apply2`.

I've done the work the brief and your follow-up asked for. Both levers are validated and pushed to `wip/ds-apply2` (83c1bdfcc); the shared branch was not touched. Checks: full unit suite 4314/4314, all event repros, the gate, and the all-deck A/B.

**Commits**
- **0bb75f0a3 (landable, code):** `MTG_CCO_RESERVE_JOINT` and `MTG_LINE_NC_LEFT`, both default ON, with `=0` byte-identical.
- **83c1bdfcc (landable, docs):** the Round 2 section in `docs/design/dragonstorm-apply-drop-2026-11-26.md`, the scripts and the final `logs/ds-apply2/handoff.md`.
- The predecessor's WIP commits were reworded into these two. The old head is kept on `backup/ds-apply2-wip`.

**Verdicts (not USER-owned; I'm stating them for visibility)**
- **`MTG_CCO_RESERVE_JOINT`: adopt, default ON.** Two Unclaimed Territories each counted the other as covering both of Karrthus's {B} and {G}, so neither was held for him. The release now requires the other lands to cover the shortfall jointly.
- **`MTG_LINE_NC_LEFT`: adopt, default ON.** This replaces `MTG_LEFTOVER_SWAP_EXACT`, which I deleted because its premise was wrong.
  - In s30129376 the {B} is not lost at Ruby's payment. Ruby leaves {B} floating, then Seething Song's {1} spends it, and nothing is left floating to swap.
  - The real cause: the line-keep rung decides "only creatures remain" by comparing against the line's total noncreature mana value (5). That total is never counted down, so at Song's payment Karrthus was judged as a noncreature bill and nothing was kept.
  - Both apply worlds now count the line's noncreature casts down at the cast site.

**Event repros:** every one goes to 0 flags, and each is fixed by exactly one lever.
- `CCO_RESERVE_JOINT` fixes s30096797, s30112157, s30142602, s30155973, s30089733 and s30103016.
- `LINE_NC_LEFT` fixes s30129376 and s30126750.

**Gate** (Dragonstorm, 180,218 games):

| | off | on |
|---|---:|---:|
| Declared APPLY-DROP events | 59 | 50 |
| Need-later events | 48 | 49 |
| ORACLE-GAP | 0 | 1 |
| Turns | 313,583 | 313,583 |

- 10 declared Karrthus events are fixed.
- The 2 new APPLY-DROP events are in blocks whose play changed (5 of 362 blocks differ).
- The new ORACLE-GAP is in a block where play is identical, so it is the oracle's own coverage moving, not the engine.
- On the leave-one-out slice, `CCO_RESERVE_JOINT` alone takes declared events from 14 to 10, and `LINE_NC_LEFT` alone leaves them at 14.

**All-deck held-out A/B** (seed base 5220000, 87,600 games): 43,800 pairs, net 0, 0 slower and 0 faster in every one of the 128 shapes, for every deck in both standard and 2HG. Units are -0.00% overall (largest move boars 2HG -0.01%), and CPU is +0.09%, which is noise.

**d8 b0 replays:** none were needed, because no game got slower.

**Still open (separate work):** the Kolaghan declared cluster (144 flags), the need-later Karrthus cluster (270–279 flags), and round-1 items d and e.

No jobs are running and there are no USER-owned questions.
