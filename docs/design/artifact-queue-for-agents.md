# Running the artifact refresh queue: a guide for agents

For any agent asked to run, join, watch or land work from `scripts/artifact_queue.py` on any machine.
For how the tool works and why, see `docs/design/artifact-staleness-queue.md` (section "2026-10-23: what was
built"). This page covers what to do, where to look, and what to leave alone.

## NOTICE 2026-10-25: update the tool before your worker claims its next deck

**UPDATE 14:55 UTC: the minimum is now the commit that adds this paragraph.** The 1d1cd04ec marks fix was
incomplete: `assess_all` reloaded the config from the frozen tree (no config there), so USER marks were still
dropped and a worker took a drift-ranked deck (Dragons) ahead of marked Angels/Giants. Pull before the next claim.
Also: a worker started from a VS Code terminal inherits that window's git credential socket, and a window
reload makes every push fail (`could not update artifact-queue after 8 attempts`). Start workers with
`GIT_CONFIG_COUNT=2 GIT_CONFIG_KEY_0=credential.helper GIT_CONFIG_VALUE_0= GIT_CONFIG_KEY_1=credential.helper
GIT_CONFIG_VALUE_1='!gh auth git-credential'` so they use the gh token instead.

Every worker started before **6e42ca948** (`origin/artifact-queue-tool`) runs code with two bugs, and every
frozen generation tree synced before **13a3ac81e** (`origin/phase-1-2-deck-analyzer`) has a third. **Do NOT stop
or kill a run in progress:** a generation that has already run for hours is a real one and is not affected.
Fix the NEXT job:

1. `python3 scripts/artifact_queue.py stop` now. It only takes effect between jobs; the current one finishes.
2. When the worker has exited: `git pull --rebase` in the tool checkout (to 6e42ca948 or later), then
   `python3 scripts/artifact_queue.py run` again, harness-tracked, with a new `kill -0` waiter. The worker re-syncs
   its frozen tree to the engine branch, which picks up 13a3ac81e.

The bugs:
- **The USER's marks were ignored (1d1cd04ec).** `cmd_run` loaded `scripts/artifact_queue.json` from the frozen
  worktree, which does not have it, so it fell back to defaults and the Angels/Giants marks never applied.
- **A regeneration could fake-resume the OLD table (6e42ca948).** `ungzip_incumbent` unpacked the incumbent's
  `raw.json.gz` to the plain `<stem>.keepmodel.exhaustive.raw.json`. That is the generator's floor-checkpoint
  path. When play had not changed, every fingerprint matched and the generator "resumed" the old table: about 3
  rollouts per size-7 cell, 0 refine waves, done in minutes, and then judged against the incumbent. Colossus
  Hammer did this on 2026-10-25 (5 min, "REJECTED +0.100 t"). Its claim is released; **do not land
  `artifact-refresh/colossus_hammer-20261025-0245`**. Any result from the old tool whose mulligan stage took
  minutes, not hours, is suspect for the same reason. Check its gen log for `refine` waves before trusting it.
- **Bucket reuse blew the 20 GB RSS cap on a large table (13a3ac81e, engine).** `MTG_KEEP_BUCKETS_FROM` parsed
  the whole incumbent table into a DOM (FiveColour's is 1.8 GB). It now keeps only the bucket lists (4 MB RSS).
  Tables of a few hundred MB, Hinata2 and Snow included, stayed under the cap anyway.

The bucket rulings landed today (USER) are in each deck's `<stem>.buckets.json`:
- Angels and Giants: keep_apart.
- KittyEquipment, Dragonstorm and Galadriel: merges, with Galadriel pinned at K=9.
- Hinata2's Remand merge is **confirmed correct** by the USER, so the in-flight Hinata2 regeneration that reuses it
  is fine. Its ruling file is written after that run lands, because a policy change moves `bucket_fp`.

## Why it exists, and what that means for how you work

The USER's framing (2026-10-24): inference budget is the scarce resource and CPU is the free one. The queue
is meant to be **high-CPU, low-inference**: a detached worker spends hours of cores on regenerating keep tables
and value leaves, and the agent's part is a few tool calls to set it up and a short check every two hours. The
USER could have waited for the morning to launch anything. They started it at once on purpose, because *"the
more testing the better"*: every hour the worker runs is real exercise of a generation path that had never run
before 2026-10-24.

So, while a worker runs:
- **Do not poll.** One `run_in_background` waiter on the PID plus one 2-hourly session-only timer is the whole
  watch (below). No sleep loops in the foreground, no checking every few minutes.
- **Read tails, not files.** `tail -20` of a log answers almost every status question; `worker.log` and the
  drivers' logs grow to thousands of lines.
- **No fan-out, no side investigations** on a healthy run. If something looks wrong, check the one log that
  would show it, then act or report.
- **Report briefly**: pid alive, deck/stage, latest heartbeat line, and anything abnormal.

## Where to look

| What | Where |
|---|---|
| The tool | `scripts/artifact_queue.py` + `scripts/artifact_queue.json`. **Until this branch lands, they exist only on `origin/artifact-queue-tool`**, not on `phase-1-2-deck-analyzer`: check that branch out (or a worktree of it under `.claude/worktrees/`) to get them. |
| The design, claim protocol and open USER questions | `docs/design/artifact-staleness-queue.md` |
| What is stale, ranked | `python3 scripts/artifact_queue.py status` (git reads only, ~1-2 min on the 9p mount); `explain <deck>` for one deck and its regeneration plan |
| What every machine is doing | `python3 scripts/artifact_queue.py claims` -- reads origin branch `artifact-queue`, one `claims/<deck>.json` per deck (worker id, state, heartbeat, run id) |
| One worker's live state | `logs/artifact_queue/<worker-id>/` **in that machine's checkout** (not pushed): `worker.log` (the job sequence), `build.log`, `<deck>.mulligan.log` / `<deck>.valueleaf.log` (the drivers' own output, including `[batch] heartbeat: N/M workers busy`), `inflight.json` (the job being resumed), `worker.pid` |
| The worker's frozen tree | `.claude/worktrees/artifact-worker-<worker-id>` (journals, `.PREV`, quarantined `.DISABLED` tables stay here) |
| Finished results | branches `origin/artifact-refresh/<deck>-<date>` |
| Verdicts (adopted / kept-incumbent) | `test/artifact_ledger/<deck>.json`, carried on each result branch |

The worker id comes from `MDT_WORKER_ID`, or else is minted once per checkout into
`logs/artifact_queue/WORKER_ID`. The first worker is `MagicDeckTester2-76d2cc` (the `MagicDeckTester2`
checkout, started 2026-10-24 13:06 UTC, first job StompySurprise).

## Joining from a new machine

1. **A persistent checkout** (the worker refuses a temp root) with push access to origin, on
   `artifact-queue-tool` until it lands.
2. **One worker per box, and nothing else generating.** Generations are strictly serial (CLAUDE.md). Before
   starting, confirm no `mtg`/`mtg-analyze` is running and no other session's worker owns the box. The idle gate
   sees this container's processes plus the 1-minute load average (VM-wide under WSL2), so it will usually
   catch a sibling container's generation, but not always: if a box is shared, ask.
3. Run, in order: `selftest` (no games; must pass), `status`, `run --dry-run` (prints what it would claim),
   then `start`. `start` detaches the worker and prints its pid.
4. **Set up the watch in the same turn** (CLAUDE.md "EVERY OVERNIGHT RUN GETS A SEPARATE 2-HOUR STATUS TIMER"):
   - a `run_in_background` waiter keyed on the pid: `while kill -0 <pid> 2>/dev/null; do sleep 60; done`, then
     tail `worker.log`. Never `pgrep -f`. Re-arm it before its 2 h cap.
   - a `CronCreate` on a 2-hourly off-minute, **`durable: false`, never durable** (a PreToolUse hook refuses a
     durable one). Its prompt names YOUR session as owner, the worker id, pid and log directory.
     `.claude/scheduled_tasks.json` must stay `{"tasks": []}`.
   - `CronDelete` it when the worker has finished and you have reported.

## On each status check

- `kill -0 <pid>`; `tail` `worker.log` and the current stage log; `claims`.
- **Healthy:** pid alive, the stage log's latest `[batch] heartbeat` near M/M, claim heartbeat under 20 min old.
  Report in two or three lines and stop.
- **Waiting for idle:** `worker.log` says why (a foreign process, or a load average over the limit). That is the
  gate working, not a fault. Check whether whatever is loading the box is expected.
- **Exited:** the tail of `worker.log` says why. A MACHINE fault (build failure) releases the claim and stops the
  worker on purpose; so do three failed jobs in a row. Fix the cause, then `start` again. An interrupted job
  resumes from `inflight.json` in the same frozen worktree, and the drivers resume from their own journals.
- **Never kill a progressing run.** It is a USER-requested run; past ~10 minutes only the USER cancels it.
  `python3 scripts/artifact_queue.py stop` takes effect after the CURRENT job, never mid-generation.
- **When the USER cancels a job: `python3 scripts/artifact_queue.py pause`.** It stops the worker now (its whole
  process group), keeps `inflight.json`, and marks the claim `paused`. The next `start` on that checkout resumes
  the job from its journal; no other worker takes a paused claim, however old its heartbeat. Copy the journal
  aside first anyway (CLAUDE.md "A GENERATION JOURNAL IS ..."). First use: Hinata2, 2026-10-26, after 45 h; a
  backup of its journal is on `origin/artifact-partial/hinata2-20261026`.

## What to leave alone

- **Another machine's claim or worker.** Claims are a compare-and-swap on origin; a stale lease (no heartbeat
  for 3 h) is taken over by the next worker automatically. Don't hand-edit `origin/artifact-queue`.
- **Another session's timer or worker.** A status prompt naming a different owner session is not yours.
- **`scripts/artifact_queue.py` in a checkout whose worker is running** (never edit a running script). Change
  it in a separate worktree and push; the worker picks it up on its next `start`.
- **The worker's worktree** between stages: it is re-frozen only between jobs, and its journals are days of
  compute (CLAUDE.md "A GENERATION JOURNAL IS ...").

## Landing a result

**The worker lands its own results** (`land_result`, USER 2026-10-26: *"include adoption steps after each successful
improvement"*), right after each job, before claiming the next. The drivers' A/Bs have already decided:
- **Kept incumbent** (`rejected`): only `test/artifact_ledger/<deck>.json` lands; any artifact file on the branch is
  dropped (pre-6e42ca948 branches carry a regenerated raw sidecar beside the old table -- Anti-Lifegain's does).
- **Adopted** (`done` with an artifact change): cherry-picked onto the branch TIP in
  `.claude/worktrees/artifact-land-<worker-id>`, rebuilt, the deck's own smoke and regression cases run
  (`regression.sh --deck=<suite key>`), their GT accepted, `check_gt_logs.py` clean, pushed. The GT move IS the
  adoption, not a failure. Overnight GT is left to the periodic overnight run.
- Anything else -- a cherry-pick conflict, a tier that aborts (no `Result:` line), stale gt_logs, a push lost twice --
  is **left on its branch** with the reason in the claim's `land_note` and in `worker.log` (`LAND ... NOT landed`).
- A value-leaf **trade** is never adopted by the driver, so it never lands: that is the in-between left to the USER.
- `python3 scripts/artifact_queue.py land <branch>...` runs the same step by hand (the backlog from before it
  existed). It refuses a branch whose claim is not done/rejected (e.g. Colossus `-0245`, released). `auto_land:
  false` in the config turns the worker's step off.

**No USER sign-off is owed for an adoption** (USER 2026-10-26: *"There is no need for me to sign-off. If the table is
better overall (average quality and performance not worse) it is adoptable."*). Report each landed A/B with its
numbers as DONE, not as a pending decision. Only a trade-off (better quality but slower, or the reverse) goes to the USER.
The same holds for a value leaf, where the performance half matters more (USER: *"this is the same for value-leaf,
though the performance item is more relevant there"*): check its per-game cost as carefully as its quality delta.

## Known gaps

- The worker's generation path first ran on 2026-10-24. Treat its first results as tests of the tool as much as
  artifacts: check the verdict the worker recorded against what the driver's log actually says.
- `mullgen.sh` still tests the plain keep-table name for an incumbent; the worker works around it (unpack the `.gz`
  before, re-pack after). Hand-run regenerations outside the worker do not get that fix.
- Open USER questions (`leaf_follows_mulligan`, the weights, `min_drift`) are listed in the design doc.
