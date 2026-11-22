# Beasts analyze-deck: handoff to Codex (2026-11-22)

The USER asked to move the in-flight Beasts `analyze-deck` run from Claude to Codex (Claude budget).
This file is the entry point; the authoritative state is the ledger `docs/design/analysis-Beasts.md`
(Checkpoint section, its **Running** line, the Stage 2 card table, the two "USER rulings" sections and
"Approved deferrals"). Read AGENTS.md / CLAUDE.md first: every hard rule there applies.

## Where

* Worktree `.claude/worktrees/beasts` (under the primary checkout `/workspaces/MagicDeckTester`),
  branch `beasts-analysis`, based on origin/phase-1-2-deck-analyzer c9a2e1ff6.
* **Nothing is on the shared branch yet.** The LOCAL branch is authoritative. An older backup exists at
  `origin/backup/2026-11-21/beasts-analysis` (last pushed at the "integrator H running" commit; the
  handoff commits are local only, per the USER).
* Per-card research drafts: `logs/beasts/drafts/*.md`; per-batch integrator handoffs:
  `logs/beasts/integrator-{A,C,D,E,F,H}-handoff.md` (gitignored, on the host drive in that worktree).
* Builds land on the build volume via `./build.sh` (the worktree's `build/` is a symlink there).
  Scratch output: `/workspaces/MagicDeckTester/build/scratch/beasts*/`.

## Done

Batches A-G integrated, each with unit tests, `audit_card_costs.py`, `audit_card_fields.py --update`,
`audit_viewer_decisions.py --no-sweep` clean and smoke (no accept) 235/235 with 0 play-changed:
A data cards (Packleader, Warchief, Questing Beast, Thragtusk, Cragplate Baloth, Kaheera lord), B Contested
Cliffs, C Elder Gargaroth, D Garruk's Harbinger, E Migloz (+ perf fix keeping sizeof(Permanent) 328), F
Nykthos (63aa2fe6b), G Bloated Contaminator (e2838e47a). Commit list: `git log --oneline c9a2e1ff6..`.

## Remaining, in order

Batch H is integrated as `c279a5ff3`; batch I is integrated locally as `cd1ea5116`.
Batch I passed 3892/3892 units and 235/235 smoke configurations with 0 play changes before
a human-only viewer menu follow-up. That follow-up passed 60 human-mode assertions. The full
pre-push regression then passed 375/375 with 0 config or play changes; viewer reference replay
reported 930 refs with 0 protocol drift. The shared push queue currently has Merfolk and
Worldfire ahead of Beasts. Resume at step 3 (viewer readiness); historical batch H/I
instructions below are retained for context.

1. **Batch H, Woodland Bellower -- IN PROGRESS, start here.** Everything is in WIP commit `f75719a58`
   (ETB Natural-Order put route in `FireOwnEtbTriggersBody` step 4b, `tutor_nonlegendary` +
   `TutorPutTargetMatches`, both decline arms with no shuffle, put-in-line soundness credits in
   `DevotionGrowthBound` / reducer credit / three mana-prune gates / both payment walks, cards.json entry,
   viewer audit mapping + DECISIONS.md, 10 unit tests). **It is UNTESTED:** the last build failed on 4 errors
   in `test/unit/test_beasts_cards.cpp`, which were fixed in the WIP but never rebuilt; the binaries in the
   build dir predate batch H. `audit_card_costs.py` passed 698/698. Full detail and remaining steps:
   `logs/beasts/integrator-H-handoff.md`. Remaining: rebuild; Bellower tests (the put-Warchief test may
   fail on cast order -- fix with an order domain like batch F's `DevotionOrderDomain`); full unit suite
   (expect 3888; routing test still sends Beasts to Boars); `audit_card_fields.py --update`;
   `audit_viewer_decisions.py --no-sweep`; smoke without accept (watch the Natural Order decks); squash
   `f75719a58` (and any later WIPs) into one batch H commit on top of `2d181ad20` WITHOUT touching the ledger
   commits; ledger commit (Bellower row integrated + approved, "Batch H integrated" Checkpoint bullet,
   Running -> batch I). Known gaps found by H: (a) in HUMAN play, if the pre-search Packleader draw takes the
   card the human named, the put falls back to the provider's top candidate (viewer only; fix by re-asking);
   (b) a put creature is not counted for a later Craterhoof pump in the damage projection (live in Beasts: the list has one Craterhoof).
2. **Batch I, Kaheera companion**: CR 702.139, "{3}: put your companion into your hand", sorcery speed,
   once per game, searched; viewer offer; teach `scripts/analyze_deck.py`'s coverage that a companion
   makes the sideboard card reachable. Draft `logs/beasts/drafts/kaheera.md`.
3. **Viewer readiness -- the USER wants to be TOLD when Beasts is playable in the viewer, to save
   reference games.** After H and I: run the regression suite (pre-push gate), rebase onto
   origin/phase-1-2-deck-analyzer (never merge; rebuild + smoke + `test/check_gt_logs.py` if src/GT
   replayed), `scripts/check_references_safe.py`, push the shared branch (at most once per 4 h), watch CI
   in the background with `--interval 60`. Then fast-forward the PRIMARY checkout (the viewer runs only
   there): first check it for uncommitted references and a live viewer game; it holds an UNTRACKED
   `decks/Beasts/` copy -- diff it against the branch's version, move it aside (never delete), then
   `git -C /workspaces/MagicDeckTester pull --rebase`. It also has a modified `test/ref_bench.json` and
   stray `o1.txt`/`r*.txt` -- leave them alone. Then tell the USER.
   **Completed (2026-11-22):** H+I passed the full pre-push regression and queue-head validation
   after combining with Worldfire (4022/4022 units, 243/243 smoke, no play changes, 1409 GT logs
   consistent). Landed as `c841cd59c`, pulled into the primary checkout and rebuilt there. The
   original untracked deck copy was preserved under `logs/beasts/primary-deck-backup-20261122-1703/`
   (its only difference was line endings). A local baseline profile from seed 221122 enables the
   primary viewer at `http://localhost:8080`; `/api/step` returned the seed-1 mulligan decision.
   The USER was told immediately. The analyzer's discard evidence may still be running; Windows
   CI is being watched in the background. Regenerate the profile after batch J settles.
4. **Batch J, BeastsProvider** (owed list in the ledger Checkpoint, every "Gaps for batch J" bullet):
   route above `boars` + re-pin the routing test; Certificate (must decline when opponent poison or a
   poison source exists); per-turn `ProviderSecondMainLive` gate (combat-time draw/dig/proliferate);
   same-name dig fold; cap-27 ordinal-product heuristic prunes per the USER's lethal-aware rule (below);
   Nykthos W-vs-G prune; Kicked Baloth MV fix; point Adaptive Omnitool at the Harbinger pin; **draft a
   per-deck cast order for USER review** (the USER said they would look it over).
5. Stage 3 coverage recheck; Stage 4 profile + 4a provider audit + the **second-main measurement for the
   USER** (none / per-turn / deck-wide: units, ms/game, avg win turn; deck-wide reachable via
   `MTG_FORCE_USES_M2`); 4-bis suite rows in all three tiers under `suite_gate.py --cost` (3x Hinata);
   Stage 5 (`verify_deck.py`, the play sweep -- a Codex session spawns Codex agents and records it as
   Codex-play -- viewer audit incl. Gargaroth's 3-option prompt); Stage 6 report.

## Rules specific to this run

* **Never add to `DeckUsesSecondMain`** -- the second main is USER-owned and unsettled (ruling in the ledger).
* Keep `sizeof(Permanent) == 328`, GameState 1720, Plan 792 (static_asserts).
* Only a DecisionProvider may restrict/order options; no host-side default/top pick; gates must
  over-approximate (never make a line unfindable).
* One heavy job at a time, under `flock /workspaces/MagicDeckTester/build/scratch/BOX.lock`.
* Subagents: 250K-token handoff rule (stop 250K-350K, never above 400K), no push, never amend over the
  orchestrator's ledger commits.

## USER rulings to carry (verbatim copies are in the ledger)

* Cap-27 ordinal products: prune with a provider heuristic; lethal-aware. Gargaroth per ordinal: token ONLY
  when the token provably brings lethal sooner; draw ONLY when the token provably cannot make the game end
  faster; otherwise (typical) search BOTH. Life mode already folded.
* All seven deferrals approved (see "Approved deferrals").

## Working this from a local Codex session

* State at handoff: no Claude agent, build or test run is left running, and the Claude session's status
  timers are deleted, so nothing else is acting on this worktree. Check with `git -C .claude/worktrees/beasts
  status --short` (expect clean) and `ps -eo pid,etime,args | grep -E 'build.sh|regression.sh|mtg'`.
* Long runs (build ~20 min under load, full unit suite, smoke ~12-20 min): follow CLAUDE.md's "Codex IDE
  equivalent" -- keep a yielded cell waiting on the captured PID (`kill -0`), never `pgrep -f`. A run that
  touches 5:30 AM - 1:00 PM PDT needs the 2-hour status check (timed notification).
* The box is loaded by an outside container (load average 30-75 on 24 cores), so wall-clock A/Bs are noisy;
  use instruction counts (callgrind/cachegrind on a Profile build) for perf questions.
* Each batch was run by ONE integrator agent at a time (cards.json and shared C++ cannot take concurrent
  edits). The pattern that worked: the orchestrator commits a ledger "integrator X running" line; the
  integrator reads this ledger, its draft and the previous handoffs, implements per the draft, adds unit tests
  in `test/unit/test_beasts_cards.cpp`, runs the full unit suite, `scripts/audit_card_costs.py` (Scryfall 429s:
  retry per card at a 1 s throttle), `scripts/audit_card_fields.py --update`, `audit_viewer_decisions.py
  --no-sweep`, then smoke WITHOUT `--accept` (`flock .../BOX.lock env -C <worktree> bash test/regression.sh
  --smoke`; expect 0 configs changed / 0 play-changed on every other deck, root-cause anything that moves),
  squashes only its own WIPs, and commits a ledger update (card row "integrated <hash>", a "Batch X
  integrated" Checkpoint bullet with soundness sites + gaps for batch J, Running line -> next batch).
* Other deferral docs this run created (already committed): `cavern-of-souls-creature-type.md` (Merfolk Turns,
  USER: fix "only when there is time"), `omnitool-combat-dig-greedy.md`, `omnitool-restricted-second-main.md`.

## Future decisions (no USER reply currently pending)

1. Three Tree City: giving it Nykthos's treatment would move Goblins / Maze's End / Merfolk Turns
   play. Run an A/B before proposing adoption.
2. Bloated Contaminator: proliferate resolves after the other combat-damage triggers of the step
   (weakly dominant, fixed order). Investigate whether reverse order has a live benefit.
3. Second main and the cast order: produce the Stage 4 measurements and batch J draft before
   asking the USER to choose.
