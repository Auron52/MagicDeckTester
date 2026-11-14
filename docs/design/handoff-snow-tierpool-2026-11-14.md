# Handoff 2026-11-14: Snow continuation-pay package + tier pooling harness

This is the checkpoint for whichever agent (Claude or Codex) continues. Verify running processes before acting.

## Codex continuation, 2026-11-14

The USER chose to land the Snow package as-is, treating the measured LHC d0
+0.0010 as low priority. This accepts the reserve's correct realization of the
committed plan; the d0 Martyr-sac valuation and missing trace action remain
separate follow-ups. `MTG_D0_SNOW_PUMP_CREDIT` stays on by default as measured.
The existing smoke and regression runs were inspected and accepted without
re-running them. Both tiers had zero slower games; `test/check_gt_logs.py`
reported 1170 consistent logs, zero stale, zero missing.

Snow and its accepted GT were committed as `32da5c41a` and pushed to
`origin/phase-1-2-deck-analyzer`. CI build run `43596869487` was started.
The pooled harness was squash-merged into the Snow worktree and built. A single
`--smoke --regression --overnight --deck=th` run passed all three tiers; the
scenario-failure probe aborted and killed its batch; the explicit serial
fallback passed its smoke row; a VPC sample recorded zero stateless
fallbacks in 1,339 interactive steps; and the simulated no-FIFO fallback
passed its smoke row (544 scenarios, three game jobs). These results are in
`build/scratch/tier-pool/` in the primary checkout.

Cyclone backstop decision: **keep the fixed 30031..30033 block**. Seed 30033
now hits the two-million-unit cap, which is exactly the regression signal
specified by `.claude/skills/regression-testing.md`; re-screening would hide it.
The stale `test/regression_cases.sh` comment was corrected.

## 1. Snow package (branch `snow-contpay`, worktree `.claude/worktrees/snow-contpay`, LOCAL, not pushed)

- **Branch state:** rebased onto origin `bca756347` at 10:2x UTC. It carries 10 commits ending at `6ad20e507`. The pre-rebase backup is branch `backup/snow-contpay-pre-rebase`.
- **What it contains:**
  - `MTG_REPEAT_EXTRAS_RESERVE`: Owl K-repeat extras keep what later activations owe.
  - `MTG_EXEC_SITE8_CONT_INLINE`: the executor applies a site-8 continuation's activations inline.
  - `MTG_D0_SNOW_PUMP_CREDIT`: the d0 lethal check credits Rimefeather Owl's ice counters. It is default ON, and the USER may veto it.
  - `MTG_LINE_PAY_AUDIT` audit classes.
- **Evidence:**
  - Held-out A/B: every deck and axis is net <= 0. Ledger `docs/design/snow-continuation-pay.md` section 5.1; data in `build/scratch/snowcp/ab3/`.
  - Built, Release. Unit tests pass (3088/3088).
- **RUNNING:** `build/scratch/snowcp/run_tiers.sh` (PID 634677, detached), which runs the old harness's `--smoke` and then `--regression` in that worktree.
  - Smoke FINISHED at 10:53 UTC: rc=1 (7 cases changed, none slower; see "Smoke result" below). rc=1 is the expected GT drift, not a crash.
  - Regression FINISHED at 11:57 UTC: rc=1, 9 rows changed with every score identical (see 1b).
  - Logs: `build/scratch/snowcp/tier_{smoke,regression}.log`.
  - DONE marker: `build/scratch/snowcp/tiers.DONE` (one `mode rc= wall=` line per tier; `tiers.DONE.tmp` holds the lines so far).
- **Next steps:**
  1. Read both logs' per-game audit. A searched slower game must recover at `--depth 8 --budget-ms 0`. Smoke already moved non-Snow decks (below).
  1b. **RUNNING since 11:57 UTC:** the held-out A/B, PID 698885 (`build/scratch/snowcp/ab4/run.sh`, detached). DONE marker: `build/scratch/snowcp/ab4/DONE`. It is 23,200 games, package off (`base`) vs default (`new`), seeds from 131000, chunks paired by seed and game_index. Cells: `pdP` 600/arm, `jumboP` and `lhcP` 1500/arm (play depth, b20; PD plays d6), and `pd0`, `jumbo0`, `lhc0`, `edf0` at 2000/arm (d0). Report: `python3 build/scratch/snowcp/tools/ab4_report.py build/scratch/snowcp/ab4`. Do NOT start another. Regression tier (rc=1, 3820 s): 9 rows changed, every score identical, 0 slower; VPC 837 refs, 0 drift; scenarios 544/544.
  **1b RESULT (12:05 UTC; report `build/scratch/snowcp/ab4/report.txt`): NOT PUSHED. LHC d0 is +0.0010 (2 slower, 0 faster), which fails "every deck net <= 0".** Every other cell is neutral or better: pdP / jumboP / lhcP 0 changes in score (lhcP play-changed 2 games), pd0 0, edf0 0, jumbo0 -0.0010 (2 faster).
  - **Mechanism (single-game traces, `build/scratch/snowcp/lhc/`):** in lhc0 gi326 and gi400 the d0 runner's plan is `[Heliod {1}{W} lifelink x2, Martyr of Sands sac {1}]`. The OLD apply paid both Heliods first and ran out before Martyr. The reserve pays the plan's named Martyr step and caps the extra Heliod at 0. So the reserve does what it was designed to do (realise the committed plan). But here the plan is worse than the accident: a 2nd lifelinker beats Martyr's small life gain, and the win moves T5 -> T6. The loss is the **d0 greedy's poor Martyr-sac choice**, which the payment bug used to hide. At play depth the search does not make it (lhcP: 1500 games, 0 slower).
  - Also seen: a trailing sac-outlet activation (Martyr) is NOT logged as an ABILITY action in the game trace (logging gap; the board shows it fired).
  - **USER decision:** (a) land as is, accepting LHC d0 +0.0010 since d0 is the lighter-bar greedy and searched play is unchanged; (b) first fix the d0 runner's Martyr-sac valuation (or make the d0 plan omit a step worth less than the extra it displaces), then re-run 1b; (c) keep the reserve but do not land until (b). Agent recommendation: (b). Do NOT scope the reserve away from d0 to dodge the test (CLAUDE.md "no gating to dodge a test").
  1c. (What 1b runs.) Run ONE pooled held-out A/B over every deck either tier moved: package off (`MTG_REPEAT_EXTRAS_RESERVE=0 MTG_EXEC_SITE8_CONT_INLINE=0 MTG_D0_SNOW_PUMP_CREDIT=0`) vs default, at each moved row's depth and budget. Seeds are disjoint from the suite's; reuse the `build/scratch/snowcp/ab3/` manifest shape. Every deck must be net <= 0, or STOP and do not push.
  2. Accept from the inspected runs in the worktree: `bash test/regression.sh --smoke --accept`, then `bash test/regression.sh --accept`. These promote the existing runs; they do not re-run.
  3. Run `python3 test/check_gt_logs.py` and `python3 scripts/check_references_safe.py`.
  4. Commit the GT, fetch, and rebase if origin moved. If src or GT replays, rebuild and re-run the tiers.
  5. Push the shared branch `phase-1-2-deck-analyzer`. This session last pushed at 06:06 UTC, so the window has been open since 10:06.
  6. Start a CI watch in the background: `gh run watch <id> --interval 60 --exit-status`.
- **Smoke result (11:30 UTC): 7 cases changed, none slower; Jumbo d0 has one game faster.** Besides the 3 Snow rows, play moved in `pd_smoke_d5` (gi33, still a loss), `edf_smoke_d0` (gi551, opponent 1 life higher at T5, same T6 win), and in `jumbo_smoke_d0` and `lhc_smoke_d0`. A single-game run with each flag off shows `MTG_REPEAT_EXTRAS_RESERVE` causes the PD and EDF changes; the other two flags change neither game. That reserve is generic: it applies to any {T}-less K-repeat activation (for example PD's Pyrohemia), so this is a play change reaching other decks, not a leak. The earlier held-out A/B (section 5.1) covered Snow, Selesnya v2 d6, EDF d5 and Kithkin d6, but not the d0 rows or PD. **Before accepting:** run ONE pooled held-out A/B over every deck the smoke and regression tiers moved, package off vs default. Every deck must be net <= 0. Data: `build/scratch/snowcp/mv/`. Both branches are also on origin as `wip/snow-contpay` and `wip/tier-pool`.
- **After landing:** the `MTG_RESCUE_TAP_SOURCE` re-test goes to the MANA agent (below).

## 2. Tier pooling harness (branch `tier-pool`, worktree `.claude/worktrees/tier-pool`; backed up on origin as `wip/tier-pool`)

- **Commits:**
  - `f9dc6404d`: WIP. Jobserver tokens, a persistent `--interactive` replay child per reference, xargs-parallel scenarios, VPC beside the batch, and `--both` combined smoke+regression.
  - `f0457802e`: VPC stderr via pread.
  - `36d69fa43`: scenarios beside the batch, longest first.
  - `556cd8f43`: skill stage table and backstop comment.
- **Verified on one binary:**
  - All 525 `.wins` files are byte-identical, and units match (493,166,963).
  - All 837 reference verdicts are identical, stateless vs interactive.
  - Scenarios are identical (530/530).
  - Time outside the batch went from about 1,050 s to about 80 s for both tiers; paired smoke was about 34% faster.
- **Remaining before landing:**
  - Squash the WIP commits.
  - Exercise the scenario-failure kill, `SIDE_STAGES_SERIAL=1`, the no-mkfifo (Windows) fallback, and a combined run that includes overnight.
  - Count the interactive child's fallbacks to stateless replay.
  - Then push in a later 4-hour window.
- **Full handoff:** `.claude/worktrees/tier-pool/logs/tier-pool/handoff.md`. Measurements are in `build/scratch/tier-pool/`.

## 3. Open USER decisions

1. A veto of `MTG_D0_SNOW_PUMP_CREDIT` (default ON).
2. The Cyclone unbudgeted backstop row is screened stale. Seed 30033 hits the 2M-unit cap, but its comment says all six games finish. Re-screen it?
3. Adopting `MTG_RESCUE_TAP_SOURCE`, after its re-test.
4. Game-count trims, which were deferred until the pooling landed.
5. A queue idea, the USER's own: upgrade relatively cheap decks from `fast` to `complete`, predicting the cost from the fast run's journal. Regenerations happen ONLY via the artifact queue.

## 4. Morph (report done, nothing running)

- **Overnight 2026-11-14:** the slower Morph games have no play defect. They are all different kept hands (fast keep table); the fetch rule loses no line.
- **Owed, suite:** Morph's overnight rows are still the ENTRY set (d5 4x300 is under the 425 floor). Resize them at the next periodic overnight, with the table hidden for the resize accept. Martyr's rows look the same but were not checked.
- **Stale cost entry:** `test/suite_cost.json` still says Morph is 1.72x Hinata (measured before the keep table). It is about 0.46x now, measured in the same run as Hinata. Refill it with `suite_gate.py --measure-all`.
- **Owed:** a Morph claude-play sweep, because play changed after INT-8..17. Morph's value leaf is on HOLD until the weekend (USER).

## 5. Other outstanding items

- **Snow, unfixed: send to the MANA agent (USER 2026-11-14).** All the leftover items are about payment. See the ledger `docs/design/snow-continuation-pay.md`.
  - g42 T3: the payment rescue lets Scrying Sheets fund its own {T} (`MTG_RESCUE_TAP_SOURCE`; its re-test with the manifest in `build/scratch/snowcp/rts/`; adoption is the USER's call).
  - g30 T4 and g120 T5: a carried breakpoint continuation casts Jorn or Abominable Treefolk with mana the plan had counted on (a reservation problem in the continuation path).
  - The Selesnya v2 and EDF producer-cast shortfalls are already the mana agent's.
- **Worktree cleanup, ONLY when the USER asks.** Candidates once their work is on origin:
  - misc-config, morph, morph-dblprobe, morph-dblsplit, morph-exalted, morph-histfix, morph-integ, morph-merge, morph-payer, morph-viewer
  - tier-pool-old (the detached baseline for the timing A/B)
  - snow-contpay and tier-pool, after they land
  - Run `python3 scripts/check_references_safe.py` first, then `scripts/safe_worktree_remove.sh <path>`. Show the dry-run list (`scripts/worktree_audit.py`) before removing.
- **Not ours, do not touch:**
  - The primary checkout (`/workspaces/MagicDeckTester2`, branch `morph-integ`) has uncommitted edits belonging to the artifact-queue session: CLAUDE.md, the `artifact-staleness-queue.md` / `cross-decision-tree-reuse.md` docs, `test/ref_bench.json`, a deleted `logs/.gitkeep`, and the untracked `scripts/artifact_queue.*` and `docs/design/artifact-queue-for-agents.md`. The artifact queue itself is PAUSED by the USER (11-11).
  - The mana items A and I(2) from the overnight are with the mana agent (`docs/design/handoff-mana-overnight-2026-11-14.md`).
