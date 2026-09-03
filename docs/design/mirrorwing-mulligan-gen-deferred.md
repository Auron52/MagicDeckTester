# Mirrorwing mulligan generation — DEFERRED (2026-08-15), and what unblocks it

> **2026-09-01 — THIS DOCUMENT DESCRIBES THE LIST NOW ARCHIVED AS `v2-instigator-entrance`.**
> The shipping list is the Anger-4 / Oracle-3 rebuild (`3faf5c76`). What carries over, what does
> not, and one error in the text below:
>
> * **The settings quoted below as `d3/b3` are WRONG for any run after 2026-08-16.** The real
>   setting is **`mull_gen_depth=2` / `mull_gen_budget_ms=3`**, derived by measurement that day
>   (`scripts/derive_mullgen_setting.py`, 24 hands, R=12): d1/d2/d3 at b3 are indistinguishable in
>   rank fidelity (rho 0.9922 / 0.9917 / 0.9915 — noise at that sample), so the tie broke on
>   units/rollout, the deterministic work-unit currency: **9997 vs 11448, d2 being 13% cheaper than
>   d3**. It deliberately rejected d3/b20 despite higher fidelity (rho 0.9991) because it costs
>   1.067x the *play* settings. v2 then carried d2/b3 forward under the user directive of
>   2026-08-22, restated 2026-09-01: **use the same mulligan generation settings as the previous
>   list.** So v3 generates at **d2/b3** as well.
> * **New list structure** (measured 2026-08-31, contention-free numbers only): **K=16 buckets**,
>   **155,978** distinct size-7 hands, **227,210** total, plus 142,464 fused sub-table batches.
>   That is ~23% fewer size-7 cells than the 202,878 below — the same K, a smaller space.
> * **RETRACTED: the 2026-08-31 feasibility reading on the new list.** A `recommend` probe measured
>   8 rollouts/s and was read as "tens of hours, not one night". That verdict is void: the probe ran
>   **with no value leaf attached**, at inherited depth 5 instead of d2, and contended against a live
>   value-leaf generation. The value leaf replaces the horizon rollout with an O(1) evaluator and the
>   H-cell ladder is guarded on the sidecar existing (**1.35–84.8x**), so it was timing a path this
>   deck will never ship on. No feasibility conclusion may be drawn without the value leaf attached —
>   now a standing rule in CLAUDE.md and `mulligan-profile.md`.
> * **The dork-flood tail is still present** and is the thing to watch: the first slow cell logged was
>   `Ignoble Hierarch x3; Mirrorwing Dragon x1; Mountain x1; Oracle's Restoration x1; Zada x1` at
>   9.5 s. Same shape as the 61 s–1442 s cells below. How much it costs *with* the value leaf is
>   exactly the open question.
> * **PRIOR EXPECTATION (user, 2026-09-01): "it would be very surprising if Mirrorwing was
>   intractable. It might be slightly over an overnight period, but not much more than that."**
>   Treat that as the hypothesis the re-probe tests. If a measurement says "intractable", suspect
>   the measurement first — that is exactly how the retracted reading above went wrong.
> * **CALIBRATION ANCHOR — Minotaur, the closest comparable, and it finished.** `5243288c`
>   (2026-09-01) adopted a Minotaur exhaustive profile at **complete / R40 / K=16** — the same K as
>   Mirrorwing — in **5.3 h wall** (floor pass 3.55 h, then refine) **against a projected 15.6 h
>   upper bound**. Two things follow: a K=16 `complete` gen is demonstrably an overnight-scale job
>   on this box, and **`recommend`'s projection ran ~3x PESSIMISTIC**, so divide before despairing.
>   The open difference is Mirrorwing's dork-flood tail, which Minotaur does not have.
> * **Order of work:** value leaf (running, frozen at `52d1ba29`) → then, alone on the box and at
>   d2/b3, the `recommend` probe → then the decision about a full gen.

**Status (updated 2026-09-03): NO LONGER DEFERRED** — the exhaustive keep+bottom table was
generated, repaired (dfffa638) and ADOPTED on 2026-09-02 (17dac2d7, both gates pass, GT
rebaselined 20 cells).

**Status:** the exhaustive keep/bottom profile for `decks/Mirrorwing Dragon` is NOT generated and
generation is deliberately deferred. The deck plays on defaults + the adopted value-leaf sidecar
(`Mirrorwing Dragon.value.json`, presence-only, `mull_gen_depth=3` / `mull_gen_budget_ms=3` —
**superseded: the measured setting is d2/b3, see the 2026-09-01 header**).

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
2. Re-scout at **d2/b3** (not d3/b3 — see the header) after the prune lands: if the tail collapses, `--gen-mulligan fast` on ONE
   frozen commit (mulligan-profile.md Rule 0 — generation is commit-bound, so generate only after
   the prune and any other play-logic work has landed).
3. If still too slow: the slow.log hands name the degenerate atom to fix or gate first.
