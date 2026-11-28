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

Pending at handoff: `S/perf531.sh` was still waiting for a lane slot (its lane wrapper had not been admitted). When `S/perf531.DONE` lands, read `S/p531lf.self.txt` (self time) and `S/p531lf.children.txt` (inclusive time), and compare them with the pre-fold flat profile in `S/s531_memo.perf`. Before the fold, the leaders were BuildSimKeyBase at 24%, memmove at 7.4% and ApplyPreTap at 3.8%. Expect the breakpoint derivation under muted recomputes to lead now.

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
