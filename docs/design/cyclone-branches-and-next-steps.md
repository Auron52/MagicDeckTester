# Unpredictable Cyclone: branch map, state and next steps (2026-10-30)

> Completion update (2026-10-30): the backup branches have been rebased and integrated on
> `cyclone-finish` against `6b655b71f`; original branch refs remain intact. The fixed 512 MB per-thread
> plan-cache policy and the companion per-thread FSL quota were removed. The existing shared,
> RAM-sized budgets remain, together with exact keys and hit-equals-miss pricing. The user prefers
> landing Cyclone before the separate cache-budgeting project. No broader cache redesign is being
> adopted as an implied prerequisite. Current implementation/verification state is tracked in
> `cyclone-completion-checkpoint.md`; historical branch measurements below are not final-source results.


Standalone hand-off for whoever resumes the Cyclone work. The per-deck ledger, with every measurement, is
`docs/design/analysis-Unpredictable Cyclone.md`. Each agent branch carries its own section of it, so read the copy on the
branch that did the work.

**Goal (USER 2026-10-29):** get Cyclone's per-game cost through the 3x suite gate **with bottoming on**, then generate its
mulligan profile and its value leaf. The USER: *"Bottoming is only as expensive as it is because we already have major
slowness in our handling of the deck."* Later the same day: *"I would prefer to fix known issues first, especially as I
am expecting Cyclone to be really difficult to generate the mulligan profile for."* So there is no freeze deadline. Fix the
known defects first.

**Where it stands:** all work stopped at 13:00-13:30 UTC on 2026-10-29, when the Hinata2 generation took the box. Every
branch below was pushed to origin on 2026-10-30 **as a backup**. None has landed on `phase-1-2-deck-analyzer`. CI is
green on all nine runs (Linux + Windows).

## 1. Branch map

Every branch forks from `7933887b7` (phase-1-2, 2026-10-28). That base is now **194 commits behind**
`origin/phase-1-2-deck-analyzer`. They share the `cyclone-analysis` trunk. Worktrees are under `.claude/worktrees/<name>`.

| Branch (origin) | Head | What it is | In `cyclone-int`? |
|---|---|---|---|
| `cyclone-analysis` | 5123a5425 | The trunk and ledger: the sound cantrip folds F2/F3/F5 + Q36 (ON), F4 (OFF), the cast-order docs (`cyclone-cast-order.md`, USER-approved draft + cycle order draft 2), and the discard-buckets design note. | The folds yes; the cast-order docs and discard note no (docs only) |
| `cyclone-refs` | 330cc85bf | Reference games: 10/10 replay, 0/10 shortfalls, the s8 gi7 manual-tap fix, QR1-QR2. | Yes (it is the base) |
| `cyclone-mem` | c81473985 | Memory and determinism: M1+M3 lazy clone blocks, M2 lists priced at their stored size (ON), plan caches made exact (`5a3c185b2`: exact keys, hit = miss pricing, per-thread pools), t1 = t3 = t6 = t12 invariance, the Q54 Kitty measurement. | Yes (renumbered Q56/Q57) |
| `cyclone-q53` | e60aa2bd1 | Correctness fix: the rollout files the cast COPY of a resolved spell, not template #0 (`MTG_SPELL_COPY_ID`, ON). d3 +0.040 (se .020), d5 0.000. | Yes |
| `cyclone-discard` | ce53794ca | The USER-designed bucketed cleanup discard + the USER's rulings; EL2 flipped ON (d3 -0.180, d5 -0.040, 0 lost lines). KN/DIG/E2S stay OFF. | Yes (EL2 = 83952b007) |
| `cyclone-discard-blind` | 71f481e02 | A blind scratch tree for the discard A/B. **Do not land.** | n/a |
| `cyclone-prunes` | 0b8c27fbb | Known-top tracker + blind test; plan-level put-back / Ponder / seed / cycler prunes. `CYCLER_RULE` ON (cd8bd730b); `PB_PLANS` OFF; `KEEP_AMEND`/`KEEP_MYRIAD` OFF; **head 0b8c27fbb = the two-Eldrazi JUNK fix, WIP, UNTESTED.** Resume note: `logs/cyclone/prunes/RESUME.md` in the worktree (gitignored, so copied into section 3 below). | Only the first commit (4ec5a0957, as 63b454310) |
| `cyclone-cost` | 76a733712 | Per-game slowness census + fixes: `MTG_CYC_JUNK_MEMO` (exact, ON), `MTG_LEAF_KILL_ONLY_EXTRA` (ON), `_NOM2` (OFF, pending the USER), `NESTED_EXH_AXIS` (OFF); same-run gate with PB on/off; QC1-QC5. | No |
| `cyclone-castorder` | b3f5abd88 | The drafted cast order, draw-turn opt-in and cycle order levers (all OFF); the tier-attribution handles `MTG_CYCLONE_CO_REV_*`; the Q61 searched payment split `MTG_CYCLE_SEED_PAY_SPLIT` (OFF); Q60-Q64. | No |
| `cyclone-viewer` | dbe51e677 | Q37 (unpayable one-step lines error with the board's reason, e.g. Seething Song at 1 mana) + QR1 (cycling is a plan item on every deck). Smoke PASS. | No |
| `cyclone-int` | 3892d2159 | **The integrator.** `cyclone-analysis` + mem + the first prunes commit + fix b991ec733 (F2's exact width honours the prunes' fan cap; the exact plan-cache key folds `KnownTopMask`) + Q53 + discard + the EL2 flip. Gates on 83952b007: unit 2130/2130, scenarios 339/339, smoke 149/149, play-changed 0. Resume note: `RESUME-cyclone-int.txt` at the branch root. | — |

The `cyclone-base` and `cyclone-fill` worktrees are detached at commits that are already on origin. They have nothing to
add.

## 2. The 3x gate

Gate figures are per-game core-ms at the deck's worst searched case, as a multiple of Hinata's. They come from the cost
agent's same-run gate (`cyclone-cost` ledger):

| Configuration | ms ratio | units ratio |
|---|---|---|
| Shipped settings | 15.9x | — |
| PB_PLANS **off** (current default) | ~13x | — |
| PB_PLANS **on** + CYCLER + JUNK_MEMO + KILL_ONLY | 2.65-2.98x | 3.9-7.2x |

- **The gate hinges on `MTG_CYCLONE_PB_PLANS`.** It is OFF because of a real lost line on d3 gi92 (isolated T1 test
  -0.44, se .22). With two Eldrazi and no go-off this turn, JUNK's Emrakul-first order is worse given what the player
  knows. gi179 was ruled clairvoyance (USER rule: isolated T1 test, 40 blind salts per order).
- The fix is coded (0b8c27fbb) and untested: when going off this turn, the better Eldrazi goes on top; otherwise the
  lesser one does, with Emrakul second.
- The 2.65-2.98x was timed on a loaded host (load 50-90). It is marginal, so re-time it on a quiet box (QC5).
- Bottoming trials are 72-75% of units. The keep policy's curve check ignores cyclers, and 16% of hands are mulliganed
  only because of it (QC1).

## 3. Next steps, in order

All of these wait for a free box. While a generation runs, nothing runs alongside it.

1. **Rebase first?** Every branch is 194 behind. Pick the work onto `cyclone-int` (step 3) on its current base, or
   rebase `cyclone-int` onto origin first. Either way, rebuild and re-run smoke after any rebase that replays engine
   changes. Run `check_gt_logs.py` if GT moved. Recommendation: rebase `cyclone-int` onto origin **before** the remaining
   picks, because the cost gate and every A/B should be measured on the tree that will land.
2. **Finish the two-Eldrazi JUNK fix** (`cyclone-prunes`, from its RESUME.md):
   - Build with `MTG_BUILD_JOBS=2 ./build.sh`.
   - Run the Cyclone unit tests, then the full unit run, then the scenarios.
   - Re-test gi92 (manifests `build/scratch/cyclone/prunes/iso92/{A,B}/m.json`; B runs with `MTG_DIAG_PB_T1_ORDER=2`).
     Expect the earlier reading with the signs swapped.
   - Re-run the PB A/B on the Phase C seeds: d3 109000 x400 b10 `ignore_play_profile`; d5 110000 x200 b20 with the
     profile, paired to `pc/glogs/base_*.wins`.
   - Read the USER's bar (2), then commit the PB flip with its pins.
   - Then the KEEP_AMEND cells that did not land, QP5 (root-cause SITU/SEED), and the aggregate blind ensemble
     (`pc3/manifest7.json`, UN-RUN).
3. **Integrate the rest into `cyclone-int`** (cherry-pick, diff-of-diffs against each original):
   - prunes: a6766244f, e672a8f73, cd8bd730b, 9764ea0b8, plus the PB fix and its flip;
   - cost: 479bb66b6, 76a733712;
   - castorder: 03baf1a73, 9066f252d, 6e887cbe5, b3f5abd88;
   - viewer: a89fd7ca2, dbe51e677;
   - the cast-order docs from `cyclone-analysis`.
   After each group: unit, scenarios, smoke.
4. **Re-run the cost gate on a quiet box** with the commands in `RESUME-cyclone-int.txt`. Rename the arms if PB/CYCLER
   are default ON by then. Both runs are bottoming-on runs: never set `MTG_BOTTOM_ROLLOUTS=0` for the gate. It was set
   once by mistake and read as 0.8x.
5. **If it passes at ≤ 3x:**
   - land on phase-1-2 (rebased, linear), add Cyclone to `test/regression_cases.sh`, and accept smoke/regression GT;
   - then `mullgen.sh run`, with a 2-hourly session-only timer plus a PID waiter;
   - then the value leaf.

   **If it still fails:** bring the numbers and the gap to the USER. Do not ask for an override; the USER wants it
   passed with bottoming.
6. **Other owed items:**
   - Q61 completeness fix: the searched payment split is built OFF (partial reading 0.000); finish its proof, tests and
     A/B. It is a completeness defect: the surplus-Cyclone cycle's {2} taps Mountains, so 3 Mountain + 4 Island is
     unfindable at b0.
   - Q64: the `MTG_CYCLONE_CO_REV_CYCLERS` arm (gi94/gi110: the creature cyclers' hard-cast rank). Run it before asking
     the USER to adopt the cast order.
   - The USER asked why the cast order produces almost no faster games than the generic order. Answer that with the
     Q64 result.
   - mem: the tiny12 + 12-thread shape runs (`deferred12.sh`; killed when Hinata2 started).
   - viewer: `viewer_validate_check`.
7. **Line up with the cache / budget-reliability work** (another agent, brief on `origin/budget-reliability-docs`,
   `docs/design/cache-unit-neutrality-audit.md`). `cyclone-mem`'s `5a3c185b2` holds the plan-cache determinism the
   audit builds on. The plan cache's 512 MB/thread was my setting and has not been ruled on; it should follow the USER's
   floor + overflow design once that lands.

## 4. Open USER decisions (none blocks the steps above)

| Id | Question | Recommendation |
|---|---|---|
| QP3 | Adopt `KEEP_AMEND` (d5 -0.045, d3 +1 game)? | Measure it with PB on first |
| QC1 | Should the keep policy's curve check count cyclers? (16% of hands mulled only for it) | Yes |
| QC2 | `NOM2` default ON fleet-wide? 12/13 decks net ≤ 0; Fluctuator +1/3000, which is budget luck and recovers at d8 b0 | ON |
| QC3-QC5 | Leaf fidelity setting; Ponder width 7 (~25% of wall); quiet-box re-time | See the cost ledger |
| Q63/Q64 | Adopt the drafted cast order? Blind net: CO d3 +0.010 (se .040); CO+CYC1 -0.030; the raw loss was clairvoyance | Run the Q64 arm, then decide |
| Q54/Q57 | Kitty +12-16% wall from the determinism fix (the decision-scoped patch was not exact) | Accept for now |
| — | Mulligan buckets: K=21 with the two Ulamogs merged (USER's own call, done) | — |
| — | Further merges: Watery Grave ~ Island, Blood Crypt ~ Mountain (K=19)? | USER's call |
| — | The bucketed discard's 4 defaulted readings (FYI) | — |
