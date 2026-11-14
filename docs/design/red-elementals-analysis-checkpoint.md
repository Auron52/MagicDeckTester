# Red Elementals analysis -- live checkpoint (handoff document)

Shared state for whichever agent (Claude or Codex) continues the Red Elementals analysis. Read this first,
then `docs/design/analysis-Red Elementals.md` (the per-deck ledger), then verify files/processes before acting.

Last updated: 2026-11-14 21:00 UTC (Claude coordinator, handing off to Codex).

## >>> CODEX HANDOFF -- START HERE (state at 2026-11-14 21:00 UTC) <<<

**What may still be RUNNING when you arrive.** Check each with `kill -0 <pid>` and `ps` before you touch the tree.

1. **Claude agent "H2" may still be working in this worktree.** It holds UNCOMMITTED edits:
   * the Stage 5f fold `MTG_REDELEM_TRICK_FOLD`, in `src/ai/HeuristicArm.h`, `src/ai/DecisionProviders.h` and
     `src/ai/DecisionProviders.cpp`, plus a unit test in `test/unit/test_red_elementals.cpp`. A copy of the patch
     is in `logs/redelem/agentH-trickfold.patch`;
   * a draft ledger section in `docs/design/analysis-Red Elementals.md`;
   * a new `docs/design/red-elementals-search-cost.md`.

   If `logs/redelem/agentH2-handoff.md` exists, H2 has stopped: follow that file. If it does not exist and the
   tree is still changing, H2 is live. Do not edit the same files. Wait for the handoff file, or ask the user to
   stop the Claude agent.
2. **Release build for the fold.** It is `MTG_BUILD_JOBS=4 ./build.sh`, ninja PID 1387809, log
   `build/scratch/redelem/H/build_fold.log`. At 20:37 UTC it was at step 159/301; it compiles slowly beside job 3.
3. **The 5c2 horizon tie-break check.** This is `scripts/leaf_tiebreak_check.py`: 12 blocks x 1000 games x 2
   arms at play settings d5 b20, on a snapshot binary of HEAD `cfa0d2c13`
   (`build/scratch/redelem/H/bin_cfa0/mtg`), with `MTG_PGO=0`. Python PID 1387602, batch PID 1387605. Output
   `logs/leaf_tiebreak/redelem_H.out`. The verdict goes to `build/scratch/redelem/H/ltb.log`; the marker is
   `build/scratch/redelem/H/ltb.DONE`. Started about 19:00 UTC and expected to take 5-8 h. **Do not kill it.**
   Act on the verdict as `.claude/skills/analyze-deck.md` §5c2 says:
   * KEEP or NEUTRAL: nothing to do.
   * NO SIGN: re-run with a fresh `--seed-base` at the suggested `--blocks`.
   * OPT OUT: add `bool GradesNoWinLeaf() const override { return false; }` to `RedElementalsProvider`, and
     record it in `docs/design/horizon-honest-leaf.md`.

**Stage 5 results so far** (agent H; numbers in `logs/redelem/agentH-handoff.md`, ledger section pending):
* **5a mismatch harnesses: PASS.** 1,500 games across d0/d3/d5 and b10/b20/b100 produced 0 `[nonconv]` and 0
  `[fd-diverge]`.
* **5b depth sweep: PASS.** d0 to d3 is 103 faster / 0 slower. d3 to d5 is 0/0. d5 b20 to b100 is 1 faster.
  The clock is about T4.4. Every slow outlier was mana screw or flood.
* **5c budget starvation: NOT RUN.** The manifest is ready: `build/scratch/redelem/H/man_5c.json`. The only
  sign of starvation so far is s7100 gi83, which wins T5 at b20 and T4 at b100.
* **5f (b), "130 us/unit unbudgeted": EXPLAINED, not a defect.** The time is mulligan *bottoming*: with no keep
  table yet, the deck plays clairvoyant full-game rollouts per candidate bottom. Those units are counted.
  Budgeted cost is 254 us/unit, against martyr's 344. The keep table, which is the next pipeline stage, fixes
  it. So open question 12 is answered.
* **5f (a), +50% units: one candidate sound fold, `MTG_REDELEM_TRICK_FOLD`.** Legion Leadership may target only
  creatures that can attack, and may not be cast after combat. H2 is building and A/B-testing it. The
  adoption bar:
  * 0 slower games (any slower game must recover at `--depth 8 --budget-ms 0`);
  * the OFF arm reproduces s7100 d5 b20 digest `ec8b883b0636ad67`, avg 4.3750;
  * units go down.

  If it is adopted, re-accept GT for `redelem` only (`--deck=redelem,redelem2hg` for smoke). The A/B
  manifest is `build/scratch/redelem/H/man_fold.json`.
* **Not sound, so not taken:** Morph's `EndStepSacFaceUpDeadAfterCombat`. Post-combat Spark, Hellspark and
  Ball Lightning feed Thunderkin, unearth and champion.

**Remaining work for Codex, in order:**
1. Finish H2's items, unless H2's handoff says they are done:
   1. fold build, then the unit suite (`build/Release/mtg-test`) and `test/scenarios.sh`;
   2. the fold A/B, then adopt it or keep it OFF;
   3. 5c;
   4. the 5c2 verdict;
   5. `python3 scripts/verify_deck.py "decks/Red Elementals/Red Elementals.cod"`;
   6. the ledger section "Stage 5 runtime verification" and `docs/design/red-elementals-search-cost.md`.

   Commit locally.
2. **5d sweep: about 20 games.** Run it AFTER the fold decision, because play changes. A Codex session fans out
   **Codex** agents, and the result is recorded as `## Codex-play sweep` (USER 2026-11-11; see
   `.claude/skills/claude-play.md` "Running a sweep" and "Recording a sweep"). Use a base seed disjoint from
   the suite's. Verify each flag against `cards.json`, and record `flags: N unresolved` in the ledger.
3. **5h:** `scripts/audit_viewer_decisions.py` on RE and LFE, with the sweep included this time. Cover the
   `eot_put` decision.
4. **5i: discard bucket policy.** The analyzer said STATUS_QUO_OK: only 2 discard decisions in 400 d3 games. Write
   it up and PRESENT it to the USER. Do not adopt it.
5. **Stage 6** report, with the 6a disclosure table (`.claude/skills/analyze-deck.md` Stage 6).
6. **Push to the shared branch** `phase-1-2-deck-analyzer`:
   * rebase onto origin (never merge) and rebuild;
   * run smoke (a full smoke took about 106 min on this box), `python3 test/check_gt_logs.py` and
     `python3 scripts/check_references_safe.py`;
   * push once, then watch CI in the background with `--interval 60`.

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
   * `verify_deck.py`, nonconv/fd-diverge, depth sweep 0/3/5, `leaf_tiebreak_check.py`.
   * 5d claude-play sweep: about 20 games. A Claude session uses Claude agents (Opus for flag
     verification); a Codex session uses Codex agents.
   * 5h `audit_viewer_decisions.py` with the sweep.
   * 5e/5f/5g as needed. 5f means a deck-specific work-cutting narrowing for the ~50% extra units.
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
3. **The ~50% extra search units vs the Morph-routed base.** Accept them, or do a Stage 5f narrowing?
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
