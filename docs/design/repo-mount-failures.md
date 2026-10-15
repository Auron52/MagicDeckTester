# Repo-mount (9p) failures: what happened, what protects a run now, what would prevent it

Status: protections IN PLACE. **USER ruling 2026-10-15: the repo STAYS on the Windows drive** -- a
volume has persistence problems (hand-played references were lost once), so persisted work stays where
the user can see it. What is easily recreated (builds, batch scratch/logs) moves to the build volume,
with a way to publish it back on demand. **Long-running generation artifacts must be written to the
Windows drive -- in general, or at minimum at regular intervals -- so they remain resumable.**

## The setup
- The devcontainer runs under Docker Desktop's WSL2 backend.
- The repo is the Windows folder `C:\WorkFiles\ClaudeProjects\MagicDeckTester2`, bind-mounted into the
  container as `/workspaces/MagicDeckTester2` over **9p** (`aname=drvfs`, `trans=fd`, `msize=65536`). Every
  file operation is a round trip to a file server on the Windows side.
- Only `build/`, `~/.claude`, `~/.config/gh` and the VS Code extensions are Docker **volumes** (ext4 on
  `/dev/sde`). Those never failed.

## Incidents
| when (UTC) | effect |
|---|---|
| 2026-10-07 ~21:15 | EIO on every repo path until the container/WSL was restarted |
| 2026-10-15 02:27:13-02:27:29 | same; a running batch (binary on the mount) died of **SIGBUS** when its text pages could not be re-read; its results were lost |
| 2026-10-15 10:41:58-10:42:13 | same; every shell failed with `EIO posix_spawn` (its cwd was on the mount). Runs staged on the volume survived until the restart, but a regression tier and a phase F test running FROM the mount were lost |

The 10-15 incident in detail, from the 15 s watchdog log (`~/.claude/mdt-watch/pd-20261010T222411Z.log`):
- The container's kernel logged **nothing** at the moment of failure. The transport simply stopped
  answering, and it never recovered. The cause sits on the Windows side and cannot be seen from the
  guest.
- **The VM was saturated by OTHER containers.** VM-wide `used` climbed 12 GB -> 32 GB in about 45 s
  (a similar 12 -> 37 GB swing came five minutes earlier). Load average was 74-88 on 24 cores. This
  container's engine stayed flat at about 5 GB the whole time, and the session had no compiler running.
- **That pressure is not sufficient on its own.** The same log holds 22 earlier >12 GB/45 s surges and
  about 500 samples with load > 70, all without a failure. It is a rare event that is more likely
  under pressure; it is not a threshold.
- **The third incident (10-15 10:42) was under THIS container's own load.** Watchdog v4 caught the
  transition (`fail-20261015T104213Z/`). The guest kernel again logged nothing, memory was not tight
  (30 GB available, PSI 0), and write round-trips were normal (60-140 ms) right up to the failure. But
  load had sat at 72-76 on 24 cores for over ten minutes, all of it ours: a regression tier on every
  core, a 12-thread batch, a multi-threaded `mtg-analyze` and 8 parallel single-game runs. Two of three
  incidents now coincide with load near 75, while earlier samples above 70 did not fail, so this is a
  correlation and not a proven cause. The rule it gives costs nothing: **keep this container's total
  worker threads at or below the core count.** Oversubscribing buys no throughput, and it is the one
  pressure source here that we control.
- A separate, guest-visible failure mode: on 10-14 the kernel logged two
  `page allocation failure: order:4 (GFP_NOFS)` stacks inside `p9_fcall_init` (the 64 KB 9p message
  buffer). These came from two different containers (two cpusets). Each costs one syscall an error
  and is transient. On this kernel `vm.compaction_proactiveness` is 0 (proactive compaction off), and
  `/proc/sys` is read-only inside the container.

## What protects a run now
1. **`scripts/vol_stage.py`** stages an `mtg --batch` run onto the build volume:
   - the binary;
   - `cards.json` and `heuristic_defaults.env`;
   - each job's deck and profile directory, symlinks dereferenced.

   It runs the batch from there, detached, and the outputs land there too. Verified on 2026-10-15:
   - strace shows the staged run touches the 9p mount **zero** times;
   - digests and units are identical to the in-tree run.

   Trap it handles: the engine finds `heuristic_defaults.env` by walking UP from the binary, so a
   binary copied under `build/runs/` would otherwise read the PRIMARY checkout's copy.
2. **Worktree `build/` on the volume, automatically.** On a linked worktree's first build, `build.sh`
   makes `build/` a symlink to `<primary>/build/wt/<worktree>` (the primary's `build/` is the Docker
   volume), when the tree is on 9p and the volume is not. An existing `build/` is left alone; to migrate
   one, rename it to `build-<anything>` (ignored by `build-*/`) and build again. `/build` is in
   `.gitignore` because `build/` matches directories only. Compiles stop pushing objects through 9p,
   and a binary on the volume cannot SIGBUS.
3. **Watchdog v4** (`~/.claude/mdt-watch/watch4.sh`; the probe runs with a 10 s deadline so a HUNG 9p call cannot freeze it, run detached; it must be restarted after every
   WSL restart). Every 15 s it records:
   - a real write round trip through the mount;
   - memory split into OURS (this container's cgroup) and OTHERS;
   - load and PSI;
   - every new kernel line.

   On failure it snapshots dmesg, meminfo, buddyinfo, vmstat and ps to `fail-<ts>/` on the volume
   before WSL is restarted. A restart wipes the kernel log, which is why the 10-07 incident has no
   evidence.

4. **Publishing a volume run** (`vol_stage.py --publish logs/<dir>` at staging, or
   `--publish-existing <run> --root <tree> --publish logs/<dir>` afterwards) copies a run's outputs
   (batch.out/.err, STAGED.json, manifest, run.sh, the game-log dir) into the repo, so the user can see
   them from Windows.
5. **A failed output write can no longer exit 0** (`src/core/OutputHealth.h`). Every writer of
   generation-critical batch output -- the value/eval row dumps, `.wins` / `.units` / `.abandoned`, and the
   per-job stdout lines -- checks its stream. On a failure it prints `[output] WRITE FAILED` and the batch
   exits 5. Before this, one EIO set an ofstream's badbit and EVERY later write was silently dropped
   (the value-row dump guarded each row with `if (v_out.good())`), so a phase-A batch could bank a
   truncated rows file and mark itself complete. Every pipeline already gates on the exit status, and
   phase A re-queues missing games on a re-run.

## Generation artifacts (the user's rule above)
- **Mulligan generation (`mullgen.sh`)** writes its journal beside the decklist on the Windows drive. On a
  failed write the generator holds the records in memory and retries every 30 s, and it keeps rolling
  `.bak1`/`.bak2` copies every 900 s (`MTG_JOURNAL_BACKUP_S`). Its binary lives on the volume (the
  primary's `build/`, or a worktree's auto-linked one), so a mount hiccup cannot SIGBUS it.
- **Value-leaf generation (`valueleaf.sh`)** keeps its queue (`logs/vlq_<deck>`) on the Windows drive; its
  phases are resumable from there. With item 5, a write that fails mid-batch fails the phase instead of
  silently truncating it.
- Neither moves its state to the volume. If they ever do, it must be mirrored to the Windows drive at a
  fixed interval and restored from there on resume (the user's minimum).

## Options considered
- **Move the repo onto a Linux volume** -- REJECTED by the user (persistence; see Status) (a named Docker volume, as `build/` already is, or the WSL
  distro's ext4). Nothing would go through 9p, so this failure mode is gone, and builds and I/O get
  faster.
  - Persistence is the same as today's volumes: it survives container rebuilds.
  - Cost: Windows-side tools no longer see the files directly (`play.cmd`, the Visual Studio
    `build/`). The CLAUDE.md wording that "only the repo survives a rebuild" would change, and
    pushing would remain the off-machine backup.
- **If it stays on 9p:**
  - Cap each devcontainer's memory and CPU (`runArgs: --memory/--cpus`), so one container's surge
    cannot saturate the shared VM.
  - Set `kernelCommandLine = sysctl.vm.compaction_proactiveness=20 sysctl.vm.min_free_kbytes=...` in
    `.wslconfig` against the order-4 allocation failures.
  - Keep Docker Desktop and WSL updated.

  These lower the odds; none removes the failure mode.
