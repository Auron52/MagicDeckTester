# Merfolk Turns: the mana oracle's observe-mode menu materialised the Prophets champion fan (2026-11-19)

**Status:** FIXED on `wip/merfolk-oracle-enum`. Exact: the oracle audits the same plans in the same order.

## Symptom

`MTG_MANA_ORACLE=2`, Merfolk Turns seed 30000013 game index 13 (manifest job `games: 1, seed 30000013,
game_index 13`): the process passed 9 GB in under 9 s and aborted at the RSS cap. With the oracle off the game
plays in about 0.7 s at about 50 MB. One such game aborts a whole pooled mana-clean arm, so Merfolk Turns could
not be gated.

## Mechanism

- `manaoracle::AuditDecision` (ManaOracle.cpp), the ENUM-GAP pass, sets `t_gate_observe` and calls
  `TurnSolver::EnumerateMainPlans` a second time. In observe mode every per-subset mana gate admits its subset and
  the odometer's mana bound is off, so the list holds every plan the non-mana rules allow.
- `EnumerateMainPlans` calls `EnumeratePlansWithLand(state, pre)` with **no lazy layout** (`pb_lazy = nullptr`).
  No flag or audit mode is involved: the public wrapper never requested `MTG_PB_LAZY_CANDS` laziness, so the
  Wanderwine Prophets champion axis in `AppendSubdecisionAxes` took its eager branch
  (`if (pb_lazy != nullptr && fan > 1)` false) and copied one `Plan` per champion variant. That is the
  `Plan::Plan(const Plan&)` frame under `AppendSubdecisionAxes` in the 3 GB backtrace. The search hosts
  (`SolveWithLookahead`, `FSLineWin`) request the layout, which is why cc6edaeef's lazy legend-keep twin fixed
  plain play but not this path.
- With the mana gates open, many more base plans cast Prophets, and each is fanned by
  `(width + 1)^entries - 1`. The T6 m1 menu held 80 plans. The observe list that followed never finished.
- The oracle uses the observe list only through `SiblingKey` (the land and the action multiset). No fan varies
  either, so all but the first entry of each fan were duplicates that the loop's `done` test skipped.

## Fix

- `TurnSolver::MainPlanSeq` / `EnumerateMainPlansSeq` (TurnSolver.h/.cpp) return the same list with the fans
  held as a `PbLazyLayout`. `size()` is the full count. `At(i, storage)` gives entry i exactly. `Shared(i)` gives
  the entry or its fan prototype. `FanEnd(i)` gives one past the end of entry i's fan.
- It is exact by construction. Below its base enumeration, every edit `EnumerateMainPlans` makes is human-play
  only or the `MTG_DEVOUR_TRACE` diagnostic, so outside those the list is `EnumeratePlansWithLand`'s, and that
  function's lazy layout synthesises the materialised list at every index (`MTG_PB_LAZY_CANDS_VERIFY`). Under human
  play, `MTG_DEVOUR_TRACE` or `MTG_PB_LAZY_CANDS=0`, it returns `EnumerateMainPlans`' whole list. Both functions
  share `MainPlansBase`, which covers the play-timing and viewer plan-cap bookkeeping, and carry KEEP IN STEP
  comments.
- ManaOracle.cpp `AuditDecision` reads the observe list through the sequence. `MakeMenuView` gets an overload
  that inserts one `SiblingKey` per fan. The ENUM-GAP loop reads entry i and then jumps to `FanEnd(i)`. A fan's
  later entries share entry i's key, so the old loop dropped them at `done.insert` before any counter, budget
  read or verdict. Order, the first representative of each key, `MaxGateChecks` and the decision budget are
  unchanged. Priced work is unchanged too: `kPlans` already counts lazy entries, and `kPlanEmit` is charged
  before the axes. `Realize` is untouched.
- The menu itself (the first enumeration) stays materialised. Each entry is applied and kept as an `EngineReal`
  board, so the plans themselves are not the dominant cost.

## Measurements (Release, oracle on = `MTG_MANA_ORACLE=2`)

| | base (fb9cb5e03 PGO binary, byte-identical to Release) | fix |
|---|---|---|
| gi13 peak RSS | 9,588,040 kB, aborted at the 8 GB cap after 8.9 s | 94,820 kB |
| gi13 wall | (aborted) | 1.39 s, finishes |
| gi 0-49 pooled, 14 threads, process peak | 1,114,936 kB (49 games, gi13 excluded) | 681,116 kB (50 games) |

- Oracle output, gi 0-49, all `[mana-oracle]` lines per job except the timing-only `*-COST` lines: identical on
  the 49 common jobs (1,728 lines: DOMINATED-LAND, DROP-COVERED, PHANTOM, CONTINUATION-DIFFERS, APPLY-DROP,
  APPLY-DROP-ORDER, ENUM-GAP, DECISION).
- Play digests: base off = fix off = fix on = base on (gi 0-49, gi13 included where both ran).
- gi 50-299 pooled (250 games; the base finished every one): every per-job `[mana-oracle]` line (22,562), every
  `=== MANA ORACLE` total (decisions 2,734, applies 4,920,541, gate_rejects 57,821, every class count) and every
  play digest are identical. Process peak: 3,472,320 kB on the base and 1,757,160 kB with the fix. The summed
  oracle time was 219.5 s on the base and 211.8 s with the fix. Game wall is not comparable here, because the
  base binary is PGO and the fix binary is Release.
- `MTG_PB_LAZY_CANDS_VERIFY=1` with the oracle on, gi 0-49 without gi13: 559,949 lists (plain and observe-mode),
  3,657,512 entries, 0 MISMATCH.
- Units: 3,718 / 3,718 pass. Smoke from the worktree: 231 passed, 0 failed, 0 new, configs changed 0.

## Unit test

`test_merfolk_turns.cpp`, "The lazily held main menu is EnumerateMainPlans' list, entry for entry, mana gates open
or not": on a nested-Prophets Vial board, with the gates closed and open, the sequence equals
`EnumerateMainPlans` at every index, the champion fan is held lazily (`RealSize() < size()`), and every fan run
shares the land and actions of its first entry.

## Open

- Any other caller of `EnumerateMainPlans` on an autonomous board with a large fan still materialises it (the
  viewer, `AIEngine`'s external-chooser path, the oracle's own menu). None is on a hot path today. Move a caller to
  `EnumerateMainPlansSeq` if it shows up.
