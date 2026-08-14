# Mirrorwing value-leaf regen: third-machine handoff (2026-08-13)

Play behavior for Mirrorwing is now **settled and pushed** (discard doctrine V3 + GT, commit
`224e937` on `phase-1-2-deck-analyzer`). The value-leaf regen has been deliberately held back
all along (`value-leaf.md` Rule 0: generate against ONE consistent PLAY DIGEST) and is now unblocked — except
that **feasibility is still an open question**: the phase-A label path got dramatically more
expensive when the trick-class site-5 split made deferred continuations searchable, and the
honest levers are exhausted. This doc is everything a fresh machine needs to (1) answer the
feasibility question and (2) run the regen if the answer is yes.

## Context: why feasibility is in question

Phase-A labels are K=3 de-clairvoyed `EnumerateEarliestWins` full searches per real pre-combat
main. Known walls for the canonical heavy game ("gi69", seed-900000 set):

| binary | gi69 label wall |
|---|---|
| pre-site-5 wiring | ~15 s |
| post-site-5, GR gate + prefix-resume cache, pinned idle | ~1290 s |
| current (post crack fix `fda8b07`) | **UNKNOWN — the number this handoff exists to get** |

The crack fix raised `rolled` +71% on gi69 (23.9M vs 14.0M rollouts, contention-immune
counters) — all real reachability (Treasure-funded continuation lines previously no-op'd), so
the wall is expected somewhat above 1290 s. Lever field exhausted: label-wave-off **REJECTED by
the user** ("not an honest lever" — do not re-raise); prefix-resume cache built and worth only
~4%; remaining duplicate classes are honest convergence. Full attribution:
`docs/design/mirrorwing-gen-perf-profile.md`.

## Step 0 — setup

```
git clone <repo> && cd MagicDeckTester
git checkout phase-1-2-deck-analyzer        # at or past 224e937
git config pull.rebase true && git config rebase.autoStash true
./build.sh                                  # NEVER raw cmake (-O0 footgun)
git rev-parse HEAD:src                      # record: a FAST PATH, not the freeze criterion (see below)
```

**What "frozen" means here.** The artifacts are engine-state fingerprints, so every row must be
measured under the same PLAY — but a commit hash is only a cheap proxy for that. `HEAD:src` moving
does not imply play moved: a doc, a script, a refactor, or a byte-identical perf change all move it
while leaving every game identical. So the driver records `HEAD:src` **and** the smoke play digest,
and re-checks before every phase:

| what moved | what happens |
|---|---|
| nothing | proceed (the cheap check short-circuits; the digest, which costs a smoke run, is never computed) |
| `src`, play digest UNCHANGED | play-identical: the freeze is re-stamped, chunks re-labelled, nothing discarded, nothing stops |
| play digest CHANGED | the run continues and banks every full set |

This is not theoretical: on 2026-08-14 the dev box rebased onto 20 commits that touched
`TurnSolver.cpp`, `DecisionProviders.cpp` and `main.cpp`, and FiveColour's play digest was
**unchanged** — a commit-keyed freeze would have thrown away a completed matrix for a change that
provably could not alter one of its games.

## Step 1 — feasibility measurement (BEFORE any regen)

On an **idle** box, pinned. Two probes, both cheap relative to the regen:

1. The canonical heavy game (comparable to the 1290 s known point):
   ```
   mkdir -p logs/mwprof
   MTG_DUMP_VALUE_ROWS=logs/mwprof/gi69_rows.jsonl MTG_EVAL_ROWS_K=3 MTG_EVAL_ROWS_ROLLOUT=0 \
     taskset -c 2 build/Release/mtg "decks/Mirrorwing Dragon/Mirrorwing Dragon.cod" \
     --seed 900069 --game-index 0 --games 1
   ```
   The engine's `SLOW-GAME` line prints the wall. (This exact invocation produced the 1290 s
   figure on the dev box; the dev box was too contended for a trustworthy current-binary wall.)
2. A 60-game label batch (seeds 900000+, same env, all workers) to see the tail shape — the
   pre-site-5 profile had 1 game of 60 carrying 951 s of a 952 s batch, so the mean matters
   less than the tail.

**Then report the numbers to the user — the go/no-go is the user's call, not the agent's.**
Rough framing for the report: total regen cost ≈ (heavy-game wall × heavy-game count in the
phase-A seed set) / workers; compare against an overnight-to-a-few-days budget on the third
machine.

Measurement traps (hard-won on the dev box; re-verify per machine): callgrind DIVERTS
label-config games (do not use it here); `perf record` broken under WSL2; walls need `taskset`
pinning; counters/digests are the contention-immune currency.

## If the numbers say INFEASIBLE

Infeasible is a routing decision, not a dead end. The remaining honest levers (ranked, detail
in `mirrorwing-gen-perf-profile.md`) would land on the DEV box first, before any regen starts. These
are optimisations that change what the search WALKS, so they move play for this deck and would
invalidate rows measured before them — unlike the src movement described above:

1. **Equivalence collapses at continuation enumeration** (designed, not built): fold symmetric
   target/strive variants before the wave walks them — the provable-fold family (sibling
   tap-backtrack collapse precedent). Note the duplicate-probe verdict: remaining dup classes
   are crack-colour convergence + unaffordable-cast fallback; only provable subsets qualify.
   The label-wave-off lever is REJECTED (user, 2026-08-13) — do not re-raise it.
2. **Tail-rollout cost**: post-crack-fix, `rolled` rose 14M→24M on gi69 (ex-duplicate walks now
   take full FSLineTail rollouts) — the dominant label-wall slice. FSLine no-win memo already
   absorbs 69% of revisits.
3. **GameState deep-copy / apply-undo rework**: big engine-wide item, but only ~3.5% of the
   label wall post-site-5 — a play/suite lever more than a regen rescue.
4. Micro: CleanupDiscardRanking rank-cache per hand multiset (~6% Ir, runs per shed on rollout
   hands >7); incremental BuildSimKey (~8% Ir).

So the report to the user should frame the choice as: run as-is (N days) / land lever(s) 1–2
and re-measure the play digest before starting / defer the regen.

## Step 2 — if the user says go

```
bash scripts/valueleaf.sh run "decks/Mirrorwing Dragon"     # the WHOLE interface
bash scripts/valueleaf.sh status "decks/Mirrorwing Dragon"  # progress
```

Per `.claude/skills/value-leaf.md`: do NOT hand-roll the phases, do NOT add knobs. The script
pools all five phases, resumes incrementally, and stages without adopting. A `git pull` before a
resume needs no special handling — the driver does the `HEAD:src`-then-play-digest check itself and
re-stamps a play-identical move rather than stopping. **If `src/` moved, just resume**; only a
changed play digest costs anything, and even then the run continues and banks every full set.

Two traps from the skill worth repeating: sidecar PRESENCE activates the hybrid in play
(`enabled: false` is NOT off — a rejected model ships as `<stem>.value.DISABLED.json`), and the
H-cell ladder guards on the sidecar existing (a missing model silently costs 1.35–84.8x).

## Step 3 — adoption

Measurement/adoption happens against the suite on the dev box (or anywhere whose play digest
matches the one the rows were generated under — not necessarily the same commit): staged model
A/B'd per the value-leaf skill, user approves adoption. Nothing on the third machine adopts
anything.

The FiveColour adoption on 2026-08-14 is the worked example of what that A/B looks like: a paired
comparison of the staged config against the shipped one over 8 seeds x 1000 games, reported to the
user as `delta +/- se` and `t` per seed **before** anything was copied into `decks/`. Pair on seeds —
comparing row means over unequal game sets is the recurring defect in this whole area, and it
produced a sign error in the FiveColour table that survived until the numbers were re-derived on
matched sets.
