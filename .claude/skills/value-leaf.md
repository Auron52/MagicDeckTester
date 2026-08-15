# Value-leaf skill

The value leaf is a learned O(1) evaluator that replaces the search's horizon rollout. This skill is
the whole process for **building one for a deck** — new deck or regeneration — and deciding whether
to ship it.

## Rule 0 — one command, one consistent PLAY DIGEST

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

**Degenerate games can now be ABANDONED rather than waited out (2026-08-14), but the policy is not
settled — so the mechanism is OFF.** `valueleaf_depth_matrix.py --abandon-units N` sets a per-game
search-work ceiling: a game past it is voided, excluded from every cell of its `(deck, seed)`, and
BACKFILLED so the cell still reaches its game count. It is in deterministic work units, not seconds,
so the same games are dropped on every machine — which is what lets the skip list be shared and the
run stay reproducible. The table discloses the filter as `~~ FILTERED`, because it changes the
estimand to "games that complete within the ceiling" and a reader comparing against an unfiltered
table would otherwise be comparing different populations.

What is NOT decided is the threshold. The useful one is relative to a cell's own median (cells span
11 ms to 700 s per game), and nothing computes that yet; `MTG_DUMP_UNITS=1` writes a `<job>.units`
file so the distribution can be measured first. Until a multiplier is chosen and validated, treat
`--abandon-units` as a calibration lever and do NOT set it on a production run — an absolute ceiling
picked by eye would silently filter a cheap cell's normal games and an expensive cell's not at all.

**Do not hand-roll the phases and do not re-add settings** — if something the pipeline needs is
missing, fix the pipeline, not your invocation.

The settings that matter are FIXED inside the script, because each has already gone wrong once:

| fixed setting | why it is not a knob |
|---|---|
| pooled batching, always | a per-item loop pays a load-imbalance tail PER ITEM, and every barrier between items idles the box. Two designs have already failed here: a per-cell loop (3 of 24 cores for fifteen hours) and then one batch per (deck, arm) in two waves (**3 of 20 cores for 23 hours**, 2026-08-10 — the waves were the barrier and the arms never shared threads). Phase C is now the WHOLE matrix -- every chunk of every cell of both arms -- in ONE `mtg --batch`, expensive cells first, condemnable cells metered in rather than sorted to the front. No waves, no per-arm split, one tail for the entire matrix |
| the H ladder is 1-5, V is 1-8 | H5 is the ESCALATION CAP -- the strongest fallback the runtime can ever take -- so "trust the leaf" means "the leaf matches H5", and nothing deeper can inform a decision the runtime cannot make. The crossover's `maxH+1` sentinel then reads as NEVER fall back, which is the wanted behaviour. H6 is dropped (2026-08-14): it has never been completed on any deck, every H6 on disk is a small condemned side-sample, and the 5-deck tables' H6 was merged in from a 150-game `d68` pass into tables declaring `games=500`. Its old "~11x CHEAPER than H5" rationale was refuted by the FiveColour run — H6 measured 1.2x MORE expensive, and only over the 4–50 games that tripped the one-hour guard, so even that is a floor. Keeping it cost that run 47.4 core-h for 111 unusable games and flipped the derived fallback rule from "never" to "fall back at H6". Not cut to H4: the two decks still improving at H3->H4 are exactly the two whose H5 was never measured |
| no condemnation at d<=5 | the H cells ARE the crossover; condemning one leaves a HOLE in the answer rather than saving cost, and the guard is wall-clock based so which cells it hits is partly luck |
| profile always attached | measuring profile-less describes a deck we do not ship — it invalidated every table in this repo once |
| staged model before the matrix | the H-cell ladder is guarded on the sidecar EXISTING; missing it does not error, it silently runs every H cell on the slow path |
| per-GAME results, always | a chunk used to store one MEAN over its games, so nothing below a chunk could be dropped: a ragged condemned chunk (n=7) forced whole 25-game chunks out of the keep set across twelve rows, and two earlier attempts to keep part of one FABRICATED the surviving piece's score. Chunks now carry `g` = [(offset, win turn)] read from the `.wins` file, so retention is per game and a skip is a FILTER over data already on disk. It is also what makes a comparison paired by construction -- unequal game sets are how the FiveColour table came to read `V6-H5 = -0.0001` when the matched answer is `+0.0020`, the wrong sign on the comparison the adoption turned on |
| slow games + heartbeat, on by DEFAULT | an expensive deck's cost is concentrated in a few pathological games (75% of a FiveColour arm sits in 14% of its games), and the repro list is the input to any optimization pass. Both instruments now default ON in `mtg --batch` itself, so they cannot be forgotten by a caller: a `[batch] heartbeat` line every 10 min leads with **workers busy**, and any game over 30 s prints a one-line repro. Utilisation-first is deliberate -- the 23-hour run above was misdiagnosed twice (as an engine regression, then as condemnation) when the actual fact was 3 of 20 cores, which this line states outright |

The monolithic matrix path is DELETED (not merely disabled), `--no-incremental` exits with an error,
and `--never-condemn-at-or-below < 5` is refused outright. The per-(seed,depth) driver that shared the
same single-invocation helper (`valueleaf_incremental.py`) is deleted too — dead code that still reads
as a working alternative is how a superseded design gets re-adopted.

**The run must be consistent in PLAY, not pinned to a commit (user, 2026-08-13).** These artifacts are
engine-state fingerprints, so a table whose rows were measured under different play is a table whose
rows disagree with each other — but a commit hash is only ever a cheap PROXY for that. Plenty of
commits (a doc, a script, a refactor, a perf change that is byte-identical by design) move `HEAD:src`
without moving play at all.

So the driver records `HEAD:src` **and the smoke play digest** at start, and re-checks before every
phase:

| what moved | what happens |
|---|---|
| nothing | proceed |
| `src`, play digest UNCHANGED | play-identical: the freeze is re-stamped and the chunks re-labelled. Nothing is discarded, nothing stops |
| play digest CHANGED | the run CONTINUES and banks every full set — see the next section |

The commit is still worth recording, but as an OPTIMISATION rather than the rule (user, 2026-08-13):
an unchanged `HEAD:src` means play cannot have changed, so the cheap check short-circuits and the
digest — which costs a smoke run — is never computed. It is a fast path, not the criterion.

It never stops. An earlier version halted on any `src` movement, which cost a 60–70 core-hour restart
for a change that provably could not alter a single game.

**Prerequisite:** the deck needs `decks/<Deck>/<Deck>.profile.json` (run the analyze-deck skill
first). The driver exits 2 without it rather than measuring a deck we do not ship.

**A new deck and a regeneration are the same command.** The driver detects whether
`decks/<Deck>/<Deck>.value.json` exists and adapts: with one, it merges only `eval_model` into a copy
so the old table/crossover survive until the new one is measured; without one, the freshly trained
model *is* the staged artifact, and the A/B's "live" arm is the deck with **no sidecar at all** —
which is the correct baseline, since presence alone activates the hybrid. You do not pass a flag for
this and there is no separate path to remember.

## Play mismatch: the unit of consistency is the CHUNK

When play genuinely does change -- a merge lands, an optimization ships mid-run -- the run does not
have to be thrown away, and **this is handled by the tool, not by hand**. The rule (user, 2026-08-09):

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
chunks stamped with `git rev-parse HEAD:src`, and on a play change it **keeps every FULL SET**: an
offset survives iff every cell that still owes work already holds it. Contiguous or not — cells do not
finish in order, and an earlier contiguous-prefix rule threw away perfectly good sets sitting above an
in-flight hole. Three properties this rests on (2026-08-12/13):

* **Retention is chunk-atomic, resolved to a fixpoint.** A chunk stores one MEAN over its `n` games,
  never the games, so keeping part of one would have to invent the surviving piece's score. And
  dropping a straddling chunk shrinks that cell's coverage, which shrinks the intersection, which can
  strand a chunk in another cell — so a single pass would leave cell A having dropped offsets cell B
  still holds, which is the very mixing this prevents.
* **A condemned cell gets no vote.** It is reference-only and capped, so it observes the cross-cell
  invariant rather than participating in it. Before this, an H6 row condemned at 7 games could never
  reach its own 50-game target, stayed "unfinished" forever, and capped its entire seed's banking at
  its own 7 offsets — while its ragged chunk length forced every 25-game chunk in the group to be cut
  to 7 games carrying a 25-game mean.
* **A fully-complete seed is untouched** whatever engine it ran on.

Banking is grouped per `(deck, seed)`: seeds are judged independently, so one slow seed cannot cost
the others their sets.

**Do not re-run whole cells for this.** An earlier draft of this section made the seed the unit; on
FiveColour that was 26 cells / ~10,400 games where the chunk rule is 24 chunks / 587 -- 18x more work
for no extra consistency.

## Progress and restarts

`status` prints the freeze (commit + play digest) and whether it still holds, a per-phase checklist with
completion times, rows-so-far against target, whether a model is staged, the last held-out RMSE,
matrix progress, whether games are running, and the exact resume command.

Restarting is the normal case, not an error path:

- **`run` is idempotent** — finished phases are skipped via marker files in `<queue-dir>/done`.
- **Rows are durable per row** and dedupe on `(seed, turn)`, so an interrupted dump loses only
  in-flight games; re-running picks up where it stopped.
- **To redo one phase**, delete its marker and re-run.
- **If the clock ran out inside phase A**, use `finish` instead of `run`: it accepts the rows on disk
  as final and runs B..E on them. Phase A is all-or-nothing otherwise.
- **If `src/` moved, just resume.** The driver compares the PLAY digest: identical play re-stamps the
  freeze and keeps everything; a real play change banks every full set and re-queues the rest. Do NOT
  restart from scratch on a freeze message — that mistake cost 60–70 core-hours once for a change
  that could not alter a game.

Each run gets its own queue dir (`logs/vlq_<deck>`), so a single-deck run never collides with a fleet
run's rows, table or markers.

## What it does — five phases, three tails

| phase | what | games |
|---|---|---|
| A rows | dump labelled positions at SHIPPED play (K=3 searched labels) | yes — one pooled batch |
| A split | bucket by seed, dedupe on `(seed, turn)`, sort | no |
| B train | GBDT → `logs/eval/<stem>.value.STAGED.json` | no |
| C matrix | H×V depth matrix, the whole thing in ONE pool | yes — one pooled batch, one tail |
| D metadata | derive crossover + NOMINATE a `value_trust_depth_candidate` into the staged sidecar | no |
| E measure | staged-vs-live A/B + play sweep + **trust acceptance (ON vs OFF)** | yes — one pooled batch |

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
- **Trust is NOMINATED by the matrix and DECIDED by games** (user, 2026-08-15). Phase D writes
  `value_trust_depth_candidate`; phase E runs trustON vs trustOFF on 8 held-out seeds × 1000 games
  and promotes it into `value_trust_depth` (still only in the STAGED file) iff the one-sided 95%
  bound on ON−OFF sits at or below tol. The tolerance's only job is to gate that test — it does not
  settle trust, because the matrix measures the arms separately and UNBOUNDED while trust is a claim
  about keeping leaf lines inside real budgeted play. Non-inferiority, not improvement: trust is a
  cost lever whose upside is the escalation it skips, so the claim under test is that skipping does
  not cost quality. Not accepted ⇒ no trust ⇒ everything stays eligible to escalate, which is the
  side that cannot cost quality (`docs/design/value-leaf-quality-floor.md`).
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
