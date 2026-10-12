# KittyEquipment v3 mulligan profile — paused 2026-10-12, how to resume

**Status: CANCELLED BY THE USER at 2026-10-12 23:09, mid-`refine`. Nothing is lost.** The journal
holds 14.6 h of rollouts and is resumable. Branch bookmark: `kitty-v3-mulligan-wip`.

## The one command

```
MTG_KEEP_ROLLOUT_WALL_SEC=0 MTG_KEEP_FLIP_EPS=0.10 \
  bash scripts/mullgen.sh run decks/KittyEquipment/v3-adelbert-gauntlets fast
```

Every token matters:

| token | why |
|---|---|
| `fast` | **positional, and `mullgen.sh` defaults to `complete`** (`RECIPE=${3:-complete}`). The journal is R=30; `complete` is R=40 and the resume gate refuses it. Omitting this is how the 2026-10-11 launch ran the wrong recipe. |
| `MTG_KEEP_ROLLOUT_WALL_SEC=0` | now the in-code default (`0279f832`), so this is belt-and-braces. **Never set it non-zero on this path** — see below. |
| `MTG_KEEP_FLIP_EPS=0.10` | the USER's ruling for this run (2026-10-11). The in-code default stays 0.02 and is still an OPEN question. |

## State reached

| | |
|---|---|
| generation time spent | **14.6 h** (12.9 h + 1.7 h resume) |
| journal | 252,478 records, 13.1 MB, `R=30 K=15 play_digest=9c8fd2e5a7016018` |
| `floor` phase | **COMPLETE** — refs fixed, sub-refine 64,422/64,422 (100%), subwave 5 converged, 0 frozen-at-floor |
| `refine` phase | **12.8%** — 13,687 of 106,746 cell-sides frozen |
| refine remaining | **16–36 h** (see the estimate note below) |

The engine has only three phases: `pre-pool` → `floor` → `refine`. Refine is the last, after which the
profile is written and `mullgen.sh validate` runs the gates.

### The refine ETA, and why it is a range

Measured over 21 monitor samples, refine was **accelerating** (2.44 %/h over the first half of the
samples, 5.43 %/h over the second), giving 35.7 h and 16.0 h respectively. Weight the SLOWER end:
a cell freezes when its argmin is settled, so the cells still unfrozen are the CLOSE ones that need
more rollouts to separate — the early acceleration is the easy cells clearing, and the tail should
decelerate. Do not quote a single number from a short window; that mistake produced a "12 h" estimate
three times better than the run's own projection.

## Copies of the journal (it is GITIGNORED — these are the only copies)

- `decks/KittyEquipment/v3-adelbert-gauntlets/KittyEquipment.keepmodel.exhaustive.raw.json.journal` (live)
- `~/.claude/mdt-v3-R30-journal-cancelled-20261012T231032Z.journal.gz` (1.3 MB, persistent docker volume)
- `~/.claude/mdt-journal-aside-v3-R30-failout-20261012T211921Z.journal` (state at the wall fail-out)
- `~/.claude/mdt-journal-aside-v3-R16-*.journal` x2 — the SUPERSEDED R=16 run. **Never delete a
  killed run's journal.**

`decks/**/*.journal` is gitignored, so the branch does NOT carry the journal. Only `~/.claude` and the
repo survive a container rebuild — `/tmp` and `/workspaces` do not.

## RESUME PRECONDITIONS

1. **`play_digest` must still be `9c8fd2e5a7016018`.** The resume gate keys on it
   (`PlayIdentityAllows`). **Any change to play code invalidates this journal** — which is the tension
   in "optimise the engine, then resume": a play-identical speedup (memoisation, allocation, codegen)
   keeps it; a play-changing one does not, and then the only options are a from-scratch run or
   `MTG_KEEP_RETAIN_FOREIGN=9c8fd2e5a7016018`, which mixes two labellers and is a user decision.
2. **PGO must match `HEAD:src`.** As of the cancel it does NOT (the wall fix `0279f832` changed src
   after the PGO build), so `harness_bin` falls back to `build/Release`. Run `./build.sh pgo` first.
3. **Expect this warning, which is benign here:** `RESUME(journal): ... predates the
   refinement-schedule stamp -- UNVERIFIABLE`. The `669bdc69` stamp lands on the raw's meta, **not on
   the journal header** — a real gap worth closing. The original run's environment was verified as
   `MTG_KEEP_FLIP_EPS=0.10` from `/proc/<pid>/environ`, so no schedules are actually mixed.

## KNOWN CONTAMINATION: 7 cell-sides

The wall fail-out left 7 cell-sides (of 213,492) carrying one truncated sample out of R=30: the
journal persisted their records while the poison list was in-memory only. Bounded and tiny, but a
fully clean table needs a from-scratch run. Not fixable by resuming.

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
