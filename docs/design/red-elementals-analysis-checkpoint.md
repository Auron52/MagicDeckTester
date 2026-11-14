# Red Elementals analysis -- live checkpoint (handoff document)

Shared state for whichever agent (Claude or Codex) continues the Red Elementals analysis. Read this first,
then `docs/design/analysis-Red Elementals.md` (the per-deck ledger), then verify files/processes before acting.

Last updated: 2026-11-14 (agent G: suite addition 4-bis/5j done, all three tiers with accepted GT).

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
12. **Unbudgeted cost per unit.** Red Elementals spends ~130 us of wall per search unit at budget 0, about 3x
    martyr's ratio. Its budget-0 tail reached 60 s on one game (s30121 d5 gi2, 0.97M units) without
    abandoning. That suggests work the unit meter does not count. It is a Stage 5f candidate, alongside the
    +50% units.
