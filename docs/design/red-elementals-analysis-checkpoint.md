# Red Elementals analysis -- live checkpoint (handoff document)

Shared state for whichever agent (Claude or Codex) continues the Red Elementals analysis. Read this first,
then `docs/design/analysis-Red Elementals.md` (the per-deck ledger), then verify files/processes before acting.

Last updated: 2026-11-17 (Claude; value-leaf stage complete).

## Value-leaf stage COMPLETE (2026-11-17, current)

- The `valueleaf.sh` run on frozen `24949783a` finished. It shipped the leafless final-depth single
  pass (`nlfit`), first at d6/b20 and now **d6/b30** (USER); the learned model stays staged and unshipped. Red's smoke and regular GT
  were accepted after sidecar-off attribution (0 slower, 1 faster, 21 same-score line changes).
  Overnight rows are left to the periodic run. Details: the ledger section "Value leaf and play setting
  (2026-11-17)". The handoff `red-elementals-value-leaf-handoff.md` is closed.
- USER 2026-11-17: adopt **d6/b30** (1.000x, -0.00062 t -3.42), the near-free trade the driver did not
  take. The sidecar now ships `budget_ms 30` and Red's d5 suite play rows run at b30 (fleet convention);
  the scoped smoke and regular gate was byte-identical at b30. The staged learned model is still available
  if wanted (`logs/eval/Red Elementals.value.STAGED.json`).

## Final mulligan adoption progress (2026-11-15)

- The definitive 13-bucket R30 keep and bottom table is generated, committed, and live. The
  structural gate passed (39,945 seven-card entries, 47,068 subcells, minimum R2). The keep
  A/B improved by 0.101750 turns and the confounded bottoming A/B by 0.029250 turns; both
  passed their predeclared significance gates after four favorable seed pairs.
- After rebasing onto upstream `69c69f2ae`, all ten references pass (human 4.400, search 4.300,
  zero shortfalls), and Red's six regular-tier scores and digests match the pre-rebase table-on
  run byte for byte. Smoke and regular GT have been accepted after attribution: regular has
  549 mulligan-count changes, 130 bottom changes, two card-order-only digests, and zero
  unexplained games. The integrated `verify_deck.py --no-network` gate passes. The strict
  viewer protocol passes 864 references, the scenario suite passes all 561, and the reference
  safety scan passes. `check_gt_logs.py` reports 1,252 consistent GT logs, zero stale/missing.
- The overnight FULL-set adoption is complete on a freshly trained PGO+LTO engine. Red's four
  d5 play rows grew from 300 to 425 games per seed, meeting Hinata's play-row floor. The
  resized table-off baseline was accepted first; its original 300-game prefix had no play
  changes. The table-on searched mean improved from 4.3471 to 4.3077 turns across 1,700
  paired games (239 faster, 173 slower). D0 improved on all four seeds. The overnight
  adoption replay reproduced both sides: 4,001 mulligan-count changes, 925 bottom changes,
  29 bottom-order-only digests, **zero unexplained**. The table-on overnight GT is accepted;
  `check_gt_logs.py` again reports 1,252 consistent, zero stale/missing.
- The accepted GT and notes were committed, then rebased onto shared upstream `6cb9a7c85`
  (Gods decision and dredge changes). A pooled filtered smoke, regular and overnight run on
  the rebased Release engine passed every Red row and the 2HG canary with **identical scores
  and digests**; strict viewer replay again passed 864 references, with zero drift or enum
  gaps. The branch was pushed at `a27d7bf8a`; the primary checkout is back on
  `phase-1-2-deck-analyzer` at that commit, and the viewer serves Red Elementals on port
  8080 from the primary checkout with the persistent reference backup active. The local
  `viewer/redelem` branch was removed after the reference safety scan. The original primary
  `.vscode/settings.json` edit remains; the old local `test/ref_bench.json` cache is saved in
  `build/scratch/redelem/primary-local-backup/` and in a stash because it conflicts with the
  newer shared cache. The Stage 5i
  discard proposal remains unadopted and user-owned; the human viewer still automatically
  declares attackers, so deliberate Soulstoke holds need the forced-hold verifier path.

## Mulligan generation update (2026-11-15, current)

- Red Elementals play verification is complete and committed through `dc09f4b6a`; the screened
  mulligan contract is committed at `cbefb5068`. The screen chose 13 buckets and d1/b3 generation
  labels (rank fidelity 1.0000, rho 0.9808; 0.18x the d5/b20 play cost). Mountain and Fiery Islet
  are the sole merged pair. `src/` and the tracked deck files were clean at launch.
- PGO+LTO was trained on the pooled 275-job / 7,447-game suite and built against source tree
  `5c46ffd6d3350b10b6b5c6fad17e12018b865deb`. The `recommend` scout finished: 39,945
  distinct seven-card hands, 126,958 play/draw cells across sizes 1-7, 727 s floor, projected
  fast ~4.0 h and complete ~8.1 h. Its R=1 probe is reusable by the generation run.
- The R30 generation completed 13:43 UTC (~2 h 17 m), faster than projected. Its structural
  gate passed: K=13, 39,945 size-7 entries, bottoming enabled, 47,068 sub-cells, minimum R2.
  The first-version A/B driver exited successfully 14:42 UTC: keep vs static -0.101750 turns
  (4/4 seed pairs favorable, early-stop t=-15.3); confounded bottoming vs lookahead -0.029250
  turns (4/4 favorable, early-stop t=-8.09). Both passed. Driver log:
  `logs/redelem/mullgen-fast-driver.log`; generation log:
  `logs/Red_Elementals_mullgen/gen.log`; A/B logs:
  `logs/Red_Elementals_mullgen/ab_*.log`.
- The driver's regular-tier regression visibility run moved all six Red rows, with searched games
  faster=44/slower=28 and d0 faster=194/slower=97. Strict viewer protocol passed 864 references.
  **Currently running:** `python3 test/keep_adoption_attribution.py regression redelem`
  (session 79058) to classify all changed games before any GT acceptance. After attribution,
  commit the generated runtime profile and gzipped raw, rebase onto upstream, rebuild and rerun
  the relevant suite/attribution on the current engine before accepting GT and pushing.
- Upstream shared branch had moved to `279d4f76f` at launch, adding Shadowborn, Prevent Damage,
  and mana-payment changes. After generation and its driver validation, rebase the Red branch,
  rebuild, and check the profile on the current engine before push. A completed, internally
  consistent profile does not automatically expire on a later source commit; use the skill's
  play/regression evidence to decide if the new source materially affects this deck.

## Integrated verification update (2026-11-15, latest)

- `redelem-analysis` has been rebased onto upstream `f92bb3b7c` (Boars, Merfolk and Shadowborn);
  current play HEAD is `9591de110`. Merge conflicts kept both the upstream mechanics and Red Elementals
  pins, provider routing and viewer decisions. The release engine builds.
- The saved Soulstoke put then Legion Leadership `s1 gi0` d8 b0 line still wins T4. All ten reference
  games pass (human 4.400, search 4.300 turns; zero shortfalls). Red smoke (3 rows), regular (6), and
  overnight (11) match accepted scores and digests exactly. The strict reference protocol passes.
- Two Landfall Elementals regular-tier summary digests were stale after the rebase. A filtered rerun
  showed all six per-game logs unchanged; filtered acceptance repaired the two summaries. The global
  GT-log consistency check reports 1,251 consistent, zero stale.
- `verify_deck.py --no-network` passes the integrated coverage, fields, viewer, mismatch, invariant,
  suite and recorded Codex-play gates. Its first pass revealed the new upstream `creature_type`
  decision was missing from `play_invariants.py`; the driver now follows its emitted default. The
  focused rerun passed 4 games / 76 decisions, and the full rerun passed 8 / 148. The network card
  cost audit passed on the preceding base. Reference safety passed.
- The focused Red test binary, linked from the current Release core and Red test object, passed all
  76 cases and 1,623 assertions (including same-plan Soulstoke put plus Leadership). The rest of
  the repository-wide unit-object build was stopped after that focused result. Next, commit the
  small harness/GT/doc changes, build `mtg-analyze`,
  run the sole-box `--gen-mulligan recommend` scout, then `mullgen.sh run` at the selected recipe.
  After generation/validation, push the shared branch and update the primary checkout/viewer.
- The Stage 5i discard proposal remains user-owned and unadopted. The human viewer's automatic
  attacker declaration still prevents a deliberate Soulstoke hold for the opponent's end-step put;
  this is disclosed in the ledger. The search and verifier's forced-hold path cover that window.

## Current continuation (2026-11-15, after the rebase)

- Worktree `.claude/worktrees/redelem`, branch `redelem-analysis`, has been rebased onto
  `origin/phase-1-2-deck-analyzer` (`c171d3324` at rebase). The search-completeness fix is
  in HEAD `038d905a6`: same-plan Soulstoke put targets can receive Legion Leadership, and the
  put executes before the trick in rollout and real execution. The saved `s1 gi0` d8 b0 game
  now wins T4 with Ball Lightning put before Leadership. All ten reference games pass:
  human avg 4.400, search avg 4.300, zero shortfalls.
- Red Elementals' smoke, regression and overnight tiers have been rerun and accepted after
  per-game attribution. Searched outcomes only improved: smoke 7 faster; regular 13 faster;
  overnight 51 faster, with zero slower in each. The rebased upstream Elementals regression
  had two stale summary digests; an affected-deck rerun proved all six logs unchanged and its
  filtered accept repaired them. `test/check_gt_logs.py` reports 1,226 consistent, zero stale.
- Stage 5h: Red and Landfall viewer audits pass; the forced Soulstoke `eot_put` prompt and
  Firecat `face_up` both verify. The human can put an Elemental in one main-phase decision and
  target it with Leadership in the next. Stage 5d: parent Codex played 15 distinct seeds
  730000–730014 through the external-play protocol; 12 equal win turns, three one turn later
  than the d5 b20 search, zero unresolved flags. The subagents lacked shell tools and played
  zero games. Choices are recorded in `logs/redelem/codex-play-730000-730014.json`.
- `verify_deck.py` first full run passed card costs and every other gate except the harness
  lacking upstream `choose_creature_type`. `scripts/play_invariants.py` now recognizes it and
  follows the emitted default. The rerun with `--no-network` passes all runtime gates:
  8 games / 148 invariant decisions, mismatch, viewer and suite. The full network card-cost
  audit had passed immediately before this one-line harness fix. Reference safety passes.
- **Currently running:** `MTG_BUILD_JOBS=5 ./build.sh release -- --target mtg-test` (session
  42207) to compile and run the focused Red unit. Do not launch a batch beside the build.
  Next: build `mtg-analyze`, run the bounded `--gen-mulligan recommend` scout, then start
  `bash scripts/mullgen.sh run 'decks/Red Elementals' fast` on the frozen play source. Before
  generation, update the ledger/checkpoint with any final test outcome and commit the source,
  baselines and docs. The Stage 5i discard proposal remains user-owned and unadopted.

## Codex continuation update

- The 48-block 5c2 job ended `rc=0`: one changed game (one worse, zero better) in 48,000 pairs;
  verdict NO SIGN. Keep the default tie-break ON under the predeclared rule. The ledger and
  `horizon-honest-leaf.md` now record it.
- The ten user references are safe (`check_references_safe.py` passed). Their `s1 gi0` T4 line
  exposes a concrete search gap: the user puts Ball Lightning with Soulstoke and *then* casts
  Legion Leadership on it. Search enumerates Leadership targets before the plan, and applies
  all casts before trailing put activations. It therefore cannot express that order at any depth.
  Fix both action enumeration and scored/executed ordering, then rerun the reference benchmark,
  per-game GT attribution and Stage 5d. Do not generate the definitive mulligan profile before
  this play fix.
- Codex-play fan-out was attempted with three Codex subagents. This session exposed no shell or
  filesystem tools to any of them; zero games ran. Record Stage 5d as UN-RUN, not clean.
- The viewer auditor's sweep escalation was corrected so `verify_card`'s `NOT_FORCED` result
  remains unverified instead of being promoted back to HARD MISS. Dynamic sweeps passed with
  no hard misses for Red Elementals (8 seeds from 620000) and Landfall Elementals (6 seeds from
  620100). In both, Soulstoke's `eot_put` is UNVERIFIED: the automated combat attacks with it,
  closing the end-step window. A hand-built protocol repro or a hold choice remains owed.
- A Stage 5i discard bucket proposal is in the ledger, awaiting user review; it is not adopted.
- Mulligan feasibility discovery completed (400 probes, threshold 0.01, d5): 13 classes; Mountain
  and Fiery Islet merge, all other cards stay separate. The size-7 composition space is 50,388
  hands. This is not a runtime estimate; the `recommend` scout belongs after the play fix.

## >>> CODEX HANDOFF -- START HERE (state at 2026-11-15 04:30 UTC, orchestrator; H2 block below still current) <<<

**What may still be RUNNING when you arrive.** Check each with `kill -0 <pid>` and `ps` before you touch the tree.

1. **5c2 tie-break re-run, 48 blocks (agent H2).** `leaf_tiebreak_check.py --blocks 48 --seed-base 3000000`
   (96,000 games at d5 b20) on the fold binary snapshot `build/scratch/redelem/H2/bin_fold/mtg` (= `38454a1a0`
   src), `MTG_PGO=0`. Shell PID 1416767, python PID 1416771, batch PID 1416779. Output
   `logs/leaf_tiebreak/redelem_H2_b48.out`; verdict `build/scratch/redelem/H2/ltb48.log`; marker
   `build/scratch/redelem/H2/ltb48.DONE` (`rc=`). Started 21:40 UTC, 12/12 busy, expected ~05:30-06:00 UTC
   2026-11-15. **Do not kill it.** Verdict handling: ledger "Stage 5 runtime verification", 5c2 paragraph.
   Run 1 (12 blocks, agent H) was NO SIGN: 0 changed of 12,000 paired.
2. H2's own probe jobs are all finished. H2 has stopped once `logs/redelem/agentH2-handoff.md` exists.
3. **The play viewer runs Red Elementals from the PRIMARY checkout on a local branch** (USER 2026-11-14: *"When
   elementals is ready for such please launch the play viewer."*). At 00:50 UTC 2026-11-15 the orchestrator
   switched `/workspaces/MagicDeckTester` from `phase-1-2-deck-analyzer` (`e944eeea4`, its fork point) to the
   new local branch **`viewer/redelem` = `1864683a4`**, rebuilt it, and restarted the viewer there. Check
   `ps` for `node tools/play/server.js`. The user's untracked `decks/Red Elementals/` folder was moved to
   `build/scratch/redelem/primary-deck-backup/orig-dir`; its `.cod` is the committed copy except for CRLF line
   endings. **After the shared push (item 6 below):** run `check_references_safe.py` and commit any new
   references, switch the primary back with `git switch phase-1-2-deck-analyzer && git pull --rebase`,
   rebuild, restart the viewer with `MDT_REFERENCE_BACKUP_DIR=/home/vscode/.claude/mdt-reference-backups`,
   and delete `viewer/redelem`.

**NEW 2026-11-15: the USER played 10 reference games in the viewer.** They are committed as
`references/Red_Elementals/claude_s{1..10}_gi*.json`: `26933ae30` on `viewer/redelem` (pushed to origin) and
cherry-picked onto `redelem-analysis` as `ae3eca75f`. The viewer autosave branch holds them too.
* **`scripts/ref_bench.py --deck red_elementals`** (fold binary, shipped d5 b20; log
  `logs/redelem/refbench-2026-11-15.log`): human avg **4.400**, search avg **4.400**. 8 equal, 1 search faster
  (s8 gi7: 6 -> 5), and **1 SHORTFALL (s1 gi0: human T4, search T5)**.
* **s1 gi0 does NOT recover with budget or depth:** T5 at d5 b20, d5 b2000 and **d8 b0**. It stays T5 at d8 b0
  with each of `MTG_REDELEM_TRICK_FOLD=0`, `MTG_REDELEM_OPAQUE_ORDER=0`, `MTG_CHAMPION_DOOMED_PRUNE=0`,
  `MTG_REDELEM_NOVA_MAIN2=0` and `MTG_UNPRUNED=1`. Under that bar it is a **search-completeness defect**, not a
  budget or prune effect, and it is the top Codex item (do it before the 5d sweep, since a fix changes play).
  * **Human line:** T1 Mountain + Spark (17). T2 Mountain + Bladewhirl. T3 Mountain + **Incandescent Soulstoke**,
    Bladewhirl attacks for 3 (14). T4 Mountain; **Soulstoke {1}{R},T: put Ball Lightning** (7/1 haste with the
    lord); **Legion Leadership on Ball Lightning** (14 first strike); attack 14 + 3 = 17 >= 14.
  * **Search line (d8 b0):** T1 Fiery Islet + Spark (17). T2 Bladewhirl. T3 Ball Lightning, attack 8 (9). T4
    Ball Lightning, attack 8 (1). T5 wins.
  * **Lead to check first:** the viewer's T4 plan label reads `cast: Legion Leadership -> Flamekin Bladewhirl,
    put Ball Lightning onto battlefield (Soulstoke ...)`. The plan names the Leadership target BEFORE the put.
    The human then got a separate target prompt (`heuristic_default` = Ball Lightning) after the put. So the
    search's plan may bind or execute the Leadership target (Bladewhirl: 6 + 7 = 13 < 14) before the
    Soulstoke put lands, and never branch on "put first, then Leadership on the put creature". Verify the
    enumeration of Soulstoke-put + trick ORDER inside one main phase, and whether d8 even values the T3
    Soulstoke cast.
  * **Repro:** `MTG_PGO=0 build/Release/mtg --batch logs/redelem/s1d8.json --threads 1 --game-trace-dir <dir>`
    (`logs/redelem/s1.json` = d5 b20 / d5 b2000 / d8 b0). Human line: `references/Red_Elementals/claude_s1_gi0.json`.
  * Fix it as a searched capability (no greedy pick). Then re-run `ref_bench.py --deck red_elementals`, A/B the
    play change on redelem, re-accept redelem GT, and only then run the 5d sweep.
* `test/ref_bench.json` (the viewer's GREEN cache) was NOT updated. The primary checkout has an uncommitted
  edit to it from another session, so leave that file alone.

**Stage 5 results** (ledger section "Stage 5 runtime verification (agents H/H2)";
`docs/design/red-elementals-search-cost.md`):
* **5a mismatch: PASS** (0 `[nonconv]`, 0 `[fd-diverge]` in 1,500 games).
* **5b depth sweep: PASS**, monotone; slow outliers are mana screw or flood.
* **5c budget starvation: DONE.** 2 of 9 slow games starve at b20 (s7100 gi83; s1001 gi46 with the fold off).
  Both recover with budget; 7 of 9 are identical at b2000.
* **5f (b): EXPLAINED.** The budget-0 tail is mulligan bottoming (no keep table).
* **5f (a): fold `MTG_REDELEM_TRICK_FOLD` ADOPTED** (`38454a1a0`). Over 1,240 paired games it is 0 faster / 0
  slower, units -4.0%, and the OFF arm reproduces `ec8b883b0636ad67`. The proof was re-checked card by card.
  redelem smoke + regression GT were re-accepted with per-game attribution (`d7d0d55b6`).
* **5c2: RUNNING** (above).
* `verify_deck.py` gate lines: see the ledger (claude_sweep PENDING until 5d).

**Remaining work for Codex, in order:**
1. The 5c2 verdict (job 1 above), handled as the ledger's 5c2 paragraph says. Everything else of H/H2 is done
   and committed.
2. **5d sweep: about 20 games.** Run it AFTER the fold decision, because play changes. A Codex session fans out
   **Codex** agents, and the result is recorded as `## Codex-play sweep` (USER 2026-11-11; see
   `.claude/skills/claude-play.md` "Running a sweep" and "Recording a sweep"). Use a base seed disjoint from
   the suite's. Verify each flag against `cards.json`, and record `flags: N unresolved` in the ledger.
3. **5h:** `scripts/audit_viewer_decisions.py` on RE and LFE, with the sweep included this time. Cover the
   `eot_put` decision.
3b. **`verify_deck` viewer gate FAILS**, a Soulstoke `eot_put` HARD MISS. In the claude-play protocol the engine
   decides combat, so Soulstoke always attacks and the end-step window never opens. H2 added a forcing driver
   and a NOT_FORCED verdict to `audit_viewer_decisions.py verify_card`. Still owed: route the sweep path's
   verdict through it, or get a USER-approved deferral. A related USER question is open: a human cannot hold an
   attacker back in the viewer, so should a hold choice be exposed?
4. **5i: discard bucket policy.** The analyzer said STATUS_QUO_OK: only 2 discard decisions in 400 d3 games. Write
   it up and PRESENT it to the USER. Do not adopt it.
5. **Stage 6** report, with the 6a disclosure table (`.claude/skills/analyze-deck.md` Stage 6).
6. **Push to the shared branch** `phase-1-2-deck-analyzer`:
   * rebase onto origin (never merge) and rebuild;
   * run smoke (a full smoke took about 106 min on this box), `python3 test/check_gt_logs.py` and
     `python3 scripts/check_references_safe.py`;
   * push once, then watch CI in the background with `--interval 60`.
   * **Upstream moved a lot:** as of 00:40 UTC, `origin/phase-1-2-deck-analyzer` is **107 commits** past the fork
     point `e944eeea4`. They touch `src/ai/DecisionProviders.*`, `HeuristicArm.h`, `TurnSolver`, `cards.json`
     and GT, so expect conflicts. Upstream play changes move redelem play too: re-accept its GT under the
     rebased binary, never by hand-merging numbers (`check_gt_logs.py`).

   The backup branch `origin/wip/redelem-analysis` is at `cfa0d2c13`, and its CI is green on ubuntu, windows
   and the parity check. **The shared branch has not been pushed in this effort.**
7. **Close-out:** the Claude session's 2-hourly status cron (session-only) dies with that session. Codex has
   to set its own status timer for any overnight run.

**Not in scope until the USER starts it:** the mulligan profile (keep table) and then the value leaf. The
pipeline order is profile, then mulligan, then value leaf, and they run strictly serially. When the keep table
is adopted, resize the overnight rows to the full set first.

**Box rules:** nproc 12, 10 GB RAM.
* At most 2 mtg batches at once.
* Never compile beside 2 batches.
* Use `MTG_BUILD_JOBS=4` beside 1 batch and `MTG_BUILD_JOBS=5` alone; 12 jobs swaps the box.
* Never edit headers while a build runs.
* Waiters key on the PID or a DONE marker, never `pgrep -f`.

## >>> end of handoff block <<<


## Where the work lives

* Worktree: `/workspaces/MagicDeckTester/.claude/worktrees/redelem`, branch `redelem-analysis`, forked from
  `origin/phase-1-2-deck-analyzer` at `e944eeea4`. **Nothing is pushed yet.** All commits are local.
* Deck: `decks/Red Elementals/Red Elementals.cod` (committed `ad8d83ae7`). Stage 4 baseline profile
  `decks/Red Elementals/Red Elementals.profile.json` (`0f16b98a2`, analyzer seed 20261114; no `value_play` block, so
  play = d5 b20). No keep table, no value leaf.
* Suite key: `redelem` (+ `redelem2hg` smoke canary) in `test/regression_cases.sh`, GT accepted in all three tiers.
* Ledger: `docs/design/analysis-Red Elementals.md` (Stage 1-4a recorded; 6a disclosures accumulate there).
* Scratch (regenerable, build volume): `build/scratch/redelem/` -- baselines `F_base/` (mtg + cards.json at
  `854902a73`), `D_base/`; A/B harness `F/run_ab.sh` + `F/man_ab.json`; identity script `b3/ident_b3.sh`;
  pairwise compare `D/cmp.py <base.wins> <new.wins>`.
* Agent handoff notes (gitignored, local only): `logs/redelem/agent*-handoff.md`.

## Done (commit hashes on `redelem-analysis`)

| Stage | What | Commits |
|---|---|---|
| 1 | Coverage | -- |
| 2 | Spark / Ball Lightning / Bladewhirl / Sunspine Lynx / Hellspark unearth | `e313899fc`, `59ae36b26` |
| 2 | Champion (Nova Chaser), linked exile | `0f82612c3`, `b53c6f21c` (every in-window pick searched; provider prune `MTG_CHAMPION_DOOMED_PRUNE`), `112febf7c` (validation) |
| 2 | Thunderkin Awakener: searched attack-trigger reanimation | `f409f2e07`, `05b603a2e`, `44154dd93` |
| 4a | `RedElementalsProvider` (routed above Morph); champion order/prune and Thunderkin order moved in; USER Nova main-2 rule (`MTG_REDELEM_NOVA_MAIN2`); `OrderOpaqueCastsByRank` (`MTG_REDELEM_OPAQUE_ORDER`) | `f0bf6f249`, `854902a73` |
| 3-gap | Incandescent Soulstoke put at the OPPONENT's end step (searched; reuses the morph face-up window / `Plan::eot_face_up_choice`; source hold searched in every host) | `06f2e9c98` (code), `6db011c8b` (tests), `1e6c24632` (ledger) |
| 4 | Baseline profile | `0f16b98a2` |
| 4-bis/5j | Suite addition: cost gate 0.16x Hinata PASS; smoke/regression/overnight ENTRY rows + backstop + 2HG smoke canary; GT accepted per tier; LFE regression GT re-accepted (2 keys, Soulstoke window, attributed) | see the commits after `0f16b98a2` (rows + GT, then docs) |
| 5a/5b/5c | Mismatch 0 lines (1,500 games); depth sweep monotone; budget: 2 of 9 slow games starve at b20, both recover | ledger "Stage 5 runtime verification (agents H/H2)" |
| 5f | (b) the budget-0 cost anomaly = mulligan bottoming (no keep table), not a defect; (a) sound dead-trick fold `MTG_REDELEM_TRICK_FOLD` (0 faster / 0 slower over 1,240 games, units -4.0%); redelem smoke + regression GT re-accepted | `38454a1a0` (code + test), `d7d0d55b6` (GT); `docs/design/red-elementals-search-cost.md` |

Stage 4a A/B: 0 slower / 0 faster vs the Morph-routed base on 840 games; the main-2 rule gives 1 faster,
0 slower; ~50% more search units than the base (Morph's work-cutting hooks do not apply; Stage 5f candidate).

## Done: Soulstoke opponent-end-step put (agents F / F2 / F3)

Design: `docs/design/opponent-end-step-window.md` §0. Measurements, disclosures and the open questions S1-S6:
`docs/design/analysis-Red Elementals.md`, section "Soulstoke opponent end step". In short:
* The full unit suite passes (3095/3095), and `test/scenarios.sh` passes 533/533, including the new Soulstoke
  fixture (the pre-change binary fails it).
* Paired A/B at d3 b100 vs `F_base` (`854902a73`): Red Elementals 840 games and Landfall Elementals 720 games
  (LFE with `ignore_play_profile`). **0 faster, 0 slower, net 0** on both decks.
* Units: RE +4.5% / +3.0% / +3.9%, LFE -0.2% / -0.4%. That is under the +25% threshold, so no collapse was
  added.
* The 240 traced games contain no real window put; the window matters only in a fixture-like spot (a lord or
  doubler next turn).
* Census: searched-frame=0, eot-put unfanned=0, fslinewin-shadowed=0, twins 603,344 (158,108 inert-skipped).
* Morph, Goblins and Spirits are byte-identical to `F_base`. Audits rc 0 (costs, fields, viewer `--no-sweep`
  on RE and LFE).
* F3 fixed two defects: hold twins were discarded by the post-apply state dedups (`EotHoldFoldKey`), and the
  look-top ORDER front row carried a pin.
* Scratch: `build/scratch/redelem/F/new_bin` (final binary + cards.json), `F/ab_base`, `F/ab_new`, `F3/`
  (identity, traces, unit/scenario logs). The pre-squash history is kept on the local branch
  `backup/redelem-F3-pre-squash` (`28b1d7685`).
* The viewer audit's claude-play SWEEP has not been run with the window (Stage 5h covers it).

## Plan after Soulstoke (serial; box = 12 threads / 10 GB: <=2 batches, never compile beside 2)

1. ~~Stage 4 profile~~ DONE (`0f16b98a2`).
2. ~~Suite addition (4-bis / 5j)~~ DONE (agent G). Details: ledger section "Regression suite (4-bis/5j)".
   * Cost gate: redelem 3,562.9 ms/game against Hinata 21,704.9, which is **0.16x** (limit 3x).
   * Rows: martyr/Hinata shape and the overnight ENTRY set. Backstop blocks are 30201 (regression) and
     30241 (overnight). There is a 25-game `redelem2hg` smoke canary; the deck reads no heads or opponents.
   * LFE regression GT re-accepted: 2 keys, s2002 gi22 at d3 and d5. It is a real Soulstoke window put at the
     same win turn, attributed against `F_base`.
   * LFE smoke is byte-identical. LFE overnight GT is LEFT for the periodic overnight run.
   * Identity: the full smoke tier gives 203/203 existing keys byte-identical, and Hinata regression is
     byte-identical. `check_gt_logs.py` is clean.
   * Owed at the keep-table adoption: resize the overnight rows to the FULL set and accept that on its own
     before adopting.
3. **Stage 5.**
   * DONE (agents H/H2): 5a mismatch, 5b depth sweep, 5c budget starvation, 5f (fold adopted, cost anomaly
     explained). Details: ledger section "Stage 5 runtime verification (agents H/H2)" and
     `docs/design/red-elementals-search-cost.md`.
   * RUNNING: 5c2 `leaf_tiebreak_check.py` re-run at 48 blocks (run 1 at 12 blocks: NO SIGN, 0 / 12,000 changed).
     Python PID and marker in `logs/redelem/agentH2-handoff.md`; verdict `build/scratch/redelem/H2/ltb48.log`,
     marker `build/scratch/redelem/H2/ltb48.DONE`. Verdict handling is in the ledger's 5c2 paragraph.
   * `verify_deck.py`: see the ledger for the gate lines (claude_sweep PENDING until 5d runs).
   * NEXT: 5d claude-play sweep, about 20 games. A Claude session uses Claude agents (Opus for flag
     verification); a Codex session uses Codex agents.
   * NEXT: 5h `audit_viewer_decisions.py` with the sweep.
   * 5e/5g as needed.
   * 5i: write the discard-bucket policy and **present it to the USER**. Do not adopt it unilaterally.
4. **Stage 6** report, with the 6a disclosure table.
5. **Push:** rebase onto `origin/phase-1-2-deck-analyzer` (never merge). Rebuild, then run smoke,
   `check_gt_logs.py` and `check_references_safe.py`. Push once, then watch CI in the background with
   `--interval 60`. Push the shared branch at most once every 4 h.

## USER rulings (verbatim)

* "I think we could have a heuristic that orders targets for champion most likely."
* "No, the provider can prune other options."  (A provider may prune, even to one option, behind
  `MTG_UNPRUNE`; human play stays unpruned.)
* "Note that Nova Chaser should be cast main 2 if it is hardcast, but it might also be dropped in main 1 via
  Incandescent Soulstoke."
* "Please do everything you can for the deck autonomously overnight."

## Open questions for the USER (none blocks work; defaults taken are stated)

1. **`OrderOpaqueCastsByRank` cast order.** It is ON by default. It recovered all 8 games the plain
   derivation lost. Keep it, or write a Red Elementals cast order? This is a USER-owned cast-order
   decision.
2. **Nova main-2 rule.** Confirm it. It gives up the main-1 hardcast where the champion is declined, which
   is what feeds the Thunderkin 10/2 reanimation line.
3. **The ~50% extra search units vs the Morph-routed base.** The sound fold `MTG_REDELEM_TRICK_FOLD` takes 4%
   (adopted, default ON). The rest would need Morph's largest-attacker trick target (one cast-time Legion
   Leadership target), which is LOSSY. Adopt a Red Elementals version? Default: not adopted.
4. **Doomed-champion prune.** It measured exactly neutral (840/840 identical). Keep it ON?
5. **Hellspark unearth replacement.** It is routed only through sacrifice and the end-step sweep.
6. **Sunspine Lynx statics** are inert in goldfish, and INERT_PARAMS is set for Lynx and Thunderkin.
7. **Always-on second main** (via `champion_subtype`). Its cost has not been measured.
8. **Thunderkin provisional choices:**
   * no same-card picks;
   * the nested Nova return is not searched;
   * a toughness bound;
   * 7x16 slots.
9. **Same-line Elemental token makers** are not champion targets.
10. **Soulstoke window (S1-S6 in the RE ledger section).** The FSLineWin dork-kind shadow, the put-menu front
    order, the logging gap, and whether to keep the window ON given 0 games changed (default: on). Window mana
    follows the morph precedent, with no mana-reservation axis, so LFE's multicolour payer could strand {R}.
11. ~~LFE GT re-accept~~ DONE for smoke (no move) and regression (2 keys at the same score). The window
    really puts in LFE play: s2002 gi22 puts Flamekin Harbinger at the opponent's end step. That updates S6,
    which saw no put in RE traces. LFE overnight waits for the periodic run.
12. ~~**Unbudgeted cost per unit.**~~ ANSWERED by agent H: the time is mulligan bottoming rollouts, because
    there is no keep table yet. They are counted units; budgeted cost is 254 us/unit against martyr's 344.
    Original note: Red Elementals spends ~130 us of wall per search unit at budget 0, about 3x
    martyr's ratio. Its budget-0 tail reached 60 s on one game (s30121 d5 gi2, 0.97M units) without
    abandoning. That suggests work the unit meter does not count. It is a Stage 5f candidate, alongside the
    +50% units.
13. **(agent H) Lossy proposal: Morph's largest-attacker trick-target rule.** It would allow ONE cast-time
    Legion Leadership target. That is likely a large unit saving, but the rule is lossy, so the USER decides.
    Default: not adopted.
14. **(agent H) Mulligan policy is USER-owned.** s7400 gi197 mulliganed a 1-land + MDFC seven-card hand. This is
    only surfaced; the keep table will decide.
15. **(agent H) A post-combat Nova Chaser cast with no Elemental is still offered.** Declining the champion
    sends it to the graveyard, where it feeds a Thunderkin return at toughness >= 3, so it is not dominated.
16. **(agent H2) Mid-combat Legion Leadership is a modelling gap.** Leadership cannot be cast during combat, so it
    cannot double an attacker that Thunderkin Awakener returned (for example a returned Ball Lightning, 6 -> 12).
    That is a real MTG line. Generalise the doubler-after-exalted window to after attack triggers? Default: not
    modelled; disclosed. The fold neither causes nor widens it.
