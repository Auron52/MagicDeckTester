# bs-oracle handoff

Branch `wip/bs-oracle` in worktree `.claude/worktrees/bs-oracle`, off `origin/phase-1-2-deck-analyzer` 038940b9f.
Scratch is `/workspaces/MagicDeckTester/build/scratch/bs-oracle/` (S below).

## Done

- **30efe9772**: `MTG_ORACLE_PAY_TABLE_MEMO`, default ON, `=0` hatch. A per-decision frontier payment-table memo in
  `src/ai/ManaOracle.cpp` (`PayTableMemo`, at `Realize`'s `MinimalPayments` call).
  - `MTG_ORACLE_PAY_TABLE_MEMO_MB` (default 256) caps kept tables.
  - `MTG_ORACLE_PAY_TABLE_MEMO_CHECK=1` re-enumerates every hit and reports a mismatch.
  - Test seam `manaoracle::MenuVerdicts`.
  - Unit test "Ulamog's target-choice copies share one frontier payment table, with the same verdicts" in
    `test/unit/test_mana_oracle.cpp`.
- **bc99543ab**: `logs/bs-oracle/` holds the apparatus (`arm.sh`, `cmp.py`, the manifests, the chain scripts) and the
  results (`cmp_bs20.txt`, `cmp_other.txt`, `identity.txt`).

## Measurements

| set | origin CPU | memo CPU | ratio |
|---|---:|---:|---:|
| 20 Blood Sun games (on arm) | 3,605 s | 2,920 s | 0.81x |
| Dragonstorm, 20 games | 9.1 s | 8.1 s | 0.89x |
| Prevent Damage, 20 games | 85.0 s | 75.7 s | 0.89x |

- Blood Sun oracle ms fell from 3,347 s to 2,567 s. Its off arm is 551 s.
- g389 fell from 429 s to 77 s (the Ulamog target fan).
- Flags are equal on every class, and every oracle exit counter is equal. For example, applies are 11,165,596 on both
  arms and options 13,530,553 on both.
- Play digests are equal on all 60 games: memo on, memo off and origin on.

## Running or queued jobs (lane)

All of these are lane jobs launched via `scripts/box_lane.sh`. Each one writes a DONE marker.

- **units** (2 slots): `S/units.sh`. It builds `mtg-test`, then runs the new case (`S/unit_new.log`), then runs
  `scripts/unit_par.sh` with 2 jobs (`S/unit_par.log`).
  - DONE marker: `S/units.DONE`.
  - Lane log: `S/lane_units.log`.
- **chk** (1 slot): `S/chk.sh`, the memo CHECK run on g176, g389 and g373.
  - DONE marker: `S/chk.DONE`.
  - Output: `S/chk.err`. Look for a `[pay-table-memo] MISMATCH` line and the memo summary line `mismatch=`.
- **study531** (2 slots): `S/study531.sh`. It runs g531 T4 m1 (413 plans, a decision that spends the full budget)
  on origin and on the memo binary, with the cost trace, plus perf on the memo binary.
  - DONE marker: `S/study531.DONE`.
  - Output: `S/s531_{base,memo}.{out,err}` and `S/s531_memo.perf`.

## Remaining, in order

1. Read `S/units.DONE`, `S/unit_new.log` and `S/unit_par.log`; every shard must pass.
2. Read `S/chk.err`; mismatch must be 0.
3. Read study531. Compare the `DECISION-COST` lines (priced and kinds) and the plan counts audited before
   BUDGET-SPENT between base and memo. That measures what the memo does for the decisions that spend the full budget,
   which are 42% of the gate's games and its cost mass.
4. `python3 scripts/check_references_safe.py`, then push `wip/bs-oracle` only (never phase-1-2-deck-analyzer).

## USER rulings in the brief (verbatim)

- "cut CPU" -- suite headroom never justifies CPU.
- A sound collapse defaults ON; a lossy narrowing needs its A/B; everything findable at unlimited budget.
- USER-owned decisions are surfaced, never settled.
- HARD RULES: NEVER push phase-1-2-deck-analyzer (the parent lands it); no GT accept; no smoke/regression tiers.
  Do NOT take claude_box.lock. Every run goes through `scripts/box_lane.sh --slots N [--cap-gb G]` with N <= 2.
