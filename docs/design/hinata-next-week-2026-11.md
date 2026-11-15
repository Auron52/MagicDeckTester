# Hinata depth-5 performance: work queued for next week

Status: deferred until the week of 2026-11-16. The approved play setting is
depth 5 / 30 ms; do not lower depth below 5 or budget below 20 ms to make a
performance result look better. The depth-4 change was reverted.

## Evidence to carry forward

The matched 500-game Release sample in
`docs/design/hinata-perf-regression-2026-10.md` held the deck, keep table,
value model, search shape, depth 5 and 30 ms fixed. It measured 2.13 core
seconds/game at the October 21 source, 6.30 at the November 11 local peak,
and 4.33 at the November 15 source. Search units grew 2.32x across the early
October no-greedy work; later CPU per unit also rose. These are sample means,
not a claim that every current game costs ten core seconds.

The no-greedy change is required for play correctness. Its continuation waves
produce many exact duplicate states that are currently detected after a plan
has been applied. A historical probe found 84% post-apply duplicates at the
affected breakpoint stage. `docs/design/no-greedy-in-search-window.md` lists
two candidate exact prunes: a base-measured continuation length and a
content-exact fold of interchangeable same-card continuations. Plan-signature
deduplication was previously unsound.

The cache read guard cost about 6% in a corrected November 15 80-game A/B
(267 versus 250 CPU seconds); disabling it changes search work and is not a
valid optimization. A stale PGO build caused the most recent harness run to
fall back to Release. Refresh PGO before operational throughput estimates.

## Next-week sequence

1. Profile representative depth-5 / 30 ms Hinata games and separate
   enumeration, state application, deduplication, rollout and cache-guard CPU.
   Keep seed, deck, keep table and model fixed when comparing changes.
2. Implement one **provably exact** continuation prune at a time, starting
   with pre-apply duplicate work. Require byte-identical unbounded play
   digests before claiming equivalence, then compare depth-5 / 30 ms cost and
   win turns on paired seeds.
3. Investigate the remaining CPU-per-unit rise using profiles. Preserve the
   cache guard's budget correctness and the no-greedy search-window rule.
4. Run Hinata/Hinata2HG smoke, regression, strict reference replay and a
   matched cost batch before adopting a change. Report both core seconds/game
   and deterministic search units/game.

This work is deliberately parked while Sunday artifact generation uses the
machine. No Hinata performance experiment should contend with those runs.
