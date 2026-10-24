# Running the artifact refresh queue: a guide for agents

For any agent asked to run, join, watch or land work from `scripts/artifact_queue.py` on any machine.
For how the tool works and why, see `docs/design/artifact-staleness-queue.md` (section "2026-10-23: what was
built"). This page covers what to do, where to look, and what to leave alone.

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

## What to leave alone

- **Another machine's claim or worker.** Claims are a compare-and-swap on origin; a stale lease (no heartbeat
  for 3 h) is taken over by the next worker automatically. Don't hand-edit `origin/artifact-queue`.
- **Another session's timer or worker.** A status prompt naming a different owner session is not yours.
- **`scripts/artifact_queue.py` in a checkout whose worker is running** (never edit a running script). Change
  it in a separate worktree and push; the worker picks it up on its next `start`.
- **The worker's worktree** between stages: it is re-frozen only between jobs, and its journals are days of
  compute (CLAUDE.md "A GENERATION JOURNAL IS ...").

## Landing a result

A result branch is not live until it lands. Rebase it onto `phase-1-2-deck-analyzer`, run smoke and regression,
and accept the GT it moves. A **verdict-only** branch (only `test/artifact_ledger/` changed) moves no play and
lands as-is. The drivers' own gates decide adoption, but the USER's sign-off is still owed: report each A/B with a
recommendation in your message, and keep working while you wait for it (CLAUDE.md "NEVER BLOCK ON A QUESTION").

## Known gaps

- The worker's generation path first ran on 2026-10-24. Treat its first results as tests of the tool as much as
  artifacts: check the verdict the worker recorded against what the driver's log actually says.
- `mullgen.sh` still tests the plain keep-table name for an incumbent; the worker works around it (unpack the `.gz`
  before, re-pack after). Hand-run regenerations outside the worker do not get that fix.
- Open USER questions (`leaf_follows_mulligan`, the weights, `min_drift`) are listed in the design doc.
