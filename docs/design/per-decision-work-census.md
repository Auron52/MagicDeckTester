# The per-decision work census, and why candidate-list prunes cannot make the search cheaper

Status: **BUILT and inert by default** (2026-09-30). `MTG_TURN_CENSUS=<path>` off = byte-identical
(smoke: 118 scenarios pass, `configs changed: 0 unchanged: 104`, `slower=0 faster=0
play-changed=0`). Nothing here is adopted; the census is a measuring instrument, not a lever.

This document exists because of two connected questions the user asked on 2026-09-30:

> *"I'd also like to know why condemning activations is not helping."*

> *"It's crucial that we look at real turns. Once we are done considering and maybe adopting any
> remaining options we need to create or find tooling that can give us a list of something like
> 1000 slowest turns in terms of branching and how our caching/cost pruning deals with them. Then
> we can analyze the results and you can even present some of them to me to double check because
> we need to make sure we are doing only the work that is necessary."* — and, on the tooling,
> *"I guess condemnation should also show up in there."*

Part 1 answers the first question, because the answer turns out to be a **structural property of
the breakpoint search that applies to a whole family of levers** — including ones not yet built —
and it is the reason the second question is the right one to be asking. Part 2 is the tool.

---

## Part 1 — Why condemning activations does not make anything cheaper

`MTG_BP_CONDEMN_ACTIVATION=1` works. It is not inert: it fires 3,342 times on the measured snow
cell, the play is byte-identical, and the work moves by **+0.35%** — i.e. very slightly *dearer*.
That is a strange-looking result for a filter that provably removes thousands of candidates, and
the explanation is not in the condemnation rule at all. It is in how the breakpoint wave consumes
the list the rule is filtering.

### The mechanism: `bp_choice` is a positional index, and the wave emits a fixed number of them

`AppendBreakpointVariants` (TurnSolver.cpp) does not select candidates from the continuation list.
It emits variants **by rank index**, and the index range is a constant:

```cpp
const int w = BpSearchWidth();          // 2
...
for (int at = 0; at < BpSearchDepth(); ++at)
    for (int k = 0; k < w; ++k)
    {
        TurnSolver::Plan v = p;
        v.bp_choice = k;                // <-- a RANK, resolved later
        v.bp_at     = at;
        variants.push_back(std::move(v));
    }
```

The list itself is not consulted here — it does not even exist yet. `bp_choice` is resolved at
apply time by indexing, which the engine's own comment states explicitly:

```
// ... it is a positional index (`out = cands[plan.bp_choice]`), so a list that
// changed between scoring and replay would make the executor play a candidate the search never
// scored.
```

So the number of candidates the search scores at a breakpoint is

```
    w  x  BpSearchDepth()  x  (number of base plans fanned out)
```

and **the length of the continuation list appears nowhere in it.**

### What follows, and it is not a subtlety

A filter that removes entries from the continuation list has exactly two possible effects:

* **The entry sat at rank ≥ W.** That rank was never indexed by anything. Removing it changes
  nothing whatsoever — not the work, not the play.
* **The entry sat at rank < W.** Every entry behind it **shifts down by one**, and rank `k` is now
  occupied by a different plan. The search still scores `w` candidates at that breakpoint. It
  scores a *different* one.

So condemnation is **content substitution under a fixed-width index, not a reduction**. Its work
delta is not the work of the candidates it removed; it is the difference between what the removed
candidate would have cost to apply and score, and what the candidate that shifted into its slot
costs instead. There is no reason for that difference to be negative, and on snow it came out at
+0.35% — the replacements are marginally dearer, which is exactly what you would expect when you
delete the cheapest thing in a window and backfill from further down the ranking.

The byte-identical play says the same thing from the other side: none of the substituted-in
candidates ever beat the incumbent winner. The search spent the same budget and reached the same
answer.

### The general rule this establishes

> **A filter upstream of a fixed-width index cannot reduce work. It can only change which
> candidates occupy the slots.**

This retires a whole class of hoped-for cost levers, and it should be checked *before* building
the next one rather than after measuring it. The only things that can actually reduce breakpoint
work are levers that change one of the three factors in the product above:

1. `BpSearchWidth()` (W) — fewer slots.
2. `BpSearchDepth()` — fewer breakpoint positions per plan.
3. the number of base plans fanned out (`BpMaxBase`, the site mask, `PlanOpensBreakpoint`).

...or levers that make an *individual* apply cheaper, which is a different target entirely and is
where the census below points.

### The corollary that matters most, because it re-frames work already planned

**The double-booking filter** (`docs/design/trailing-activation-payment-hole.md`: a breakpoint
continuation re-solving mid-trailing-pass and activating the same physical permanent the base plan
still owes a `{T}` to — one permanent, one tap, CR 602.2a) sits at the **same site** as
condemn-activation: `CollectActions`' ModeSpec loop, inside the continuation derivation. So it is
subject to this same rule, and I should predict its result before measuring it rather than
discovering it:

* Its `cand_scored` will **not** fall. W is fixed.
* Its work delta will be **approximately zero**, sign unknown.
* Its drop counters **will** fall, and that is the point.

That does not make it worthless — it makes it a **correctness and quality** fix rather than an
optimisation, and it must be argued and measured as one. Right now a W-wide window can be occupied
by lines whose activation cannot legally execute; applying them silently drops the activation, so
the search scores a line that is not the line it thinks it is scoring. Filtering them means the two
slots hold lines the executor can actually play. The work is not saved, it is **redirected from
lines that buy nothing to lines that might** — which is precisely the user's framing, *"we need to
make sure we are doing only the work that is necessary."*

The metric to read is therefore **not** `cand_scored` and **not** the drop count on its own, but
held-out play quality plus the drop counters. Writing this down now removes the trap of building
it, measuring work-neutral, and filing it as a failure.

### What is CLOSED by this

* `MTG_BP_CONDEMN_ACTIVATION` — fires, play-identical, +0.35% work. **Not a cost lever, and the
  reason is structural.** Do not re-propose it as one. (Mode 2, the rank-comparison variant, is
  separately not the rule the user asked for — see its comment at the emission site.)
* Any future "prune the continuation list" proposal, unless it also shrinks W, the breakpoint
  depth, or the base fan-out.

---

## Part 2 — The per-decision work census

### Why it had to be built

Every work instrument in the engine was a **whole-run aggregate**: `[rollout-stats]` prints one
number per counter for the entire process, and `cost.py` divides totals by games. That can say
"this deck costs 2.9x" and can never say *which turns* cost it, nor whether the memo and the
pruners engaged on those turns or sat idle.

That gap is not academic here. The `matrix-cost-is-abandonment-rate` finding established that
~90% of a snow cell's wall clock is games pinned at the work ceiling — this engine's cost lives in
a **tail**. A deck whose median turn is cheap and whose worst turn is 1,000x the median reads, in
the aggregate, exactly like a deck that is uniformly mediocre. Sizing a lever against the mean is
sizing it against a number no real decision ever had.

### What it emits

`MTG_TURN_CENSUS=<path>` writes one TSV row per **real-play decision root**, carrying the *delta*
of every work / cache / prune / condemnation counter across that one decision, plus the
deterministic unit cost and the wall time.

The decision boundary is deliberately **not** a new concept: the census hangs on the same root
predicate as `DecisionWorkMeter` (`decision_root` in `SolveWithLookahead`, `fs_decision_root` in
`FullSearchLineHybrid`). That predicate is the engine's existing commitment about what "one real
decision" means, and a census with its own boundary would attribute work to turns the work ceiling
bills elsewhere. `root=1` tags the `SolveWithLookahead` host, `root=2` the hybrid.

Columns, in four groups:

| group | what it answers |
|---|---|
| `u_*` (13) | **Where the units went.** One bucket per `SearchBudget::Consume` site (`unitsite::kNames`), so they sum **exactly** to the row's `units`. A slow turn names the site that owns it. |
| branching | `cand_scored`, `rollout_calls`, `rollout_steps`, `pay_calls`, `act_fired`, `dom_*` |
| caching | `enum_*` (the continuation memo: hits / misses / nested / clears), `bplen_*`, `lazy_*` |
| pruning + condemnation | `sres_*` (overrun / partial / escalated / refused), `dedup_*`, and the whole condemnation family — `bp_condemn_seen` beside `bp_condemn_drops`, plus `condemn_act_drops`, `newonly_*` |
| silent drops | `drop_tapped`, `drop_unpaid`, `drop_gone`, `drop_notap`, `hold_*`, `cont_tap_*` |

**Condemnation carries `seen` next to `drops` on purpose.** A rule asked 5,000 times that declines
40 is a *different fact* from a rule never reached, and the two are indistinguishable from `drops`
alone. That is condemnation's established failure signature in this repo — an inert arm dressed as
a working one — so both halves are per-turn columns.

### Why snapshot-and-diff, and the price of that choice

The census diffs the **same atomic counters** `[rollout-stats]` prints, through one X-macro table
(`MTG_TURN_CENSUS_FIELDS` in `TurnSolver.cpp`) that generates both the column names and the reads.
A parallel set of per-turn counters would need an increment beside every existing one and would
drift from them on the first edit that touches only one of the two sets. Diffing the originals
cannot drift: a mislabelled column is impossible, and a counter no longer bumped reads as a hard 0.

The price is that those counters are **process-global**, so a delta is only attributable to one
decision if no other thread is playing. **The census is a single-threaded instrument.** That is
enforced, not documented: the outermost scope bumps a global live count, any row written while a
second decision was in flight is stamped `contended=1`, a one-time warning goes to stderr, and
`scripts/turn_census.py` **drops contended rows from every ranking**. A contaminated run reports
itself in its own output instead of producing a plausible table of garbage.

Three counter gates are **forced on** by the census flag (`MTG_ROLLOUT_STATS`,
`MTG_ACT_DROP_AUDIT`'s counting half, `MTG_BP_ENUM_PROBE`) through the shared `EnvPath` reader, so
they cannot disagree about whether the census is running. This closes the
`census-flag-needs-its-printer-flag` footgun directly: a census whose columns were dark because a
second flag was unset would report the absence of the instrument as the absence of the problem.
Gates that do **real work** when armed (the O(n²) plan-dominance census; the enum verifier's
re-derivations) are deliberately *not* forced — arming them would change the very unit counts the
census ranks on. The file records which families are live in a `#` header line, and the analyzer
reads it.

### Running it

```sh
# N single-threaded PROCESSES, one per chunk -- full machine, clean counters
bash scripts/turn_census_run.sh decks/Snow 3 10 40 logs/turncensus/snow_d3

python3 scripts/turn_census.py summary logs/turncensus/snow_d3/chunk_*.tsv
python3 scripts/turn_census.py rank    logs/turncensus/snow_d3/chunk_*.tsv --top 40
python3 scripts/turn_census.py explain logs/turncensus/snow_d3/chunk_*.tsv -n 0
```

**On the batching rule.** CLAUDE.md requires long runs to pool into one `mtg --batch`. That rule
is not violated here, it is *inapplicable*: `--batch`'s workers are threads sharing one process's
counters, so it cannot host this instrument at all. Separate single-threaded processes have no
shared counters, and each is given a whole equal-sized chunk of games — one tail per process, which
is the property the batching rule actually protects. `MTG_TURN_CENSUS_MIN_UNITS=<n>` keeps the file
to the tail on a long run.

A `budget_ms=0` (d0/greedy) cell emits **zero rows**, correctly: both roots require a real bounded
budget, and there is no search decision to account for.

### First read: snow, d3/b10, 1,280 games, 17,961 decisions, 0 contended

Held-out seeds (910000+), 32 single-threaded processes. The `u_*` buckets sum exactly to `units` on
every one of the 17,961 rows, so the per-site attribution is complete.

#### 1. The cost is a tail, and the very top of it is TRUNCATION rather than slowness

| slowest… | share of all units |
|---|---|
| 1% of decisions (179) | 14.6% |
| 10% (1,796) | 46.7% |
| 25% | 71.3% |

Median decision 3,429 units; max 227,446; **max/median = 66x**. But the top of that distribution is
a **flat plateau**: 15 rows sit within 2% of the maximum, 14 rows exceed 25x their budget and
**zero** exceed 30x. That is not a distribution, it is a ceiling — and it is the shipped one:
`kOverrunBudgetMult = 25.0`, so with a 9,000-unit budget the proportional overrun guard caps a
decision at exactly 225,000 units and **rolls the pass back**.

So the slowest decisions in the run are not slow, they are *cut off*. The census located a
documented, user-gated mechanism from its output alone, which is the cheapest possible confirmation
that the instrument reads true.

Budget discipline over the whole census:

| | decisions | share |
|---|---|---|
| over 1x budget | 4,072 | 22.67% |
| over 2x | 1,411 | 7.86% |
| over 5x | 231 | 1.29% |
| over 25x (at the ceiling) | 14 | 0.08% |

**Nearly a quarter of all decisions exceed the budget they were given.**

#### 2. Re-work is small, and the anytime commit is earning its keep

| | units | share of all |
|---|---|---|
| overrun-DISCARDED (rolled back) | 3,159,790 | 2.58% |
| ladder WARM-UP (superseded by design) | 3,507,421 | 2.86% |
| order-free FILL-IN re-search | 0 | 0.00% |
| **total re-work** | **6,667,211** | **5.44%** |

100% of the discarded units are in the slowest 10% of decisions. So "we are doing work we throw
away" is **not** where snow's cost is — 94.6% of the work is used.

One line in that block is worth keeping: of the 14 aborted passes, **10 had already PROVEN a better
win turn than the pass that was committed.** `MTG_ID_ANYTIME` is default **ON**, so those ten were
rescued — the counter is the control-arm report of what the pre-anytime path would have thrown
away. Read correctly this is a **retrospective validation**: 71% of overrun aborts would have cost
play without it. It is not a live defect, and must not be filed as one.

#### 3. The caching is fine. This disconfirms the hypothesis the tool was built to test.

Median decision vs slowest decile:

| metric | median | slowest 10% | |
|---|---|---|---|
| continuation-memo hit rate | 81.4% | **81.3%** | **flat** |
| units / candidate scored | 3.05 | 2.87 | flat |
| mana payment solves / unit | 2.91 | **9.11** | 3.1x worse |
| silently-dropped activations | 6.3% | **12.0%** | 1.9x worse |

The memo covers the tail **exactly** as well as the median. "The cache does not reach the expensive
turns" is **false** on snow, and I would have gone looking for it. Likewise units-per-candidate is
flat, which decomposes the tail precisely: the slowest decision scores **136x** the median's
candidates at **the same unit price each**. The tail is a *branching* explosion, not a per-node cost
explosion — which is why it loops straight back to Part 1: the levers that can matter are the ones
that change how many candidates get built, not ones that filter a list before a fixed-width index.

#### 4. The budget charges for search nodes; the work is mana payment — at a ratio that varies 119x

This is the largest finding in the census.

```
  pay_calls   932,304,227     = 7.60 per charged unit, 51,907 per decision
  units       122,630,341
```

`SearchBudget::Consume` charges **one unit per search node**. Each node drags **7.6 mana-payment
solves** behind it, and that ratio is not a constant:

| percentile of decisions | pay_calls per charged unit |
|---|---|
| p1 | 0.48 |
| p25 | 1.59 |
| p50 | 2.95 |
| p75 | 5.86 |
| p90 | 11.49 |
| p99 | 56.82 |
| max | 4,616 |

**p99/p1 = 119x.** Two decisions handed the identical 9,000-unit budget can perform work differing
by two orders of magnitude, because one is payment-bound and the other is not.

And payment volume is the better cost meter. On the 16,533 decisions with ≥1 ms of wall (Pearson r
on logs; single-threaded, so wall clock is clean):

| counter | r vs log(wall) |
|---|---|
| **pay_calls** | **+0.9532** |
| units (what the budget charges) | +0.9049 |
| rollout_steps | +0.8314 |
| cand_scored | +0.7482 |
| act_fired | +0.7409 |

`units` is already a decent predictor — this is not "the budget is meaningless". But `pay_calls`
explains ~11% more of the log-wall variance than the quantity the budget actually spends, and the
119x ratio spread is the operational consequence: **a unit budget is not a work budget.** It is a
cap on a proxy whose relationship to work is board-dependent.

Worked example, the single most payment-bound decision in the run (seed 910046, turn 5, pre-combat
main, depth 3): **149,621 units (16.6x budget), 18.4 SECONDS of wall clock at a 10 ms budget, and
9,371,450 payment solves — 62.6 per charged unit.**

#### 5. Silent drops at scale: 12.6%, and 60% of the tapped class is double-booking

| | count | share of activations enumerated |
|---|---|---|
| fired | 109,097,919 | 87.4% |
| dropped `tapped` (the `{T}` was spent for mana) | 5,959,513 | 4.8% |
| dropped `unpaid` (`{T}` paid, mana half failed, rolled back) | 9,788,358 | 7.8% |
| **total dropped** | **15,747,871** | **12.6%** |

Of the 5,959,513 `tapped` drops, **3,570,135 (59.9%) are double-booking** — attributed by the
per-apply join to a continuation that re-solved and took a source the base plan still owed a `{T}`
to. That reproduces the earlier 2-game finding at 1,280-game scale, and it is the slice with a
lossless argument behind it (one permanent, one tap, CR 602.2a).

The drop share is a *tail* phenomenon (6.3% median → 12.0% in the slowest decile), and the worst
single decision in the run drops **33.4%** of its activation attempts (seed 910936, turn 6).

#### 6. Two filters, measured per-turn for the first time

* **`bp_condemn`: 34,628,434 consultations, 422,594 declines — 1.22%.** 15,041 of 17,961 decisions
  ask; only 8,956 decline anything. Per Part 1 those declines cannot reduce work anyway, so this is
  35 million predicate evaluations buying a content substitution. (The consultation itself is cheap
  — this is not being proposed as a cost lever, it is context for why condemnation measures flat.)
* **`bp_newonly`: 31,964,971 seen, 19,598,666 dropped — 61.3%.** By far the hardest-working filter
  in the engine, and it lives upstream of the same fixed-width index, so the same caveat applies to
  reading its drop count as a saving.

#### 7. What the staged `MTG_SNOW_LOOK_COLOR` lever would actually prune

| site-8 playability gate | count |
|---|---|
| opens | 64,481,215 |
| pass the mana-value test | 46,237,625 (71.7%) |
| …but are COLOUR-unpayable | **7,335,337** (15.9% of those) |

**59.4% of the colour-unpayable opens are in the slowest 10% of decisions.** So the lever already
measured adoptable on held-out seeds (0 cells worse, net −1.00 turns, −0.58% CPU) is specifically a
**tail** lever, which is consistent with a small budgeted CPU delta: inside a budget, work freed on
a tail decision is respent (memory `matrix-cost-is-abandonment-rate`). Its value is concentrated
exactly where the cost is.

### Second read: the CONTROL decks, and the shapes are completely different

Same settings (d3, 10 virtual-ms, held-out 910000+ seeds, single-threaded). EldraziDisplacerFlicker
was chosen because it has no snow tap-draw engines, Melira Pod because its infinite-combo boards are
what motivated the per-decision work ceiling in the first place.

| deck | decisions | pay/unit | memo hit | activations dropped | over budget | re-work | `bp_condemn_seen` | worst decision |
|---|---|---|---|---|---|---|---|---|
| Snow | 17,961 | 7.6 | 82.8% | 12.6% | 22.7% | 5.44% | 34,628,434 | 18.4 s |
| **EDF** | 8,886 | **43.9** | 64.8% | **32.7%** | 14.9% | 7.42% | **0** | **72.5 s** |
| Melira Pod | 1,292 | **1.2** | 80.2% | **0.0%** | 28.5% | 8.45% | 0 | 3.2 s |

Per-site unit share:

| site | Snow | EDF | Melira |
|---|---|---|---|
| `la_cand` | 35.9% | 28.9% | 0.3% |
| `rollout_step` | 25.9% | 35.1% | 6.9% |
| `greedy_fallback` | 25.2% | 33.6% | 6.9% |
| `la_bp_wave` | 11.7% | 0.4% | 0.1% |
| `fs_main2` | 0.0% | 0.0% | **45.2%** |
| `fs_pre` | 1.0% | 1.5% | **32.6%** |

Four things follow, and none of them was visible in any aggregate:

1. **`pay_calls` per unit varies 37x BETWEEN DECKS** (Melira 1.2 → EDF 43.9), on top of the 119x
   spread *within* snow. So the budget unit is not a common currency: **comparing two decks at the
   same `--budget-ms` compares different amounts of work**, by more than an order of magnitude.
   That has a direct bearing on `test/suite_cost.json` and the 3x cost rule, which are denominated
   in per-game core-ms rather than units — those are measuring wall clock and are therefore fine,
   but any reasoning that treats a unit budget as a work budget across decks is not.
2. **EDF is the pathological deck, not snow or Melira** — a **72.5-second single decision** at a
   10 ms budget (7,250x nominal in wall terms), 43.9 payment solves per charged unit, and 32.7% of
   activations silently dropped. It reached that with its **breakpoint machinery entirely dark**
   (`la_bp_wave` 0.4%, `bp_condemn_seen` exactly 0 across 8,886 decisions). So breakpoints are not
   the universal cost, and a deck can be far worse than snow without touching them.
3. **Melira's cost is a different mechanism entirely**: `fs_main2` (the second-main plan loop) at
   45.2% plus `fs_pre` at 32.6%, with pay/unit of 1.2 and **zero** dropped activations. Its cost is
   plan *enumeration*, not payment. A lever aimed at payment volume would be inert on it.
4. **Snow is the only one of the three where condemnation runs at all.** 34.6M consultations on
   snow, 0 on both controls. So every condemnation measurement in this repo is a snow (and
   snow-like) measurement.

### Deduplication: what the census was NOT reporting, and two bugs it exposed in the tool

USER 2026-09-30: *"Are we including information on deduplication and such?"* — **no, and the
reasons were both defects in this tool.**

#### Bug 1: a whole family of columns was dark and the file said nothing

`dedup_*`, `dom_*`, `bplen_*` and `lazy_*` all read hard zeros on every deck, because each is gated
on a probe or lever that was off (`MTG_DEDUP_CENSUS`, `MTG_DOM_CENSUS`, `MTG_BP_NSKIP_GLOBAL`, the
lazy leaf). The `#` gate line — the safeguard written specifically to prevent this — listed only
`dom_census`, so three families of zeros read as measurements. **A hand-maintained list of gates is
not a safeguard; it is one more thing to forget.**

Fixed two ways. The gate line now names every optional family (including the runtime-scope caveat
for `MTG_BP_NSKIP_GLOBAL`, which is inert under a budget *by design*). And, structurally,
`scripts/turn_census.py` now reports **any column that is zero on every row**, consulting no list at
all:

```
*** 33 COLUMN(S) ARE ZERO ON ALL 9261 ROWS -- do NOT read these as 'it never happens':
      bplen_*  bplen_records, bplen_hits, bplen_skips
      dedup_*  ...
      sres_*   sres_passes, sres_overruns, sres_partial, sres_escalated, sres_refused
      u_*      u_fs_main2, u_fs_tranche, u_fs_group_wave, u_la_group_wave, u_esc_eval, u_fs_m2_wave
```

That immediately paid for itself beyond dedup: `bp_condemn_greedy` / `bp_condemn_exec` are zero, so
the searched/exec/rollout split I had been quoting from the aggregate printer is degenerate on snow;
and six `u_*` sites are zero, which is a genuine finding (snow's search work lives in exactly four
Consume sites) rather than a dark instrument. The check cannot tell those two cases apart — that is
the reader's job — but it makes the question unmissable.

#### Bug 2: a column named for a rate read a different counter

`dedup_dup` was wired to `g_dedup_exactdup` — the exact-fingerprint *cross-check* — not to
`g_dedup_dup`, the counter the dedup census actually reports. Dividing it by `cand_scored` (also the
wrong denominator: the dedup is consulted at two specific sites, not once per scored candidate) gave
**5.2%** against the 64% documented at `MTG_CAND_DEDUP`. I nearly reported that as a discrepancy.

The X-macro table guarantees a column's name and its expression stay on one line; it cannot stop the
wrong counter being paired with a plausible name. So the fix is that any column naming a rate now
ships **with its own denominator in the same row**: `dedup_seen`, `dedup_dup`, `dedup_copy_perm`,
`dedup_copy_false`, and the old one renamed to `dedup_exactdup` so it says what it reads.

Correctly measured (snow d3/b10, 256 games, `MTG_DEDUP_CENSUS=1`):

```
  consultations 8,480,510   duplicates 4,379,501   dup_rate 51.6%
  of those duplicates, recognisable from the PLAN alone: 1,941,280
  plan-test FALSE positives:                            1,965,128
```

**51.6%**, consistent in magnitude with the documented 64% (different sample, different cell) — the
5.2% was entirely my denominator.

#### The plan-signature test is wrong about half the time, which retracts a claim I made

`MTG_CAND_DEDUP`'s comment records **"DO NOT 'OPTIMISE' THIS BY DEDUPING ON THE PLAN INSTEAD OF THE
STATE -- MEASURED UNSOUND, TWICE"**, with a 32% false rate even for a fully-widened plan signature.
This run measures **1,965,128 false against 1,941,280 true — a 50.3% false rate**.

That matters because the first decision dump printed "117 enumerated, **27 DISTINCT action lists**",
and I read it out as *"90 of the 117 are duplicate payments — 77% redundancy"*. An action-list
string **is** a plan signature, i.e. exactly the test recorded as unsound. **Retract that framing.**
The dump now computes both and labels them:

```
MAIN PLANS: 117 enumerated
    distinct ACTION LISTS      27   (a plan-signature test -- MEASURED UNSOUND; shown for contrast)
    distinct POST-APPLY STATES 31   <-- the ENGINE's test (BuildDedupKey).
                                        86 of 117 candidates reach a board a sibling already reached
  CONTINUATION LIST: 37 enumerated
    distinct ACTION LISTS      27
    distinct POST-APPLY STATES 25   <-- 12 of 37
```

The two tests disagree **in both directions on the same decision**: the string test merges 4 plans
that reach genuinely different boards (27 vs 31 distinct), and on the continuation list it misses
duplication the state test finds (10 vs 12). The real, engine-defined redundancy on this decision is
**86 of 117 (73.5%)** — close to my number in magnitude, arrived at by a method that is wrong half
the time, and with the distinct count itself wrong.

#### Settling `MTG_CAND_DEDUP`'s "NOT RESOLVED" cost — partly

That lever's comment states its own blocker: *"settling it needs a low-noise instrument
(single-threaded deterministic runs, or instruction counts), not more reps of the same kind."* The
census runner is single-threaded deterministic runs, so it is worth reporting what it can and cannot
do. Two replicates, arm order **reversed** in the second so drift pushes the opposite way (256
games, held-out seeds, 32 processes per arm):

| metric | OFF | ON | rep1 Δ | rep2 Δ |
|---|---|---|---|---|
| units | 24,591,807 | 23,918,313 | **−2.74%** | −2.74% |
| rollout_calls | 4,468,648 | 3,995,318 | **−10.59%** | −10.59% |
| **pay_calls** | 172,165,846 | 171,910,924 | **−0.15%** | −0.15% |
| cand_scored | 8,480,510 | 8,637,000 | **+1.85%** | +1.85% |
| user CPU (whole arm) | 599.53 s | 608.82 s | +1.55% | +0.63% |
| wall (whole arm) | 51.95 s | 53.74 s | +3.45% | +2.35% |

**The counter columns are byte-identical across the two replicates** — the instrument is fully
deterministic, which is what makes the work rows exact rather than estimates.

**Resolved (work):** dedup cuts rollout calls by **10.59%** and units by **2.74%**, exactly and
reproducibly.

**NOT resolved (time), and I should not claim otherwise.** The within-arm CPU spread is 1.61% (OFF:
599.53 vs 609.20 s), which still swamps a between-arm effect of 0.63–1.55%. The original verdict
stands on the time axis; my instrument did not beat that noise.

**But the mechanism now explains why there is nothing to find**, which is more useful than another
timing rep:

* `pay_calls` moves **−0.15%** — and `pay_calls` is the counter that best predicts log(wall) in this
  census (r = +0.953 vs units' +0.905). The rollouts dedup skips are not where the payment work is,
  so the best available predictor of time says the time effect is ~zero.
* `cand_scored` goes **UP 1.85%**. The budget respends the saving on more candidates — exactly
  `matrix-cost-is-abandonment-rate`. A work saving inside a budget cannot surface as a speedup.

So `MTG_CAND_DEDUP` staying default OFF is unchanged, and its own stated rationale ("built to make
Snow faster, and there is no evidence that it does") is now supported by a mechanism rather than by
an unresolved timing comparison. It remains a clean *quality* lever (`slower=0 faster=5
play-changed=19`), and adopting it on that basis would move 17 GT keys — a user decision.

### The per-TURN breakdown (`turn_census.py turns`) — where in a game the cost actually arrives

`rank` answers "which decisions cost most". That is a different question from "where in a GAME does
the cost arrive", and only the second says whether a lever is aimed at the right end of the curve.

**snow d3/b10, 1,280 games, 17,961 decisions**

| turn | decisions | units | % of total | u/decision | memo% | pay/u | drop% | >1x budget |
|---|---|---|---|---|---|---|---|---|
| 1 | 3,013 | 14.8M | 12.0% | 4,900 | 79.2% | 5.73 | 11.3% | 11% |
| 2 | 3,002 | 22.3M | 18.2% | 7,417 | 81.0% | 6.54 | 10.9% | 28% |
| **3** | 3,050 | **28.8M** | **23.5%** | **9,445** | 80.4% | 6.91 | 11.3% | **35%** |
| 4 | 3,084 | 26.8M | 21.9% | 8,689 | 82.1% | 8.39 | 12.3% | 31% |
| 5 | 2,800 | 18.8M | 15.3% | 6,701 | 84.8% | **10.50** | 13.6% | 20% |
| 6 | 1,777 | 7.5M | 6.1% | 4,203 | 87.9% | 8.12 | 14.4% | 11% |
| 7 | 798 | 3.6M | 2.9% | 4,473 | 91.6% | 5.52 | 17.5% | 12% |
| 8 | 437 | 0.2M | 0.2% | 453 | 97.3% | 4.30 | **21.9%** | 0% |

Four monotone trends, and one of them undercuts a target I had been steering toward:

1. **The cost is EARLY-MID, not late.** It peaks at turn 3 and turns 2–4 hold **63.6%** of all
   search work. The big-board late turns are cheap.
2. **`memo%` rises monotonically, 79.2% → 97.3%.** The continuation memo is *worst* on the turns
   that cost the most — but only by ~3pp between turn 1 and the peak, so this is a weak effect, and
   it is the third independent confirmation that caching is not the problem.
3. **`pay/u` rises to 10.50 at turn 5** — the board accumulates activation sources faster than the
   budget accounts for them — then falls as games end.
4. **`drop%` rises monotonically, 11.3% → 21.9%**, i.e. the silent-drop rate is worst exactly where
   there is almost no work left to save (turn 8 is 0.2% of units). **So on snow the drop defect is
   concentrated away from the cost.** That is an argument for treating the double-booking filter as
   a quality fix and not a cost one — the same conclusion Part 1 reached structurally, now reached
   from the other direction.

**The controls, and they differ more than the site table showed**

| | peak turn | share in turns 1–4 | pay/u profile | drop% profile | decision PHASE |
|---|---|---|---|---|---|
| snow | 3 | 75.6% | rises 5.7 → 10.5 | rises 11% → 22% | **100% pre-combat main** |
| EDF | 2 | 93.2% | **39–50 at EVERY turn** | **~33% flat from turn 1** | **100% pre-combat main** |
| Melira Pod | 2 | 98.2% | 1.0–1.4 (trivial) | **zero, every turn** | **98% SECOND main** |

Three things that were not visible before:

* **EDF's payment load is not an accumulation.** It is 50.5 pay/u on **turn 1** and never drops
  below 13. So the "board accumulates sources" story that fits snow does not fit EDF at all; EDF is
  payment-bound from the opening. Its worst decision is turn 4 at **72.5 seconds**.
* **EDF's drop rate is ~33% at every turn, including the turns that hold the work** (turns 1–3 =
  82.4% of its units). Unlike snow, EDF's drop defect sits exactly on top of its cost.
* **Melira's decisions are SECOND main; snow's and EDF's are pre-combat main.** 391 of 398 turn-1
  Melira decisions are second-main, and *zero* snow or EDF decisions are. That is why Melira's units
  live in `u_fs_main2` — and it means **a lever acting on pre-combat-main enumeration is
  structurally inert on Melira, and a second-main lever is structurally inert on snow and EDF.**
  Worth checking before sizing any enumeration lever on one deck and generalising.

All three decks peak at turn 2–3 and are >75% done by turn 4, so "the cost is early-mid deployment
branching" is the one generalisation the three of them support.

## Open items this leaves

* **Read the scaled census** (1,280 games) and rank the real 1,000 slowest turns. Present a sample
  of individual decisions to the user for the "is this work necessary" check — `explain -n K`
  prints the full counter set plus an exact single-game repro command per row.
* **`pay_calls` is unbilled.** If it holds at scale, the per-decision work ceiling is measuring the
  wrong quantity on precisely the decisions it exists to bound. Whether `Consume` should charge for
  payment solves is a behaviour change (it would move every budgeted cell's play) and therefore a
  user decision, not an agent's — but it is the largest single finding the census has produced.
* **The double-booking filter** — build it, and read it as a quality fix per Part 1's corollary.
* **Census a second deck.** Everything above is snow. The tail's shape on a deck without snow's
  tap-draw engines is the control, and `u_*` share shifts are the cheapest way to see whether
  breakpoint waves dominate the tail generally or only here.

## 2026-09-30 (later): THE HEAVIEST TURNS, with the branching funnel that explains them

USER: *"I'm looking for the heaviest turns in a set of games and a breakdown on the branching (what
are we trimming, how the cache interacts and such) even better if we can also include the cost of
each so that we can diagnose why it is slow"*, and *"I don't want summaries."* Snow only.

Three instruments were merged for this: this census (per decision), `enumstats` (the subset funnel),
and `shapestats` / `MTG_BRANCH_SHAPE` — the last written by another agent for Fungus and taken from
`c9e75a99`. All are forced on by `MTG_TURN_CENSUS` and all are inert by default (smoke 118 passed 0
failed, `configs changed: 0 unchanged: 104`, `slower=0 faster=0 play-changed=0`).

    bash scripts/turn_census_run.sh decks/Snow 3 10 40 logs/turncensus/snow_full 32
    python3 scripts/turn_census.py heavyturn logs/turncensus/snow_full/chunk_*.tsv --top 20
    python3 scripts/turn_census.py heavy     logs/turncensus/snow_full/chunk_*.tsv --top 6

### 1. THE PER-DECISION RANKING WAS HIDING THE WORST CASES — a turn is not a decision

17,961 decision roots collapse into **7,233 real turns: 2.5 roots per turn, max 40.** Every heavy
decision is pinned at the same 25x overrun ceiling, so ranked by decision they all look identical.
Ranked by TURN the spread reopens:

| seed | turn | roots | units | x budget | trunc | discarded | pay solves | walk A | walk B |
|---|---|---|---|---|---|---|---|---|---|
| 911223 | 5 | 7 | 1,230,318 | **137x** | 2 | 450,428 | 17,898,713 | 1,214,753 | 34,432,544 |
| 910387 | 5 | 7 | 1,062,070 | 118x | 1 | 225,442 | 5,075,189 | 553,730 | 6,755,074 |
| 910716 | 7 | 15 | 867,771 | 96x | 2 | 451,762 | 4,498,771 | 856,042 | 301,979 |
| 910506 | 5 | 5 | 826,700 | 92x | 2 | 450,681 | 11,473,725 | 592,662 | 13,454,417 |
| 910671 | 4 | **36** | 632,902 | 70x | 0 | 0 | 1,551,190 | 280,556 | 1,305,490 |

**CORRECTION (2026-10-01): the mechanism is NOT `MTG_PLAY_SEGMENT_ALWAYS`.** That was this
document's first answer and it is wrong -- the flag is HUMAN-PLAY ONLY (its branch sits inside
`use_external && !m_in_rollout`, and `AIEngine.cpp:2511` states *"the autonomous search never enters
here"*), while every census run here is autonomous goldfish, so it was never active in the
measurement. What IS measured: roots arrive in exact `root=2`/`root=1` PAIRS, and the leading
candidate is `AIEngine.cpp:4417` -- *"A COMMITTED line's unsearched breakpoint continuation is a REAL
DECISION"* -- re-solved with `SearchBudget bp_budget = DecisionBudget()`, a brand-new FULL budget.
UNCONFIRMED, and it does not account for the repetition noted below. Either way the budget is per
decision ROOT and **there is no per-turn ceiling at all**:

| roots/turn | turns | % turns | units | **% units** | units/turn |
|---|---|---|---|---|---|
| 1 | 5,177 | 71.6% | 39,463,493 | 32.2% | 7,623 |
| 2–3 | 804 | 11.1% | 7,259,525 | 5.9% | 9,029 |
| 4–8 | 925 | 12.8% | 48,493,886 | **39.5%** | 52,426 |
| 9–20 | 272 | 3.8% | 18,762,073 | 15.3% | 68,978 |
| 21+ | 55 | 0.8% | 8,651,364 | 7.1% | **157,298** |

**Turns that re-enter 4+ times are 17.4% of turns and 61.9% of all units.** A 21+-root turn averages
157,298 units against a 9,000-unit budget — 20.6x a single-root turn. A per-TURN work ceiling is the
shape of lever this points at.

`root=2` (FullSearchLineHybrid) is **94.6%** of units over 15,284 roots; `root=1` is 5.4%.

**AND THE SAME POSITION IS SOLVED REPEATEDLY.** Seed 910671 turn 4 executed **6 actions** (play
Scrying Sheets, cast Arcum's Astrolabe, draw, activate Sheets, cast Ice-Fang Coatl, draw) and spent
36 roots / 632,902 units / **259,633 scored candidates** on an 8-option board. Several root-pairs
recur with byte-identical cost -- `6,361/6,084` appears three times -- so it is one state re-solved,
not progress. Driver NOT YET IDENTIFIED: it is not the collapse arms (on, and measured firing) and
not segmentation (inactive here). Largest unexplained waste on the deck, and unlike the fold it needs
no soundness argument to remove.

### 2. THERE ARE TWO SUBSET WALKS, and the heavy decisions use the one that was never instrumented

`EnumeratePlans` (walk A, builds the decision list) and `SolveUncached` (walk B, the rollout leaf)
each keep their own private copy of `consider`. Only walk B had a funnel, so the heaviest decisions —
which run walk A in the millions and walk B at **exactly zero** — appeared to have no branching at
all. Walk A is now instrumented (`enum_*`). Deck-wide, 1,280 games:

| walk A — EnumeratePlans | visits | share |
|---|---|---|
| entered | 120,658,041 | 100.00% |
| rejected by saturation | 0 | 0.00% |
| rejected by subset rules | 8,987,250 | 7.45% |
| rejected by mana/other | 37,734,566 | 31.27% |
| **emitted as a plan** | 73,936,225 | 61.28% |

| walk B — SolveUncached | survivors | rejected here |
|---|---|---|
| entered | 1,321,872,661 | — |
| passed subset rules | 753,078,764 | **568,793,897 = 43.03%** |
| passed flat mana | 563,229,523 | 189,849,241 = 14.36% |
| passed SubsetPayable | 560,779,384 | 2,450,139 = 0.19% |
| passed colour feasibility | 557,767,467 | 3,011,917 = 0.23% |
| **scored** | 557,767,467 | — |

Total **11.8 subset visits per charged unit** (1.44 billion visits for 122.6 M units). Only 0.42% of
walk-B rejections happen after a payment solve, so late/expensive rejection is NOT the problem.

### 3. WHAT WE ARE TRIMMING IS ONE RULE: the canonical-prefix fold, 43.7% of every walk-B visit

`SubsetHasDuplicateSacSource` owns 100% of walk B's rule rejections. Its NAME is about sacrifice and
Snow has no sacrifice outlet, so the total was unusable until the clause was named — the predicate is
really a nine-clause "one use per source per plan" rule. Instrumented per clause
(`dupc_*`), on 1,280 games, **eight of the nine clauses are exactly zero** and one owns everything:

| clause | rejects | share of ALL walk-B visits |
|---|---|---|
| `fold_prefix` | **577,781,147** | **43.71%** |
| every other clause (8) | 0 | 0.00% |

That clause is the INTERCHANGEABLE-SOURCES canonical prefix: among activations sharing a nonzero
`equiv_tag` the sources were proved indistinguishable, so it admits only the prefix selection and
rejects every other arrangement. Its own comment says it "collapses 2^n arrangements per class to
n+1" — **and it does, in the OUTPUT. The walk still VISITS all 2^n and throws 43.7% away at the
leaf.** Snow is dense in interchangeable sources (Scrying Sheets x2, Coldsteel Heart x2, the
Snow-Covered lands), so the classes are large.

It is also a TAIL lever: **47.4% of visits in the slowest decile vs 30.8% at the median.**

**Why this one is worth pursuing when condemnation was not.** `fixed-width-index-prune-cannot-save`
retired filters that sit upstream of a fixed-width index. This is the opposite shape: it is a
GENERATION-side change (have the odometer offer "how many of this equiv class", n+1, instead of
"which subset", 2^n), the surviving set is identical BY CONSTRUCTION because that is already what the
leaf test computes, and so `units`, plans scored and play should be byte-identical while the visit
count falls. That combination — same output, less work — is the "same-work-cheaper" case of
`optimization-vs-estimand-change`. **UNBUILT AND UNMEASURED; the claim of identical output is an
argument, not a measurement, and must be proved with `MTG_FOLD_VERIFY=1` before anyone believes it.**

Note `MTG_FOLD_SEARCH_ODO` (default OFF) is a DIFFERENT thing and does not do this: the main odometer
already arms `foldsel::g_from_odometer` unconditionally at three sites; that flag arms a second,
private copy of the odometer elsewhere.

### 4. THE RESCUE PATH COPIES THE BOARD 212 MILLION TIMES to say "no" 74% of the time

`SubsetPayableWithFilters` copies the board per call: **211,598,172 calls, 54,742,865 rescued
(25.9%)** — so **156,855,307 board copies answered "no"**. This is invisible to `units` entirely.

### 5. CACHING IS FINE — now confirmed a fourth way, and this one is the strongest

Memo hit rate is **82.7% in the slowest decile vs 83.5% at the median** — flat, and slightly WORSE at
the median. The slowest decile does **487,095 walk-B visits per decision against the median's 17,176
(28x)** at nearly identical per-enumeration shape (board 12.5 vs 9.5, option groups 3.98 vs 4.00,
odometer 50 vs 46 per enumeration). The walk dedup removes 46.5% of emitted plans deck-wide.

**So the tail is pure VOLUME, not a bigger or worse-cached search.** This diverges from Fungus, where
the hot spot WAS a situation class with huge odometers (avg_odo 1,704, 5–16x every other turn). Same
symptom, different mechanism — do not carry the Fungus framing onto Snow.

### 6. Corrections made to this tool while producing the above

1. **An absent column rendered as a measurement.** `heavy` printed `situation: 0.00 option groups,
   board 0.0 avg` for every row of a file written before those columns existed, and `heavyturn`
   printed `walkA 0`. Both now print `n/a` and say the column is missing. This is the third variant
   of the dark-column trap in this instrument.
2. **A clause printed as 102.1% of its own denominator.** The `dupc_*` counters span both walks while
   `sub_rej_dupsrc` covered site 0 only. Fixed by instrumenting site 1 (`sub_rej_dupsrc_s1`) so the
   denominator covers exactly the sites the numerator does; it now reads 100.0%.
3. **The two walks were conflated under one "BRANCHING" heading**, which is what hid walk A.
4. My first reading of the 73.9% was that the canonical-prefix clause could not be responsible,
   because it is gated on `from_odometer` and `MTG_FOLD_SEARCH_ODO` is default off. That was wrong:
   the main odometer arms the flag unconditionally. The data corrected the inference.

## 2026-10-01: THE WASTE, LOCALISED PER ARM -- and the levers measured as a bundle

USER: *"take whatever options we can 'quality-lever' or not to minimize unnecessary branching"*,
*"We should do exactly the work we need to and no more"*, *"It isn't worth doing work we don't need
to just to prevent us from searching deeper"*, *"If we want to do that, then we should just adjust
how we charge against the budget instead (and do less work overall)"*, *"there is a balance between
the two... But the point is we should take one or the other, not waste it"*, and the diagnostic
reason: *"When we analyze the plans etc. It should be clear if anything in there is waste. If we let
this known waste stand that becomes extremely difficult."* Plus the acceptance criterion:
*"Them being in there is somewhat okay as long as the dump marks them as duplicates (and doesn't
actually do a bunch of work for them)."*

So the bar is NOT "remove the duplicate candidates from the list". It is: **mark them, and do not do
work for them.** The dump already marks them (`[STATE-DUP of #n]`). This section measures the work.

### The lever bundle: -9% units / -16% rollouts at no quality cost

`scripts/lever_sweep.sh`, 330 games per arm, snow d3/b10, ONE pooled queue of 33 processes:

| arm | avg_turns | units_total | cand_scored | rollout_calls |
|---|---|---|---|---|
| base | 6.0364 | 31,787,144 | 10,532,557 | 5,787,281 |
| `MTG_CAND_DEDUP=1` | 6.0333 | **-5.76%** | -0.01% | **-12.42%** |
| + `BP_ARM_NEW`, `BP_NSKIP_GLOBAL=2`, `BP_NSKIP_ATPLAY`, `SNOW_LOOK_COLOR` | 6.0303 | **-9.01%** | -4.98% | **-15.98%** |

Quality is unchanged WITHIN THE METRIC'S RESOLUTION and must not be reported as an improvement: at
330 games one changed game is 0.0030 turns, so -0.0030 and -0.0061 are 1 and 2 games
([[delta-at-the-metric-quantum]]). Train seeds (920000+); a real adoption needs held-out seeds and
moves GT.

The two arms already ON are not inert -- in the BASE arm they skip 2,041,118 uniform-collapse and
841,600 NOBP variants over 330 games.

`MTG_BP_ARM_NEW` alone is ~INERT ON SNOW: one game, 252 units of 3,569,398 (0.007%) and
`cand_scored` IDENTICAL. Its decline needs an EMPTY continuation list; snow's arrivals are usually
usable. It was measured on knights (site 10: 61.7% of opens offered no new line), so it is a
knights-class lever. Default OFF "(measuring)" -- that measurement is now done, for snow.

### WHAT SURVIVES THE BUNDLE: the apply of duplicate candidates

With the whole bundle ON, **57.3% of scored candidates still land on a state a sibling already
reached** (775,048 of 1,351,443) -- essentially unchanged from 57.5% with everything off. The reason
is structural: `MTG_CAND_DEDUP` keys on the POST-APPLY state, so a duplicate must be APPLIED before
it can be recognised. It saves the rollout (-12.4% rollout_calls) and not the apply.

`dedup_why_arm` localises the surviving applies exactly (one game, seed 910671, bundle ON):

| arm | applies | duplicates | rate |
|---|---|---|---|
| base | 659,825 | 174,041 | 26.4% |
| rank | 382,211 | 293,323 | 76.7% |
| uniform | 118,303 | 116,891 | **98.8%** |
| chain | 191,104 | 190,793 | **99.8%** |

**The chain slot and the uniform arm are ~99% duplicate applies: 309,407 of 1,351,443 applies (23%)
for which the engine already holds a soundness argument.** `dedup_why_chain` refines it further:
`covered(ci<W)=80,883` of which **80,883 are duplicates (100%)**, and `EMPTY(ci<0)=109,860` of which
109,841 are duplicates.

### The two fixes this points at, and why they are not the ones already shipped

1. **A chain-slot analogue of `MTG_BP_W0_UNIF_COLLAPSE`.** There is no collapse for the chain arm at
   all, and it is 99.8% duplicate on snow. Same identity: a chain variant on a base plan whose apply
   reaches one breakpoint resolves to that base plan's own continuation.
2. **Extend the uniform collapse's COVERAGE.** The arm is 98.8% duplicate yet still applied 118,303
   times, because the collapse is conservative by design -- "a missing memo entry never skips", so a
   unif variant whose rank sibling did not fill the memo is applied anyway. The gap is coverage, not
   the identity.

Both are generation/pre-apply suppressions, so they remove the APPLY, which is the half no shipped
lever removes. `g_bp_base_bps == 1` is the condition and it already exists (`MTG_BP_ARM_NEW` uses it).

### Why `units` is the wrong readout for all of this, and what to charge instead

`SearchBudget::Consume` charges 1 unit per simulated turn-step (`SearchBudget.h:57`,
`NODES_PER_VIRTUAL_MS = 900`). A node drags **7.6 payment solves on snow, varying 119x p1->p99**, and
`pay_calls` predicts log(wall) better than units (r=+0.953 vs +0.905). So a lever that frees work
gets its saving re-spent and `units` under-reports it -- visible here as `cand_scored` -0.01% while
rollout_calls fell 12.42%, and as walkB visits RISING 5.68M -> 5.95M with the bundle on.

USER's direction: *"adjust how we charge against the budget instead (and do less work overall)."* The
targeted form is to charge a weighted amount for payment solves in addition to the node, then
RE-CALIBRATE `NODES_PER_VIRTUAL_MS` so the MEDIAN decision keeps its present allowance. That
compresses the 119x tail (expensive-per-node decisions stop earlier) while leaving the median
untouched -- and the recalibration constant IS the quality/performance dial the user described.
**It moves every budgeted cell's play on every deck, so it is a deliberate rebaseline, not an
optimisation.** SEQUENCING HAZARD: GT already carries an UNCOMMITTED `ACT_LINE_HOLD` rebaseline, so
landing this on top would make the two GT moves inseparable -- commit that one first.

---

## THE TWO FIXES, BUILT AND MEASURED -- one landed, one REFUTED (2026-10-01)

The section above names two fixes. They were built. **Fix 1 works and is small; fix 2 does not
exist.** Both the prediction that was wrong and the number that refuted it are recorded here,
because the refutation is the more useful finding and the framing it kills is an attractive one.

New flags, both **DEFAULT OFF (measuring)**, both in `src/ai/TurnSolver.cpp`:

| flag | what it arms |
|---|---|
| `MTG_BP_W0_FSW` | the two ADOPTED wave-0 collapses (`MTG_BP_W0_NOBP`, `MTG_BP_W0_UNIF_COLLAPSE`) inside **FSLineWin's** plan loop, which never had either |
| `MTG_BP_W0_CHAIN_COLLAPSE` | a chain-arm collapse, in **both** hosts -- the arm had none anywhere |

Shared implementation in the new `w0collapse` namespace, deliberately ONE copy for both candidate
loops: every skip in this family so far was written into one loop and not the other, and that is not
a hypothetical drift risk, it is what the measurement below found.

### Correction 1: the applies are NOT in FSLineWin, and "94.6% of units" does not say they are

The section above reasons from *"`root=2` (`FullSearchLineHybrid`) is 94.6% of units"* to "FSLineWin
is where the work is". That inference is wrong, and the port measured it:

```
snow d3/b10, 3 games, seed 910671
  SolveWithLookahead's loop   w0_nobp  59,540   w0_unif_collapse  264,190
  FSLineWin's plan loop       nobp      2,140   unif                1,311   (MTG_BP_W0_FSW=1)
```

**FSLineWin's loop sees ~1% of the variant candidates.** `fs_decision_root` classifies the
OUTERMOST frame; underneath a `FullSearchLineHybrid` root, FSLineWin applies the node's plans and
then the tail simulates to the horizon, and every simulated turn re-enters `SolveWithLookahead` --
whose candidate loop is where the variant applies actually happen. A frame census answers "who is
the root", not "where is the volume". The two collapses were therefore already running on ~99% of
the population, not ~5%.

### Correction 2 (the refutation): the uniform collapse has NO coverage gap

Fix 2 rested on *"a missing memo entry never skips"* leaving 118,303 applies through. Instrumented
directly -- for every uniform variant the collapse declines to skip, record WHY:

```
w0_unif_miss  nomemo=0  multi=131,550  nomemo_share=0
```

**Zero.** There is no coverage gap at all. Every uniform variant that survives has a rank sibling
that reported, and that sibling's apply reached **2+ class-on breakpoints** -- the case where
`bp_all` ("take k at EVERY breakpoint") and rank k ("take k at breakpoint 0, canon elsewhere") are
genuinely different lines, so declining one deletes a line rather than a duplicate. The arm's
conservatism costs nothing.

So the 98.8% post-apply duplicate rate on those applies is REAL and is NOT waste the identity can
reach: they are distinct candidates that happen to land on the same state. This is the general trap
in the per-arm duplicate table above -- **a high post-apply duplicate rate does not imply a
removable candidate.** `MTG_CAND_DEDUP` exists precisely because the equality is a fact about STATES,
and its own comment ("DO NOT 'OPTIMISE' THIS BY DEDUPING ON THE PLAN INSTEAD OF THE STATE --
MEASURED UNSOUND, TWICE") is the same lesson from the other direction.

### Fix 1 works, and the chain arm's miss profile is the real finding

The chain collapse's identity is the uniform collapse's with `ci` in place of `k`: a chain variant
resolves `kBpChainChoice + j` to the j-th continuation that itself opens a breakpoint, and when that
scan lands at `ci < W` wave 0's own rank variant already scored that exact entry -- so at one
class-on breakpoint the two are the same candidate. The host learns `ci` without applying anything:
`BpChainCandIndex` is a walk over the MEMOISED continuation list, and rank 0's apply is standing at
the same breakpoint with the same list in hand, so it does the walk once and publishes the answer on
`g_bp_chain_ci0` exactly as it already publishes the list's LENGTH on `g_bp_cands_last`. One vector
walk per rank-0 apply replaces one `ApplyPlanDirect` per chain slot.

```
snow d3/b10, 3 games, seed 910671, MTG_BP_W0_CHAIN_COLLAPSE=1
  collapsed 28,649   prescans 204,173
  miss:  noci 0   empty 116,657   past_w 395   multi 53,640
```

Of 199,341 chain variants:

| outcome | n | share | reading |
|---|---|---|---|
| `ci < W`, one breakpoint | 28,649 | 14.4% | pure duplicate of rank `ci` -- **now skipped pre-apply** |
| `ci < 0` (EMPTY) | 116,657 | 58.5% | nothing chainable; deliberately NOT collapsed (below) |
| `ci < W`, 2+ breakpoints | 53,640 | 26.9% | not the same candidate -- same `multi` case as the uniform arm |
| `ci >= W` | **395** | **0.20%** | the case the arm was BUILT for |
| no coverage miss | 0 | -- | `noci=0`: rank 0 always published |

**The chain slot's entire purpose fires on one in five hundred of its own candidates on snow.** It
exists for a chainable continuation ranked past W (Dragonstorm's rank 32 of 47, a measured T5->T4),
and that is not snow's shape at all. 58.5% of the arm is instead resolving to EMPTY at every
breakpoint -- i.e. **the chain arm is accidentally doing `MTG_BP_EMPTY_ARM`'s job**, at the price of
a per-breakpoint scan, while `MTG_BP_EMPTY_ARM` itself is default OFF. That is a shape question
about which arms snow should carry, not a duplicate to collapse, and it is sized in the sweep below.

### What is deliberately NOT collapsed, and why the tempting half is left alone

`ci < 0` is the LARGER half (58.5%) and measured **99.98% duplicate** (109,841 of 109,860) in the
earlier per-arm census. It is left in place because the obvious identity is FALSE:

* "`ci < 0` falls through to EMPTY, which is what the base plan takes there" stopped being true when
  `MTG_BP_BASE_CANON` went to **1** (`TurnSolver.cpp:3546`). Outside a rollout a BASE plan at a
  class-on breakpoint is handed `ncands.front()`; a chain variant (`bp_choice >= 0`, and `bp_all` so
  the nested-canon branch skips it too) still takes EMPTY. They are different candidates.
* So what the 99.98% is duplicating has not been named. Declining a line on an unexplained statistic
  is exactly the mistake `understand-why-before-discarding` exists to prevent. The half with a proof
  goes; this half stays until someone can say what it collapses onto.

### The order premise, which is why the port is not a copy-paste

Both memos are keyed POSITIONALLY on `Plan::bp_base` and are sound only while base plans precede
their variants and rank variants precede the uniform ones. `SolveWithLookahead` gets that for free
(`EnumeratePlansWithLandUncached` sorts BEFORE `AppendBreakpointVariants` appends, and nothing
reorders after). **FSLineWin sorts AFTER the append** -- the stale-index hazard its own `bp_self`
remap exists for. The premise survives there for a different reason, and this is the load-bearing
argument of the port: `MoveOrderPlans` is a **stable** sort, and a variant is `Plan v = p` with only
`bp_choice / bp_at / bp_all / bp_base / bp_sched` overwritten, so every field the comparator reads
(`wins_this_turn`, `pump_waste`, `atk_forfeit`, `value`) is byte-copied from the base plan. A family
compares EQUAL throughout and keeps its emission order.

Three things could break that, so the host WITNESSES each rather than inheriting a default:

* `MTG_BP_VARIANT_FIRST` sets `bp_sched` on the rank arm only, hoisting ranks above their own base
  plans (default OFF, REJECTED 2026-09-17);
* the escalation beam's value-ranked reorder permutes `pre` outright (`order_perturbed`);
* **`MTG_BP_WAVE_NSKIP=0` removes the remap**, leaving `bp_base` addressing pre-sort positions. This
  is the LOSSY failure mode -- declining a variant on a measurement belonging to a different base
  plan -- and it is a separate flag from the collapse, so the arming predicate tests it.

### THE NOBP IDENTITY IS FALSE, AND SITE 9 IS WHY (the session's real finding)

Carrying the NOBP skip into FSLineWin **lost a whole turn** on two hand-built fixtures --
`critter_heliod_post_entry_lifelink_grant` and `whiteknights_gideon_emblem_anthem`, both 6 -> 7 --
and lost it at **`budget_ms = 0`**, i.e. unbounded, where there is no freed work to re-spend. So it
is a DELETED LINE, not churn. `MTG_BP_W0_FSW` was split into a bitmask to bisect it: **1 = NOBP
loses both fixtures; 2 = the uniform half is clean on both.**

`MTG_BP_W0_NOBP`'s own header calls its identity *"THE STRONGEST ONE IN THIS FILE"*:

> A variant differs from its base plan ONLY at a breakpoint: `bp_choice`, `bp_at` and `bp_all` are
> read nowhere else in an apply. No breakpoint occurrence therefore means no read, which means the
> identical action list applied to the identical state -- the identical result. **There is nothing
> for a missing condition to hide in.**

There is. `MTG_BP_W0_FSW_VERIFY` applies every candidate the arm would have declined and compares
its post-apply dedup key against its base plan's:

```
w0_nobp_verify  ok=16  MISMATCH=25  bad_share=0.61
[w0-verify] NOBP MISMATCH turn=4 base=0 arm=1 bp_choice=0 bp_all=0
            var_any_delta=1 var_classon_delta=1
            base_plan=[Heliod, Sun-Crowned; +Plains]  variant=[Heliod, Sun-Crowned; +Plains]
```

**`var_any_delta=1` on a byte-identical action list.** The variant's apply fires a breakpoint its
base plan's apply does not. The reader the identity missed is **site 9's own raise condition**
(`TurnSolver.cpp:34413`), which begins `plan.bp_choice >= 0` -- the site is **SEARCHED-ONLY by
deliberate design**, and its own note says so explicitly:

> ONLY for a plan that carries a choice (`plan.bp_choice >= 0`). ... A base plan whose gate is
> merely TRUE fired nothing here, yet reporting it re-solved every such second main inside every
> rollout: dragonstorm regression gi117 5->8 at d3 AND d5. **Variant plans still count (their apply
> DOES fire).**

So a base plan is **structurally blind** at site 9. Its zero does not mean "there is nothing to
decide", it means "base plans cannot see this" -- and NOBP reads the two as the same thing.

**Status of the SHIPPED arm: latent, not active.** `MTG_BP_W0_NOBP` has been default ON since
2026-09-23 and lives only in `SolveWithLookahead`. Running all 118 fixtures with it **disabled**
(`MTG_BP_W0_NOBP=0`) gives byte-identical win turns, so it is costing nothing today. It escapes
because the fixtures' committed decision is taken in FSLineWin -- the host the shipped skip never
reached. Its 7-deck byte-identical proof (burn, fivecolour, fluctuator, kitty, mirrorwing, snow,
treasure_hunt) **contains neither deck that just failed**, which is why the hole survived adoption:
no proof deck has a mana-sink permanent that reaches site 9.

**THE FIX** (`g_bp_searchonly_suppressed`): a base plan's apply evaluates site 9's gate and, when it
holds, marks the apply so no NOBP consumer may admit that plan. Unconditional in the new FSLineWin
path -- a new path must not ship a known-false identity -- and behind `MTG_BP_NOBP_SITE9`
(default OFF) for the shipped arm, because tightening a default-ON skip moves the committed line on
every budgeted cell of every deck. The gate costs one battlefield scan per base-plan apply, so it is
only evaluated when a consumer asks (`w0collapse::NobpSite9Watch`), leaving the default path free.

With the identity repaired, both fixtures pass and the verifier admits **nothing** on CritterLifegain
(`ok=0 bad=0`) -- the correct conservative answer for a deck whose base plans are blind at site 9.

### What it is all worth -- snow, 150 games/arm, one pooled queue, train seeds 920000+

| arm | units | `cand_scored` | avg turns |
|---|---|---|---|
| base | 12,182,309 | 4,003,242 | 6.0533 |
| `MTG_BP_W0_FSW=3` (repaired) | **-0.26%** | **+0.15%** | 6.0533 |
| `MTG_BP_W0_CHAIN_COLLAPSE=1` | -0.03% | **-1.12%** | 6.0533 |
| both | **-0.38%** | **-1.13%** | 6.0533 |
| `MTG_BP_CHAIN_SLOT=0` (the whole arm off) | **-1.88%** | **-10.72%** | 6.0533 |

Win turns are identical **chunk by chunk**, all five chunks, for every arm. Two readings, and the
second is the one that matters:

1. **The two suppressions are sound and small.** -0.38% units / -1.13% candidates together. The FSW
   port is a wash on cost after the site-9 repair (`cand_scored` +0.15%: the repair makes it skip
   less, and the gate costs a scan) -- what it buys is on the QUALITY axis, where smoke reports
   **slower=0, faster=5, play-changed=6**. It is not a performance lever.
2. **Turning the chain arm OFF is 5x bigger than collapsing it**, with identical win turns across
   150 snow games -- which is exactly what `past_w = 0.20%` predicts. Collapsing duplicates inside
   an arm that essentially never pays is optimising the wrong thing.

`MTG_BP_CHAIN_SLOT=0` is NOT adoptable globally: the slot is load-bearing for Dragonstorm (claude_s1_gi0
and claude_s26_gi25 both T5 -> T4, bisected to the rank-32-of-47 continuation). It is a per-deck /
per-archetype question, and on snow the measured quality it buys is **zero** against -10.72% of
candidates. That is the user's *"sometimes it is better to take the extra quality and other times the
extra performance ... but we should take one or the other, not waste it"* -- and here the evidence
says snow is paying for quality it does not receive.

### The generalisable lesson: a duplicate STATE is not a removable CANDIDATE

Both fixes came out of the per-arm table above (uniform 98.8%, chain 99.8% duplicate applies), and
the table **over-promised by roughly an order of magnitude**. The duplication is real; what it does
not tell you is whether the duplicate candidate can be identified BEFORE the apply. Three separate
walls, all measured here:

* the uniform arm has **zero** coverage gap (`nomemo=0`) -- its 131,550 survivors sit at 2+
  breakpoint applies where the arms are genuinely different lines;
* the chain arm's larger half (`ci < 0`, 58.5%, 99.98% duplicate) has **no valid identity** since
  `MTG_BP_BASE_CANON` went to 1, so it stays;
* the identity that did look airtight was **false**, and only a verifier found it.

This is the same lesson `MTG_CAND_DEDUP`'s own comment records from the other direction -- *"DO NOT
'OPTIMISE' THIS BY DEDUPING ON THE PLAN INSTEAD OF THE STATE -- MEASURED UNSOUND, TWICE"*. State
equality is a fact about outcomes; candidate removal is a claim about causes, and the two are not
interchangeable in either direction.

### Inertness

Smoke with every new flag off: **104 passed, 0 failed, configs changed 0, unchanged 104,
`slower=0 faster=0 play-changed=0`**, and all 118 fixtures byte-identical. Each arm proves it fires
rather than measuring byte-identical (`digest-equality-can-mean-broken`). With both repaired arms ON:
118 fixtures byte-identical, smoke **11 configs changed, slower=0, faster=5, play-changed=6** -- no
regression on any axis, so adoption is a GT rebaseline in the favourable direction. **SEQUENCING
HAZARD UNCHANGED: GT already carries an uncommitted `ACT_LINE_HOLD` rebaseline; commit that first or
the two GT moves become inseparable.**

### Flags added (all default OFF / 0)

| flag | effect |
|---|---|
| `MTG_BP_W0_FSW` | bitmask: 1 = NOBP skip, 2 = uniform collapse, 3 = both, in FSLineWin's plan loop |
| `MTG_BP_W0_CHAIN_COLLAPSE` | chain-arm collapse (`0 <= ci < W` only), both hosts |
| `MTG_BP_W0_FSW_VERIFY` | do not skip -- APPLY and compare post-apply keys; the NOBP refutation |
| `MTG_BP_NOBP_SITE9` | apply the site-9 repair to the SHIPPED `MTG_BP_W0_NOBP` arm as well |

---

## THE COST IS NOT IN THE SEARCH. 59.6% OF SNOW'S WORK IS MULLIGAN TRIAL GAMES (2026-10-01)

Everything above measures levers inside the search. This section measures **who asks for a search at
all**, and the answer moves the whole problem: on Snow d3/b10 over 720 games, **59.6% of all charged
units are complete trial GAMES played to decide which cards to put on the bottom of the library
after a mulligan** -- not decisions in the game being played.

### How it was hidden, and what made it visible

The census emits one row per decision root, keyed on `(seed, turn)`. `AIEngine::RolloutWinTurnFrom`
plays a whole trial game, and its decisions **are** census roots: `m_in_rollout` is the EXECUTOR's
flag, while the root predicate reads TurnSolver's `g_rollout_nest`, which that path leaves at 0. A
trial game therefore produces rows carrying the real game's seed and turn numbers, indistinguishable
from real play. So `heavyturn`'s headline -- *"snow seed 910716 turn 7 has FIFTEEN decision roots"* --
was never a statement about one board. It was **"turn 7 was played seven times, twice each."**

Two new columns settle it, and they are deliberately orthogonal:

| column | question |
|---|---|
| `site` | WHO asked (1 main decision, 2 breakpoint re-solve, 3 mode-2 post-draw, 4 pod twin) |
| `probe` | is this root for the game being PLAYED, or for a trial game labelling a decision |

`skey` was added beside them: the engine's own `BuildDedupKey` of the root state, folded to 64 bits,
so "two roots solved the same position" is testable rather than inferred.

### The measurement (snow d3/b10, 720 games, 30 single-threaded processes, seeds 910000-910719)

```
TRIAL-GAME WORK (probe=1): 6,116 of 10,684 roots (57.2%), 43,756,672 of 73,358,942 units (59.6%)
  trial games per phase: median 6, p90 13, max 39
```

| site | host | probe | roots | units | % units | u/root |
|---|---|---|---|---|---|---|
| main | 2 (hybrid) | **1** | 5,369 | 40,071,286 | **54.6%** | 7,463 |
| main | 2 (hybrid) | 0 | 3,691 | 28,679,673 | 39.1% | 7,770 |
| main | 1 (lookahead) | **1** | 747 | 3,685,386 | **5.0%** | 4,934 |
| main | 1 (lookahead) | 0 | 109 | 501,294 | 0.7% | 4,599 |
| bp_resolve | 1 | 0 | 768 | 421,303 | **0.6%** | 549 |

**It is a TAIL, not a tax.** Only **126 of 720 games (18%) bottom at all** -- but on those, trial
games are a **median 90%** of the game's cost (p90 95%, max 98%). Per-game units: median 31,769,
p90 241,075, **max 6,132,438 = 193x the median**. The nine heaviest games in the sample are
84-97% trial work. This is `matrix-cost-is-abandonment-rate` re-explained: the tail is not boards
whose search runs away, it is **games that mulliganed**.

### What the lever is, and that it already exists

`MulliganProfile`'s `bottom_eval_depth` / `bottom_eval_budget_ms` / `bottom_eval_units` /
`bottom_eval_topk` price exactly this decision cheaper, with the real settings kept for a top-K
refine stage. Unset (`-1/-1/-1/0`) means **the trial games run at the deck's full play depth and
budget**, which is what Snow does. Its own header already records the precedent: on Melira
*"full-settings bottoming was ~55% of d3 suite cost"*, and `depth 0 + topk 5` measured
**win-turn-identical over the 50-game d3 set at 1.5x** -- while the same settings forced fleet-wide
moved 6 smoke cases, *"so this must not be a global default."*

Across the whole repo only **two** decks ship a policy: `Melira Pod` (`depth 0`, `topk 5`) and
`Prevent Damage` (`budget_ms 1`). Snow -- the most expensive deck in the suite -- does not, and
nobody had a reason to look, because no instrument attributed cost to the mulligan.

### THE SECOND FINDING: the hybrid falls through on 9.4% of phases and its budget buys NOTHING

`skey` pairs each root with others on the same position. Same state, SAME host: **0 rows** -- there
is no literal repeated solve anywhere (and the earlier reading of a recurring `6,361/6,084` pair as
a repeated identical solve was this artifact: two different TRIAL GAMES of one opening hand).

Same state, DIFFERENT host: **856 phases (9.4%)**. These are
`FullSearchLineHybrid` returning `phases=0`, after which the executor pays a second full
`DecisionBudget()` for the `SolveWithLookahead` fallback on the identical position:

```
hybrid FELL THROUGH on 856 phases: its own units 7,461,525 (10.2% of all work) bought no
  committed plan;  the fallback then spent 4,186,680 (5.7%) on the same position.
```

The mechanism is in FSLineWin: `best.phases` is only written when `tail.win_turn < best.win_turn`,
so when **no candidate wins inside the horizon** the strict `<` never fires and the search returns
an empty line. `MTG_FD_TRACE` on seed 910671 shows it on every phase of the game:

```
[fd] T2 pre=1 ENTER #0 cast=0 hand=6 committed=0
[fd] T2 line win=9 searched_depth=1 verified=0 refuted_full=0 phases=0
[fd] T2 pre=1 FALLBACK lookahead
```

`win=9` with `--max-turns 8` is "no win". `refuted_full` -- the existing short-circuit -- cannot
help here: it requires the searched horizon to reach the turn cap (`turn + searched_depth - 1 >=
max_turns`), and at T2 with `searched_depth=1` that is 2 >= 8.

**Is it predictable? Measured: no.** After a game's first fall-through, 39% of later phases also
fall through against a 10.4% base rate -- 4x enrichment, but only **32%** of the wasted hybrid units
sit after a first failure. A "stop seeking after one failure" latch would therefore reach ~3.3% of
all units while changing play on 167 phases: a trade, not waste removal. Recorded, not built.

Related and deliberately NOT pursued: the hybrid and the fallback build **separate** transposition
tables for the same position (`fd_tt` vs `m_shared_tt`, both nullptr outside the bottoming loop, so
each owns a per-call table). Sharing them is sound but `TranspositionTable.h` already measured the
cross-decision case at **+0.14pp hit rate**, and notes a hit frees budget that the start gate spends
on depth -- LP-neutral, so it would show up as quality, not cost.

### Corrections to earlier sections of this document

1. **`bp_resolve` is not the driver.** The breakpoint re-solve (`AIEngine.cpp`, *"a COMMITTED line's
   unsearched breakpoint continuation is a REAL DECISION"*) was carried here as the largest
   unexplained waste. Measured: **768 roots, 0.6% of units, 549 units each** -- the cheapest site in
   the table. The hypothesis was built on a turn's root COUNT, which is exactly the number the
   `probe` column shows was never a count of decisions.
2. **There are no repeated identical solves.** Same-state/same-host duplicates are **0**. Both the
   36-roots turn and the recurring unit pair were trial games.
3. `heavyturn`'s docstring now carries the caveat in-tool, so the next reader cannot repeat the
   inference.

### The lever, MEASURED on Snow (600 games/arm, train seeds 920000-920599, one pooled queue)

Two sweeps, same seeds and chunking both times (`ARMS_SPEC` exists so a second measurement cannot
drift onto a different set). `chunks!=base` counts chunks whose MEAN win turn differs from base at
all -- a count of chunks containing at least one changed game.

| arm | units | Δ | cand_scored | Δ | avg turns | Δ | chunks ≠ base |
|---|---|---|---|---|---|---|---|
| base | 63,423,525 | — | 22,368,466 | — | 6.0433 | — | 0 / 8 |
| `MTG_BOTTOM_EVAL_UNITS=2700` | 50,178,154 | −20.88% | 17,918,952 | −19.89% | 6.0433 | 0 | **0 / 8** |
| `MTG_BOTTOM_EVAL_UNITS=900` | 44,552,344 | **−29.75%** | 15,850,518 | −29.14% | 6.0433 | 0 | **0 / 8** |
| `MTG_BOTTOM_EVAL_UNITS=450` | 41,654,560 | −34.32% | 14,656,966 | −34.47% | 6.0450 | +0.0017 | 1 / 8 |
| `MTG_BOTTOM_EVAL_UNITS=225` | 38,552,588 | −39.21% | 13,403,193 | −40.08% | 6.0450 | +0.0017 | 1 / 8 |
| `MTG_BOTTOM_EVAL_UNITS=90` | 35,206,027 | −44.49% | 12,010,748 | −46.30% | 6.0467 | +0.0033 | 2 / 8 |
| `MTG_BOTTOM_EVAL_UNITS=45` | 33,113,292 | −47.79% | 11,141,624 | −50.19% | 6.0467 | +0.0033 | 4 / 8 |
| `MTG_BOTTOM_EVAL_DEPTH=0` | 29,362,059 | −53.70% | 9,642,553 | −56.89% | 6.0516 | +0.0083 | 4 / 8 |
| `MTG_BOTTOM_EVAL_DEPTH=0 TOPK=5` | 52,277,972 | −17.57% | 18,122,337 | −18.98% | 6.0433 | 0 | 2 / 8 |

**900 units (1 virtual ms, against the cell's 9,000) is the knee**: the cheapest setting at which no
chunk mean moves. Below it play starts to move and moves the WRONG way (+0.0017 = one game slower).
Above it the saving is simply smaller. `DEPTH=0` buys nearly twice as much and costs +0.0083 turns --
a trade, not a free win, and the opposite verdict to Melira's, where depth 0 was turn-identical.

**`bd0k5` is the reason this table reports chunk counts and not just the pooled mean.** Its pooled
average is bit-identical to base (6.0433) while **two chunks differ** -- individual games moved and
the means cancelled. A pooled mean cannot distinguish "nothing changed" from "two changes in
opposite directions", which is exactly the claim an adoption rests on.

### EXACT per-game check: 900 units is NOT byte-identical — 1 game of 600 moves, same win turn

`chunks!=base = 0` is strong but not a proof: two games swapping turns inside one chunk leave its
mean bit-identical. `scripts/wins_ab.sh` settles it by running one pooled batch twice with
`--game-log-dir` and diffing the per-game logs (the same artifact `test/gt_logs/<key>.wins` holds):

```
per-game win logs: 7 identical, 1 differ; approx 1 game(s) moved
  wab_d3_s920225.wins line 29:   28 7 8d351384e51847aa   ->   28 7 660e8eba0243e808
```

**The win TURN is the same (7); only the play DIGEST differs.** So `bottom_eval_units: 900` on Snow
is a −29.75% work reduction whose entire measured behavioural footprint is one game of 600 taking a
different route to the same turn-7 win. That is a GT rebaseline, not an inert change -- small, but it
must be declared as one rather than described as byte-identical (`fresh-full-was-not-a-clean-win`:
clean means no regression on ANY axis vs SHIPPED, and the axis here is the digest).

Held-out validation is still owed: every number above is on train seeds 920000+.
