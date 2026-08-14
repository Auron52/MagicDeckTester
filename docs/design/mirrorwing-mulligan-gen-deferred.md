# Mirrorwing mulligan generation — DEFERRED (2026-08-15), and what unblocks it

**Status:** the exhaustive keep/bottom profile for `decks/Mirrorwing Dragon` is NOT generated and
generation is deliberately deferred. The deck plays on defaults + the adopted value-leaf sidecar
(`Mirrorwing Dragon.value.json`, presence-only, `mull_gen_depth=3` / `mull_gen_budget_ms=3`).

## Why deferred (the measurement)

User rule (2026-08-14): run the `--gen-mulligan recommend` scout at d3/b3; start the fast profile
only if the R=1 sweep is comfortably under ~2h. It is not:

- K=16 buckets → **202,878 distinct size-7 hands** (292,855 total with sub-sizes);
- the R=1 sweep covered 89k/202k cells in ~55 min on 32 threads with a **declining** rate — the
  sweep alone busts 2h, and a full `fast` gen (R30 + sub-tables) is a large multiple;
- the tail is the problem, not the mean: single R=1 rollouts at d3/b3 ran 61s, 74s, 139s, 331s,
  516s, and **1442s** — dork-flooded no-win hands (many Elvish Mystic, no payoff) hitting Class B
  no-win exhaustion in every rollout turn.

## What survives from the attempt

- `decks/Mirrorwing Dragon/Mirrorwing Dragon.keepmodel.exhaustive.raw.json.slow.log` — the
  slow-cell corpus: reproducible seeds + hands for the 1–24-min cells (the profiling target).
- The 89k-cell journal existed but is **STALE**: it was stamped pre-rebase (commit `0d9a8930`-era
  src), and the 2026-08-15 rebase onto origin changed `HEAD:src` — a re-run will fingerprint-
  invalidate and restart. Do not count on resuming it.
- `fix(analyzer)` `0d9a8930` (now in the rebased chain): `value_play` blocks carrying ONLY
  `mull_gen_*` keys are parsed correctly (previously the whole block was silently dropped unless
  `target_depth` was present, so the first scout ran at d5/b20 — watch for this class of bug when
  a presence-only sidecar carries gen settings).

## What unblocks generation (in intended order)

1. **EOT dominance prune** (`eot-dominance-pruning.md`) — the slow cells are exactly its target
   (Class B state mass). Census probe `MTG_DOM_CENSUS` is in tree; next steps there: per-type
   counter direction, price monsters at b0, then build + full standing gate (must-find included).
2. Re-scout at d3/b3 after the prune lands: if the tail collapses, `--gen-mulligan fast` on ONE
   frozen commit (mulligan-profile.md Rule 0 — generation is commit-bound, so generate only after
   the prune and any other play-logic work has landed).
3. If still too slow: the slow.log hands name the degenerate atom to fix or gate first.
