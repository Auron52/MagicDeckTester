# Sea Creatures analysis — resume checkpoint

Short resume note for the in-flight Sea Creatures `analyze-deck` run (Claude orchestrator session,
2026-11-16). The full state is the ledger `docs/design/analysis-Sea Creatures.md`.

## Where things live

* Worktree `/workspaces/MagicDeckTester/.claude/worktrees/sea-creatures`, branch
  `sea-creatures-analysis` (local commits only; NOT pushed; base `8cd0bbf23` (rebased 2026-11-16 from f92bb3b7c)). The primary checkout's
  `decks/Sea Creatures/` is untracked there (the user's drop); the worktree copy is committed.
* Scryfall JSON: `/workspaces/MagicDeckTester/build/scratch/sea/scry/`.
* Research drafts (gitignored): `logs/sea-research/*.md` in the worktree.

## Stage status (2026-11-17, before compaction)

* Stage 1 coverage DONE. Stage 2 research DONE (6 drafts in `logs/sea-research/`).
* Serial integration (brief `logs/sea-research/INTEGRATOR-BRIEF.md`; each step's row is in the ledger):
  * I1 DONE `4eb5a3e10` + `4cf605b4f` (Misty viewer-only hold) + `8682648b6`.
  * I2 DONE `fac368913` + `403b7f58c` (Koma, Spawning Kraken, Serpent, Lorthos, Whelming Wave, shared
    `ReturnPermanentToOwnersHand`).
  * I3 DONE `407838f7b` + `ffad8ce2d` (Kiora, Summon: Leviathan on the World War Hulk Saga engine;
    conditional main 2 on Leviathan-draw turns, `MTG_FORCE_USES_M2=1` = every-turn arm).
  * CG investigation doc cherry-picked `7374f09b1`: the CG churn was an A/B artifact (base binary on the
    new cards.json). RULE: every byte-identity arm uses its OWN commit's cards.json.
  * I4 DONE `8e1be70c1` + `314dbfc9c` (Hullbreaker, Brinelin; search declines by USER ruling).
  * I5 DONE `03c800faf` + `e9815ca9c` (Sin: searched as-enters counter harvest; provider prune
    `MTG_SEA_SIN_STRIP_SAGAS` strips every Saga, nothing else; open Q18).
  * I6 Trench Gorger IN FLIGHT (agent launched 2026-11-17 on base `e9815ca9c`): engine full menu; provider
    prune `MTG_SEA_GORGER_ALL` exile all lands if >= 6 in library, else decline; bounced-Gorger P/T fix.
* Smoke (d3 b10, 40 g, avg counts losses as 9): I2 37/40 avg 6.20; I3 avg 5.925; I4 37/40 avg 6.225 (the
  earlier "40/40" was a misread); I5 37/40 avg 6.15. Losses gi 6/21/33 persist -> explain in Stage 5.
  The I3->I4 move is NOT composition: missing cards are dead placeholder slots (DeckLoader::MakePlaceholder),
  so I4 turned two dead slots into castable 7-8 drops (a play change, ~1.5-2 se).

## Progress 2026-11-17 (after I6)

* I6 Trench Gorger DONE `ddf109ddc` + `9515c52d3` + `433409550` (=0 arm picked exile_all 4/4).
* Stage 3 clean; Stage 4 profile `3a21f5afb`; 4-bis all three tiers + GT `b96973a0a` (cost 0.99x Hinata).
* Claude-play sweep 16 games, 0 flags, recorded in the ledger `9420b5739`.
* Stage 5 A/B (one pooled batch, `build/scratch/sea/ab5/`, 400 g/arm d5 b20, seeds 8810001..8840001):
  fetchForestFirst / sprawlUG / bounceSearched / sinSearched / waveSearched = identical win turns (units +0..+51%);
  gorgerSearched 1 game worse; fetchAll (prune OFF) 11 better 0 worse (+71% units); m2Every 11 better 0 worse
  (+12.9% units). Root-cause agent writing `logs/sea-research/ab-rootcause.md`.
* Queued chain `build/scratch/sea/discbound/chain.sh`: waits for leaf_tiebreak_check (`build/scratch/sea/tiebreak/`),
  then the discard best-vs-worst bound batch, then verify_deck.py (`build/scratch/sea/verify/out`); DONE marker
  `build/scratch/sea/discbound/DONE`.

## Next steps

1. Read ab-rootcause.md; surface fetch + main-2 findings to the USER with amended-rule proposals.
2. Read tiebreak, discard bound, verify_deck results; author the discard bucket proposal (USER approves first).
3. Stage 6 report with the open-question list. Push (4h rhythm) after rebase + rebuild + smoke + check_gt_logs.

## Landing, 2026-11-17 ~13:40 UTC (USER: "Please continue autonomously overnight")

* Stages 1-5 done; 5c2/5i, verify_deck (GATE PASS), land-Aura main-1 fix and Q12 re-measure all in the ledger.
* Branch rebased TWICE: onto origin 82f5... earlier, then onto origin `18ab35ca8` (67 new upstream commits with
  src changes to ManaPayment / TurnSolver / SpellEffects / ManaOracle and NO GT accept). Backups:
  `backup/2026-11-17/sea-prerebase2` (pre-2nd-rebase tip). Only conflict: HeuristicArm.h list appends (kept both).
* The first `--both` (pre-2nd-rebase) failed 118 rows across ~30 decks: far beyond Sea + land-Aura decks.
  Running now: `build/scratch/sea/land2.sh` (rebuild, units, origin-tip worktree `.claude/worktrees/sea-origin`
  + build, tip `--both`, origin binary on the tip's failing non-Sea rows, `attrib.py cmp` -> `land2/attrib.txt`).
  Rows where the origin binary reproduces the tip's digest are UPSTREAM drift (not ours, not accepted blind);
  rows that differ must be Sea / land-Aura decks (Alchemist, EDF, Jumbo) and are the accept candidates
  (`regression.sh --accept --deck=` scoping).
* Hedge Maze flag `MTG_SEA_FETCH_MAZE` (USER 2026-11-17, "if we do not need it for mana this turn it is
  possible, otherwise not"; Triomes "similar"): worktree `.claude/worktrees/sea-maze`, branch `sea-maze-fetch`
  (uncommitted). Build + units + A/B chain `build/scratch/sea/maze/{build.sh,ab.sh}` -> `maze/result.txt`.
  Design note `docs/design/fetch-tapped-typed-land.md` (there).
* Next: per-row verdicts -> scoped accept -> check_gt_logs -> push -> CI watch -> Stage 6 report.

## Landing result, 2026-11-17 ~15:50 UTC

* `land2/attrib.txt`: 130 failing rows. 104 are UPSTREAM drift (the origin binary reproduces the tip's
  digest; the 12:07 UTC engine commits have no GT accept since 19d7624a5). 9 Sea rows. 17 differ from origin:
  Alchemist / EDF / Jumbo (land-Aura fix) and Creature Giving d0 x2.
* Creature Giving d0 root-caused: `7c680f8dc` (Misty battlefield model) lets a Misty put onto the battlefield
  by Crop Rotation crack (origin leaves it uncracked all game). 2 games faster (smoke gi832 6->5, regression
  gi355 8->6). NOT accepted: its other 8 failing rows are upstream-only, so the deck is left to that owner.
* Accepted (scoped) in `e294012fe`: 24 rows (Sea 9, Alchemist 4, EDF 6, Jumbo 6 -- jumbo d0 also carries
  upstream drift). Only SLOWER game on an accepted deck: sea_regression_d5_s3003 gi69 (fetch-shuffle variance).
  check_gt_logs 1278 consistent / 0 stale.
* Hedge Maze: committed `4147878d2` on `sea-maze-fetch` (backup `backup/2026-11-17/sea-maze-fetch`). A/B std
  1000 + 2HG 300: 6 faster, 0 slower (std -0.0050 se 0.0022). Repro std gi223: on-arm Windswept Heath fetches
  Hedge Maze T3 (mechanism confirmed; arms already diverge at T1, so draw order is mixed in). Default OFF ->
  USER. To cherry-pick onto this branch for the NEXT push window (needs build + units + smoke byte-identity).
* Next: push phase-1-2-deck-analyzer, CI watch, remove `.claude/worktrees/sea-origin`, Stage 6 report.
