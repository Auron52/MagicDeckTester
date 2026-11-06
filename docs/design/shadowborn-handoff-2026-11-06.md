# Shadowborn Apostle work -- handoff checkpoint (2026-11-06, Claude -> Codex)

This page is the entry point. Per-stream detail is in the handoff files linked below. Read CLAUDE.md first;
every rule there applies (build only with `./build.sh`; no timeouts; rebase, never merge; never block on a
question; a USER-owned decision is surfaced, never decided; nothing runs alongside a generation; references
are commit-only).

Worktrees are under `/workspaces/MagicDeckTester2/.claude/worktrees/`. Heavy jobs serialize on a shared lock:
`flock -x /workspaces/MagicDeckTester2/build/scratch/sb-heavy.lock <cmd>`.
Shared branch: `origin/phase-1-2-deck-analyzer`.

## The USER's scope ("Let's do all of the shadowborn work now"), in order

1. Main-phase levers ON -- **DONE, pushed** (dfd675afb "gt: accept smoke + regression for the Shadowborn
   main-phase levers ON"). The EDF/Bruna digest movers in that accept were proven to be origin drift.
2. On-demand Lotus Bloom / Lake of the Dead payer as the ONLY route: retire the SacForMana action and delete
   `MTG_IDLE_CRACK_FOLD`. Worktree `sb-sacpayer`.
   Handoff: `sb-sacpayer/logs/sb-sacpayer/sacpayer-handoff.md`.
   State (local, not pushed; handoff committed 547c2d3e2): held-out A/B FAILS the bar only on Dragonstorm d0
   (4.981 -> 5.073, +0.092). Every other cell is net <= 0 (Shadowborn d0 -0.455). Cause: the Dragonstorm
   storm-card ritual rule never sees the hidden Lotus crack. The proposed fix 0f46edde0 is NOT built or tested.
   Still to do: d8 b0 repros, regression, ref_bench, removing the temporary `MTG_SAC_ON_DEMAND` selector.
3. Searched picks. Worktree `sb-picks`. Handoff: `sb-picks/logs/sb-picks/handoff.md`.
   - 895e54b14: Profane Tutor executor upkeep searched (`MTG_PROFANE_REAL_NODE`, default ON).
   - 9e7984961: Crop Rotation consumes the searched pin (`MTG_LAND_TUTOR_PIN`, default ON).
   - Remaining item 3: decide at resolution for Search for Tomorrow, Emeria, Abiding Grace, and Titan's
     attack trigger.
   - Unit 2614/2614, scenarios 419/419, smoke unmoved. A detached chain (PID 3279052) runs regression
     (`build/scratch/sb-picks/reg.txt`) and then the pooled A/B (`build/scratch/sb-picks/ab1/`).
     Regression finished 277/277 with nothing moved (no GT accept needed). The A/B is running; `chain1.DONE` marks the end. Item 3 is not
     started.
4. nlfit redesign (leafless-first reserve). Worktree `shadowborn`, branch shadowborn-analysis (LOCAL).
   Handoff: `shadowborn/logs/shadowborn/nlfit-reserve-handoff.md`.
   Commits: 8ae1bc7b1, 748f78582, 33c80063f, plus WIP 0d8cf8958 (mode 3, hard reserve), which is NOT built or
   measured. Mode 2 fixes Alchemist (-0.0175) but is +0.04 worse on Shadowborn (a memo-hit misprice), and
   Angels/Pirates still cost 1.8-2.5x. The ab4 manifest is ready in `build/scratch/nlfit/ab4`. The Hinata2
   before/after has not been run.
   Ledger: `docs/design/analysis-Shadowborn Apostle.md`. Hinata counts as another before-vs-after case for it.
5. After 2-4 land: emul adoption, ALONE on the box:
   1. copy `logs/vlq_shadowborn_apostle/Shadowborn Apostle.value.EMUL.json` via the DECISIONS.txt route;
   2. delete the E7_playset / E7b_trust / E6_skipcal markers;
   3. run `bash scripts/valueleaf.sh run "decks/Shadowborn Apostle"`.
6. Then the leaf0 vs leafless vs budget comparison.
7. Then the TutorSearchWidth 32 A/B.

Adoption bar for 2-4 (CLAUDE.md, no-greedy rule): every deck's aggregate is net <= 0, and every slower game
recovers at `--depth 8 --budget-ms 0`. Clairvoyance-only losses are accepted (USER). A digest mover counts as
explained origin drift ONLY if origin's own binary reproduces the same digest. `--accept-with-regressions="<note>"`
promotes the last inspected run, and only while origin has not moved.

## Red shared-branch CI -- fix in progress (do this FIRST)

- Run 42137011583 is red on ubuntu (Windows/parity were cancelled). The cause is another session's commits
  25261417f (Modern RW Burn) and df30bfce1 (fetchlands now enter the battlefield; the crack is a
  `PermAbilityMode::SacLandSearch` activation). Two stale tests:
  - `test_landfall.cpp:216` expects 2 Beasts;
  - `test_pirates_provider.cpp:371` says "decks/ holds 47 folders but the table pins 46".
- Fix is uncommitted in worktree `sb-push` (branch ci-fix-fetch-tests off origin c1cba3e87):
  - test_landfall plays Wooded Foothills, checks 1 Beast, then cracks via
    `ApplyPermAbility(..., PermAbilityMode::SacLandSearch)` and checks 2 Beasts and life-1;
  - test_pirates_provider adds `{"Modern RW Burn", "Burn"},`. **"Burn" is a guess**: if the test fails,
    its message prints the real provider name.
- A build plus the full unit run was queued on the lock (output `build/scratch/sbpush-build3.log`; the test
  run follows it). Next:
  1. Run `.claude/worktrees/sb-push/build/Release/mtg-test` and confirm everything passes.
  2. Commit "test: update the fetchland landfall test and deck-routing table for the battlefield fetchland
     and Modern RW Burn".
  3. `git fetch`, rebase onto `origin/phase-1-2-deck-analyzer`, push to that branch.
  4. Watch the run with `gh run watch <id> --exit-status` in the background, then report Linux, Windows and
     parity.

## Other pending items

- Land the batch-resume (31fc243e2) and aq-resume branches.
- Resume Hinata2 later: `logs/artifact_queue/hinata2_pause_20261105/SALVAGE.txt` (83 of 216 jobs salvaged;
  run `python3 scripts/artifact_queue.py start` from the primary checkout with NO MTG_* env). The inputs key now
  excludes keep-generation journals. The USER accepted this with skepticism ("I remember missing journals being
  a reason for failure to restart"), so verify the resume actually restarts.

## Open USER decisions (surface them; do not decide)

1. Should the viewer offer Lake of the Dead's crack as a pay-click option?
2. Hinata2: filter out the d4 arms at the E.7 verdict step?
3. Keep the ported MTG_BATCH_RESUME, and who lands keepgen-slow-tail-fix?
4. nlfit reserve: adopt or not (the agent's recommendation was "not yet"; see its handoff file).
5. Payer: should the Dragonstorm Lotus/ritual rule also apply inside the SEARCH, or only in the d0/playout
   policy? The fix 0f46edde0 assumes playout only, since the widening loses nothing.
