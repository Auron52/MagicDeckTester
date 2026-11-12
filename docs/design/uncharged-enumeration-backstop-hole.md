# The per-game wall backstop cannot reach a top-level plan enumeration (found 2026-11-11, Hydras)

Status: OPEN, deferred. Found while the Hydras value leaf (frozen at 387b4b646) was in its phase C matrix.

## What happened

Two H4 cells (depth 4, budget 0) each had one game run past the matrix's 3.5 h hard cap
(`max_game_wall_sec` 12600) with nothing able to stop it:

- `hydras_staged_H4_s9009_off0`, gi 15 (seed 9024)
- `hydras_staged_H4_s11011_off275`, gi 287 (seed 11298)

At 23:10 UTC they stood at 4.5 h and 4.4 h. The rest of the matrix (~1 core-h of seed 9009 / 11011 chunks)
was queued behind them, with the box at ~2 of 32 cores.

perf (both threads, 8 s): ~100% inside `EnumeratePlans` -> `EnumeratePlanPositions<...>` (~67% self) and
`FungibleEquipCopyAdvance` (~13%), with memset / push_back for the rest. The stack is
`FullSearchLineHybrid -> FSLineWin/FSLineTail (x4) -> SimulateToEnd -> RolloutPreCombatSolve ->
SolveWithLookahead -> ApplySecondMainInSearch -> SolveSecondMainInSearch -> SolveWithLookahead ->
EnumeratePlansWithLand -> EnumeratePlansWithLandUncached -> EnumeratePlans`.

A one-game repro (gi 287 as a one-job manifest, same binary, `--threads 1`) reaches the same state within 30 min:
`build/scratch/hydras-night/runaway/`.

## Why the backstop misses it

- The unit ceiling and the wall deadline are both read from `gamework::Add` -> `CheckDeadline` (every 64
  units). An enumeration charges no units: "budget units are charged per consider/rollout, not per position".
- `gamework::Poll` (2026-10-21) closed this for the greedy walk and for `EnumeratePlans`' per-visit body, but
  the per-visit poll is gated by `bp_abandon_watch` (`g_bp_enum_depth > 0 || AlchemistComboRouteInProgress()`).
  The comment there states the assumption this game breaks: "Any other top-level enumeration runs to its end --
  one enumeration, after which its host's own cut (Overrun) unwinds."
- Even that poll lives in `eval_and_push_body`, so positions the odometer REJECTS before emitting are never
  polled at all.

## Remedies (none applied; each needs its own review)

1. **For the frozen run (USER's call; it is a user-requested run):** stop the batch, add the two games to
   `logs/vlq_hydras/matrix.txt.skipped.json` (`"hydras_staged|9009"` += 15, `"hydras_staged|11011"` += 287), and
   re-run `bash scripts/valueleaf.sh run decks/Hydras`. This is exactly what the hard cap would have done had it
   fired: the driver drops a skipped offset from every cell of its (deck, seed) and resumes incrementally.
2. **Engine liveness fix:** poll `gamework::Poll` on the stride inside the odometer walk itself, at every
   enumeration depth, and make a void game's truncated list unstorable (the derivation path already does this
   with `g_bp_derive_interrupted`; the plan cache and enum memo must refuse a void game's list too). This is inert
   unless a ceiling or cap is armed, and only changes games whose results are dropped.
3. **The enumeration itself:** find what makes this odometer astronomically large on this Hydras board
   (`MTG_ODO_FALLBACK_STATS` counts flat-fallback calls and positions; the two-stage split abandons to the
   ungated flat walk past `MTG_ODO_FLAT_FALLBACK`, 1e6 lines). That is the search-shape defect; 2 is only the
   backstop.

## 2026-11-12 05:09 UTC: remedy 1 applied (USER: "let's stop it and mark those games as abandoned")

Queue state copied aside first (`logs/hydras-night/prestop-20261112T0509/`). Stopped the matrix driver and its
batch (and my own repro), added gi 15 (seed 9009) and gi 287 (seed 11011) to `matrix.txt.skipped.json`, and
resumed `valueleaf.sh run` (src tree still 4fb4b4fa76 = freeze). Checked the resumed manifest against every
`.wins` file written before the stop: **0 queued games overlap a finished game** (16,048 finished, 4,731 queued);
the two killed chunks are re-split around the skipped offsets ([0,15) + [16,25), [275,287) + [288,300)), and the
pairing replacements are offsets 400-402 (seed 9009) and 400 (seed 11011). The status's "~1 core-h left" was an
underestimate: H1-H3 and V1-V8 had not yet run seed 9009 at all.

**What WAS lost: compute, not results.** `mtg --batch` reports a job only when the whole job finishes, so the
finished games of each killed chunk (up to 24 each; fewer if a job plays its games in order) existed only in the dying process and are being replayed (identical
results, deterministic). USER: "we should make sure that this doesn't lose more games than the ones that were
still running ... If it does I would like to fix that as well." Owed fix (after the leaf; it is a src change and
the generation is frozen): persist each game's result as it completes (per-game append to the job's `.wins`, or a
per-job partial file the driver merges on resume), so a killed or abandoned job loses only its in-flight games.
