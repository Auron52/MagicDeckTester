# th-resid handoff (2026-11-27 ~15:02 UTC, agent stopped at ~335K tokens on the parent's request)

Worktree `/workspaces/MagicDeckTester/.claude/worktrees/th-resid`, branch `wip/th-resid`, based on origin
`ce045ad8b`. Scratch is in `/workspaces/MagicDeckTester/build/scratch/th-resid/`. The design doc draft is
`docs/design/treasure-hunt-oracle-residuals-2026-11-27.md`; read it first, since it carries the mechanisms and seeds.
Its "Measurements" section is still empty.

## Done (in the WIP commit on wip/th-resid; see `git log -1`)

The finding is that all four residual classes are ORACLE false positives. The changes are oracle-only
(`src/ai/ManaOracle.cpp`), so there is no play change, no GT and no play A/B. Play is verified byte-identical on 4
repros: the game output matches the base binary. Three new levers, each `EnvOn(..., true)` and read on EVERY call
(not cached) so unit tests can pin them with setenv:

1. **`MTG_ORACLE_EG_PROVIDER_PRUNE`** (ENUM-GAP). `ContLandCastProviderPruned`: when a realisation pays a later
   flood-engine cast with a continuation land (one played mid-plan), the script is replayed to that payment's
   frontier and the provider's `ShouldCastDrawEngine` is asked. If it refuses (TH strict flood: drop spent, no
   outlet), the realisation is rejected and counted as `enum-gap rejections: ... provider=N`.
   `TapsContinuationLand` gained an `int* num` out-parameter.
   - Mechanism: s30100161 T3 `{no land + TH + TH}`. The menu offers TH only with the land deferred, which is the
     USER's land rule. TH2 after the land drop is pruned by the USER's `THStrictFlood` provider rule. This is NOT a
     completeness defect.
2. **`MTG_ORACLE_DROP_WON`** (APPLY-DROP / APPLY-DROP-ORDER become DROP-COVERED, with the text "(would-be X)").
   This fires when the plan's engine line has `OpponentHasLost`, or when ANY menu entry's line has won
   (`MenuHoldsWin`, which reads `t_menu_real`).
   - Mechanism: s30109829 T5 (41 of the 62 ORDER flags). The TH continuation casts Land's Edge and throws about 30
     lands (opponent at -48), so Throes is unpaid.
3. **`MTG_ORACLE_UNDECL_MENU_COVER`** (undeclared APPLY-DROP becomes DROP-COVERED). It fires when
   `LostUndeclared(plan, er)` holds and every full-payment board is matched by another menu entry's ENGINE board
   (same CoreKey, at least the resources). `t_menu_real` is a thread-local pointer to the decision's `real` vector,
   set in AuditDecision (`MenuRealScope`) and in `PlanVerdict`.
   - Mechanism: s30106563 T4 `{no land + TH}` is covered by `{land=Cascade Bluffs + TH}` (the Vampires s30230089
     shape).

Item 4 (Throes continuation) is confirmed as a re-plan on new information; lever 2 covers it where a menu line wins.

Unit tests appended to `test/unit/test_mana_oracle.cpp` (4 TEST_CASEs at the end, helper `OracleEnvPin`). **NOT YET
COMPILED OR RUN.** One compile error has already been fixed: `counters.Add(Counter::Type::Depletion, 2)`.
- ENUM-GAP provider prune on a synthetic 3-Island board, with a Land's Edge-in-play control.
- Drop-won on the Slivers replicate board with the opponent at life 0. This is contrived and may need rework if
  ApplyPlan stops early on a lost opponent.
- Undeclared cover on the s30106563 T4 fixture: 29-card hand, the reveal list as the library, Skerry with depletion 2,
  opponent at 500.

The fixtures (the bp land choice, the greedy continuation) are unverified. Iterate on them if they fail.

Repros on the new binary (`MTG_MANA_ORACLE=3`, `build/scratch/th-resid/repro.sh <tag> <turn> <gi>`, `BIN=` overrides),
base -> new:

| seed | turn | flags, base | flags, new |
|---|---|---|---|
| s30100161 | T3 | ENUM-GAP 1 | 0 (provider=3) |
| s30109829 | T5 | APPLY-DROP-ORDER 41 | 13 with lever 2's first cut, then 0 with MenuHoldsWin |
| s30106563 | T4 | APPLY-DROP 2 | 0 |

s30108799 T4 still has 2 declared APPLY-DROPs: `{no land + TH + TH}`, where the continuation itself cast the second
TH, so the declared one reads as "dropped" although the card was cast. This is a likely further oracle false
positive. It was not fixed: about 27 declared non-Throes drops remain in the old gate, among them this shape and
`{land + TH + LE}` dropping LE (seeds 30113855, 30113536, 30104496, 30101759, 30100134, 30110094).

## Running jobs (left running)

- **Unit-test build**: `bash scripts/box_lane.sh --slots 2 -- ./build.sh` in the worktree. Log
  `build/scratch/th-resid/build3.log`; the DONE marker is a `rc=N` line at the end. It was at 32 of 370 at 15:01 UTC,
  about 90 min to go. On `rc=0`, run unit tests through the small lane:
  `bash scripts/box_lane.sh --slots 2 -- bash scripts/unit_par.sh` from THIS worktree. Check that the 4 new cases
  pass, and fix the fixtures if not (wait about 200 s after edits before rebuilding; this is 9p).
- **Oracle gate**: PID **1635002**, `flock claude_box.lock bash logs/th-resid/run_gate2.sh ...`. It is queued behind
  the Dragonstorm agent's lock (until about 17:45 UTC). It runs the 20,000-game TH slice (gi 100000-119999,
  manifest `build/scratch/th-resid/gate.json`), first with `base=build/scratch/th-resid/mtg_base` (land-1129 build =
  origin ce045ad8b), then `new=build/scratch/th-resid/mtg_new` (this branch). The two run back to back because the
  oracle levers are process-level. Outputs: `build/scratch/th-resid/gate_out/{base,new}/flags.err`, `batch.out`,
  `DONE` (rc), plus `gate_out/ALL_DONE`. On completion:
  - run `python3 logs/th-resid/gate_report2.py build/scratch/th-resid/gate_out`, which gives per-binary counts and
    the reclassified DROP-COVERED columns AD-won / ADO-won / AD-undecl-cov;
  - `grep 'enum-gap rejections' gate_out/new/batch.out` (the provider count is in the `===` lines of flags.err);
  - confirm that the per-job digests in base and new `batch.out` are identical, which proves play is unchanged;
  - fill in the doc's Measurements section and copy the table into `logs/th-resid/`.
- **CPU**: the changes are oracle-only. Play CPU is unchanged by construction (digests); record the oracle `ms=` from
  the `=== MANA ORACLE` summary lines, base vs new.

## Remaining, in order

1. Unit build, then run the unit tests; fix the fixtures. Commit.
2. Gate result, then the report and doc Measurements. Commit, run `python3 scripts/check_references_safe.py`, and
   push **wip/th-resid only**.
3. Optional: the s30108799 "continuation cast the declared card" false positive (the lost name is still counted as
   dropped although the card left the hand). Also check the remaining declared `{land + TH + LE}` drops on the new
   gate output.
4. Final report to the parent: mechanisms, levers, gate table, the statement "no play change, so no A/B, no slower
   games, no GT", and the information below.

**Information for the USER (not a question):** the TH1 -> land -> TH2 line is refused by the USER's own strict-flood
rule (`MTG_TH_STRICT_FLOOD`). If the USER ever wants Treasure Hunt chains through a mid-turn land drop with no outlet,
that is a change to that rule. Nothing in this work changes it.

## Brief rules (verbatim from the parent's brief)

BOX RULES (hard): ONE big engine process at a time machine-wide. Any gate, A/B, tier or full-core run goes under `flock /workspaces/MagicDeckTester/build/scratch/claude_box.lock` with `MTG_RSS_CAP_GB=40 MTG_PGO=0`; another agent's Dragonstorm gate+A/B currently holds that lock until roughly 17:45 UTC, so do investigation first. Small work (single games, d8 b0 replays, unit runs, a worktree build) goes through `bash scripts/box_lane.sh --slots N [--cap-gb G] -- <cmd>` with N <= 2. Use `scripts/unit_par.sh` from YOUR worktree (the primary checkout's copy is stale). Never `timeout`; never wait with `pgrep -f` (use `kill -0 <pid>` or a DONE marker in a run_in_background waiter, not polling turns). Pool every batch into one `mtg --batch` manifest.

ENGINE RULES (hard): no greedy pick inside the search window; "do nothing more" stays available at every root; everything must stay findable at unlimited budget (a budget may re-price, never make a line unfindable); no unbudgeted-only changes. A sound collapse/completeness fix defaults ON; a lossy narrowing needs an A/B and stays default OFF. Every new lever is `EnvOn("MTG_X", true)` with `=0` byte-identical to the base, and gets unit tests (lever on fixes / off reproduces).

VALIDATION for a play-changing fix: (a) gate re-run on the TH slice (logs/th-apply/mk_gate.py; MTG_MANA_ORACLE=2; off vs on, one pooled batch) showing the flags drop; (b) held-out play A/B over every deck the change can reach, standard and 2HG, seed base 5510000 + gi with game_index = gi, one pooled batch, net slower/faster/units/CPU per shape; (c) every slower game replayed at `--depth 8 --budget-ms 0` both arms (it must recover, or be shown clairvoyant/budget luck). USER adoption bar: judge the average at play settings; adopt if many decks gain, mitigate the few worse after; ignore seed noise and clairvoyant losses; d0 barely counts. USER: "cut CPU" -- measure CPU (instruction counts or units/cpu from the batch) and cut any cost; suite headroom never justifies CPU. Do NOT accept GT, do NOT run smoke/regression tiers, and NEVER push the shared branch phase-1-2-deck-analyzer -- push only wip/th-resid (run `python3 scripts/check_references_safe.py` before pushing). The parent lands it with GT.

USER-OWNED decisions (cast order, mulligan policy, a lossy lever's default) are never settled by you: state them in your final message with numbers and a recommendation.

CONTEXT LIMIT (USER rule): when your context passes ~250K tokens, finish the current step, commit WIP, write logs/th-resid/handoff.md (done with hashes; remaining in order; these rules verbatim; running jobs with PIDs, outputs and DONE markers; measurements), push wip/th-resid, and stop with a short report. Normally stop between 250K and 350K; never exceed 400K. If a fix line iterates without progress, park it and report rather than grinding.
