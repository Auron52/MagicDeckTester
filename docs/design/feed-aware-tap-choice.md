# Feed-aware tap choice — MTG_FEED_FILTER_FIRST (built + measured 2026-09-01)

**Status:** ADOPTED default-ON (2026-09-01, user: "if there are no drawbacks let's adopt" —
the drawbacks on record being the small measured ones below: hinata gi39 d3 / gi95 d5 +1 turn
each, the gi1225 d0 knock-on, a couple of d0 ±1s, against 13 faster games and both residuals
fixed). `=0` restores the feed-blind greedy. All three GT tiers rebaselined at the flip; the
tier diffs matched the battery's per-game expectations exactly (only hinata/th cells moved;
the known slower games and no others). One unit pin updated for the new contract: on
{Bluffs, Island} a lone {U} pip now routes the Island THROUGH the Bluffs (both tap, leftover
{U} floats) instead of stranding the filter.
Recovers both true residuals of the strict-filter-feed adoption (`filter-feed-strict.md` §4c/§4d):
th gi249 (6→5) and th gi448 (4→3), at d3 AND d5, with strictly-legal lines (matcher-verified).

## The gap (corrected from the first draft of this doc)

The per-cast greedy in `TapForCostSharedOnce` optimizes each cast in isolation, and the complete
backtracker only runs when the greedy FAILS. So when the greedy *succeeds* by spending a filter's
last feeder on an ordinary pip, nothing ever notices that a LATER cast this turn needed the
filter's fed mode — a whole-turn stranding no per-cast fallback can see. Under the lenient feed
this never mattered (any float fed); under the adopted strict model it priced a few boards'
filters out of lines that are really available.

Traced concretely (th gi448 T3, the control's win line): Skerry (1 counter) + Bluffs + Tower
paying Treasure Hunt {1}{U} twice. Greedy pays TH#1 from Skerry's {U}{U} alone — succeeds — and
strands the Bluffs (Tower is {C}-only): TH#2 strictly unpayable, the rollout scores the Skerry
T1-land-drop branch a turn worse, and the search plays Temple T1 instead. The turn-optimal
payment routes TH#1 THROUGH the Bluffs: Skerry-U feeds it → {U}{U} out + leftover Skerry-U =
three blue units from the same two sources; the leftover carries (CR 500.4) and TH#2 pays.

Note the arithmetic that makes this nearly a dominance rule: when tapping source S would strand
filter F anyway, routing S *as F's feed* yields amt+1 units instead of amt — the only loss is
F's {C} mode, which the strand was about to reduce it to.

(The first draft blamed a "preserve the feeder" shape traced on hinata gi68; the final
adjudication later proved gi68's control T3 ILLEGAL as executed, and §4d's matcher fix proved
hinata d0 gi1516's control line illegal too — Sol Ring paying for its own cast through a
phase-boundary hole in the matcher. Neither is a residual; the two th games above are.)

## The lever

`MTG_FEED_FILTER_FIRST` (default OFF; heurarm slot FEED_FILTER_FIRST for pooled A/Bs), read in
`FeedFilterFirstOn()` (SpellEffects.h), applied in TapForCostSharedOnce's scarcity selection:
for a coloured pip, when the chosen DIRECT source is the LAST source able to feed an
also-candidate filter (no feed colour floating, no other untapped non-filter source produces
one), reroute the pip through the filter's fed mode (the kind-2 feeder loop then taps the same
source as the feed). Preference only — candidates, legality, and the backtracker fallback are
unchanged (Rule 0b: pure tap reordering, no branch removal).

**Filter-chain escape (v2, the th625 lesson):** the strand test must count filter-class
re-feeders. At th d0 gi625's T5, the reroute consumed Snarl+Bluffs on a {U} pip although
Ferrous Lake (ramp filter, fed by a Tower {C}) could re-feed the Bluffs for the later Land's
Edge — the backtracker chains filters, so the greedy's strand test must too. The reroute now
declines when another untapped filter/ramp-filter producing one of F's colours is itself still
feedable from a third source. v1 (without this) cost ~38 slower d0 games incl. one won→unwon;
v2 cured them.

**Cache note** (batch-pool-contamination.md rule): the lever changes the GREEDY only; the mana
cache memoizes BACKTRACKER answers, which a greedy success short-circuits before reaching, so it
does not enter ManaCacheKey. solve/enum memos are decision-epoch-scoped (unreachable across
jobs).

Unit test: "feed-filter-first: the last feeder routes THROUGH the filter..." (gi448 T3 shape,
executor+rollout twins agree; OFF-arm pins the stranding this lever exists for).

## Measurement (2026-09-01, ONE pooled 80-job battery, 33,450 games/arm, full suite footprint
of both filter decks; ctl arm byte-matched the strict-feed adoption battery's strict arms in
ALL 40 cells)

- **th: net faster** (weighted −0.00076): 10 faster / 2 slower of 19,825. Both residuals fixed
  at d3 and d5. d0: 6 faster (7→4, 8→5, 6→4, 7→6, 6→5, 6→4) / 2 slower (both +1 turn).
  All searched th cells: zero regressions.
- **hinata: flat** (weighted +0.00015): 3 faster (d3 8→7, d3 6→5, d0 8→7) / 4 slower
  (d3 gi39 5→6, d5 gi95 5→6, d0 gi348 7→8, d0 gi1225 7→unwon) of 13,625.
- Every lever-slower game's lever line was run through the §4d matcher: flags appear only where
  the CONTROL line flags too (matcher model gaps on Reflecting-Pool-class boards, equal in both
  arms) — no illegality introduced; the strict engine enforces the feed at payment time.

**The one ugly residual, mechanism-known:** hinata d0 gi1225 (7→unwon) is a KNOCK-ON, not a
payment error: the reroute's banked leftover {U} made a second Preordain payable at T2 and the
d0 cast ranker picked it over Sol Ring (which the same float could equally have paid) — the
lever exposes a cast-rank ordering question (rock-vs-cantrip with float up), it does not
misplay mana. Fixing that ordering is HINATA_MANA_FLOAT_RANK-adjacent work, out of scope here.

## Measurement-apparatus lesson (cost ~90 min of ghost-hunting)

Deriving a new battery manifest by copying a prior battery's jobs copied a STALE
`"flags": {"MTG_FILTER_FEED_STRICT": false}` from the adoption battery's th control arm
(legitimate there — it ran post-flip and needed forced-lenient controls) into this battery's
control arm — which therefore measured lenient-vs-lever and reproduced §4b's known strict-cost
numbers as a phantom "lever regression", while probes/env runs "mysteriously" disagreed (a
manifest flag beats env by design). **When reusing a prior manifest, strip its `flags` blocks;
a control arm is FLAGLESS.** The tell that resolved it: `MTG_BATCH_STATE_DUMP`'s `hf` heurarm
hash ≠ 0 on a supposedly-flagless job.

## Adoption (done 2026-09-01)

User approved; `FeedFilterFirstOn()` flipped to `EnvOn("MTG_FEED_FILTER_FIRST", true)` and all
three GT tiers re-run and accepted at the flip. Tier-by-tier: smoke — only th/hinata cells
moved (th d0 −0.002 = the battery's gi699 6→4; the rest same-score tap-order churn), searched
slower=0. Regression — the three known slower games exactly (gi39 d3 5→6, gi95 d5 5→6, gi348
d0 7→8) and nothing else. Overnight — see the accept log (`logs/feed_aware/adopt_overnight*`).
Deferred follow-up (out of scope, noted for whoever picks it up): the gi1225 knock-on is a d0
cast-rank question — a mana rock vs a cantrip when float is up — not a payment defect.
