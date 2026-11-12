# The per-game wall backstop cannot reach a top-level plan enumeration (found 2026-11-11, Hydras)

Status: REMEDIES 2 AND 3 LANDED 2026-11-12 (see "Landed" at the end); the per-game result persistence fix and
two USER questions remain open. Found while the Hydras value leaf (frozen at 387b4b646) was in its phase C matrix.

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
still running ... If it does I would like to fix that as well." **LANDED 2026-11-12 (P10, branch
`phase-1-2-deck-analyzer`; see `src/runner/BatchPartial.h`).** Every finished game is appended, flushed, to
`<game-log-dir>/<job>.partial` (one self-checksummed line: gi, seed, win turn or abandoned, digest, units, ms,
the per-game ceiling it ran under) before it counts as finished; a torn last line is dropped. `mtg --batch
--resume` prefills every matching game (same job identity = play-relevant manifest entry + binary + cards.json
+ MTG_* env + deck/profile-dir stats; same global gi and seed; same ceiling) and plays only the rest, so a
resumed job's `.wins`, avg, digest, units and played are byte-identical to an uninterrupted run (ms: each
reused game contributes its recorded ms). Records are keyed by game, not chunk name, so the matrix driver's
re-split chunks find them. Calibration games of a relative-ceiling cell are never persisted (always replayed).
Partials are removed when the whole batch finishes cleanly. Persistence is OPT-IN by `--resume` (a resumable driver passes it on its first run too): a caller without it -- the regression harness, deck_compare, mullgen -- writes nothing, so it adds no per-game write traffic to the 9p host mount (`docs/design/repo-mount-failures.md`). `valueleaf_depth_matrix.py` (pool and leafcal) and
`valueleaf.sh` phase E pass `--resume`. Not applied to the queue running at the time (it is frozen on its own
src tree); it takes effect for any queue started, or resumed, on a tree that has it.

## 2026-11-12: Landed (P8 investigation, P9 landing)

Mechanism (P8, `logs/hydras-research/P8-runaway-enum.md`): a horizon rollout at turn 5 main 2 with a 32-card hand
(mass draw) re-enumerates once per GROUP-WAVE tranche (budget 0 walks every tranche); each enumeration's two-stage
split exceeds `MTG_ODO_FLAT_FALLBACK` and takes the ungated FLAT walk (1e10-6e14 positions), and Gargos sets the
gate's `block` term so every Gargos position passed the walk and was refused one at a time at emission.

Landed on `phase-1-2-deck-analyzer`, three levers (each with a `=0` hatch):

* **`MTG_ODO_FLAT_GATE_CUT` (ON)** -- remedy 3: a sound, order-preserving radix cut in the flat fallback (skip a
  block when a lower bound on need-minus-budget over every completion exceeds the pool). `_VERIFY` on the gi 287 /
  gi 391 repros (900 s each, self-exclusion on): 3.5e9 + 3.4e9 skipped positions, **0 violations**. Unit tests
  check the cut against the whole walk (identical emission sequences) on a Hydras board and 3000 random gates.
* **`MTG_ENUM_WALK_POLL` (ON)** -- remedy 2: `gamework::Poll` once per odometer position on the
  `kWalkAbandonStride` stride, at every depth (flat / two-stage `stop()`, the inline walk, the plain DFS walk,
  Solve's walks); a void game's truncated list is never stored (unit test). gi 287 / gi 391 now end at the wall
  cap to the tenth of a second (900.0 s). Inert unless a ceiling or cap is armed.
* **`MTG_SEL_GATE_SELF_EXCL` (ON)** -- a generic same-turn reducer never discounts its own cast. Held-out A/B
  (seed base 70707, 12,000 paired games over Dragons / Dragonstorm / Giants / Goblins / Hydras / KittyEquipment at
  d3 b10 and the d5 play rows): play digests byte-identical in all 480 chunks, net 0, units equal or lower on
  every deck. `MTG_SEL_GATE_EXACT_VERIFY`: 0 violations over 2.5e9 refusals on the two repros.

Smoke (195) and regression (309) tiers: 0 play changes with the cut + poll (self-exclusion off); with it on,
regression 0 again and smoke 0 except `extraturns2hg_smoke_d5_s1001` gi 19 (play digest only, score unchanged),
which reproduces GT solo (`cc399f1a95984da0`, identical units in both arms; Extra Turns holds no soft reducer):
the known pooled-run residency drift on the Extra Turns rows, not this change. No GT was accepted.

Still open:
* **gi 287 at d4 b0 is still not tractable** (P8: unfinished after 28 min under the cut). The residual is per
  EMISSION: half the admitted selections die at `ef-scalar` (coloured pips over the credited ceiling). Next sound
  lever: a coloured-pip term priced in the cut. It needs a per-selection upper bound on the ef ceiling (credited
  `eff` pool + aura bonus + the optional ETB-untap refund), so it was NOT built in this change.
* USER questions (not built): (a) should a budget-0 generation cell walk every group-wave tranche to the full hand
  (completeness) or is that a settings change; (b) should the walk CHARGE units per N positions so the unit
  ceiling bounds these games deterministically (changes every game's units and the matrix calibration).
* The per-game result persistence fix above (a killed job loses only its in-flight games): LANDED 2026-11-12 (P10).
