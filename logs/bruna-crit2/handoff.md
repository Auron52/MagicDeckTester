# bruna-crit2 handoff (2026-11-24 ~15:30 UTC)

Worktree /workspaces/MagicDeckTester/.claude/worktrees/bruna-crit2, branch wip/bruna-crit2 (push ONLY this branch; never
phase-1-2-deck-analyzer, never push_queue.py). Scratch: /workspaces/MagicDeckTester/build/scratch/claude_bruna_crit2/.
Record: docs/design/line-keep-crit-2026-11-24.md.

## Done (commits)
- 864969114 MTG_LINE_KEEP_CRIT (default ON) + unit tests + three fixture pins.
- f04665636 doc (reproduction, checks, CPU), mk_ab.py --extra. Pushed.
- Binaries: scratch/mtg.base (origin e8a2f7d64, src tree 7e8f9795, copied from gate-1124's clean Release),
  scratch/mtg.crit + mtg-test.crit (864969114; src identical in f04665636, so mtg.crit is the A/B binary).

## Findings
- Bruna s30051257 / s30051968 (the 7a / 7b cases) do NOT reproduce on origin: APPLY-DROP 0; with MTG_LINE_KEEP_ROCK=0
  8 / 3. They are Sol Ring lines, fixed by KEEP_ROCK (ON since 2026-11-20). Jumbo gi1447 / Selesnya gi837 were 7a's own
  losses, not shipped-payer defects (origin T4 / T4).
- Existing line-keep misses the class only on lines with NO rock and no reducer (LineKeepBound false; KEEP_ALL parked).
- Every 7a / 7b regression game digest-identical base vs crit at its shapes, and at d8 b0 (both arms).
- Play slice (13 shapes x 12 games x 2 arms, seed 2690000): no game turn changes; 483 repairs; +0.59% instructions.
- Oracle probe (MTG_MANA_ORACLE=2, seed 2700000, 8 games/arm; scratch/orc.{out,err}, DONE_orc): Bruna / Angels / Jumbo
  / Selesnya / Alchemist APPLY-DROP identical off vs on (the remaining drops are not this class); EDF was running.

## Remaining, in order
1. build2 (test-fixture pins; logs/bruna-crit2/build2.log) was queued on claude_build.lock; when it ends, run
   build/Release/mtg-test (expect 4170/4170). The pins are arm=0, equivalent to MTG_LINE_KEEP_CRIT=0, under which the
   three tests passed on mtg-test.crit.
2. The A/B: `python3 logs/bruna-crit2/mk_ab.py <out.json> [--extra]` (107,000 games / 4,280 jobs; --extra 131,000 /
   5,240) with mtg.crit, one pooled batch, by the coordinator.

## USER rulings (verbatim, from the brief)
- "For Bruna, we shouldn't adopt with those results but we should come up with another way to fix the issue."
- "I think I would be willing to accept something that helps a lot of decks, but doesn't help a few deck and then look
  for ways to mitigate the cost on the other decks without blocking the adoption. Naturally, if the only reason it is
  worse on those other decks are the specific seeds we can just move on. d0 is not very important at all as I said
  previously. The primary thing that matters is play settings, though we should also check whether any worse case is a
  result of clairvoyant play. We can ignore those."
- "Play settings are the most important, so if the change was specific to Maze's end we shouldn't adopt." "(until we
  can show it is clairvoyance, variance or fix it)"
- "cut CPU; 3x is not a CPU licence"
