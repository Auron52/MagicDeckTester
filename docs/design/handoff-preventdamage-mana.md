# Handoff: Prevent Damage mana-clean gate (wip/mc-pd)

Checkpoint 2026-11-04 ~10:30 UTC (successor 2 hands off at the 250K context cap). Branch `wip/mc-pd` in
`.claude/worktrees/mc-pd`, rebased onto origin `23d73e505`, NOT pushed (the coordinator lands). Findings and
mechanisms: `docs/design/mana-clean-prevent-damage.md` (read it first; the DOMINATED-LIFE / APPLY-DROP sections
there are the predecessor's diagnosis).

## Commits
- `827a594da` oracle: the end-of-main-1 sweep's taps are not unscripted payments (checker only).
- `b519f508e` docs: findings + handoff (predecessor).
- (this commit) WIP payer: three default-ON levers + unit tests + diagnostic counters. **A PLAY CHANGE, not
  adopted: no held-out A/B, no slower-game recovery check yet.**

## Task 1 -- DONE (checker commit verified)
- `mtg-test -tc="*Prevent Damage*,*mana oracle*"` on b519f508e: 48/48 pass.
- `THREADS=8 MTG_PGO=0 bash test/regression.sh --smoke` on the clean b519f508e snapshot: **169 passed, 0 failed,
  0 new; 401 scenarios pass; "no searched-depth slowdowns or play changes"** (log
  `build/scratch/mc-pd/smoke1.log`).

## The three levers (all default ON, heurarm slots, so one batch runs every arm)
1. **`MTG_PD_USEFUL_LINE_OK`** (`dmgev::UsefulLineOkEnabled`, `src/core/DamageEvents.h:813`; the repair lambda
   in `PainAwarePay`'s useful branch, `:1133`). The pain-USEFUL payment now asks the harmful mode's exact
   `line_ok` after paying; on "strands" it re-pays holding each source it tapped, one at a time, and keeps the
   first payment that leaves the line payable (else the original). Covers both useful sub-branches (pain-safe
   incl. the deferral, and the capped one). Unit test: "Prevent Damage useful-mode payment: the rest of the line
   stays payable (seed 30006387 T4)" (control arm reproduces the drop).
2. **`MTG_PD_DEFER_LINE`** (`dmgev::DeferLineEnabled`, `DamageEvents.h:780`; `PainDeferQuery`,
   `src/core/SpellEffects.h:44673`, victims `:44691`, line casts `:44730`; inputs from `ComputePlanTraits`,
   `src/ai/TurnSolver.cpp:56600`; new `PlanTraits::pd_*` fields in `src/ai/PlanContext.h`, hashed only when set
   in `PlanContext.cpp`). The deferral's after-chain is the chain at the sweep for the WHOLE line: the line's
   still-owed permanent casts join it, and a sac-outlet's named victim (`sac_victim_id`) leaves it, so a
   `{sacoutlet}` plan is no longer refused outright. Any other own-board hazard (Pyrohemia ping, Rolling
   Earthquake, ETB destroy, sac additional cost) still keeps the deferral off (`pd_hazard_other`; a hazard with no
   recorded victim also blocks). Unit test: "a sac-outlet victim leaves the sweep's chain (seed 30000020 T6)".
3. **`MTG_PD_DEFER_SAME`** (`dmgev::DeferSameEnabled`, `DamageEvents.h:797`). The deferral also runs when
   nothing changes the chain (Later = the damaging tap now, so the DP is the value-max assignment), and
   `CapabilityCovers` falls back to a unit-split matching (`CapabilityCoversUnits`, `:967`: Ancient Tomb's {C}{C}
   covered by two held single sources). Still adopted only on a strictly better REALISED value + cover. **Built
   (build6) but NOT yet measured, and no unit test yet.**

Diagnostic counters added to `MTG_PD_PAY_STATS=1` (`q_off_gate`, `q_off_hazard`, `q_same_chain`, `q_not_exact`,
`defer_no_pred`, `defer_no_cover`, `useful_line_strand`, `useful_line_repaired`). Inert unless that env is set.

## Measurements so far (games 0-395, seed 30000000+g, d6 b20 value_play, MTG_MANA_ORACLE=2, one pooled batch)
Run `build/scratch/mc-pd/m4.{out,err}` (binary `mtg.b`, before levers 3 and the hazard-gate fix):

| class | base (all off) | lineok | deferline | both |
|---|---|---|---|---|
| APPLY-DROP | 23 | **8** | 23 | **8** |
| APPLY-DROP-ORDER | 12 | **0** | 12 | **0** |
| DOMINATED-LIFE | 1132 | 1135 | 1075 | 1078 |
| DOMINATED-LIFE decisions | 141 | 141 | 137 | 137 |
| UNMODELED | 213 | 213 | 207 | 207 |
| CAPPED | 174 | 174 | 174 | 174 |
| DOMINATED-LAND | 747 | 747 | 743 | 743 |
| ORACLE-GAP | 6 | 6 | 6 | 6 |
| avg win turn (396 g) | 5.4318 | 5.4318 | 5.4318 | 5.4318 |
| units | 11,995,417 | 11,979,295 | 11,995,435 | 11,979,313 |
| job ms (sum) | 1,829,026 | 1,826,609 | 1,903,019 | 1,867,835 |

- **Measured improvement so far:** `USEFUL_LINE_OK` clearly works (APPLY-DROP 35 -> 8, every game digest
  unchanged: the repairs are on menu plans the search did not pick here). `DEFER_LINE` is only a small gain
  (DOMINATED-LIFE -5%; 7 of 12 chunks' digests move, avg win turn equal). Units are flat (-0.13% / +0.0002%).
  Wall ms is noise on a box at load ~55. **Instructions not yet measured** (`perf stat` or the harness's
  instruction counter on a fixed sample, both arms, MTG_PGO=0).
- The 8 APPLY-DROP left: ONE decision, seed 30000019 T7 at 2 life (Pyrohemia + Quake X=1); low-life edge, not
  looked at.
- Pay-stats on 132 games, `both` arm (counts of useful pain-safe deferral queries): q_off_hazard 217,487;
  q_same_chain 378,449; defer_live 155,202 (same 96,255, no_pred 14,753, no_cover 4,864, kept 39,330,
  rejected 0); q_not_exact 17,781; useful_line_strand 2,609, repaired 330. So the deferral was OFF for ~77% of
  queries: same chain (49%, what lever 3 targets) and non-sac hazards (28%).
- Remaining DOMINATED-LIFE (`both` arm, 1,078 flags) by plan: tutor-only 303, hazard+tutor 156, permanent cast
  118, sac+tutor 110, hazard 108, hazard+perm 76, sac+perm 62, tutor+perm 55, sac+hazard 50, sac 34, other 6.
  Traced examples: seed 30000004 T6 (Vito pending, deferral live, the DP's hold of Citadel rejected by the
  whole-source cover against Ancient Tomb's {C}{C}: what lever 3's unit cover targets); seed 30000020 T5 (5 life:
  not PaymentPainSafe, the capped branch has no deferral at all).

## Running job
- Chain `build/scratch/mc-pd/run_m5.sh` (PID 3314451): waits for build6 (`build/scratch/mc-pd/build6.log`, `rc=`
  line), copies the binary to `build/scratch/mc-pd/mtg.d`, then runs the 3-arm oracle batch `m5.json` (arms
  `base` = all off, `both` = LINE_OK + DEFER_LINE, `all` = all three; games 0-395, 8 threads). Outputs
  `build/scratch/mc-pd/m5.{out,err}`, batch PID in `m5.pid`, DONE marker `m5.done` (`rc=0`). Wait with
  `while kill -0 $(cat m5.pid) 2>/dev/null; do sleep 30; done` (or on `m5.done`), never pgrep -f.
- Read it: `python3 build/scratch/mc-pd/cntarms.py build/scratch/mc-pd/m5.err` (class counts per arm +
  DOMINATED-LIFE decisions); per-arm avg/units/digests: the python snippet pattern in this session parsed
  `m5.out` lines `arm__gN: played= avg= digest= ms= units=`. `base` and `both` must reproduce the m4 numbers above
  (a sanity check that build6 changed nothing else).

## Remaining steps, in order
1. Read m5. If `all` does not move DOMINATED-LIFE beyond `both`, apply the STOP RULE to lever 3 (park it and
   delete it, or keep it default OFF only with the USER's say). Add a unit test for lever 3 if kept (seed 30000004 T6
   shape: Vito pending, Tomb vs Citadel).
2. Unit tests (`mtg-test -tc="*Prevent Damage*,*mana oracle*"`, 50/50 on build5) and smoke on the final tree.
   Expected movers: prevent_damage only. Other decks arm `dmg_events_armed` via Faithmender/Bilbo/lifegain params,
   but the levers act only in the pain-USEFUL branch and `pd_*` traits are hashed only when set. Check that no
   other deck's row moves.
3. Slower-game recovery: every PD game the levers make slower must recover at `--depth 8 --budget-ms 0`
   (clairvoyance exempt only if clear; test with MTG_NC_SEARCH=1).
4. Held-out A/B, PD only (plus any other deck smoke shows moving): `scripts/deck_avg_arms.py --deck
   'decks/Prevent Damage/Prevent Damage.cod' --arm off MTG_PD_USEFUL_LINE_OK=0 MTG_PD_DEFER_LINE=0
   MTG_PD_DEFER_SAME=0 --arm on --seeds <fresh, not smoke/regression/overnight> --threads 8 ...` at the deck's
   shipped settings (the batch reads value_play: d6 b20). Bar: net <= 0 avg win turn.
5. Instructions cost for each lever (both arms, MTG_PGO=0, same sample); justify anything above ~1%.
6. Remove the diagnostic counters if unwanted (they are inert), squash WIP into per-lever commits, update
   `mana-clean-prevent-damage.md`.
7. Mana gate on a fresh block: copy `/workspaces/MagicDeckTester/test/mana_clean.json` to
   `build/scratch/mc-pd/mana_clean_state.json`, then from the worktree
   `python3 scripts/mana_clean.py run prevent_damage --games 5000 --threads 8 --state
   build/scratch/mc-pd/mana_clean_state.json --name mc-pd-pass2`.
8. PARKED (STOP RULE / scope): CAPPED; GSZ fetches (the target is a resolution-time tutor-axis pick,
   `Plan::tutor_choice` into `RankedTutorCandidates`, not on the Action, so the deferral cannot see it: biggest
   remaining bucket); non-sac damage hazards (Rolling Earthquake X / Pyrohemia pings kill chain creatures by
   toughness; computable from the plan but order-sensitive); the capped (not PaymentPainSafe) useful branch has no
   deferral; hybrid costs (Beseech) bail out of the DP.

## USER rulings in force (verbatim from the brief)
- "we don't want to add a whole lot of extra branching to the mana engine" (the levers add payer RETRIES only:
  per tapped source on a stranding payment, and one DP + one attempt per live deferral; no search branching).
- Adoption bar: every slower game recovers at `--depth 8 --budget-ms 0`; each moved deck net <= 0 on a held-out
  A/B. Never accept GT (the coordinator does). Never touch references/. THREADS <= 8, MTG_RSS_CAP_GB=43, builds only
  via `build/scratch/buildq.sh`, wait ~200 s after src edits (9p lag).
