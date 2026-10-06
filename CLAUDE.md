# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Purpose

MagicDeckTester simulates Magic: The Gathering games to compare card and deck performance. The goal is to let users build or modify decks and run simulated games to evaluate how different card choices affect outcomes.

## Building — ALWAYS use `./build.sh` (never raw `cmake`)

Build with the repo-root script, which produces an **optimized** binary every time:

```
./build.sh                 # Release (-O3)  -> build/Release          (DEFAULT; use this for almost everything)
./build.sh relwithdebinfo  # -O2 + symbols  -> build/RelWithDebInfo   (debugging a crash with a faithful stack)
./build.sh profile         # -O3 + symbols  -> build/Profile          (faithful profiling: Release codegen + symbols)
```

**Do NOT run `cmake` directly.** A bare `cmake -S . -B build/Release` leaves `CMAKE_BUILD_TYPE`
empty, which compiles with **no optimization (`-O0`) — a silent ~10x slowdown** (it once turned a
~2h Hinata gen chunk into ~26h). `build.sh` sets an explicit build type per mode, each in its own
directory, so an unoptimized binary cannot happen by accident. A CMake guard also defaults an unset
build type to Release as a backstop. There is deliberately **no Debug (`-O0`) mode** — if one is ever
needed it must be a separate, deliberate route (`cmake -DCMAKE_BUILD_TYPE=Debug -B build/Debug`), never
the default. `scripts/analyze_deck.py` rebuilds via the same multi-config `build/` tree (always
optimized); the regression harness expects a pre-built binary at `build/Release/mtg` — build it with
`./build.sh` first.

## Repository Conventions

- **THE PLAY SERVER MUST NEVER, EVER RUN FROM A TEMPORARY FOLDER (user directive, 2026-10-05).**
  The viewer (`play.sh` / `play.cmd` / `tools/play/server.js`) saves the user's hand-played
  reference games into **its own checkout's** `references/` (`ROOT` = the tree the server was
  started from). Start it from `/tmp`, a scratch worktree, a build sandbox, or any directory that
  can be wiped, and every game the user saves lands there and **dies with it**.
  * **What it cost:** on 2026-09-28 an agent started the viewer from a scratch worktree at
    `/tmp/pd-wt`. The user then played and saved reference games (Angels, WhiteKnights,
    SelesnyaLifegain, Snow) through it for a week. On 2026-10-05 the container was recreated,
    `/tmp` was emptied, and **every reference saved since the last commit was destroyed** —
    unrecoverable, hand-played user work. The user: *"one of the stupidest errors I have seen
    from agents."*
  * **The rule:** run the viewer ONLY from a **persistent, up-to-date checkout** — never under
    `/tmp`, `/var/tmp`, `$TMPDIR`, or any `git worktree` an agent created for scratch work. If
    unsure whether a directory is persistent, it is not: use one that is.
  * **It is GATED:** `tools/play/server.js` refuses to start when its root resolves inside a
    temporary directory. Do NOT add a bypass, and do not work around it by copying the server
    elsewhere.
  * **Commit references promptly.** Whenever an agent sees new or modified files under
    `references/` in the viewer's tree, commit them (and push) at once — an uncommitted
    reference has exactly one copy.
  * Before restarting or killing a viewer, check its tree for uncommitted references and commit
    them first.
  * **HARD RULES added 2026-10-06 (the user asked for "a lot of hard rules to prevent this under
    any situation"). Every one is mandatory; none has a bypass.**
    1. **The viewer runs ONLY from the PRIMARY checkout** (`/workspaces/MagicDeckTester` here) —
       never a linked worktree, never a clone made for scratch work. GATED: `server.js` exits 2
       when its root is a temp dir OR a linked `git worktree` (`tools/play/refsafe.js`). To run
       new viewer code, land it on the branch and fast-forward the primary checkout
       (`git -C /workspaces/MagicDeckTester pull --rebase`), then restart there.
    2. **Before removing, pruning, resetting or recreating ANY worktree or clone, run
       `python3 scripts/check_references_safe.py`** (all worktrees) and commit + push every file it
       lists. Remove worktrees with **`scripts/safe_worktree_remove.sh <path>`**, never a bare
       `git worktree remove` / `rm -rf` (`worktree_audit.py --prune-stale` runs the same scan).
    3. **Commit AND push a new reference the moment it appears** — or confirm it is already on
       `origin/references-autosave` (the scanner checks exactly that). A local commit alone is
       not enough: a container rebuild takes the unpushed branch with it.
    4. **Session-close checklist item:** run `python3 scripts/check_references_safe.py`; a
       non-zero exit means a reference is not safe yet — fix it before ending the session.
    5. **What the viewer does by itself** (defence in depth, not a substitute for 1-4): each save
       copies the file to `$MDT_REFERENCE_BACKUP_DIR` (default `~/.mdt-reference-backups/<repo>/`,
       never overwritten — **in the devcontainer `~` is the container's throwaway overlay, so start
       the viewer with `MDT_REFERENCE_BACKUP_DIR=/home/vscode/.claude/mdt-reference-backups`, a
       persistent docker volume**; only `/workspaces/MagicDeckTester` and the `~/.claude`, `~/.config/gh`
       and `build/` volumes survive a container rebuild — check `findmnt` before trusting any path), commits it to the `references-autosave` branch with git plumbing (your
       working tree, index and branch are untouched) and pushes that branch; at startup it does
       the same for any untracked/modified reference. A failure shows as a RED banner in the GUI
       — treat one as an emergency: commit and push the file by hand. NOTE: in the devcontainer
       `~` is on the ephemeral overlay, so the local backup does not survive a rebuild; the push
       is the copy that does.

- **NO GREEDY PICK INSIDE THE SEARCH WINDOW — the ENGINE is heuristic-free (user hard rule,
  2026-09-30).** The user's words: *"We need to make sure all of them are purged"*, *"I don't care
  whether it is main 1 or 2"*, *"the code that calls that way should be deleted"*, *"Only heuristics
  in the provider are allowed to interfere with the search and there just to prune options."*
  * **Where heuristics may live:** in a `DecisionProvider`, and only to RESTRICT or ORDER options (a
    tested one-option prune such as a fetch target is fine). The engine (TurnSolver, AIEngine,
    SpellEffects, ManaPayment) makes no heuristic choice at any node with search depth remaining.
  * **The only permitted greedy:** the playout policy BEYOND the horizon (remaining depth <= 0), the
    depth-0 runner, and the standing attack / mana-payment exemptions.
  * **Delete, don't lever.** A default-off flag that gates a missing searched capability is the same
    defect. A host that takes a provider's top candidate out of several because it did not branch is
    a greedy pick.
  * **It is gated.** `TurnSolver::Solve()` requires a `GreedyPermit` naming a site from a closed enum
    (`HorizonLeaf`, `D0Runner`); the constructor ABORTS if remaining depth > 0, in every build. Do not
    add a site or a "diagnostic" exemption without the user.
  * **What it has cost when it slipped:** a month of "audit clean" readings missed a live in-window
    greedy (the instrument never counted the playout's main 2). Full history, the step log and every
    worse case: `docs/design/no-greedy-in-search-window.md`, `docs/design/no-greedy-worse-cases.md`.
  * **The adoption bar for a change here:** every slower game must recover at `--depth 8
    --budget-ms 0`, and every DECK's aggregate must be net <= 0 vs the committed tree on a large
    held-out sample (*"+1 is not neutral. 0 is neutral."*; the goal is *"overall better"*, not better
    on the regression seeds).

- **AGENT FAN-OUT IS EXPECTED — do NOT stop to ask before it (user directive, 2026-08-26).**
  Where a skill in this repo says to fan work out across subagents — the analyze-deck Stage 5d
  claude-play sweep, the Stage 2 per-card research fan-out, the Stage 5 verification verdicts —
  **just do it.** The user's words: *"I don't want agents stopping before doing the fan-out."*
  Pausing to request permission, or quietly downgrading a prescribed fan-out to serial work, is
  the failure mode this rule exists to prevent: it strands a mandated verification step
  (`verify_deck.py`'s `claude_sweep` gate) as un-run and hands the user a deck that only *looks*
  analyzed.
  * **Cost is not the constraint it once was.** The user is on a Max 20x plan and has said
    plainly that **cost is not a concern** here.
  * **FABLE is the only genuinely scarce model. Sonnet AND OPUS are both fine for a fan-out.**
    The user's words: *"Sonnet agents are quite cheap. If they were Fable agents it would be a
    different story"* and *"even opus would be fine as part of the fan-out... and I have
    difficulty using the non-fable part of my usage."* So the non-Fable budget is going UNUSED —
    defaulting a fan-out to Sonnet to "save money" saves a resource the user has a surplus of
    while giving up judgement quality. **Pick the model on task difficulty, not on price:** Opus
    for anything whose value IS the judgement (the claude-play sweep's flag verification, card-data
    reasoning, adversarial review), Sonnet for mechanical protocol-following at high volume.
    Reserve Fable for where it is genuinely required.
  * This OVERRIDES the `model: 'sonnet'` pin in `.claude/skills/claude-play.md`'s sweep section,
    whose stated rationale is cost-control ("the chain's most expensive step") — that rationale no
    longer holds. Likewise treat its "~15-20 games, never near 100" as a *signal* guideline, not a
    budget one, and size up when a wider sample buys real coverage.
  * If a session's harness blocks agents/workflows outright, say so explicitly and record the step
    as **UN-RUN** in the per-deck ledger — never as "deferred to the user", which reads as a
    sign-off the user never gave.

- **NEVER BLOCK ON A QUESTION — SURFACE IT AND KEEP WORKING (user directive, 2026-08-31).**
  Asking is not the problem; **waiting for the answer is.** Raise the question in your message
  so the user sees it if they happen to be reading, then **immediately carry on and finish
  everything.** Nothing — no stage, no run, no commit — may sit idle pending a reply.
  * The user's words: *"you must not ask questions until everything is complete"*, and
    *"you can surface the question while other things are running for if the user happens to
    read, but you must not block anything on it."*
  * **Concretely:** put the question in prose, mid-report, and continue in the same turn. Do NOT
    call `AskUserQuestion`, and do NOT end a turn waiting on an answer, for anything that is not
    a genuine blocker (below). Batch the full set of open questions again in the closing message
    so none is lost.
  * This OVERRIDES every "stop and check with the user" instruction in the skills —
    `analyze-deck`'s Tier-4 escalation and deferral approvals, `audit_viewer_decisions.py`'s
    self-guard ("needs the user's OK"), `mulligan-profile`'s and `heuristic-optimization`'s
    adopt-on-approval steps. Those sign-offs are still **required**; they are *collected*
    without ever halting the work.
  * When you hit one: **state the question, take the documented default (or the option you would
    have recommended), say plainly which way you went, and keep going.** A wrong assumption
    costs a rerun of one cheap step. Blocking costs the user their window.
  * **What this cost, and why the rule exists.** 2026-08-31: with 32 cores just freed by the
    Dragons generation and a ~36 h Mirrorwing value-leaf run not yet started, the agent called
    `AskUserQuestion` about which of three `cards.json` params were inert — pure classification
    paperwork that changed nothing about the run — and stopped. The machine sat idle through the
    user's weekend window and it did not come back. *"Wasting this many hours is a massive
    headache for me."*
  * **And note WHICH question it was, because that is the sharpest part of the lesson.** It was
    about **play-viewer decision surfacing** — whether a human should be offered a trigger-order
    choice. Value-leaf and mulligan generation depend on **play correctness**, which the mismatch
    harnesses, play-invariants and claude-play sweep had already passed; they do not read the
    viewer manifest at all. So the question was not merely deferrable, it was **entirely
    unrelated to the work it halted**. Before letting anything wait, ask the one-line test:
    *does this answer change what the pending run computes?* If it does not even touch it —
    which is the common case for classification, naming, docs and viewer paperwork — then
    blocking on it is indefensible, not just suboptimal.
  * The ONLY true blockers are the ones already carved out elsewhere in this file: an action
    that is **destructive or hard to reverse**, and a fork where **either choice would waste the
    run itself** (which deck, which list, which commit to freeze). Nothing else. If you are
    unsure which kind you have, surface it and keep working.

- **A USER-OWNED DECISION IS NEVER SETTLED BY AN AGENT — NOT EVEN "LEAVE IT OFF" (user directive,
  2026-10-04).** Cast order, its range, main-1-vs-main-2, mulligan/keep policy, discard buckets, card
  deferrals: the user decides these per deck. That covers REJECTING, keeping default-off, or quietly
  falling back from something the user asked for, exactly as much as adopting it. A measurement is
  input to the user's decision, never the decision.
  * **What went wrong.** 2026-09-25 an agent measured the user's own Snow cast order, found it a few
    thousandths of a turn worse, wrote *"`MTG_SNOW_CAST_ORDER` stays OFF, measured, not deferred"* into
    `docs/design/snow-intractable-games.md` §7, and never told the user. Snow ran the generic order for
    nine days, until the user asked: *"we are still using the generic order for snow? We shouldn't be."*
    *"I need agents to stop making these decisions unilaterally without telling me."*
  * **How to apply:** state the decision IN YOUR MESSAGE to the user (verdict, numbers, what you would
    recommend), keep working, and repeat it in the closing summary until they answer. A doc line, a
    commit message, or a code comment is NOT telling the user. Until they answer, the user's stated
    intent stands. The current setting is not the default just because it is already in place.
    This is surfacing, not blocking (see NEVER BLOCK above).
  * **When the user's own order/rule measures worse, ROOT-CAUSE IT and propose a fix.** USER: *"If I
    gave you an order you should try to figure out why it doesn't work and maybe suggest
    alternatives."* Find the games it loses, name the mechanism (e.g. which sub-rule, which card
    interaction), and bring back an amended version plus its measurement. Never stop at "it's worse,
    so it stays off", and never fall back to a generic default. *"There is no case where we should do
    that."*

- **NEVER create merge commits — REBASE local work onto origin (user directive, 2026-08-12).**
  The user wants linear history. `git config pull.rebase true` + `rebase.autoStash true` are
  set in this repo; keep them set on any new clone, and integrate remote work with
  `git pull --rebase` (or `git fetch` + `git rebase origin/<branch>`), never `git merge`.
  Never rebase commits that are already pushed — only your local, unpushed commits move.
  Two repo-specific caveats:
  * After a rebase that replayed engine changes, REBUILD and re-run the byte-identity check
    (smoke) before trusting any measurement — same rule as after a merge.
  * If both sides touched `test/regression_gt.txt`, do not hand-resolve numbers: rebase the
    code, rebuild, and regenerate/accept GT under the rebased binary (see the
    gt-rebaseline-rebase lesson — GT is a measurement, not a text file to merge).
  * A rebase SPLITS THE TWO GT HALVES even when every conflict is resolved correctly:
    conflicted files take the side you picked while non-conflicting ones replay yours, so
    `regression_gt.txt` can end up holding one run's aggregate and `gt_logs/*.wins` another
    run's per-game outcomes. Run `python3 test/check_gt_logs.py` after ANY rebase that
    touched GT. Accepting a tier repairs its own keys; for a tier you are not re-running
    (someone else owns it), restore its `.wins` to the side whose aggregate you kept —
    otherwise that tier's next audit diffs against a baseline no binary ever produced.
  * Generation freezes (value-leaf / mulligan artifacts) key on the `HEAD:src` TREE hash, so
    a rebase that lands identical src content keeps a paused run resumable; verify with
    `git rev-parse HEAD:src` against the queue's `freeze.src` before resuming.

- **NEVER wrap commands in a timeout (no `timeout N`, no Bash `timeout` parameter).**
  A timeout silently truncates a run — a partially-finished regression sweep, batch,
  or build reads as a *result* when it is actually cut off, which corrupts A/B
  comparisons and hides real slowness. Let long commands run to completion; if one is
  exceedingly slow the **user** will manually stop it (that choice is theirs, not the
  agent's). This applies to every tool call in this repo: analysis runs, the test
  harness, builds, and ad-hoc scripts.

- **ONLY THE USER cancels a USER-REQUESTED run past ~10 minutes — a question is not a cancel.**
  What this rule protects is a long run **the user asked for**: an overnight sweep, a
  generation job, a rebaseline, anything they set going or told you to start. For those,
  the ~10-minute mark is a **detection deadline**. If you spot a *clear* defect within a
  run's first ~10 minutes (wrong flags, a methodology bug, obviously-corrupt output), you
  MAY stop it, fix the cause, and restart right away — little is lost, and that is the
  right move. **Past ~10 minutes, do NOT `TaskStop`/`kill`/`pkill`/Ctrl-C it for ANY
  reason** — not to "fix" it, not to restart it more efficiently, and above all not because
  the user asked a question about it. A question about a run's progress, CPU use, or
  correctness is NOT a request to cancel it. If you believe such a run is wrong or
  inefficient, **let it keep running and surface a question to the user** (flag the problem,
  propose the fix, note that re-running is their call); the decision is theirs alone. (This
  rule exists because agents kill in-flight runs when the user is only inquiring —
  destroying work the user did not ask to discard.)

  **EXCEPTION — your own experiments.** A run *you* started on your own initiative (a probe,
  a benchmark, an A/B you chose to launch) is yours to kill at any age, no permission needed.
  Nothing is destroyed that the user asked for, and a superseded probe left running is just
  noise: kill it, say you did, and move on. The test is *who asked for it*, not how long it
  has been running. When in doubt about which kind a run is, name it and ask — that is
  cheap; guessing wrong in the protective direction costs only a couple of cores, guessing
  wrong the other way destroys user work.

- **Long / multi-item runs MUST batch into ONE pooled work queue — never a loop of
  many small invocations.** Launching a run as many separate per-item commands (e.g.
  one `mtg` per seed, per profile, or per A/B arm) strands cores on *each* invocation's
  load-imbalance tail, and any serial single-threaded step between them (a rebuild, a
  profile reconstruction, a merge) idles the whole machine — cores sit ~half-used. Pool
  ALL work (every game of every job) into ONE `mtg --batch <manifest>` so the runner
  keeps cores saturated to a single tail. Do reconstruction/prep for every variant
  first, then run one batch over all of them (bake per-variant profiles into the
  manifest's `profile` field rather than re-launching per variant). This is the same
  lesson as the regression harness's per-mode pooling — one tail, not one-per-item.

  **WAVES ARE A LOOP.** Splitting a pooled run into phases/waves, or into one batch per
  arm/deck/variant, re-introduces exactly the defect the rule forbids: every wave is a
  barrier that idles the box until its slowest game lands, and separate pools never share
  threads. This has now failed twice (a per-cell loop: 3 of 24 cores for 15 h; then a
  two-wave per-arm split: **3 of 20 cores for 23 h**, delivering 5% of the job). A barrier
  is only allowed where a genuine data dependency requires it.

  **CHECK UTILISATION IN THE FIRST TEN MINUTES.** `mtg --batch` prints a
  `[batch] heartbeat: N/M workers busy` line every 10 minutes (and a `SLOW-GAME` repro for
  any game over 30 s) — both ON BY DEFAULT, `MTG_BATCH_HEARTBEAT=0` / `MTG_SLOW_GAME_MS=0`
  to silence. If that line is not near M/M, stop and fix the scheduling before letting the
  run continue; do not reach for an engine explanation first. Both times the box was
  starved, the cause was diagnosed as something else (an engine regression, a tractability
  guard) while the heartbeat number said plainly what it was. Anything that runs work
  without going through `--batch` therefore has NO utilisation reporting — which is one
  more reason the pooled queue is the only route.

- **COLLAPSE WASTED SEARCH UNCONDITIONALLY — a collapse does NOT have to buy wall to be worth
  keeping (user directive, 2026-10-01).** The user's words: *"I do indeed want to collapse
  unnecessary search as much as possible even if it causes us to search further. Wasted work is
  wasted work regardless of the situation."*
  * **Freed budget being spent on more depth is not a reason to withhold the collapse.** Narrowing
    a menu, folding a symmetry, or deleting a dominated branch very often shows up as ~0% wall,
    because the search simply goes deeper inside the same budget. That is the budget being spent
    *better*, not the collapse failing. Judge a collapse on **work removed and soundness** — never
    on whether wall fell. Do not report a sound collapse as though a small wall number were a
    verdict against it.
  * **The SECOND reason is diagnostic, and it is the sharper one:** *"Another important reason to do
    this collapse is to allow us to more accurately locate remaining issues in our branching."*
    Every unnecessary branch is noise in the enumeration/odometer census, so each collapse sharpens
    the signal that finds the *next* defect. A collapse worth 0.2% of wall that makes the census
    legible has already paid for itself. This repo has demonstrated it: pooling the interchangeable
    sac outlets took the worst odometer shape **4.61e+03 -> 2.02e+03 and `ind` -> 0**, and that is
    what made the remaining shape readable at all.
  * **The method is iterative and it TERMINATES IN A DIAGNOSIS:** *"We should just keep pruning
    anything that seems unreasonable and then the any remaining unreasonable items should be more
    obvious. Once all of those are gone the expectation would be that whatever remains is not
    branching related."* So keep pruning the unreasonable, and do **not** stop because one prune
    measured small. When nothing unreasonable is left, the residual cost is **per-node** rather than
    branching — that is the point at which allocation/evaluation profiling (`operator new`,
    `~vector<Action>`) is the right next move, and not before.
  * **Do NOT add complexity whose purpose is to dodge a depth increase:** *"I don't want to
    duplicate work or overcomplicate search just to prevent us from going to the next depth. If we
    find that going to the next depth is not worth it, (the quality gain is not worth the
    performance) then we should change our settings so that we don't go there."* Depth economics are
    a **settings** question — the depth matrix and escalation config — not a search-hack question.
    Recomputing something redundantly, or bolting on a second estimator, so the search can decide a
    ply earlier is the forbidden shape.
  * **What this does NOT license.** A *lossy* narrowing is still a quality question and still needs
    its A/B: "it removes work" never excuses dropping a line that might be best. The distinction
    that matters is **sound dominance / identity fold** (collapse freely, default ON) vs
    **projection / heuristic narrowing** (earn it, default OFF until measured).
    `MTG_FUNGUS_DEVOUR_LETHAL` is the first kind — a proven kill makes `k = own` dominant — and is
    ON despite measuring 0.2%. `MTG_FUNGUS_DEVOUR_LANDMARKS` is the second — eating 3 of eight
    identical N/N tokens really does differ from eating 5 — and stays OFF until a quality A/B.

- **A DECK MUST BE IN THE REGRESSION SUITE BEFORE THE VALUE LEAF OR MULLIGAN PROFILE (user
  directive, 2026-09-26).** Adding the deck to `test/regression_cases.sh` is a **required step of
  the analysis**, and both generators now **refuse to start** (exit 3) without it:
  `scripts/valueleaf.sh run` checks per deck before phase 0 freezes anything, and
  `scripts/mullgen.sh run` checks before it truncates its report or plays a game. The shared check is
  `python3 scripts/suite_gate.py --require decks/<Deck>`.
  * **It is a gate because a note did not work.** `verify_deck.py` already disclosed *"<deck> is NOT a
    regression case, so NO digest tracks its play"* — as a 6a disclosure nobody had to act on, and it
    was duly skipped. Worse, `mullgen.sh`'s own regression step silently logs `not in
    test/regression_cases.sh -- skipping (nothing to move)` and returns 0, so a profile can report
    **VALIDATION PASSED** having run one fewer check than every other deck gets. WhiteKnights shipped
    exactly that way on 2026-09-26.
  * **Why it must come FIRST:** a value leaf and a keep table are *fitted to the deck's play*. With no
    suite cases there is no ground truth, so no engine change can ever be shown to have broken that
    play — the hours go into artifacts whose foundation nothing is checking.
  * **THE 3x COST RULE.** A deck may be added only if its tested cost is **≤ 3x the most expensive deck
    that already has BOTH a value leaf and a mulligan profile** (often Hinata). Check with
    `suite_gate.py --cost decks/<Deck>`; the table is `test/suite_cost.json`, refilled by
    `--measure-all` from **one pooled tier run** (never a per-deck loop — that is the "waves are a
    loop" defect). The gated number is per-game core-ms at the deck's worst *searched* case, not total
    tier time, because total is confounded by the game counts we choose and so is gameable by sizing.
  * **If it is over 3x: do NOT add it and do NOT skip the suite.** Report it to the user at the end;
    **getting the deck into that range becomes the first goal**, ahead of both generators. An
    intractable deck is a performance problem to fix, not a deck to quietly exempt.
  * `MTG_ALLOW_UNTESTED_DECK=1` overrides. That is a **USER** decision, never an agent's — an agent
    that hits either gate reports it and stops.

- **GENERATION STAGES ARE STRICTLY SERIAL, IN ORDER, AT THE RIGHT SETTINGS (user directive,
  2026-08-31).** The per-deck pipeline is **profile → value leaf → mulligan**, and each stage —
  including a *scout* or `recommend` probe — runs **alone on the box, only after the previous
  stage has finished, and at the settings that stage will really use**. Never two generations at
  once, not even "just a quick probe alongside".
  * **It is a DEPENDENCY, not merely scheduling.** The mulligan generator reads its depth and
    budget from `value_play` (`mull_gen_depth` / `mull_gen_budget_ms`, and `expected_buckets`) in
    `<deck>.value.json` — a file the **value leaf's final stage writes**. Run before that and the
    gen silently inherits the *play* depth and measures a run nobody would ever do. It cannot be
    worked around by hand-writing a `.value.json` either: **sidecar PRESENCE activates the
    value-leaf hybrid in play**, so creating one mid-generation changes the very play the value
    leaf is fitting.
  * **THE VALUE LEAF CHANGES THE PERFORMANCE YOU ARE MEASURING — this is the main reason.** The
    value leaf replaces the search's horizon rollout with an O(1) evaluator, and the H-cell ladder
    is guarded on the sidecar EXISTING (a missing model silently costs **1.35–84.8x**, per
    `value-leaf.md`). So a mulligan gen or probe run *before* the value leaf is timing the SLOW
    path — the deck as it will never be shipped. Its projection is not merely noisy, it can be
    wrong by more than an order of magnitude, and always in the pessimistic direction. Any
    feasibility verdict reached that way ("this deck's mulligan is too expensive") is worthless
    and must not be recorded as a deferral.
  * **Contention makes the output WRONG, not just slow.** A `recommend` probe's deliverable IS a
    wall-clock projection, so measuring it on a shared box produces a number that cannot answer
    the question it was run for. Same for any timing-based estimate.
  * **A generation can go LIVE mid-run and corrupt its neighbour.** Keep tables and value sidecars
    are presence-gated — the file existing IS adoption — so a mulligan gen dropping a
    `keepmodel.exhaustive.profile.json` beside the decklist changes play *while* a value leaf is
    being fitted to that play. (`recommend` writes no profile, which is the only reason the
    2026-08-31 incident was recoverable.)
  * **What went wrong.** 2026-08-31, Mirrorwing: a mulligan `recommend` probe was started
    alongside a running value-leaf generation *and* before it finished — so it was wrong three
    ways at once (contended, at inherited depth 5 instead of the deck's d3/b3, and reading
    settings the value leaf had not written yet). It measured 8 rollouts/s and produced a
    projection inflated on every axis. The user: *"We shouldn't be running it with the value leaf.
    Those should absolutely be serial."*

- **After pushing platform-sensitive code, WATCH CI and report the Windows result.**
  `.github/workflows/build.yml` builds on **ubuntu-latest AND windows-latest** on every push
  (any branch) that touches `src/**`, `test/unit/**`, `CMakeLists.txt`, `CMakePresets.json`,
  `cmake/**`, `build.*`, `play.*`, or `tools/play/**`. You cannot verify MSVC from the Linux
  container, so CI is the only Windows signal — and **push CI notifies, it does not block**
  (required status checks exist only on PRs, and this repo works on shared branches). It is
  therefore on YOU to look:
  ```
  gh run watch                                  # blocks until the just-pushed run lands
  gh run list --branch "$(git rev-parse --abbrev-ref HEAD)" --limit 3
  gh run view --log-failed                      # on a red run
  ```
  Report the per-OS outcome in the same message as the push. Do NOT rely on email: pushes
  authenticate as `dtippett-bot`, so GitHub's failure mail goes to the bot's mailbox, not the
  user's. The job that matters most is **determinism parity** — it asserts Linux and Windows
  produce the same result for the same seed, which is what `src/core/Library.h`'s open-coded
  MSVC shuffle exists to guarantee. If it goes red, root-cause it; never rebaseline over it.

- **WORKTREES LIVE UNDER `.claude/worktrees/` (the persistent host mount), NEVER `/tmp` (user
  directive, 2026-10-06).** `/tmp` and `/workspaces` are the container's overlay filesystem: a
  stopped or replaced container takes them with it. Only the repo itself
  (`/workspaces/MagicDeckTester2`, a 9p mount of the host folder) survives, so a worktree is
  retrievable later ONLY if it is under the repo: `git worktree add .claude/worktrees/<name> ...`
  (gitignored; the agent harness already uses it). The user's words: *"having a location where they
  can be retrieved is important as sometimes the container is stopped or even replaced."*
  * **What it cost:** on 2026-10-05 another agent ran the play viewer from a worktree under `/tmp`;
    the hand-played references it saved were lost with the container. The viewer now refuses to
    start from a temporary root (`c7487fb9`), but that guard covers the viewer only -- a worktree
    holding unpushed commits or uncommitted edits under `/tmp` is just as exposed.
  * **Clean up occasionally, but only what has NOTHING TO ADD** (the user's words: *"we probably do
    want to occasionally clean them up if there are old outdated worktrees with nothing to add"*).
    `python3 scripts/worktree_audit.py` is the test, made explicit: a worktree is STALE only when it
    is clean (no modified/untracked files), every commit is on origin by hash OR on the main branch
    by patch (`git cherry`), no process is working in it, and it has not been touched for 2 days.
    Everything else is KEEP with the reason named. `--prune-stale --yes` removes STALE ones with
    `git worktree remove` (the branch stays; it is retrievable from the remote, which is what the
    test guarantees). Removing a worktree is destructive for whichever session owns it, so run the
    prune only when the user asks, and show the dry-run list first.

- **Log/output directories go under `logs/` (or `test/logs/`), never the repo root.**
  Any script or command that writes game logs, batch output, or A/B scratch must
  target a subdirectory of `logs/` (e.g. `logs/fd_quick`), not a root-level
  `logs_*` directory. This keeps the repo root uncluttered. Both `logs/` and
  `logs_*/` are gitignored, so this is purely about tidiness, not tracking.

- **NEVER COMMIT SCREENING / CANDIDATE LISTS — they are not final lists (user directive, 2026-09-17).**
  A deck-screening or list-settling run produces many trial decklists (`<Deck>A`, `<Deck>B`, …). These
  are *scaffolding*, not decks. Do **not** create `decks/<Deck>X/` folders for them and do not commit
  their artifacts. `decks/` is for **shipping lists only**.
  * The user's words: *"we don't need the other versions anymore. Only the original and the new list
    should be present"*, and *"We shouldn't generally commit these lists as part of the process
    either. Because they are not final lists."*
  * **Why it matters beyond tidiness:** `tools/play/server.js` `listDecks()` enumerates EVERY
    directory under `decks/` as a selectable deck, so eight screening candidates become eight bogus
    entries in the play viewer's dropdown, indistinguishable from real decks. It cost a cleanup and a
    1.1 GB disk reclaim on 2026-09-17.
  * **Where trial lists DO belong:** keep them under `logs/` (gitignored) for the duration of the
    screen — `scripts/deck_compare.py` already pools its apparatus under `logs/deckcmp/<Deck>/`.
  * **When a list IS adopted**, it lands in `decks/<Deck>/` and its predecessor is archived as
    `decks/<Deck>/v<N>-<slug>/` (the `CritterLifegain/v1-thune4-basilica`, `Mirrorwing Dragon/
    v1-twinflame-anger` convention). **Move that list's references too** —
    `references/<Deck>/v<N>-<slug>/` — because a reference belongs to the list it was played on
    (server.js: `refsOnArchivedList`). Leaving them at top level silently credits the OLD list's
    hand-played games to the NEW one and lends it a green bench it never earned.

- **A GENERATION JOURNAL IS DAYS OF COMPUTE AND THE ONLY COPY — COPY IT ASIDE BEFORE YOU RUN
  ANYTHING NEAR IT (user directive, 2026-10-02).** The user's words, after the loss below:
  *"we need to absolutely stop this from EVER HAPPENING AGAIN"*, and on the remedy: *"Disk space is
  cheap. Losing work like this is extremely expensive."*
  * **What happened.** A 350 MB / 5.58M-record Fungus candidate-b keep journal was destroyed by
    launching `mullgen.sh run` with the **wrong recipe**: the journal was rolled at `fast` (**R=30**),
    the run was launched as the default `complete` (**R=40**). The in-engine resume gate refused it
    exactly as designed — and the refusal then **truncated** it, ten minutes later, behind bucket
    discovery, into an append-mode `gen.log` nobody reads live. Journals are gitignored, so there was
    no second copy anywhere. Unrecoverable.
  * **`MTG_KEEP_RETAIN_FOREIGN` DOES NOT COVER THIS, and assuming it did is what made the mistake.**
    It admits a foreign **play digest** only. The resume gate is a short-circuit chain —
    `bucket_fp && deck_fp && seed_base && K && max_mull && equiv_seed && R && PlayIdentityAllows(...)`
    — so a mismatched **R** fails several links *before* the check that flag overrides, and the flag
    is never consulted. **Read the journal's own header first** (`head -c 1200 <journal>` prints all
    of K, R, bucket_fp, deck_fp, play_digest, commit, depth, budget_ms, max_mull, seed_base) and
    **match the recipe to its R: `fast` = 30, `complete` = 40.**
  * **A gate refusing is NOT proof the artifact is dead — read WHICH field refused.** Here it was
    resumable; just not by the recipe that was asked for.
  * **The mechanical protections now in place (do not rely on them instead of the rule above):**
    `mullgen.sh run` copies any existing journal aside *before anything runs* and **refuses to start**
    on a recipe/depth/budget mismatch, naming the recipe that would resume it; the generator writes
    **automatic rolling backups** (`.journal.bak1`/`.bak2`, `MTG_JOURNAL_BACKUP_S`, default 900 s) so
    a loss is bounded to minutes; and the resume gate now **renames a rejected journal aside instead
    of truncating it**, exiting rather than truncating if that rename fails.
  * **The agent-side rule that still matters, because tooling cannot cover every path:** before any
    command that could write near an irreplaceable artifact, **`cp` it aside first**. One inode
    operation against days of compute is never a close call. And **`MTG_KEEP_RETAIN_VERIFY` is
    diagnostic-only** — it returns *before* the journal is opened, so it must never be set on a real
    generation run, and it cannot serve as a cheap pre-flight because it sits behind discovery.

- **Each deck lives in its own folder under `decks/`, not the repo root.** The
  per-deck folder layout is `decks/<name>/` holding the decklist
  (`decks/<name>/<name>.txt` or `.cod`) plus its generated profile
  (`decks/<name>/<name>.profile.json`) and sibling models (`.value.json`,
  `.eval.json`, `.keepmodel.exhaustive.profile.json.gz` + `.raw.json.gz`). The
  analyzer writes the profile next to the deck (directory-relative), and the
  engine resolves every sibling model directory-relative off the profile path.
  Reference decks by their folder path (e.g.
  `scripts/analyze_deck.py decks/<name>/<name>.cod`); the regression harness's
  `DECK_FILE`/`DECK_PROF` maps in `test/regression_cases.sh` already point there.
  See `docs/design/per-deck-folder-layout.md` for the layout rationale and the
  raw-artifact policy (commit the gzipped `.raw.json.gz`; never the uncompressed
  raw — it's gitignored).

- **Reference games under `references/` are COMMIT-ONLY — never revert, discard,
  overwrite, or delete them.** The files in `references/<deck>/claude_s*_gi*.json`
  are user-owned, hand-played ground-truth games that represent real work the user
  saved deliberately. An agent may ONLY *commit* them (to protect them from loss);
  an agent must NEVER run `git checkout` / `git restore` / `git reset` / `git clean`
  or any other command that discards changes to a file under `references/`, and must
  never overwrite or delete one. If a reference shows as modified or untracked in
  `git status`, **commit it** — do not revert it, and do not assume a change was
  accidental (the user may have re-saved it via the play viewer). Only the user
  decides to change or remove a reference. This rule exists because reverting a
  re-saved reference already destroyed unrecoverable user work once.

- **Deferred work goes in `docs/design/`, not private agent memory.** If a
  project, plan, or idea is *deferred* — i.e. not being worked on right now — write
  it as a self-contained `docs/design/<name>.md` (see `mana-source-reservation.md`
  for the shape). The deferral is the sole trigger: do NOT reason about whether
  another agent will need it — a deferred item belongs in git and is available to
  everyone by default, because per-agent memory is not shared between agents or
  machines. Keep such docs standalone (no references to any agent's private notes).
  Private memory is only for a single agent's own continuity across compaction / a
  new session (personal working prefs, resume hooks), never for parking deferred
  project state.

## Coding Conventions Skill

Before **adding or changing an env flag (`MTG_*`), a debug toggle, or an A/B lever**, read
`.claude/skills/coding-conventions.md`. The one-line version: every boolean flag is read via
`EnvOn("MTG_X")` / `EnvOn("MTG_X", true)` from `src/core/EnvFlags.h` — `=0` always means off,
`=1` always means on; never write a presence-only `getenv(...) != nullptr` truthiness read
(that convention once made `MTG_X=0` mean ON and silently corrupted an A/B arm). Flags read
by both executor and rollout get one shared reader in `src/ai/EngineFlags.h`.

## MTG Rules Skill

This project has a custom skill at `.claude/skills/mtg-rules.md` that **all agents working in this repository must use**. It is the authoritative reference for both MTG rules correctness and implementation patterns.

The skill is not an invokable slash command. Access it by reading the file directly:

```
Read `.claude/skills/mtg-rules.md` and [answer / implement / review] ...
```

It covers four modes of use:

| Mode | Example prompt |
|------|---------------|
| Rules question | "Read the skill and answer: how does the legend rule work?" |
| Code review | "Read the skill and review this implementation for rule violations." |
| Build guidance | "Read the skill and give me patterns for implementing the stack." |
| Card implementation | "Read the skill and implement this card: [oracle text]" |

### When to consult the skill

- **Before implementing any MTG game mechanic** — read the skill for correct data models and patterns; do not implement from memory.
- **Before implementing any specific card** — read the skill and provide the card's oracle text to get the correct ability type, timing, and targeting structure.
- **After implementing any MTG logic** — read the skill and review the code for rule-violation bugs before committing.
- **When a rules question arises during development** — read the skill rather than relying on training data; edge cases (layer system, state-based actions, replacement effects) are subtle.

### Key correctness areas the skill covers

The skill contains detailed rules and implementation guidance for the areas most commonly implemented incorrectly:

- Stack resolution and priority passing
- State-based actions (must run after every event, not just end of turn)
- Combat damage assignment including trample + deathtouch interactions
- Summoning sickness tracking across turns
- Zone transitions (objects become new objects when changing zones)
- The layer system for continuous effects
- Triggered vs. replacement effects (replacement effects do not use the stack)

## MTG AI Skill

This project has a second custom skill at `.claude/skills/mtg-ai.md` covering the AI engine: decision-making, board evaluation, game logging, and deterministic seeding. It builds on top of the rules skill.

Read it before implementing any AI decision logic, the game log format, the shuffle/seeding system, or opponent behaviour.

```
Read `.claude/skills/mtg-ai.md` and [implement / design / review] ...
```

| Mode | Example prompt |
|------|---------------|
| AI decisions | "Read the skill and implement the spell selection logic for the goldfishing AI." |
| Game logging | "Read the skill and implement the game logging module." |
| Seeding | "Read the skill and implement card numbering and deterministic shuffle seeding." |
| Phase 2 planning | "Read the skill and outline what changes when we add a real opponent." |

### When to consult the skill

- **Before implementing any AI decision point** — read the skill for the heuristic ordering and evaluation approach.
- **Before designing the game log format** — the skill specifies the required structure and disk-cleanup policy.
- **Before implementing shuffle or random event logic** — the seeding contract between d1 and d2 is non-obvious; read the skill first.
- **When considering Phase 2 (opponent AI)** — the skill flags where encoded logic becomes impractical and prompts a discussion.

## Deck Analysis Skill

When the user asks to **analyze a deck**, **add a new deck**, or **run the simulator on a deck file**, read `.claude/skills/analyze-deck.md` first. It describes the full three-stage workflow:

1. **Coverage check** — run `scripts/analyze_deck.py --coverage-only` to find missing cards and implementation gaps
2. **Implement & review** — use the MTG Rules skill to implement missing cards, review each one, write to `cards.json`
3. **Analyze** — run `scripts/analyze_deck.py` to build and run the C++ simulator

This workflow requires no external API calls — all generation and review happens in the conversation.

## Regression Testing Skill

When the user asks to **run regression tests**, **smoke/overnight test**, **A/B a change**, **update/rebaseline ground truth**, or **add a deck to the test suite**, read `.claude/skills/regression-testing.md` first. It is the authoritative guide for the `test/` harness.

```
Read `.claude/skills/regression-testing.md` and [run / accept / A/B / extend] ...
```

Key points it covers: the three modes (smoke < 15 min, regression < 45 min, overnight < 8 h) with disjoint seeds; reading the `<games_won>/<avg_win_turn>` fingerprint and per-case logs/timings; the **accept flow** (`regression.sh <mode> --accept` promotes an inspected run into ground truth — never hand-edit or re-run to regenerate); using the suite itself as the A/B harness; and how to add a deck within the shared per-mode time budgets.

## Heuristic Optimization Skill

When the user asks to **optimize / tune / improve a decision heuristic**, **try different orderings or weights**, or **make a decision empirically "searched"** (offline variant testing, not in-play search), read `.claude/skills/heuristic-optimization.md` first. It is the authoritative guide for AI-driven, empirically-measured optimization of the engine's judgment heuristics — the choices the in-play search can't cover (mana-source tap order, cast order, attack/block shortcuts).

```
Read `.claude/skills/heuristic-optimization.md` and [propose variants / sweep / adopt] ...
```

**Rule 0 it enforces:** this is for HEURISTIC judgment (no correct answer, only measurably-better), NOT correctness/modeling bugs — if the engine models a card or rule wrong, that is a bug to fix against the MTG Rules skill, not a heuristic to tune. The skill drives the loop: AI authors motivated variants, exposes them behind a temporary runtime selector, sweeps the regression suite (train seeds) for win%/avg-win-turn, validates the winner on held-out (overnight) seeds, **reports the decision to the user**, and adopts only on approval — in the archetype provider, never the root. The value proposition: the alternative is a human inventing every ordering/constant; here AI proposes and the harness decides. Its worked example (measurement refuting an intuitive "rank Grove last" simplification) shows why you measure instead of assume.

## Value-Leaf Skill

When the user asks to **build / generate / regenerate / evaluate / adopt a value-leaf (value sidecar)
model** for a deck, read `.claude/skills/value-leaf.md` first. The value leaf is a learned O(1)
evaluator that replaces the search's horizon rollout.

```
Read `.claude/skills/value-leaf.md` and [build / measure / adopt] the value leaf for <deck> ...
```

**The whole interface is one command** — `bash scripts/valueleaf.sh run decks/<Deck>`
(and `status decks/<Deck>` for progress). It handles a brand-new deck and a regeneration identically,
runs all five phases pooled, resumes incrementally, and stages everything without adopting anything.
**Do not hand-roll the phases and do not add knobs**: there is ONE route (incremental batching, no
condemnation at d<=5, profile always attached), fixed inside the script. Hand-rolling is how the
profile-less-measurement bug, the silent H-cell perf cliff, and a run stuck at 3 of 24 cores all
happened. Rule 0 it enforces: generate on ONE frozen commit (artifacts are
engine-state fingerprints). Two traps it documents: sidecar PRESENCE activates the hybrid in play
(`enabled: false` is NOT off — ship a rejected model as `<stem>.value.DISABLED.json`), and the
H-cell ladder is guarded on the sidecar EXISTING, so a missing model silently costs 1.35–84.8x.

## Deck-Combination Screening Skill

When the user asks to **compare deck combinations / ratios / counts**, **try N of card X instead of
M**, **A/B a card swap within an already-implemented pool**, or **screen deckbuilding changes
quickly**, read `.claude/skills/deck-screening.md` first. It is the authoritative guide for the
per-COMBINATION loop, as distinct from adopting a combination as a deck (hours, via
`mulligan-profile.md` + `value-leaf.md`).

```
Read `.claude/skills/deck-screening.md` and [screen / floor-check / interpret] ...
```

**The whole interface is one command** — `python3 scripts/deck_compare.py <spec.json>` (plus
`--preflight` for the checks that need YOU, `--with-floor <tags>` to bracket the apparatus bias in the
same batch, and `--confirm <tag>` to re-measure a multi-arm screen's winner on held-out seeds). **Rule 0 it enforces:** do NOT regenerate per-deck artifacts per combination — every arm
shares ONE apparatus, which is not a cheap approximation but the *better* measurement (sharing one
keep table halves the se). Its traps: the deck's `profile` must be attached to the measurement **and**
to any keep-table generation **and** on the table-drop path (a profile-less gen silently fits the
table to a deck we do not ship — it merged a mana source that provably cannot cast the deck's key
artifact), and `t` alone settles nothing — an effect must clear the measured bias floor.

**Introducing a card the deck has never held is in scope, and it is where the AI's work is.** The
driver refuses a card that is not in `cards.json` or whose implementation has gaps, and prints the
`analyze-deck` + `mtg-rules` route; it then pools a `card_scores` entry for the new card
automatically, because an unscored card is scored as an *empty slot* and that penalty falls only on
the arm that plays it. What no guard can do is notice that the engine models one side of a comparison
more completely than the other (a `[bracket note]` in a card's `oracle_text`) — say so in the report.

## Mulligan Profile Generation Skill

When the user asks to **generate / regenerate / pool / A-B / adopt a mulligan (keep or bottom) profile**, or to **hand profile generation to the secondary machine**, read `.claude/skills/mulligan-profile.md` first. It is the authoritative guide for the **exhaustive bucketed mulligan profile** — the separate, expensive, hand-off-able mulligan stage (distinct from `analyze-deck`, which does cards/coverage/play).

```
Read `.claude/skills/mulligan-profile.md` and [feasibility-check / generate / merge / A-B / adopt] ...
```

**Rule 0 it enforces:** generate **late, on a frozen commit** — generation is expensive *and* commit-bound (the raw sidecar's `commit` fingerprint gates cross-machine pooling; a later play-logic fix invalidates prior sidecars). Only generate once cards are implemented, reviewed, and play is validated. It also covers the three mulligan tiers (defaults → low-R exhaustive keep → high-R exhaustive, static skipped), the feasibility pre-check, the multi-machine handoff/merge protocol (parity fingerprints + determinism handshake + seed allocation), and the clairvoyance-vs-R-noise attribution method.

**BOTTOMING IS ALWAYS ON. It is not a decision, and there is nothing to report about it.**
Generation bakes `bottoming_enabled=true` unconditionally and **there is no off switch** — every
keep table in the repo reads true. Do not describe shipping a profile with bottoming on as a
choice, a departure from a default, or something the evidence "permitted": it is the only thing
that can happen. If a confounded bottoming A/B comes back bad, the response is **raise R or fix
the heuristic**, never disable bottoming. `mullgen.sh run` already runs both A/Bs and the artifact
check (which *fails* on `bottoming_enabled != true`) as one command, so this needs no agent
attention at all. The single exception is internal to the harness: `test/keepmodel_exhaustive_ab.sh`
uses `MTG_EXHAUSTIVE_BOTTOM` to isolate the halves — `KM_MODE=keep` pins it off on *both* arms to
measure keep alone. That is the harness's business, not a knob to reach for.
*(This paragraph replaces a stale "ships off until a validated high-R run" summary that outlived
the policy by months and caused a shipped adoption to be hedged as though enabling bottoming had
been an agent's call.)*

## Claude-Play Runner Skill

When the user asks to **run the claude-play oracle / claude runner**, **have Claude play a deck**, or **sweep games with Claude to find bugs/misplays**, read `.claude/skills/claude-play.md` first. It is the authoritative guide for the opt-in `--claude-play` mode (a Claude agent drives main-phase decisions for verification).

```
Read `.claude/skills/claude-play.md` and [play / sweep / verify a flag] ...
```

**Rule 0 it enforces:** always read the *deck's* cards from `src/cards/data/cards.json` (mana cost, P/T, oracle text, parameters) before reasoning about or flagging any card — Claude's card recall is unreliable, and unverified flags are usually card-data mistakes. It also covers the stateless-replay protocol (`--choices`/`--reveal`, exit-70 decision dumps), how to play competently, what counts as a real bug flag vs a false positive, and how to run a sweep comparing Claude to the search.
