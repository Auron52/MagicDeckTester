# Strict filter feed (MTG_FILTER_FEED_STRICT) — the gi164 laundering channel, closed

**Status:** ADOPTED (2026-09-01) -- default ON, all three GT tiers rebaselined after the
full-suite adjudication in §4b (user's condition: "check a lot of examples, since I don't want
to risk any true regression slipping through" -- zero true regressions found). `=0` restores
the lenient feed byte-exactly. The companion cache fix (§5) is live unconditionally.

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

Per-game: 42 slower / 13 faster of 2825. **Ledger collapse: the strict arm prices 149/149 of
the formerly-UNPRICED hinata rows LEGAL** (lenient: 148 + gi164 LAUNDERED).

CORRECTION to the first cut: the dragonstorm flag-ON canary was NOT sufficient to conclude
"no other deck plays a filter" -- **Treasure Hunt plays 4 Cascade Bluffs** (and 4 Ferrous
Lake), caught when the flip's first smoke moved th's cells. Unpredictable Cyclone also plays
Bluffs but is not in the regression suite. §4b covers th.

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

## 4b. Full-suite adjudication (the adoption gate) -- ZERO true regressions

Both filter decks' ENTIRE suite footprint (all 20 hinata cells + all 20 th cells, smoke +
regression + overnight) was re-measured two-arm in pooled batteries (every ctl cell
byte-identical to GT), and EVERY slower game was pushed through three gates: (1) the flat
per-turn colour ledger on the control's own line, (2) a sequencing-aware re-price (rock mana
does not exist before the rock's cast; Spasm's untap credit not before Spasm; a filter cannot
feed itself -- acyclic chains only), (3) strict-arm recovery probes (4x/16x budget, then
d3/d4/d5 at b10000).

hinata (13,625 games/arm): 241 slower / 46 faster. 143 LAUNDERED outright; 57 assignment-legal
but SEQUENCING-illegal; 3 recover at higher budget; 30 d0 residue -- of which the
MTG_FILTER_FEED_AUDIT instrument (engine-side ground truth: the lenient branch reports every
COMMITTED off-colour feed) proved 29/30 rode off-colour feeds, 1 pure greedy solution-choice
churn; 5 unique searched games persist at unbounded search with fully strict-legal control
lines -- adjudicated as honest PLAN DIVERGENCE: each diverges at an early cantrip turn where
both arms' plans are legal (unit test pins the exact gi68-T2 joint bill as strict-payable),
the strict search's launder-free projections legitimately re-rank plans, and the draws then
differ. Capability loss: none found anywhere.

th (20,825 games/arm): searched cells essentially FLAT (worst +0.004, one cell -0.002 faster)
-- with search the deck reroutes around the lost launder. The cost concentrates at d0
(+0.065..+0.11): the greedy player leaned on off-colour float (Reliquary Tower / Sandstone
Needle {C}) feeding the Bluffs. First-pass adjudication: 231 LAUNDERED by the flat ledger;
all 533 d0 residue games audited as committing off-colour feeds (533/533).

Three adjudication-tool lessons recorded: the flat ledger lets a filter's own output satisfy
its own feed (the self-feed hole -- why th showed 533 false-RESIDUE); game repro is
`--seed base+gi --game-index gi` (adjudicating at the base seed prices the WRONG games); and
READ THE CARD -- a hand analysis mis-remembered Saprazzan Skerry as colourless (it taps for
{U}{U}) and "proved" launders that were legal; the checker, which reads cards.json, was right.

## 4c. FINAL adjudication, to the user's sharpened bar (2026-09-01, post-adoption)

The user's bar: *"For all of them we should be able to show there is a line that is illegal
in the original or it should be possible to recover with budget (and potentially depth)"* --
and the other side must be checked too: where the original is illegal, was there an
alternative legal approach the strict engine misses?

Instrument: a corrected timing-aware matcher over each control game's EXECUTED record (actual
tapped sources incl. vanished ones, actual manaPaid bills, the executed cast order): filter
tap-times are free variables; a filter offers its free {C} mode OR the fed mode; feeds accept
only units available strictly before the filter's tap (kills self- and mutual-feeds); rock
mana exists only after the rock's cast; Spasm's untap credit only after Spasm. LAUNDER-REQUIRED
means NO assignment over any tap-time/mode choice balances -- proven illegality of the
executed line. (This subsumed and corrected §4b's flat+sequencing gates: the executed-order
constraint flipped most of §4b's "plan divergence" games to proven-illegal -- e.g. gi68's T3
is only assignment-legal if Ponder is reordered after Spasm, which is not the line played.)

All 1,016 slower games (241 hinata + 775 th), each run once through the matcher, then
survivors through recovery probes (d0 survivors: strict at d3/d5 with real budgets; searched
survivors: d3..d7 at b10000):

- **923 ILLEGAL-LINE** -- the executed control payment is colour/timing-infeasible without
  the launder. Prong A.
- **86 of 87 d0 survivors RECOVER at d3** (strict + search reaches the control's win turn or
  better). Prong B.
- **2 searched survivors RECOVER**: hinata d3_s6006 gi233 (equals control at b10000) and
  th d3_s7007 gi335 (strict at b10000 wins T5 vs the control's T6 -- BETTER). Prong B.
- **3 unique games fail both prongs** (5 rows with depth-duplicates): th gi249 (5->6),
  th gi448 (3->4), hinata d0_s4004 gi1516 (7->unwon; probes reach 8). For each, the control
  line is proven legal AND the payment machinery is proven capable of its exact shapes by
  unit tests (gi68-T2 dork-feeds-filter; gi448-T2 depletion-burst-feeds-filter) -- the miss
  is in the SEARCH: rollout tap-sequencing is not feed-aware (see below), so the strict
  rollout mis-scores the branch and no budget/depth recovers a mis-scored rollout.

**The mechanism, traced concretely on gi68**: the T3 kill needs a mid-turn breakpoint
(Ponder's draw reveals Irencrag) and its continuation is payable only if the earlier payments
PRESERVED a U/R unit (Mountain) for the Bluffs feed. The tap-choice heuristic is not
feed-aware, spends Mountain's R on Hinata's R, and strands the feed at the breakpoint. Under
the lenient model this could never matter (any float fed); under the strict model it prices a
few boards' filters out of rollout lines that are really available. Filed as deferred work:
`docs/design/feed-aware-tap-choice.md`.

**Net verdict**: 1,011 of 1,016 slower games meet the bar outright (99.5%); the 3 residual
games are a bounded, mechanism-identified search-quality gap -- not hidden illegality, not a
payment-correctness regression -- offset by 46+ faster games and one game the strict engine
wins FASTER than the control. Countervailing capability evidence: 4 unit tests pin every
disputed payment shape as strict-payable.

**§4d correction (2026-09-01, the feed-aware follow-up): the residual set is 2, not 3.**
The matcher had a phase-boundary generosity hole: a rock cast in MAIN_1 but first tapped in
MAIN_2 appears "already on board" in the MAIN_2 record's prev-board diff, so it was credited
at avail -1 instead of cast-pos+0.5 -- letting Sol Ring PAY FOR ITS OWN CAST. hinata d0_s4004
gi1516's control T6 (Gamble + Sol Ring + Hinata off Boilerworks/Monastery/Bluffs/Sol Ring)
rode exactly that credit; every realizable strict assignment of the turn's full bill fails
(hand-enumerated, and the fixed matcher agrees). With the fix (`final_adjudicate.py` now keys
entered-this-turn on the turn's cast list, not the per-record board diff) gi1516 and one
prong-B-recovered sibling (d0_s6006 gi1292) flip to ILLEGAL-LINE; all other ALL-LEGAL rows
re-verify (`final_verdicts_fixed_survivors.json`). The true residual set is **th gi249 + th
gi448**, both since recovered by the feed-aware tap lever -- see
`docs/design/feed-aware-tap-choice.md`.

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

## 6. Adoption -- done

Flipped `EnvOn("MTG_FILTER_FEED_STRICT", true)` and rebaselined all three tiers (hinata + th
movement only; both decks' tier fingerprints match their battery strict arms exactly). The
"cost" is fictional wins repriced honestly; no legal capability is removed. Follow-ups: re-run
the 3 hinata prepay-residue rows; the gi164 prepay-defect entry is FIXED.
