# Strict filter feed (MTG_FILTER_FEED_STRICT) — the gi164 laundering channel, closed

**Status:** BUILT + MEASURED (2026-09-01), default OFF, **adoption recommended** (flip +
rebaseline is the user's call). The companion cache fix (§4) is live unconditionally.

## 1. The defect

`TapForCostBacktrackWorker`'s `is_filter` branch fed Cascade Bluffs via `ConsumeFloatingAny` —
ANY floating unit, colourless included. The real card is `{U/R},{T}: Add {U}{U}, {U}{R}, or
{R}{R}`: only U or R can pay the feed. Feeding it a Sol Ring {C} launders one off-colour unit
into two on-colour ones. This was the channel behind the gi164 UNPRICED→LAUNDERED row
(hinata_overnight_d3_s4004, seed 4168 gi164 d3 b10: T3 balanced only if the prepay fed the
Bluffs off Sol Ring's colourless). The greedy `tap_source` in ManaPayment.cpp was already
colour-strict; the backtracker — which the batch prepay and the greedy fallback both use — was
not. `ramp_filter` (Ferrous Lake, Izzet Signet) feeds a true `{1}` and keeps the any-unit feed.

## 2. The fix

Behind `MTG_FILTER_FEED_STRICT` (heurarm slot FILTER_FEED_STRICT for pooled A/Bs): the feed
consumes one unit of the filter's own colours — branching over WHICH colour, since the choice
can decide another pip's fate and the DFS never revisits float choices within a node — or one
`wild` unit (which `ConsumeFloatingAny` never spent at all). Unit test
"strict filter feed" (test_mana_payment.cpp): Island feeds stay payable both shapes; the
Sol-Ring-only board's {R}{R} is refused strictly and paid leniently (the documented launder).

## 3. Measurement (2026-09-01, pooled two-arm battery, ctl == GT byte-identical in all 8 cells)

| cell | ctl | strict | delta |
|---|---|---|---|
| smoke d0 s1001 | 6.9630 | 6.9830 | +0.0200 |
| smoke d3 s1001 | 5.6733 | 5.7267 | +0.0533 |
| smoke d5 s1001 | 5.8400 | 5.8667 | +0.0267 |
| reg d0 s2002 | 7.0440 | 7.0540 | +0.0100 |
| reg d3 s2002 | 5.6700 | 5.7000 | +0.0300 |
| reg d3 s3003 | 5.6500 | 5.6800 | +0.0300 |
| reg d5 s2002 | 5.6600 | 5.6700 | +0.0100 |
| reg d5 s3003 | 5.6600 | 5.6900 | +0.0300 |

Per-game: 42 slower / 13 faster of 2825. Dragonstorm flag-ON is digest-identical (no other
deck plays a filter). **Ledger collapse: the strict arm prices 149/149 of the formerly-UNPRICED
hinata rows LEGAL** (lenient: 148 + gi164 LAUNDERED).

## 4. Adjudication — the slowdown is the removal of illegal credit, not an engine gap

All 21 searched (d3/d5) slower games were adjudicated against the per-turn colour ledger run on
the CONTROL arm's own line:

- **13/21 price LAUNDERED** — the control's win rode an explicitly colour-illegal payment; the
  strict arm losing that turn is the fix working (gi164's own shape: 4→5 with T3 laundered).
- **8/21 price "LEGAL", but the ledger over-approves them.** Deep-dive on the worst (smoke d3
  gi86, 4→6): the T3 joint bill (Gamble + Sol Ring + Hinata) has a valid colour ASSIGNMENT
  (7 units = 7 wants — what the ledger checks) but **no valid casting order**: Sol Ring's {C}{C}
  exists only after its cast, and every legal sequence strands a coloured pip. The only working
  order feeds the Bluffs with Sol Ring's {C} — the launder. The ledger is assignment-only and
  does not model rock-cast sequencing, so its LEGAL is an upper bound. All other sampled
  "LEGAL" slower games are the same board shape (early Sol Ring + Cascade Bluffs = a standing
  every-turn launder available to the control line).
- The completeness worry (strict DFS failing to find legal feeds) is covered the other way: the
  unit test proves the strict branch finds on-colour feed orderings, and 13 games got FASTER.

## 5. The mixed-pool cache lesson (fixed unconditionally, and a rule for future levers)

First battery run: the CONTROL arm failed to reproduce GT in 3 cells, nondeterministically. The
`MTG_BATCH_STATE_DUMP` instrument (shipped hours earlier) localised it: per-job inputs clean
(ctl hf=0, str hf=2, same profile fingerprints), results still moved. Cause: **the payable-mana
cache (`g_mana_cache`) is thread_local and outlives batch job switches, and its key did not
include the flag** — so a worker replayed strict-arm solve results inside control games.
`ManaCacheKey` now mixes `FilterFeedStrictOn()` into the key; the mixed pool is deterministic
and ctl == GT in all 8 cells since.

**Rule:** any heurarm lever that changes the ANSWER of a memoised computation (the backtracker,
via the mana cache) must be hashed into that cache's key. Levers outside the cached region
(spasm's resolution-path lever) don't need it — which is why the spasm battery's control was
clean. No earlier battery is believed affected (prior payment levers were env-per-process, not
heurarm-per-job).

## 6. Adoption

Recommended: flip `EnvOn("MTG_FILTER_FEED_STRICT", true)` and rebaseline (hinata-only movement,
expected cell values in §3's strict column for smoke/regression; overnight unmeasured). The
"cost" is fictional wins repriced honestly; there is no legal capability removed. After
adoption, re-run the 3 hinata prepay-residue rows (they were flag-inert pre-adoption) and
consider retiring the gi164 entry in the prepay-defect list as FIXED.
