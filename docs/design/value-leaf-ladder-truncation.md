# Value-leaf depth-table truncation — a latent-bug class (and its guard)

## The bug that motivated this (goblins, 2026-07 → fixed 2026-08-01)

Each value deck's `value_leaf_table` (in `<deck>.value.json`) records a by-depth
loss-penalized win-turn ladder for the heuristic (`heuristic_lp` over `hdepths`) and the
pure value leaf (`value_leaf_lp` over `vdepths`). From it the writer
(`scripts/attic/valueleaf_table_to_metadata.py`) derives `value_trust_depth` — the
shallowest value-leaf depth whose LP is within `tol` of the converged heuristic floor
`h_conv`. The engine reads it as the escalation gate: an unverified line committed **below**
`value_trust_depth` is escalated to the exact heuristic leaf; at/above it the leaf is trusted.

Goblins shipped with the ladder measured only to **V5** while its play `target_depth` is
**6**, and the shallow cells were noisy (400 games). The leaf had not yet reached `tol` at
V5, so `value_trust_depth` derived as **UNSET**. With it unset, `AIEngine` defaults
`escalate_below = target_depth + 1 = 7`, so **every** unverified line committed at the play
depth 6 escalated to a deep heuristic re-search whose result the fallback crossover's
keep-leaf sentinel then **discarded** — pure wasted work (measured: `d6 redid 38` of 72
escalations per 150 games; play −23% wall once fixed). The real V6/V7/V8 cells *had* been
measured (in separate high-N logs), but the writer was hand-fed the stale partial log and
never re-run on the union.

**Two structural causes, both now closed:**

1. **Measure and write were separate manual steps that drifted.** The depth matrix
   (`valueleaf_depth_matrix.py`) wrote a gitignored scratch log; a human later hand-ran the
   metadata writer on *some* log. Fix: `valueleaf_depth_matrix.py --write-profile` folds the
   run's freshly-measured table straight into the play profile in one atomic command.

2. **Nothing enforced ladder completeness.** The writer emitted whatever cells the log
   held. Fix: a completeness guard (`completeness_error`) **refuses to write** (exit 2) when
   the ladder is inconclusive — `trust` UNSET *and* not `no_fallback` *and* the deepest V
   still **descending** (would likely cross `tol` with more depth = truncated), or the
   ladder doesn't reach the deck's `target_depth`. `--allow-partial` downgrades to a warning
   for a deck whose UNSET trust is deliberate. The scalar cap now also **auto-derives** from
   `value_play.target_depth` (was a fixed default of 5, which silently under-capped
   `target_depth=6` decks).

The guard's discriminator is the key idea: a genuine never-trust leaf (antilife, TH) has
**flattened** above `h_conv`; a truncated ladder (goblins-pre) was still **descending** at
its deepest measured cell. Flattened ⇒ conclusive UNSET (allowed); descending ⇒ inconclusive
(refused).

## Open: hinata2 and dragonstorm are in the same descending+UNSET state

When the guard was written it was checked against every committed value deck. Two — **hinata2**
and **dragonstorm** — trip it: their committed tables have `value_trust_depth` UNSET,
`no_fallback` false, and the deepest measured V cell **still descending**, only ~0.006–0.009
LP above `h_conv`:

| deck | V_top | V_prev | descent | h_conv | gap V_top−h_conv |
|------|-------|--------|---------|--------|------------------|
| hinata2 | 6.1460 | 6.2200 | +0.0740 | 6.1400 | 0.0060 |
| dragonstorm | 4.7862 | 4.8312 | +0.0450 | 4.7775 | 0.0087 |

This is the **same shape** as goblins-pre (V still dropping ~0.05–0.07/depth, a hair above
`h_conv`). It is plausible — not proven — that extending each ladder one or two depths would
cross `tol` and set a real `value_trust_depth`, which (as on goblins) would remove wasted
escalate-and-discard work at the play depth with no quality change. It is equally possible the
leaf plateaus just above `h_conv` and UNSET is correct.

**This was surfaced, not changed** — the committed hinata2/dragonstorm profiles are untouched.
To resolve, on a frozen commit:

```
# extend the ladder deeper for the one deck, then let the guard decide:
python3 scripts/attic/valueleaf_depth_matrix.py --decks hinata --hdepths 1 2 3 \
    --vdepths 5 6 7 8 --value-min-depth 0 --games 3000 --seeds <held-out> \
    --write-profile            # refuses if still inconclusive; writes trust if it converges
```

Then A/B the play profile (quality byte-identical expected; wall faster if trust becomes set,
exactly like goblins' −23%). Adopt only on the usual regression accept flow. Do **not** hand-edit
the table — the guard exists precisely so the profile can only come from a complete measurement.
