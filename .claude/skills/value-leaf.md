# Value-leaf skill

The value leaf is a learned O(1) evaluator that replaces the search's horizon rollout. This skill is
the whole process for **building one for a deck** — new deck or regeneration — and deciding whether
to ship it.

## Rule 0 — one command, one frozen commit

```bash
bash scripts/valueleaf.sh run    decks/<Deck>     # build (start / resume)
bash scripts/valueleaf.sh status decks/<Deck>     # progress, touches nothing
```

**The deck is the only input.** There are no other knobs, and this is now ENFORCED rather than
advised: the 16 environment settings that used to exist (`HDEPTHS`, `MATRIX_TARGET`, `WORKERS`,
`SLOW_GAME_MS`, `VLQ`, `AB_GAMES`, …) are retired, and the script **exits 2** naming any of them that
is set. A retired knob is refused rather than ignored on purpose — silently dropping `HDEPTHS=1 2 3`
would hand back a table that looks complete and measures something else.

`NEVER_CONDEMN` is the ONE surviving setting (user, 2026-08-10): default 5 — never condemn at or
below the shipped play depth — raisable when a deeper ladder is being measured, and clamped so it
can never go below 5.

**Do not hand-roll the phases and do not re-add settings** — if something the pipeline needs is
missing, fix the pipeline, not your invocation.

The settings that matter are FIXED inside the script, because each has already gone wrong once:

| fixed setting | why it is not a knob |
|---|---|
| pooled batching, always | a per-item loop pays a load-imbalance tail PER ITEM, and every barrier between items idles the box. Two designs have already failed here: a per-cell loop (3 of 24 cores for fifteen hours) and then one batch per (deck, arm) in two waves (**3 of 20 cores for 23 hours**, 2026-08-10 — the waves were the barrier and the arms never shared threads). Phase C is now the WHOLE matrix -- every chunk of every cell of both arms -- in ONE `mtg --batch`, expensive cells first, condemnable cells metered in rather than sorted to the front. No waves, no per-arm split, one tail for the entire matrix |
| the H ladder is 1-6, V is 1-8 | the crossover CLAMPS to the deepest measured hdepth, so a truncated ladder yields `6->5 7->5 8->5` entries that are the clamp, NOT a measurement, and cannot justify d6 play. H stops at 6 because H7 is impractical -- and on FiveColour H6 came in ~11x CHEAPER than H5, a non-monotonic ladder that is worth measuring rather than guessing |
| no condemnation at d<=5 | the H cells ARE the crossover; condemning one leaves a HOLE in the answer rather than saving cost, and the guard is wall-clock based so which cells it hits is partly luck |
| profile always attached | measuring profile-less describes a deck we do not ship — it invalidated every table in this repo once |
| staged model before the matrix | the H-cell ladder is guarded on the sidecar EXISTING; missing it does not error, it silently runs every H cell on the slow path |
| slow games + heartbeat, on by DEFAULT | an expensive deck's cost is concentrated in a few pathological games (75% of a FiveColour arm sits in 14% of its games), and the repro list is the input to any optimization pass. Both instruments now default ON in `mtg --batch` itself, so they cannot be forgotten by a caller: a `[batch] heartbeat` line every 10 min leads with **workers busy**, and any game over 30 s prints a one-line repro. Utilisation-first is deliberate -- the 23-hour run above was misdiagnosed twice (as an engine regression, then as condemnation) when the actual fact was 3 of 20 cores, which this line states outright |

The monolithic matrix path is DELETED (not merely disabled), `--no-incremental` exits with an error,
and `--never-condemn-at-or-below < 5` is refused outright. The per-(seed,depth) driver that shared the
same single-invocation helper (`valueleaf_incremental.py`) is deleted too — dead code that still reads
as a working alternative is how a superseded design gets re-adopted.

The run must sit on ONE commit, because these artifacts are engine-state fingerprints: a play change
midway produces a table whose rows disagree with each other. The driver records `HEAD:src` at start
and re-checks before every phase, and STOPS rather than mixing. It also refuses to start with
uncommitted changes under `src/`.

**Prerequisite:** the deck needs `decks/<Deck>/<Deck>.profile.json` (run the analyze-deck skill
first). The driver exits 2 without it rather than measuring a deck we do not ship.

**A new deck and a regeneration are the same command.** The driver detects whether
`decks/<Deck>/<Deck>.value.json` exists and adapts: with one, it merges only `eval_model` into a copy
so the old table/crossover survive until the new one is measured; without one, the freshly trained
model *is* the staged artifact, and the A/B's "live" arm is the deck with **no sidecar at all** —
which is the correct baseline, since presence alone activates the hybrid. You do not pass a flag for
this and there is no separate path to remember.

## Play mismatch: the unit of consistency is the CHUNK

Generation is frozen to one commit because the artifacts are engine-state fingerprints. When that
freeze is broken anyway -- a merge lands, an optimization ships mid-run -- the run does not have to be
thrown away, and **this is handled by the tool, not by hand**. The rule (user, 2026-08-09):

> Continue under the mismatch, but regenerate **the specific game chunks that were not completed**,
> across all entries of the table that have generated them already (so some cells will not need to).

The chunk is the unit because a chunk *is* a fixed set of games: `[off,off+n)` always runs
`--seed (seed+off) --game-index off`, so game 380 is the same game in every cell, at every depth, on
both arms. The table is then read down a column (does depth d+1 beat depth d?) and across arms (V vs
H at the same depth) -- both per-game comparisons averaged over the seed. What has to hold is that a
given game was measured on ONE engine everywhere it appears, not that a whole seed was. Splitting at
an offset delivers exactly that: `[0,B)` is engine A in every cell, `[B,target)` is engine B in every
cell, and every column and arm-difference is computed from games that agree.

`valueleaf_depth_matrix.py --incremental` does this automatically on resume. Cell state is a list of
chunks stamped with `git rev-parse HEAD:src`; on a src change it picks `B` = the lowest offset any
cell of that seed still owes, drops old-engine chunks at or above `B`, keeps new-engine ones, and
re-queues the gaps. A fully-complete seed is untouched whatever its engine, and a condemned cell
capped at 50 has nothing at or above `B` so it is neither redone nor extended.

**Do not re-run whole cells for this.** An earlier draft of this section made the seed the unit; on
FiveColour that was 26 cells / ~10,400 games where the chunk rule is 24 chunks / 587 -- 18x more work
for no extra consistency.

## Progress and restarts

`status` prints the frozen commit and whether the freeze still holds, a per-phase checklist with
completion times, rows-so-far against target, whether a model is staged, the last held-out RMSE,
matrix progress, whether games are running, and the exact resume command.

Restarting is the normal case, not an error path:

- **`run` is idempotent** — finished phases are skipped via marker files in `<queue-dir>/done`.
- **Rows are durable per row** and dedupe on `(seed, turn)`, so an interrupted dump loses only
  in-flight games; re-running picks up where it stopped.
- **To redo one phase**, delete its marker and re-run.
- **If the clock ran out inside phase A**, use `finish` instead of `run`: it accepts the rows on disk
  as final and runs B..E on them. Phase A is all-or-nothing otherwise.
- **If the freeze was violated** (`src/` moved), restart rather than resume — measurements before and
  after describe different engines.

Each run gets its own queue dir (`logs/vlq_<deck>`), so a single-deck run never collides with a fleet
run's rows, table or markers.

## What it does — five phases, three tails

| phase | what | games |
|---|---|---|
| A rows | dump labelled positions at SHIPPED play (K=3 searched labels) | yes — one pooled batch |
| A split | bucket by seed, dedupe on `(seed, turn)`, sort | no |
| B train | GBDT → `logs/eval/<stem>.value.STAGED.json` | no |
| C matrix | H×V depth matrix, the whole thing in ONE pool | yes — one pooled batch, one tail |
| D metadata | derive crossover + `value_trust_depth` into the staged sidecar | no |
| E measure | staged-vs-live A/B + play sweep | yes — one pooled batch |

Every batch is a barrier that returns only when its slowest game finishes, so what matters is **how
many times you pay that tail**, not how big each batch is. Three is the floor and phase C now sits at
it — the phase boundaries are real dependencies (the matrix measures the model; the A/B measures the
table), and nothing inside a phase is allowed to add a barrier of its own.

**Nothing is adopted.** Every artifact lands in `logs/eval/<stem>.value.STAGED.json`; live sidecars
are never written. Phase E produces the numbers an adoption decision needs — you make the call.

## The three traps that cost real time

**1. Sidecar PRESENCE activates the hybrid — `enabled: false` is not off.** The engine resolves
strictly `<stem>.value.json`. A model you decided NOT to adopt must be committed under a different
name (`<stem>.value.DISABLED.json`), or the suite goes red reproducing the regression you just
rejected. Creature Giving hit this: it installed the sidecar with `value_play.enabled=false` and
d5/s2002 moved 4.792 → 4.804.

**2. The H-cell ladder is guarded on the sidecar EXISTING.** `MTG_LADDER_VALUE_LEAF` runs the
ladder's warm-up passes on the cheap leaf and only the committed pass on the heuristic — worth
1.35×–84.8× less search work, most of it at d5. It is guarded on
`os.path.exists(<value.json>)`, so a deck whose model is missing or mis-pathed does not error: every
H cell silently takes the slow path. This is why phase B must precede phase C.

**3. There are TWO `enabled` meanings, and they are opposites.** Trap 1 is about the *sidecar*:
its mere presence turns the hybrid on, so `enabled: false` does not disable it. `value_play.enabled`
is the other thing entirely — the *play policy* (depth/budget/escalation cap), which steers play ONLY
when `enabled == true` (`ValuePlay::drives()`); unenabled it is a pure recommendation and is
byte-identical. Phase E's depth sweep wrote `target_depth` without `enabled`, so d-1/d/d+1 all came
out byte-identical and the sweep reported "+0.00000, depth does not matter" having tested nothing
(FiveColour 2026-08-09). A deck WITH a live enabled block never shows this — it only bites a deck's
FIRST model, i.e. exactly the case the sweep is most needed for. Fixed: the arms are written enabled
and carry `budget_ms` (an enabled block owns the budget too; omit it and it resolves to 0, confounding
depth with a resource change), plus a `dflt` arm running the staged model as-is so the sweep also
answers "is an adopted play policy worth it at all?"

**There is no deck registry to edit.** `scripts/deck_registry.py` discovers `decks/*/` and derives
every path from the folder — decklist, profile, live sidecar, staged sidecar, row seed base. The
matrix and metadata scripts both import it. The three hand-maintained dicts it replaced each failed
the same silent way: an unlisted deck did not error, it was skipped. The stale fleet list was missing
three decks that have live sidecars.

Using the model under test to accelerate its own baseline is safe **by construction**: a value-leaf
pass returns before `SimulateToEnd` (the only writer of the leaf table), pass-*k* nodes all satisfy
`turn + depth == turn0 + k` with `FSLineCache` folding both into the key so two passes can never
share an entry, and matrix cells run unbounded so there is no budget coupling. Verified 21/21 cells
byte-identical on avg AND play digest (`test/ladder_value_leaf_check.sh`). Only the committed pass
decides; it stays pure heuristic at `MTG_VALUE_MODEL=0`.

## Reading the result

- **Held-out RMSE** (phase B log): ~0.45–0.55 turns is the observed range.
- **`h_conv`**: the depth at which the heuristic stops improving. If the search saturates early
  (Creature Giving: H4 == H3 exactly), the leaf has little room at in-play depths.
- **Crossover / `value_trust_depth`**: UNSET means the leaf never matched the heuristic inside
  tolerance at a depth we actually play — normal, and not a failure.
- **Phase E A/B** is the adoption gate, at the deck's real play point. Judge on avg win turn.

**Adoption is not the default outcome.** Of the last nine models, one was a clear win, six were
neutral, one was declined at scale, and one was rejected for play and shipped inert. A neutral model
can still be worth shipping DISABLED — it is a large generation-cost lever for mulligan work.

## Verification discipline

- **On a heavy-tailed deck, wall-clock is not evidence.** Use `-DMTG_PROFILE=ON` counters or
  callgrind. A wall-clock ladder measurement once reported the mode as both 0.57× slower and 26×
  faster — the same artifact in opposite directions, caught only by user scepticism at a table
  showing "more depth for 18× less time".
- **Judge a dump by its SUSTAINED rate, never its first minute.** Turn-1/2 positions are cheap.
- **Win-turn drift is a bug and blocks adoption; digest drift is expected and fine.** Gate on
  per-game win turns via `MTG_DUMP_WINS`, not aggregate averages.
- **An 8-seed "inert" read can hide a one-sided cost.** Knights looked neutral at 8 seeds and showed
  +0.00020 (t=+2.74, 0/16 seeds better) on 16 fresh seeds × 2500 games. Verify a trust/model move on
  MORE, FRESH seeds before adopting.
- **Interim reads lie.** Partial matrix cells and 100-game seed subsets both produced confident
  phantom structure that full counts erased.

## Shipping

Commit `decks/<Deck>/<Deck>.value.json` (or `.value.DISABLED.json` if rejected for play) with the
frozen commit in `provenance`. Raw rows stay gitignored — they are machine-local and do not transfer.
A regenerated table changes play, so it needs the standing gate: **smoke + regression** before
commit, and an overnight tier for anything that moves a `value_trust_depth`.

Background and history: `docs/design/value-leaf-regeneration-queue.md` (the runbook this skill
implements), `docs/design/learned-d0-policy.md` (the broader learned-d0 program).
