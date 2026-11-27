# Dragonstorm APPLY-DROP round 3 -- agent 5 handoff report (391K tokens)

I've stopped and handed off because my context hit the ~250K trigger. The fix is committed and pushed to `wip/ds-apply3`, but its all-deck A/B is still running, so the d8 b0 replays and the Round 3 doc section aren't done yet.

**Mechanisms in the Kolaghan declared cluster** (145 flags, 23 events). All 23 reproduce in single games, and 21 of those games are still won on the event turn.
- **One Apex of Power before Kolaghan (65 flags, 9 events): the one I fixed.** Example s30120119 T6: Apex can't be paid without Lotus Bloom, so the payer cracks the Bloom for {B} and the float drain spends all three {B} on Apex's generic. A Mountain stays untapped, and Kolaghan's {B} has no source. The visible-closer lump rule never sees this board because it gives up when its ordinary trial payment fails.
- **Two Apexes before Kolaghan (42 flags, 8 events): not fixed.** My guess, unverified: the exact payability check counts only the paying Apex's ten mana, so it can't see the strand.
- **Floating {B} spent by a later Seething Song's generic (24 flags, 3 events): not fixed.** The spare-source test only credits the paying ritual's mana, not later rituals'.
- **Unclaimed Territory is the {B} source (3 events): not fixed.**

**Lever `MTG_ONDEMAND_LUMP_KEEP_OWED`** (default ON, `=0` byte-identical):
- When a Bloom crack pays a bill but leaves the rest of the line unpayable, the bill is paid again with the Bloom cracked in a colour the rest owes, and those units are held back, so another untapped source pays instead.
- It replaces the original payment only if the exact check shows the original strands the line and the new one doesn't. That check credits the paying ritual's mana and an Apex's ten.
- It reads only the named line. The unit test fails with the lever off and passes with it on.
- My verdict is ADOPT, pending the A/B.

**Validation so far:**
- **Unit suite:** 4315/4315 pass. `unit_par.sh` still exits rc=3 ("shard counts do not add up") because every shard ran the whole suite instead of its slice. Every shard showed 0 failed, so I treated it as passing; the script itself needs a look.
- **Event repros:** Kolaghan declared flags go from 145 to 100. Five events are fixed: s30118865, s30120119, s30129637, s30142598 and s30155333, which now wins on T4 instead of T5. With the lever off, all 23 game logs match the round-2 binary.
- **Gate, off vs on** (72,000 games per arm):

| | off | on |
|---|---:|---:|
| Declared APPLY-DROP flags | 270 | 231 |
| Declared events | 50 | 46 |
| Kolaghan declared flags | 144 | 99 |
| Karrthus declared flags | 35 | 41 |
| Karrthus need-later flags | 279 | 274 |
| APPLY-DROP-ORDER | 66 | 58 |
| ENUM-GAP | 27 | 25 |
| Turns | 313,583 | 313,580 |

Units are -0.019% and CPU +0.045%. The one new event is Karrthus s30106357 T5, in a block whose play changed (10 of 362 blocks differ).

**Round-1 items:** item d is the same as the one-Apex shape, and 5 of its 9 events are fixed. Item e (Karrthus need-later past Apex) doesn't overlap, since the lever reads only the named line.

**Still running:** the A/B (seed base 5310000, 87,600 games) under the box lock. The chain is pid 1192705, the engine pid was 1263006 at handoff, and it finishes when `/workspaces/MagicDeckTester/build/scratch/ds-apply3/chain.DONE` appears, about 3 h from 09:00 UTC. Per-deck net, units and CPU come from it.

**Commits on `wip/ds-apply3`:**
- 2c5615491 – the lever, its unit test and comments (landable).
- 54643b9b7 – logs, tools, reports, the Round 3 doc draft and the handoff.

**What remains, in order:**
1. Run the A/B report: `python3 logs/ds-apply3/ab_report.py <scratch>/ab_out --list-slower`.
2. Replay every slower game at d8 b0, both arms.
3. Fill in the Round 3 draft and append it to the design doc.
4. Commit and push `wip/ds-apply3`, then write the final report.

There are no USER-owned questions open.

The handoff file has the verbatim rulings, running jobs, traps and next steps. Launch a fresh agent from it:
`/workspaces/MagicDeckTester/.claude/worktrees/ds-apply3/logs/ds-apply3/handoff.md`
