# Antilife value-leaf deep-cell matrix (V6–V8, H6–H7) — INCREMENTAL plan (approved, for later)

Regenerate the **deep cells of antilife's value-leaf × heuristic depth table** (the "play profile table" that
calibrates `value_trust_depth` / `value_fallback_crossover` in `decks/Anti-Lifegain/Anti-Lifegain.value.json`).
**Not run yet — saved for a future run at the user's direction.**

## What went wrong on the 2026-07-18 overnight attempt (do NOT repeat)
- Ran `scripts/valueleaf_depth_matrix.py` monolithically. **That script only writes the table AFTER all 4
  seeds × all depths finish** — so it produces *no* incremental output, and any stop loses everything.
- Antilife **UNBOUNDED** deep search (d5–7) has an extreme **heavy tail**: a few combo hands explode into
  multi-hour searches. H5+H6 for a *single* seed took ~5.8 h; the run died at ~6.4 h having written only the
  header. **Net yield of the whole overnight window: zero saved cells.**
- Compounded by process-management thrash (duplicate detached launches, doubled logs, premature kills).
- Lesson: **never run a long generation that only reports at the end; always save per-unit incrementally, and
  never kill a run that could have produced partial results.**

## Approved method (user, 2026-07-18) — the ONLY acceptable way to do this
Measure **UNBOUNDED (no budget, no timeout)** — the true nominal-depth quality — but make it incremental,
breadth-first, and tail-tolerant:
1. **Round-robin 50-game rounds across ALL cells (breadth-first).** Each *round* adds 50 games to **every**
   still-growing cell. So after round 1 the WHOLE table exists at 50 games (a full, noisy snapshot); round 2
   brings every cell to 100; then further rounds refine. You see the table's *shape* immediately and it
   sharpens *uniformly*, instead of one cell crawling to 1000 while the rest stay blank. This is the ideal the
   user called out: "a batch that hits each cell we are missing 50 games at a time, and gives us progress."
   **Purpose = REFERENCE POINTS, not uniform 1000.** The whole point of these deep cells is to *fill in the
   table* — a usable value in every cell. Crucially, **a cell that is slow is slow because it's not usable in
   production at that depth anyway**, so **50–100 games is enough for it** (a reference point); pushing an
   unusable cell toward 1000 just burns compute. So the per-cell target is ADAPTIVE:
     - **Slow cells (deep, heavy-tail, not production-usable): cap at ~50–100 games** — reference point only.
     - **Fast cells (the usable ones): keep adding rounds** toward higher accuracy (up to 1000).
   Concretely: after each round, a cell whose batch wall-time exceeds a "usable" threshold **stops growing**
   (marked with its game count); cells that are still cheap keep going. Net effect: rounds 1–2 give the full
   reference table quickly, and later rounds spend compute only where it's actually useful.
2. **Every result written incrementally.** After each 50-game batch, update that cell's cumulative
   `(games_done, running LP, ms)` in a persistent file. Nothing is ever lost; the run is **resumable and
   stoppable at any time** with all completed batches preserved.
3. **Mark each cell's sample size** (e.g. `H7 s8008: 150 games`) so partial cells are explicit and not mistaken
   for full 1000-game cells. A cell drops out of future rounds when it reaches the target OR its batches are so
   slow they'd starve the rest (kept at whatever sample it reached, marked partial).
4. **Cross-cell core sharing (work-stealing).** All cells' 50-game batches for a round go into a shared worker
   pool concurrently. When one cell is down to a few long-tail games (leaving cores idle), other cells' batches
   — including the *next* round's — take those cores, so the box stays fully utilized and a single cell's tail
   never idles the machine, **while results still stream in per batch.** (Rounds may overlap: fast cells can be
   on round 3 while a slow cell is still finishing round 1 — that's fine, each cell just tracks its own
   games_done and every batch is saved as it lands.)

## Cells to measure
- Deck: **antilife**. Seeds: **8008 9009 10010 11011**. UNBOUNDED (budget 0), `--ignore-play-profile`.
- **Heuristic arm** (`MTG_VALUE_MODEL=0`): depths **5, 6, 7**.
- **Value arm** (`MTG_VALUE_MODEL=1`, `MTG_VALUE_MIN_DEPTH=0`, `MTG_VALUE_STARTGATE_ALPHA=8`,
  `MTG_VALUE_PROFILE=decks/Anti-Lifegain/Anti-Lifegain.value.json`): depths **5, 6, 7, 8**.
- Depth 5 = **consistency anchor** vs the committed table (`H5=4.0885`, `V5=4.0975`). If fresh d5 doesn't
  reproduce, the engine drifted since the table was built → the whole table (d1–7 / d1–8) needs regen, not
  just the tail.

## IMPLEMENTED in `scripts/valueleaf_depth_matrix.py --incremental`
This is now a first-class mode of the generator (not a bespoke driver). It does everything below —
round-robin 50-game batches, per-batch incremental writes to `<out>.cells.json` (+ a parser-compatible
`<out>` table), resume, tractability marking from the first (and every) batch, adaptive reference cap, and a
work-stealing pool. Validated: batched cell LP == monolithic (exact); resume skips done cells; a slow cell
trips the threshold on its first batch and caps at `--reference-target` (marked `*`).

Exact command for the antilife deep cells:
```
python3 scripts/valueleaf_depth_matrix.py --incremental --decks antilife \
    --seeds 8008 9009 10010 11011 --hdepths 5 6 7 --vdepths 5 6 7 8 \
    --value-min-depth 0 --target 1000 --batch 50 --reference-target 100 \
    --intractable-sec-per-game 2.0 --workers 20 \
    --out logs/eval/valueleaf_matrix_antilife_deep.txt
```
Watch progress live: `cat logs/eval/valueleaf_matrix_antilife_deep.txt` (rewritten each batch) or the
per-cell `.cells.json`. Stop any time (Ctrl-C / kill) — all completed batches are saved; re-run the same
command to resume. `--workers` × ~1GB (antilife keep model per process) must fit RAM; 20 ≈ 20GB.

### Design notes / sketch
- **Batch = (cell, game-index range `[50k, 50k+50)`).** Run each batch **single-threaded** (`--threads 1`) so
  one batch = one core: a heavy-tail game ties up ONE core, not the box. Run ~N=cores batches concurrently
  across all cells (work-stealing pool).
- **PREREQ TO VERIFY FIRST:** `mtg` must run a **deterministic game-index range** (base offset + count) whose
  per-game seeds match a full run (`seed_gi = base_seed + gi`), so that concatenating batches reproduces the
  monolithic 1000-game LP exactly. Check `--game-index` / `base_game_index` support; if only a start-index
  exists use it, if neither, add a `--game-start`/`--game-count` pair. Batches MUST be disjoint + reproducible
  or the accumulation is wrong. (A quick sanity test: batched 2×50 == monolithic 100 for one cheap cell.)
- **Scheduler / controller (round-robin, work-stealing):** maintain per-cell `games_done` and `next_offset`.
  Fill the worker pool by cycling through the cells that are `< target`, handing each its NEXT 50-game batch
  (`[next_offset, next_offset+50)`); when a worker frees, hand out the next incomplete cell's next batch —
  which naturally advances the *lowest-games_done* cells first (breadth-first) while idle cores from a slow
  cell's tail get consumed by other cells' batches. On batch completion: (a) fold the 50-game LP/ms into the
  cell's running mean, (b) write the cell's line to the results file, (c) advance its `next_offset`. Drop a
  cell from the rotation at `target`, or optionally if its batch wall-time is starving the rest (mark partial).
  Prioritise by `games_done` ascending so the whole table stays roughly level round to round.
- **LP accumulation:** cells use equal 50-game batches → cumulative LP = simple mean of batch LPs weighted by
  games (all 50). Store per cell: `games_done, cum_lp, ms`. ("avg (turns)" is already the loss-penalised avg
  win turn; combine batches by `sum(lp_b * 50)/sum(50)`.)
- **Results file:** one line per cell, rewritten as it grows, e.g.
  `antilife H d7 s8008 games=150 lp=4.7100 ms=... status=partial`. Resumable: on start, read it and resume
  each cell from `games_done`.

## Merge / validation when results exist (do NOT auto-commit play changes)
1. Confirm each cell's game count; treat partial cells as lower-confidence.
2. **Consistency-check** fresh H5/V5 vs committed 4.0885 / 4.0975.
3. Inspect whether the H5→H6 / V5→V6 worsening anomaly reproduces (real max_turns=8 horizon effect vs noise).
4. Deep cells (d>5) do NOT move `value_trust_depth` (derivation caps at `h_conv_depth_cap=5`); they extend
   `value_fallback_crossover` only at deep committed depths (rarely reached at d5 play). Dry-run
   `scripts/valueleaf_table_to_metadata.py <results> --decks antilife --dry-run`; **smoke must stay
   byte-identical (all `play_digest`s)**; then present for review before committing.

## Prereqs already handled
- Generator staleness fixed + committed **39855f0** (`--ignore-play-profile` + merged-metric `avg (turns)`
  parse) — needed because antilife's enabled `value_play` block otherwise rejects an explicit `--depth`.
- **Memory fix 82859f7** (antilife load 6.2GB → ~1GB, flat across threads) — the enabler for running antilife
  deep at all without OOM.

## Other in-flight state (context)
- d3 beam adoption paused mid-analysis. Budget-10 finding so far: the beam is **quality-positive** at a real
  budget on antilife (`vbeam3_ld1` −0.0125, faster). The 6-deck budget-10 sweep was interrupted; re-run when
  desired (memory is now safe). See `docs/design/escalation-beam-verify.md`, `scripts/esc_fallback_ab.py`
  (`ESC_AB_DEPTH` / `ESC_AB_BUDGET`).
