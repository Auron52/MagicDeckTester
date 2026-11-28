# bs-oracle handoff 2

Branch `wip/bs-oracle`, worktree `.claude/worktrees/bs-oracle`. Scratch: `/workspaces/MagicDeckTester/build/scratch/bs-oracle/` (S).
The full write-up is in report2.md.

## Done (commits)

- **633af33b2**: `MTG_ORACLE_LEFTOVER_FOLD` (default ON), with `MTG_ORACLE_LEFTOVER_FOLD_CHECK=1` as the check mode.
  - Test seams: `manaoracle::LeftoverFoldOverrideForTest`, `LeftoverFoldedProbes`, `LeftoverFoldMismatches`.
  - New unit test in `test/unit/test_mana_oracle.cpp`.
  - Units: 4349 of 4349 pass.
  - Check mode: 29.2 M folded probes, 0 mismatches.
  - Exact by construction under the two self-checked claims described in report2.md, section 2.
- The logs commit with this file: report2.md, the measurement scripts, and the cmp outputs.

## Measurements

| set | memo (30efe9772) | fold (633af33b2) | notes |
|---|---:|---:|---|
| Blood Sun, 20 games | 2,920 s CPU | 1,807 s | 0.62x; equal flags and digests |
| Dragonstorm + Prevent Damage, priced work | 24,656 ms | 24,584 ms | -0.3%; equal flags and digests |

- In the 7-game CAPPED-DECISION sample (`mdc7.json`), all 7 games still have CAPPED-DECISION.
- On that sample, 9 of the 13 heavy decisions now stop on the WALL-BP-CAP thread-CPU backstop at 274-883 priced s.
  That points to unpriced muted-recompute work.

## Running jobs (lane; never killed by me)

| job | what it is | DONE marker | output |
|---|---|---|---|
| `S/stage2.sh` | its last arm, memo_dc: the memo binary on mdc7.json, with HEAVY | `S/memo_dc.DONE`, then `S/stage2.DONE` | `S/memo_dc.{out,err}` |
| `S/perf531.sh` | the post-fold g531 T4 m1 profile, queued in the lane | `S/perf531.DONE` | `S/p531lf.self.txt`, `S/p531lf.children.txt`, `S/p531lf.err` |

## Perf finding

The perf run is `S/p531lf.*` (mtg.lf, g531 T4 m1, perf at 49 Hz with -g). Its call stacks are only partly resolved:
the build has no frame pointers, and 24% of samples are [unknown].

- **Self time:**
  - BuildSimKeyBase fell from 24% (before the fold) to 7.3%.
  - Then GameState copy 4.0%, Realize 3.0%, CollectActions 3.0%, memmove 2.9%, memcmp 2.8% and
    CollectActivationKeys 2.5%.
- **Inclusive time:** the breakpoint continuation derivation leads. BpEnumEntryFor (9.8%) calls
  BpDeriveContinuationList (9.4%), which calls BpUnbranchedCanon / BpCanonFront (9.2%), which call
  EnumeratePlansWithLand(Uncached) (9.2%), which calls CollectActions. ApplyPlan / ApplyPlanDirect add 7.0%.
- So after the fold, payment tables no longer dominate this decision. Breakpoint plan enumeration plus applies do.
  That enumeration is the kind of muted-recompute work that WALL-BP-CAP's comment names, and it is unpriced.
- The next cut is in the oracle's repeated BpDeriveContinuationList calls on frontier boards: memoise per board, or
  fold identical continuation derivations.
- **The thread-CPU backstop is not deterministic.** This run reached 9.1 M applies and 913 priced s at WALL-BP-CAP.
  lf_dc's run of the same decision stopped at 6.6 M applies and 726 priced s. The backstop reads thread CPU, which
  moves with box load and with perf's overhead, so where the decision is cut, and so its CAPPED-DECISION count,
  varies from run to run. Raise this with the parent: the gate's verdicts on these decisions depend on the box's load.

## Remaining, in order

1. When `S/memo_dc.DONE` lands, run `python3 logs/bs-oracle/cmp.py memo_dc lf_dc`, then compare per decision.
   - A decision that finishes on both arms (its DECISION-COST line has no BUDGET-SPENT) must have identical flags.
   - The arms must agree on play digests.
   - Count the games that still have CAPPED-DECISION on each arm.
2. Read the perf profile. Name the unpriced work behind WALL-BP-CAP (the muted breakpoint derivations), and cut it
   if it is a collapse. Pricing it only moves the cap.
3. g51 T4 m1 still spends 1,300 priced s on pay and simkey (119 M payments, 153 M keys). Find which part of the
   table that is.
4. Possible follow-up: fold base probes. That needs a success prediction, so it rests on a stronger claim.

## USER rulings in the brief (verbatim, carried forward)

- "cut CPU": suite headroom never justifies CPU.
- A sound collapse defaults ON; a lossy narrowing needs its A/B; everything findable at unlimited budget.
- USER-owned decisions are surfaced, never settled.
- HARD RULES: NEVER push phase-1-2-deck-analyzer (the parent lands it); no GT accept; no smoke/regression tiers. Do
  NOT take claude_box.lock. Every run goes through `scripts/box_lane.sh --slots N [--cap-gb G]` with N <= 2.
