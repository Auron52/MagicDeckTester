# Value-leaf skill

The value leaf is a learned O(1) evaluator that replaces the search's horizon rollout. This skill is
the whole process for **building one for a deck** — new deck or regeneration — and deciding whether
to ship it.


## THE ENGINE IS PGO+LTO. GENERATION RUNS ON `build/PGO` (user, 2026-10-08)

USER: *"we should be using PGO+LTO for everything except maybe quick development cycles."* A value
leaf is a multi-hour-to-multi-day run (the Hinata/Mirrorwing class is ~36 h), so a flat **~1.3x** off
every phase is the largest free win available to this skill — hours, for a one-off ~10-15 min build.

**`scripts/valueleaf.sh run` already does it** — it calls `harness_pgo_ensure` before phase 0
(`scripts/valueleaf.sh:1134`) and then logs which engine it picked, so **read that line** rather than
assuming:

```
engine: ./build/PGO/mtg (PGO+LTO; MTG_PGO=0 pins Release)
```

Every phase resolves its binary through `harness_bin`, and the **matrix** phase goes through
`scripts/engine_bin.py`, so all five phases and both tails are covered by the one call. Requirements:

* **COMMIT `src/` FIRST.** `build/PGO` is used only while its `SRC_TREE` stamp equals `HEAD:src`
  **and** `src/` is clean; a dirty `src/` silently falls back to `build/Release` and you lose ~25% of
  a multi-hour run with **no warning**. This is already implied by Rule 0 (generate on ONE frozen
  commit), so it adds no constraint you did not have.
* **`MTG_PGO=0` pins Release**, and must be set on **both** arms of any wall-clock comparison.

**IT CANNOT CHANGE WHAT THE LEAF LEARNS, AND THAT IS THE LOAD-BEARING POINT HERE.** The value leaf is
*fitted to the deck's play*, so a compiler flag that altered play would invalidate the model. PGO/LTO
change code layout, inlining and branch prediction only — **no `-ffast-math`, no `-march`** — and are
measured byte-identical on both the rollout digest and the play digest
(`docs/design/selesnya-keepgen-bulk-cost.md`). Consequences worth stating, because each one is a
question somebody will otherwise ask:

* **The PLAY DIGEST is unchanged**, so Rule 0's "one consistent play digest" is not broken by
  switching to PGO — including mid-run. A chunk produced under Release and a chunk produced under PGO
  are *the same chunk*, and the mismatch harness will agree.
* **Rows, labels and the fitted model are identical**; only wall-clock moves. So a leaf trained under
  PGO needs no extra verification beyond what this skill already prescribes.
* **`units` is unaffected by construction** (a work counter, not a clock) — which is why cost
  comparisons in this skill should be read in `units`, not `ms`, whenever the engine build may differ.

## HARD PRECONDITION — the deck MUST be in the regression suite (user directive, 2026-09-26)

`scripts/valueleaf.sh run` **refuses to start** (exit 3) unless every deck in the queue resolves to a
key in `test/regression_cases.sh`. The check runs per deck, before phase 0 freezes a commit and before a
single game is played, so nothing is spent and nothing is left half-frozen.

```
python3 scripts/suite_gate.py --require decks/<Deck>    # what the driver calls
python3 scripts/suite_gate.py --cost    decks/<Deck>    # the 3x rule (see analyze-deck Stage 5j)
```

**Why it matters here in particular:** a value leaf is *fitted to the deck's play*. A deck with no suite
cases has no ground truth, so nothing detects when an engine change breaks that play — you would be
spending hours fitting a model to a foundation nothing is checking, and Rule 0's freeze protects only
the generation window, not the months after it.

A deck may only be ADDED to the suite if its cost is ≤ **3x** Hinata's — **PINNED to Hinata** (USER 2026-10-22: *"We shouldn't be arbitrarily increasing the limit"*; the old wording ratcheted the limit up with every expensive admission), and `--cost` times Hinata in the same run. If it is over: report it to the user and make getting it into
range the first goal. `MTG_ALLOW_UNTESTED_DECK=1` overrides and is a USER decision, never an agent's.

## NOTHING ELSE RUNS WHILE THIS DOES (user directive, 2026-08-31)

This generation owns the box. No mulligan gen, no `recommend` scout, no regression tier, no screen
alongside it -- generation stages are strictly serial (CLAUDE.md). **It now runs AFTER the mulligan
profile** (USER 2026-10-18, "at least for the time being": the no-leaf optimizations leave the leaf
underperforming, and without a keep table lookahead bottoming dominates every game this run plays --
Galadriel ~96% of CPU at d3 b10 -- while the keep table also fixes the hand mix the leaf is fitted on).
Two reasons beyond the obvious core-splitting:

* **Downstream stages depend on this one's OUTPUT.** An adopted leaf becomes `<deck>.value.json`, whose
  `value_play` the mulligan generator reads. (The mulligan-generation setting screen is no longer a
  value-leaf phase: USER 2026-10-18 moved it to `mullgen.sh run`, which screens a deck once before its
  first keep table and carries a profile-screened setting into a later sidecar.)
* **This model changes performance by 1.35-84.8x.** The H-cell ladder is guarded on the sidecar
  existing, so any timing measured while this is still running (or before it lands) describes the
  slow path and is not a number anyone can plan with.

Corollary: do not create or edit a `.value.json` for this deck while the run is live. Sidecar
PRESENCE activates the hybrid in play, so writing one changes the very play being fitted.

## NEVER BLOCK THIS RUN ON A QUESTION (user directive, 2026-08-31)

Generation is the long pole — tens of hours. **Nothing may hold it up pending a user reply.**
Surface any question in your message so the user sees it if they are reading, then start/continue
the run in the same turn. Do not call `AskUserQuestion` and do not end a turn waiting.

Apply the one-line test before letting anything wait: *does this answer change what the run
computes?* Generation depends on **play correctness** (mismatch harnesses, play-invariants,
claude-play sweep) and on the frozen commit. It does **not** read the viewer decision manifest,
card-param classifications, naming, or docs — so a question about any of those cannot block it.

This is written from the failure: on 2026-08-31 a ~36 h Mirrorwing value-leaf run was left
unstarted on a just-freed 32-core box while the agent blocked on whether three `cards.json` params
were inert — viewer paperwork with no bearing on the run at all. The window was the user's weekend
and it did not come back. Only two things genuinely stop you: a destructive/irreversible action, or
a fork where either choice would waste the run (which deck, which list, which commit to freeze).

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
| NO quality-based rung condemnation at all | REMOVED 2026-08-21 (user: "I can't think of a reason to have it now that H6 is gone"). It condemned a rung once a paired equivalence test bounded the extra depth below 0.0075 turns, ignoring the d<=5 floor, and its stated payoff was capping H4->H5 — the only expensive rung there was. With H6 gone and the ladder capped at H5/V8 that payoff no longer exists, and only the harm did: it stops collecting at a sample calibrated to its OWN 0.0075 threshold (3/0.0075 = 400 pairs) while phase D pairs every row over one GLOBAL intersection and needs 0.0020 (~1500 pairs) from those same games. On KittyEquipment it capped V6/V7 at 120 games/seed against a 390 target on a perfectly flat sample, which dragged the paired set for rows holding 1560 games each down to 488 — resolution 0.00615 vs tol 0.0020 — and left `trust=UNSET`. It saved ~9 core-h of a run that finished in 2h18m against an overnight budget: it traded the deliverable for a saving nobody needed. Cost-based condemnation (`--intractable-median-sec-per-game`) is untouched |
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

## What it does — the phases and their tails

| phase | what | games |
|---|---|---|
| A rows | dump labelled positions at SHIPPED play (K=3 searched labels) | yes — one pooled batch |
| A split | bucket by seed, dedupe on `(seed, turn)`, sort | no |
| B train | GBDT → `logs/eval/<stem>.value.STAGED.json` | no |
| C0 leaf calibration | per deck: paired H3..H5, 1-ply vs greedy horizon leaf → the depth K from which the matrix's H cells use the greedy leaf | yes — one pooled batch, short tail (10M-unit ceiling) |
| C matrix | H×V depth matrix, the whole thing in ONE pool | yes — one pooled batch, one tail |
| D metadata | derive crossover + NOMINATE a `value_trust_depth_candidate` into the staged sidecar | no |
| E0 emul R | the emulated ladder's rollout cost per leaf (`emul_r`), per deck, on the staged model at its final play settings | yes — one pooled batch, play-settings games (short tail) |
| E measure | staged-vs-live A/B + shape arms + **trust acceptance (ON vs OFF)** | yes — one pooled batch |
| E.5 adopt | install the picked leaf / shape in this tree | no |
| E.7 play setting | **decide** the live sidecar's enabled `value_play` (depth x budget grid, over every phase-E shape still in the running) | yes — one pooled batch |
| E.7b trust re-check | phase E's trust acceptance again, on the live sidecar, for a deck whose setting E.7 moved | yes — one pooled batch, only when needed |
| E.6 skip allowance | the deck's escalation-skip allowance on the live leaf, at the setting and trust that ship | yes — up to three short batches |

Every batch is a barrier that returns only when its slowest game finishes, so what matters is **how
many times you pay that tail**, not how big each batch is. Three is the floor and phase C now sits at
it — the phase boundaries are real dependencies (the matrix measures the model; the A/B measures the
table), and nothing inside a phase is allowed to add a barrier of its own.

**A better leaf is adopted automatically, and the run NEVER stops for a decision** (USER 2026-10-11:
*"We should adopt automatically if the leaf is better. Nothing prevents you from updating or asking the
user as long as you do not stop."*; *"Is it better is primarily a performance metric, but we may need to
wait for a user response if the quality is notably worse"*; *"that just means we do not adopt and
continue with any other work that remains"*). Phase E.5 reads phase E's A/B (`vlq_ab_report.py
--verdict`): the staged leaf is installed as the live `decks/<Deck>/<stem>.value.json` **in the run's
checkout** iff it is **cheaper** (work-units ratio < 1; never core-s, which is wall-clock and moves with host
contention -- USER 2026-10-14: *"do not make decisions based on wall-clock"*) and its quality is **not notably worse** (not: delta >
0.002 turns at paired t > +2). Otherwise it stays staged -- a dearer-but-better leaf is a TRADE for the
user -- and the run carries on. Every such decision, and any default the C.5 risk gate takes instead of
stopping, is appended to `<queue>/DECISIONS.txt`, which `status` and the COMPLETE line print until a
human deletes it. **Read it and report it to the user**; it is how the run asks without waiting.

Installing is adoption in that tree only: nothing is committed. Committing it -- with GT for the deck's
smoke + regression keys re-accepted, since the leaf changes play (the overnight tier is left to the
periodic run) -- is the next step. (There is no phase F any more: the mulligan-generation setting is
screened by `mullgen.sh run`, once per deck, USER 2026-10-18. A sidecar's `value_play` REPLACES the
profile's generation keys wholesale, so that stage copies a profile-screened setting and K into a newly
installed sidecar rather than re-screening.) Soldiers 2026-10-08 (`docs/design/analysis-soldiers.md`) is the
hand-run precedent; Prevent Damage 2026-10-11 is the trade case (-0.036 t at 1.21x core-s).

**Phase E also screens the leafless shapes, and E.5 can adopt one instead of the model** (USER 2026-10-14:
*"Do we have a no-leaf screen in the pipeline anywhere? I think we probably should."*).
* **Why:** a model is only worth its cost if it beats the no-leaf menu shapes
  (`docs/design/per-deck-search-shape.md`). Snow's first leaf measured -0.004 t at 1.33x against plain
  rollouts, a shape that menu had already dropped as dominated on every deck it measured.
* **The arms:** phase E plays `nlesc` (#2, `{ladder: escalation, leaf: none, alpha: relaxed}`) and `nlfit`
  (#3, `{ladder: single, leaf: none, alpha: relaxed}`) beside `live` and `staged`, on the same seeds.
  * Each is built on the sidecar route: the live sidecar plus the shape keys, which is exactly the file
    that would be installed. It is never the `noleaf` stand-in; see that doc's finding 12.
  * A shape that live already ships is skipped.
* **The pick:** `scripts/vlq_adopt_pick.py` judges each candidate against live by the rule above.
  * Among the candidates that pass, it takes the cheapest, unless another passing one is notably better
    than it.
  * It records every verdict and the pick in DECISIONS.txt.
  * The shape sidecars are kept as `<queue>/<stem>.value.NLESC.json` / `.NLFIT.json`.
* **Coverage:** the shapes are measured at play settings only.

**Phase E also measures the EMULATED LADDER, and E.5 can adopt it** (USER 2026-10-21: *"Let's add this mode to
the check in the value-leaf."*).
* **The shape:** `value_play {ladder: "emulated", emul_r: [...]}` on the STAGED model. Warm-up passes run on the
  model; the committing pass runs on the rollout leaf at the depth the all-rollout ladder would reach (its start
  gate replayed on reconstructed rollout costs, value-pass cost + R x leaves); the model's preferred plan breaks
  the rollout's ties at the root (`MTG_ID_HINT_ROOT=3`, the default). The line played is always a rollout line.
* **Phase E0 measures R first** (`scripts/vlq_emul.py`): 2 x 200 play-settings games per deck with per-game
  calibration, read back per deck from the `emul R by depth, deck=` lines `MTG_ROLLOUT_STATS` prints, frozen into
  the arm's sidecar as `emul_r`. A per-game R costs more than it saves on a deck with few searched decisions
  (Landfall 1.11x live), which is why it is frozen. E0 is a real dependency (R needs D's play settings), so it is
  its own short batch; a deck with no learned model, or no usable R, gets no emul arm and the log says so.
* **The arm** is `emul`, on phase E's A/B seeds beside `live` / `staged` / `nlesc` / `nlfit`, and E.5's pick
  (`vlq_adopt_pick.py`) judges it by the same rule; the file is kept as `<queue>/<stem>.value.EMUL.json`.
* **Evidence:** Landfall -0.0355 turns at 0.93x units / 0.996x instructions of the default; fleet screen
  2026-10-21: KittyEquipment and Melira Pod ahead at equal units, most hybrid decks 1.2-3.2x the units for no
  gain (their trusted model line ends the decision cheaply). `docs/design/leaf-vs-heuristic-asymmetries-2026-10-20.md`.
* **Caveat:** the cost test is in work units, and units under-price tree work against rollouts -- the emulated
  ladder paid ~5-10% more instructions per unit than the rollout ladder on Landfall. A marginal "cheaper" verdict
  for `emul` is worth a callgrind check before committing it.

**Phase E.6 decides the deck's escalation-skip allowance** (`escalation_skip_r`, USER 2026-10-13).
It runs LAST, after E.5, E.7 and E.7b (USER 2026-10-22; it used to run before E.7): the allowance is a measurement
of the probe that ships -- the leaf now live, at the play setting and trust that ship (Vampires' E.6 had run at d5
before E.7 moved play to d6) -- and it can change play, which the mulligan stage is fitted to. (The run order is
E.5 -> E.7 -> E.7b -> E.6; a queue that finished them in the old order resumes fine, each has its own marker.) `scripts/esc_skip_stage.py` does it in three pooled batches.
* **Probe:** 1200 games at R = 0. It gives up below **4% headroom** (discarded ladder-escalation work as a
  share of all work), which is most decks, and costs seconds.
* **A/B:** a held-out paired A/B of the fitted q10 per-depth vector against R = 0.
* **Isolation:** every moved game replayed alone, to separate real play changes from batch flicker.

It adopts only with no real slower game, installs into the live sidecar in this tree like E.5, and records
every outcome in `DECISIONS.txt`. When the deck already has a key, that key is a third A/B arm and the bar
picks between them. Both pass: whichever saves more. Only one passes: that one. Neither passes: the key is
removed, back to R = 0. `bash scripts/valueleaf.sh skipcal
[decks/<Deck> ...]` runs it alone for leaves that are already live (backfill, or a hand-installed leaf). An
adopted allowance is committed like a leaf, with the deck's smoke and regression GT re-accepted.
Design and evidence: `docs/design/escalation-skip-calibration-stage.md`.

**Phase E.7 decides the deck's PLAY SETTING** (USER 2026-10-22: *"The leaf should have a stage that decides."*).
A `value_play` block drives play only when it is `enabled`, and the route to enabling one used to be a manual
recommend -> accept exercise that nothing performed any more: 18 of the 30 live sidecars had no enabled block, so
those decks played the built-in d5 / 20 without anything having decided it. Phase E's old target_depth sweep (d-1,
d, d+1, all at budget 20) only printed a table and is gone; on Vampires every depth tied because the budget binds,
so depth alone was the wrong axis. `scripts/play_setting_stage.py` does it in ONE pooled batch, right after E.5
(it measures the leaf that ships) and before E.7b and E.6, which are measurements AT a play setting.
* **Arms:** the live sidecar as it stands (`live`: its enabled block, or the built-in d5 / 20) against enabled
  blocks over depth {d-1, d, d+1} x budget {b, 1.5b, 2b} -- **never below depth 5** (USER 2026-10-29: *"It should be
  depth 5 minimum for play settings"*; lower depths clamp to 5) -- each on the sidecar route (exactly the file that would be
  installed) in a symlinked variant folder, so all arms share the keep table's cache entry. 8 held-out seeds x 1000
  paired games per arm, the leaf A/B's size.
* **The other phase-E shapes get the same grid** (USER 2026-10-22: *"My biggest concern is actually that it didn't
  happen automatically for this deck"*). Phase E judges staged / nlesc / nlfit / emul at ONE budget, but a budget is
  part of a shape (the 2026-10-21 fleet screen's equal-cost curves: KittyEquipment's emulated ladder won at b14,
  Melira Pod's at b60; `docs/design/leaf-vs-heuristic-asymmetries-2026-10-20.md` s1, s8). So `run` passes the
  queue's shape log (`--phase-e`), and every shape still in the running is gridded at the same points, its centre
  included: each one E.5 passed but did not install, and each dearer one that was NOTABLY better than live. An
  adopted shape arm (`emul_d5b10`, ...) replaces the whole live sidecar; the previous one is kept in the work dir.
  `--live KEY=FILE` replays the stage as if FILE were live (a what-if; never with `--write`).
* **Rule:** E.5's, through `vlq_adopt_pick.py` unchanged: the CHEAPEST arm in work units whose quality is not
  notably worse than live (not: delta > 0.002 t at paired t > +2), unless a passing arm is notably better than it.
  A dearer-but-better arm is a TRADE, recorded for the USER and never adopted, so E.7 can only keep or lower cost.
* **Block:** target_depth, budget_ms, enabled -- nothing else changes: every other key of an existing block (shape,
  `escalation_cap`, `emul_r`, ...) is kept, so the grid isolates depth x budget. (The old sweep also forced
  `escalation_cap = min(depth, max H)` on every arm; on Vampires that cap alone cost 1.05x units at identical quality
  and skewed every cell -- the cap is a shape question.) The arm equal to the baseline's numbers is not run when
  live's block is enabled; with NO enabled block the enabled d5 / 20 arm can still differ from live (an enabled block
  arms the static W20 beam on every depth-3 search; the d5 beam stays off unless the block sets `beam_width`), so it
  is run like any other (Vampires: byte-identical). The profile's
  mulligan-generation keys (`mull_gen_*`, `expected_buckets`) copied in, because a sidecar's `value_play` replaces
  the profile's and losing `expected_buckets` would disarm the keep table's K guard. The install appends to the
  block's `note` (a changelog).
* **Never below the current budget** (USER 2026-10-22: *"Why are we even considering b10 for play settings?"*). The
  cost-first rule was set for the leaf (a cheaper evaluator, same search); a lower budget is just less search -- on
  an untrusted deck it is what pays for the escalation that corrects the leaf -- and the suite never plays it (its d5
  cases pass their own budget). Higher budgets stay in the grid: *"Sometimes the higher budgets will actually turn
  out particularly well, but that is primarily where you have trust"* -- a budget that completes the trusted depth
  ends decisions on the leaf and can cost less; otherwise a dearer arm is a TRADE for the USER.
* **Cost is not monotone in budget:** a smaller budget can verify fewer leaf lines and escalate more, so it is
  measured, never assumed. In the suite an enabled block sets the d5 cases' DEPTH only (their budget is the
  case's), and the d0/d3 cases pin depth with `ignore_play_profile` -- but an enabled block arms the static d3 beam
  whatever the depth, so enabling a block on a deck that had none moves its d3 rows too.

**Phase E.7b re-checks TRUST when E.7 moved the setting** (USER 2026-10-22). Phase E decides trust at the staged
model's setting, and which unverified lines `value_trust_depth` spares depends on the depth and budget played.
`scripts/trust_recheck_stage.py` re-runs phase E's acceptance (same arms, same seeds, `vlq_trust_accept.py`
unchanged) on the live sidecar for each deck whose setting moved and that has a trust candidate or depth; accepted
-> the depth is installed, rejected -> none (failing safe). Nothing to do for a deck E.7 left alone. Vampires: the
d5 candidate never engaged at d5 / 20 nor at d6 / 20.

`bash scripts/valueleaf.sh playset [decks/<Deck> ...]` runs it alone for leaves that are already live (the backfill
of the undecided ones). An adopted setting is committed like a leaf, with the deck's smoke and regression GT
re-accepted.

**Phase C0 decides which H cells may use a greedy horizon leaf** (USER 2026-10-14). On a wide deck the
1-ply leaf is nearly all of a deep H cell's work (Snow's 2026-10-13 run abandoned 18% of its games at the
per-game ceiling, almost all at H4/H5), while on combo decks it decides real games -- so no single
threshold is free, and the stage measures it per deck: 50 paired games per H cell >= 3, K = the shallowest
depth from which every cell moved no win turn. It is the one barrier that is a real dependency of the
matrix (K picks the H cells' leaf), and it is short: its games run under a 10M-unit ceiling. The verdict is
recorded in `DECISIONS.txt`; the matrix stamps the thresholds and refuses to resume a table banked under
different ones. A queue that predates C0 runs it on its next `run`, and its banked matrix must be moved
aside first. A 50-game screen cannot certify the rule at the derivation's 0.002-turn precision, so
**whenever C0 adopts a K, the final product is also A/B'd with the rule on and off** (USER 2026-10-14):
phase D re-derives the H-dependent settings under the worst-case shift C0's pairs allow, and if any
play setting moves, phase E plays that `leafOFF` sidecar against the staged one on the A/B seeds; ON
ships unless notably worse. Evidence: `docs/design/snow-matrix-leaf-fidelity.md`.

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
