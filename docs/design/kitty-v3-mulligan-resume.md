# KittyEquipment v3 mulligan profile — paused 2026-10-14, how to resume

**Status: PAUSED BY THE USER at 2026-10-14 00:32Z, mid-`refine` at 91.5%. Nothing is lost.** The user
stopped it to free the box for daytime work and expects to relaunch the same night. The journal holds
**~26.4 h** of rollouts (14.6 h to 2026-10-12 + 11.75 h on 2026-10-13) and is resumable.
Branch bookmark: **`kitty-v3-mulligan-wip`** — see "DO NOT REBASE" below, it is the whole ballgame.

## The one command

```
MTG_KEEP_ROLLOUT_WALL_SEC=0 MTG_KEEP_FLIP_EPS=0.10 \
  bash scripts/mullgen.sh run decks/KittyEquipment/v3-adelbert-gauntlets fast
```

Wrapped with a launcher + watcher at `logs/kitty_v3_resume/launch_gen.sh` and `wait_gen.sh`
(gitignored, so re-create them from this doc if the tree was wiped). Every token matters:

| token | why |
|---|---|
| `fast` | **positional, and `mullgen.sh` defaults to `complete`** (`RECIPE=${3:-complete}`). The journal is R=30; `complete` is R=40 and the resume gate refuses it. Omitting this is how the 2026-10-11 launch ran the wrong recipe. |
| `MTG_KEEP_ROLLOUT_WALL_SEC=0` | now the in-code default (`0279f832`), so this is belt-and-braces. **Never set it non-zero on this path** — see below. |
| `MTG_KEEP_FLIP_EPS=0.10` | the USER's ruling for this run (2026-10-11). **Not in the journal's resume identity**, so omitting it silently resumes at the 0.02 default and MIXES two schedules. The in-code default stays 0.02 and is still an OPEN question. |

## State reached (verified after the stop)

| | |
|---|---|
| generation time banked | **~26.4 h** |
| journal | **395,241 records, 23.6 MB, 0 malformed** — `R=30 K=15 depth=2 budget_ms=3 max_mull=6 play_digest=9c8fd2e5a7016018` |
| `floor` phase | **COMPLETE** (was already complete before the 2026-10-13 resume) |
| `refine` phase | **91.5%** — 97,717 of 106,746 cell-sides frozen |
| remaining | 9,029 cell-sides |
| anomalies | **none** — 0 fail-outs, 0 aborts, 0 abandoned rollouts |

The stop was a hard `SIGTERM` (there are **no signal handlers in `src/`**), but the journal is
append-mode JSONL flushed every few seconds and verified afterwards: 395,241 records parsed, **0
malformed**, header identity intact. A hard stop here is safe; it costs at most a few seconds.

### What remains, and how long

The 2026-10-13 session measured the freeze-rate curve properly (105 monitor samples):

```
h 0-2     7,577/h  ( 7.10 %/h)
h 2-4    11,527/h  (10.80 %/h)
h 4-6    16,031/h  (15.02 %/h)   <- the easy cells clearing
h 6-8     2,331/h  ( 2.18 %/h)   <- the cliff
h 8-10    1,594/h  ( 1.49 %/h)
h10-12    1,210/h  ( 1.13 %/h)   <- gentle decline, ~-24% per 2 h
```

Two honest readings of the remaining 9,029:

- **flat at the converged ~1,280/h → ~7 h.** At the pause, the last 1 h / 2 h / 3 h windows all agreed
  (1,287 / 1,270 / 1,297 per hour), which is what makes this a real estimate rather than a burst read.
- **decline continues at −24% per 2 h → ~13–14 h.** Each successive window has been lower than the last.

Plan against **7–14 h**. Cap `R=30` bounds it absolutely — a cell cannot consume more than R rollouts
per side, so refine must terminate however close the remaining cells are. **Do not quote a point
estimate from a short window**: per-5-minute rates still swing 228/h to 5,238/h because of the rollout
tail below.

### The rollout tail is the cost driver (and there is no hidden cap)

7,301 games exceeded the 30 s reporting threshold in 11.75 h, totalling **~250 core-h** — the tail
alone is most of the spend:

| | p50 | p75 | p90 | p95 | p99 | p99.9 | max |
|---|---|---|---|---|---|---|---|
| tail game time | 66 s | 130 s | 263 s | 401 s | 855 s | 1,860 s | **3,598 s (60 min)** |

157 games over 10 min, 10 over 30 min, 2 over 50 min. **The 3,598 s max is a coincidence, not a 1-hour
cap** — the next two are 3,031 s and 2,313 s, a smooth tail with no pile-up at the boundary, and the
only wall (`MTG_KEEP_ROLLOUT_WALL_SEC`) is 0. Nothing is being silently truncated. A single hour-long
rollout stalls its cell until it returns, which is why freezing arrives in clumps.

## *** DO NOT REBASE BEFORE THE PROFILE IS WRITTEN ***

The previous version of this doc said the next step was "rebase, rebuild, regenerate GT."
**That instruction would have destroyed the journal.** Resuming requires
`play_digest 9c8fd2e5a7016018`, and `phase-1-2-deck-analyzer` is now **193 commits ahead** of this
branch with 30 changed `src/` files (3,605 insertions — TurnSolver, AIEngine, ManaPayment,
BatchRunner, GoldFishRunner). Staying on `kitty-v3-mulligan-wip` is what keeps the resume legal.

**The sharpest instance, worth internalising:** the shared branch's E.6 commit `14a0ed73` adds **one
line** to `decks/KittyEquipment/v3-adelbert-gauntlets/KittyEquipment.value.json`
(`escalation_skip_r = 43.3`). A value sidecar drives play, so that single line of *data* — in a file
nobody thinks of as code — is very likely enough to move `play_digest` by itself and make 26 h
unresumable. Before any resume: `grep -c escalation_skip_r` on that sidecar; on this branch it must be
**0**.

**So the integration order is: finish the profile → THEN rebase → THEN rebuild → THEN regenerate GT.**
`git merge-tree` previously reported 3 conflicts, all play-code headers (`src/ai/Dominance.h`,
`HeuristicArm.h`, `TurnSolver.h`); expect more at 193 commits.

### OPEN, and the USER's to decide (flagged 2026-10-13, unanswered)

The table will arrive fitted to this branch's play, which is behind what ships. Accepting that, versus
a from-scratch run on the merged engine (60–80 h), is a user decision. **Do not settle it by shipping
quietly.**

## RESUME PRECONDITIONS

1. **Branch `kitty-v3-mulligan-wip`**, and `play_digest` still `9c8fd2e5a7016018`. If a resume is ever
   refused, **read which field refused** — a refusal is not proof the artifact is dead.
   `MTG_KEEP_RETAIN_FOREIGN` mixes two labellers and is a USER decision, never an agent's.
2. **PGO must match `HEAD:src`.** As of this pause it DOES (`889da671`, built 2026-10-13 12:43Z), and
   `harness_bin` resolves to `./build/PGO/mtg`. **Any other work that runs `./build.sh pgo` overwrites
   `build/PGO`** — if daytime work happens on another branch, re-run `./build.sh pgo` (~9 min) after
   switching back, and verify with `cat build/PGO/SRC_TREE` against `git rev-parse HEAD:src`.
   A stamp mismatch falls back to `build/Release` **silently** (`MTG_PGO=1` does not force it).
3. **Expect this benign warning:** `RESUME(journal): ... predates the refinement-schedule stamp --
   UNVERIFIABLE (this run flip_eps=0.1/r_batch=16/se_prior=8)`. The `669bdc69` stamp lands on the raw's
   meta, **not on the journal header** — a real gap worth closing. The original run's `flip_eps=0.10`
   was verified from `/proc/<pid>/environ`, so no schedules are actually mixed.
4. **The engine log is APPEND-mode** (`logs/KittyEquipment_v3-adelbert-gauntlets_mullgen/gen.log`).
   Find the current run's section after the **last** `RESUME(journal)` line — never with `head`, and
   never trust a `grep` that may be reading a previous run's lines.

## Copies of the journal (it is GITIGNORED — these are the only copies)

Durable, on the `~/.claude` docker volume (survives a container rebuild; `/tmp` and `/workspaces`
do **not**):

- `~/.claude/mdt-v3-R30-journal-paused-20261014T003255Z.journal.gz` (2.2 MB) — **the state this doc describes**
- `~/.claude/mdt-v3-R30-prekill-20261014T003210Z.journal.gz` — snapshot taken before the stop
- `~/.claude/mdt-v3-R30-journal-preresume-20261013T123658Z.journal.gz` — state at the 2026-10-13 resume
- `~/.claude/mdt-v3-R30-journal-cancelled-20261012T231032Z.journal.gz`
- `~/.claude/mdt-journal-aside-v3-R30-failout-20261012T211921Z.journal` (wall fail-out state)
- `~/.claude/mdt-journal-aside-v3-R16-*.journal` x2 — the SUPERSEDED R=16 run

In-tree (gitignored): the live journal plus `.bak1`/`.bak2` (automatic rolling backups,
`MTG_JOURNAL_BACKUP_S`, default 900 s), `.backup-20261013T124438Z` (mullgen's own pre-run copy) and
two older ones. **NEVER delete a killed run's journal** — not even one a larger journal supersedes.

## *** NEVER RE-ARM THE ABANDON WALL ON THIS PATH ***

USER, 2026-10-12: *"There should be no abandon wall for mulligan profiles!"* / *"It was narrowly
applying only to value-leaf phase A and C."*

**THE GOVERNING RULE:** *"whatever process uses that abandonment has the ability to work around some
of the games being lost. Mulligan profiles do not."* The test is not "is this phase expensive" — it is
**can the consumer absorb a missing game?** Value-leaf phase A can (rows are ADDITIVE); phase C can
(C.5 must PROVE the cut moved no verdict); a keep table **cannot** — each cell IS a decision consumed
directly as policy, nothing is additive, and no stage verifies the loss was harmless.

It cost 12.8 h of compute for a profile that was never written, to reclaim the 3.2 of 220 CPU-h the
cap was priced at (1.5%). Fixed in `0279f832` (default 1200 → 0). Full post-mortem in that commit.

## KNOWN CONTAMINATION: 7 cell-sides

The 2026-10-12 wall fail-out left 7 cell-sides (of 213,492) carrying one truncated sample out of R=30:
the journal persisted their records while the poison list was in-memory only. Bounded and tiny, but a
fully clean table needs a from-scratch run. **Not fixable by resuming.**

## What runs AFTER refine (all in `mullgen.sh validate`)

1. Profile written — `mullgen` aborts here if absent (this is the message the wall produced).
2. **Artifact check**, free and no games: `bottoming_enabled` true, `bottom_keep` row widths == K,
   no sub-table cell-side under `sub_target`. Exists because the bottoming A/Bs once failed silently
   after 40+ min of games for a defect visible here for nothing.
3. **Keep A/B** — exhaustive vs static, bottoming held identical (seed base 4004, 16/round, ≤3 rounds).
4. **Confounded bottoming A/B** — blind vs lookahead (`MTG_CONFOUND_BOTTOM=3`, base 1004004).
5. **Verdict** — FIRST VERSION path (no prior profile): neither check may be worse on average, any
   amount rejects into quarantine.
6. **Deck-filtered regression** — visibility only, cannot reject. `kittyv3` has no GT rows yet so it
   reports all-NEW. Note it leaves stale `.wins` that pollute a later full-suite audit.

## Also owed, independent of this run

- **GROUND TRUTH for both kitty keys.** `kittyv2` is restored to v2-puresteel-hammer and `kittyv3` is
  new (`f09afcfd`), so neither has valid rows: the 2026-10-11 run's `kittyv2_*` numbers were measured
  on v3 and are mislabelled under the corrected mapping. The engine itself is cleared by that run
  (149 passed; `kitty` −0.2276 and `kitty2hg` −0.0400 better; `stompy` digest-only; references 174 ok
  / 382 repaired / 0 drift / 0 board-diverged / 0 enum-gap).
- **The OVERNIGHT tier has not been rebaselined.** The skill records that it silently accumulates a
  whole window when every rebaseline is smoke+regression.
- **The flip_eps cross-deck sweep is 51/156 rows** (resumable), and whether 0.10 becomes the default
  is still open.
- **Stamp the schedule config into the JOURNAL header**, not only the raw meta (precondition 3).

## Artifact custody check, 2026-10-14 (daytime handover to Fungus work)

The user moved the box back to Fungus and asked that the kitty artifacts be kept. Verified rather
than assumed, before the working tree was touched:

| check | result |
|---|---|
| live journal | 23,621,142 bytes, **395,242 lines**, sha256 `f1809a6c9bd82a1a...` |
| durable copy `~/.claude/mdt-v3-R30-journal-paused-20261014T003255Z.journal.gz` | `gzip -t` OK, decompresses to the **same bytes, same line count, same sha256** |
| journal header | `K=15 R=30 depth=2 budget_ms=3 max_mull=6`, `play_digest 9c8fd2e5a7016018` |
| E.6 precondition on `kitty-v3-mulligan-wip` | `grep -c escalation_skip_r` on the v3 `.value.json` = **0** (correct) |
| journal after switching to `phase-1-2-deck-analyzer` | **unchanged, same sha256** -- it is gitignored, so a branch switch cannot touch it; all 7 journal files still present |

**`build/PGO` HAS BEEN PRESERVED, because the Fungus work will overwrite it.** At the pause the stamp
matched (`889da671`), and precondition 2 above says other work on another branch invalidates it. The
matching binary is copied to **`build/PGO-kitty-v3-889da671/`** (on the persistent `mdt-build-*`
docker volume, 18 MB). To restore instead of spending ~9 min on `./build.sh pgo`:

```
cp -p build/PGO-kitty-v3-889da671/mtg build/PGO-kitty-v3-889da671/SRC_TREE build/PGO/
cat build/PGO/SRC_TREE; git rev-parse HEAD:src      # must be equal, on the kitty branch
```

**AND THIS DOC WAS STALE ON THE SHARED BRANCH, which is the trap worth recording.**
`phase-1-2-deck-analyzer` carried the **2026-10-12 CANCELLED** version -- a 14.6 h journal, no
refine-91.5% figure, and **no DO-NOT-REBASE warning at all**. A night agent that started on the
development branch (the normal place to start) would have read instructions describing a two-day-old
state, including the rebase-next step this doc exists to forbid. The current version is now committed
on BOTH branches. **A resume doc that lives only on the branch it protects is not protection** --
when the hazard is "do not leave this branch", the warning has to exist where the reader starts.
