# Retaining rollouts across a play-identity change (mixed-provenance generation)

**Status: BUILT (2026-09-30).** The escape hatch and its similarity test are implemented in
`src/analyzer/ExhaustiveKeep.cpp`. Requested by the user after the Fungus candidate-B mulligan
generation was cancelled at 90.8 h with 63.7% of cell-sides frozen, on an engine whose play digest has
since moved (`36a65944fd138cd0` → `4b55aac85d0e0b77`).

> *"Fungus is a good example of a case where it feels like a real waste to throw away. It's been
> running for days and has done a significant amount of work. Even if we optimize without this rule we
> likely still have to pay a major cost. It is necessary for us to have an escape hatch so we can retain
> most of that work even if the rest is optimized."*

## 1. What currently happens, and why it is right by default

`PlayIdentityAllows` (`ExhaustiveKeep.cpp:221`) refuses a resume whose `play_digest` differs, with:

> *"its rollouts are not this run's rollouts; resuming would pool two engines into one sidecar"*

That refusal exists because resume was once the **one reuse path with no such check**, and a gen
restarted after a play-logic change silently continued into the same accumulators — producing a raw
sidecar holding two engines' rollouts under fingerprints asserting they were poolable. Every other
reuse route (prior-raw, probe-carry, equiv-cache, merge) gates this. **The default is unchanged.** What
is added is a deliberate, narrow, recorded override.

Note the gate is already tolerant in the right way: it prefers the `play_digest` over `commit`
precisely so a docs/scheduling/instrumentation commit cannot strand a multi-day journal. What it
could not express is *"the play changed, and I have checked that it does not matter here."*

## 2. The prize, measured

Measured by running the hatch against the real journal (`logs/fungus_journal_backup/`, 350 MB,
5,583,349 records), not estimated:

```
[keepgen] RETAINED (MIXED PROVENANCE): 1530576 completed cell-sides / 16328781 rollouts
          from play 36a65944fd138cd0 (commit 57c36b5c); skipped 3252991 partial records
```

| | cell-sides | rollouts |
|---|---|---|
| size-7, terminal (`f=1`) | 970,364 | 3,768,929 |
| sub-table, at `sub_target` | 560,212 | 12,559,852 |
| **retained** | **1,530,576** | **16,328,781** |
| the run's own final counters (`roll7` + `rollsub`) | | 22,724,224 |
| **retained share** | | **71.9%** |

**The hatch protects ~72% of a 90.8-hour run.** Two things this measurement corrected in a first
hand-estimate, both worth keeping because they are easy to get wrong the same way again:

* **Size-7 is 3.77 M rollouts retained, not 5.25 M.** Taking `max(n)` per cell-side over-counts,
  because a terminal record can *lower* a cell's count — `compute_refs`' reconcile truncates a
  speculated cell back to its freeze point. The engine applies the terminal record's `n`, which is the
  right number; a max-n scan is not.
* **Sub-tables retain at the ADAPTIVE FLOOR, not at the cap.** This run has `bottoming: ADAPTIVE`, so
  `sub_target` is `r0 = 2`, and *every* sub-cell qualifies as "completed" — not just the 60.6% that
  reached the cap. That is consistent with what the run would itself have shipped, but "completed"
  must not be read as "well sampled". The actual depth distribution is three-level:

  | sub-cell depth | count | share |
  |---|---|---|
  | 2 (the adaptive floor) | 99,698 | 17.8% |
  | 18 | 121,247 | 21.6% |
  | 30 (cap) | 339,267 | 60.6% |

  So 82.2% of retained sub-cells carry >= 18 rollouts, and the thin 17.8% at the floor are precisely
  the ones `best_sub` already **excludes from the bottoming argmin** (that is the point of the adaptive
  design) — so retaining them is low-risk, and also low-value.

The 28% not retained is partial cell-sides, which the new engine re-rolls (§4c).

## 3. The statistics this rests on, stated plainly

A journal record is an accumulator, not a rollout list:

```json
{"H":7,"i":294127,"p":0,"s":41,"q":247,"n":7,"f":1,"fs":11,"fq":61,"fn":2}
```

`s`/`q`/`n` are sum, sum-of-squares and count of win turns for cell-side (`H`,`i`,`p`). Pooling two
engines' samples is therefore just adding sums — arithmetically trivial, and **valid only if both are
sampling the same distribution.** If they are not, the pooled mean is biased by

```
bias  =  (n_old / (n_old + n_new)) x (mean_old - mean_new)
```

With the user's example (15 R new, ~30 R retained) the OLD data carries **two-thirds of the weight**,
so a small per-cell divergence is not diluted — it is inherited. **This is the crux: the retention is
only as good as the similarity test, and the mixing ratio decides how much the test has to prove.**
That makes §5 the load-bearing part of this design, not §4.

## 4. What was built

### 4a. Permission must name the digest

```
MTG_KEEP_RETAIN_FOREIGN=36a65944fd138cd0        # the digest being admitted, never "=1"
```

Naming the foreign digest explicitly is the point: a blanket boolean would be settable by habit and
would survive into unrelated runs, which is how the original hole behaved. It also **expires on its
own** — the next engine's digest is a different string, so a stale export cannot silently admit it.
An unset or mismatched value keeps today's refusal exactly, and the refusal now prints the digest a
user would have to name, plus a distinct message when the variable is set but names something else.
This is a **USER decision**, like `MTG_ALLOW_UNTESTED_DECK` — an agent that wants it reports and stops.

Value-carrying flag, so it keeps a raw `getenv` + parse per `coding-conventions.md` rule 3; `=0` is
treated as unset, matching the repo's `=0`-means-off convention.

### 4b. Retention is JOURNAL-ONLY

The raw-snapshot resume path still refuses. A raw records a per-cell count but **no terminal flag**, so
"completed cell-side" — the restriction that makes mixing defensible at all — is not expressible from
it. The journal is also where the prize actually is.

### 4c. Only COMPLETED cell-sides are taken

* size-7: requires the terminal record (`f=1`, frozen or capped).
* sub-tables: they never carry `f=1`, so completion is `n >= sub_target`, using the same derivation
  `WriteRawSidecar` stamps. Without that, half-sampled bottoming cells would be retained — and a
  half-sampled sub-cell is exactly where `DecideBottom`'s argmin is noise.

A partially-sampled cell carries few rollouts and is where pooling two engines is least defensible, so
the new engine re-rolls it from zero. Records the *resuming* engine itself wrote are exempt, or a mixed
run could never make partial progress of its own.

### 4d. Provenance is recorded per record

The journal header gains a `provenance` table; a retained resume appends a `prov` record declaring its
own id (rather than rewriting line 1, which would break the append-only durability the journal's whole
design rests on), and every record it writes carries `g`:

```json
{"meta":{ ..., "provenance":[{"id":0,"play_digest":"36a65944fd138cd0","commit":"57c36b5c"}]}}
{"prov":1,"play_digest":"4b55aac85d0e0b77","commit":"<new>","retained_from":"36a65944fd138cd0"}
{"H":7,"i":294127,"p":0,"s":41,"q":247,"n":7,"f":1,"g":1}
```

A record predating the field reads as `g=0` (the retained engine), which is correct for exactly the
case this serves, so **every journal already on disk stays readable** and an ordinary run is
byte-identical to before (the `g` field is only emitted when non-zero).

Both output artifacts then declare the mix: `ExhaustiveKeepPolicy::provenance` in the profile JSON and
`meta.provenance` in the raw. A mixed profile must never present itself as single-engine; that is the
failure this area keeps producing. The provenance string names both engines and the split, and the
report prints a MIXED PROVENANCE block when one is written.

Two deliberate limits, both recorded at the code:

* **The binary keeptable cache carries no copy.** It is derived from the JSON and is consulted only at
  decision time, where provenance is explicitly unused — so no format bump. Audit the JSON sidecar.
* **`RunKeepMerge` is unchanged** and still gates on `play_digest`. Pooling a mixed chunk *further* is
  not automated; the raw's `provenance` is there to be read before anyone does it by hand. (Follow-up.)

A journal that is already mixed refuses a *third* engine, and does so before applying any record, so
the refusal cannot half-load a table.

### 4e. A foreign influence that is NOT a retained cell value — the refs record

Worth stating plainly because it is easy to miss: the journal's one-time `refs` record (the fixed
`Dopt` and `vg` shrink targets) is replayed **unchanged**, so a retained resume makes freeze decisions
about *new-engine* cells against targets the *foreign* engine derived. That is deliberate — refs are a
shrink target, and recomputing them from a table that is 72% foreign values would be foreign-derived
anyway, so reusing them is roughly equivalent and keeps freeze behaviour continuous across the resume.
But it means the foreign engine's influence is slightly wider than "the cell-sides we retained", and a
similarity test that comes back marginal should be read with that in mind.

## 5. The similarity test — `MTG_KEEP_RETAIN_VERIFY=<n>`

**Correction to the original design.** It proposed re-rolling an independent sample and reporting a
paired CI. Pairing can be made exact instead, and that is strictly better at the same cost: a rollout
is a pure function of `(seed_base, r, w, pd)`, so re-rolling a retained cell-side **over the same
rollout indices** compares the two engines on *identical library permutations*. Sampling noise cancels
exactly, and any residual delta **is** the engine difference. That is what shipped.

What it does:

1. Pools every retained cell-side, sorts by `(H, pd, retained mean)` and takes a **systematic sample**
   across that order — an even sweep that hits every stratum in proportion, including the degenerate
   late-win tail where engines diverge most. Deterministic, so the same journal always verifies on the
   same sample and re-running cannot shop for a friendlier one.
2. Re-rolls each on the current engine, in parallel, with the generation's own `AIEngine`
   construction — the same rollout the gen would do, not an approximation.
3. Reports the paired mean delta with its CI, how many cell-sides come back **byte-identical**, the
   worst single cell-side, and the **decision-flip rate**.
4. Writes nothing and returns before the journal is opened, so a verify cannot touch the artifact it is
   judging.

**The flip rate is computed exactly, not approximated.** `BuildPolicyFromTables` is a pure function of
the tables, so the check builds the policy on the retained values, substitutes the re-rolled values,
builds again, and diffs the `keep` flags and `bottom_keep` targets. Propagation comes for free: a
size-7 cell feeds the mulligan threshold, a sub-cell feeds many hands' argmin.

**The flip rate is the right metric, not the mean.** A keep table is an argmin over subcompositions; a
uniform +0.05-turn shift changes nothing, while a handful of sign flips near ties changes the shipped
policy. This deck reports **67.8% of leaf evaluations as ties**, so its table is unusually tie-dense
and therefore unusually sensitive to small shifts — which argues for a *tighter* flip threshold here
than one would pick generically.

**The threshold is the user's call and the tool does not choose it.** The default it prints to argue
from: accept if the paired mean CI is within ±0.05 turns **and** keep-flip is under 1%; disclose and
require explicit re-confirmation between 1% and 5%; refuse above 5%.

One honest limit, printed in the report: flips are measured with **only the sampled cell-sides
re-rolled**, so the figure is the flip rate attributable to the sample, not an extrapolation to the
whole table.

### First run against the real journal (2026-09-30)

End-to-end on the cancelled Fungus journal, frozen `57c36b5c` retained into HEAD:

```
retained        : 1530576 cell-sides / 16328781 rollouts
sample          : 24 cell-sides / 236 rollouts, PAIRED on identical seeds
mean delta      : +0.0139 turns (se 0.0139, 95% CI +/- 0.0272)
byte-identical  : 23/24 cell-sides (95.8%)
worst cell-side : +0.333 turns (H=7 pd=0 idx=346045)
keep flags      : 0 / 10,654,672 = 0%
bottoming target: 0 / 10,654,672 = 0%
```

**Read that as a working demonstration, not as a verdict.** At n=24 the test is under-powered in a
specific way: only 24 of 1,530,576 cell-sides were perturbed, so most of the table had no opportunity
to flip. A 0% flip rate over 10.7 M decision slots sounds decisive and is not — the denominator is
large because the table is large, not because the test was thorough. The default bar in this file was
written for a 300–500 cell sample and should be judged at that size.

**And the definitive verify should be run against the OPTIMISED engine, not this one.** The digest will
move again when the search narrowing lands, so a large verify today measures a gap we are about to
replace. What today's run establishes is that the mechanism works, retains what it claims, writes
nothing, and reports the right metric.

Two properties observed while running it, both worth knowing before sizing a real verify:

* **The journal replay is the fixed cost** — 5.58 M records, ~1 minute, paid before any re-rolling.
* **The re-roll is tail-bound at small n.** It parallelises per cell-side, so with 24 samples on a
  24-core box the makespan is the single slowest cell-side; the run sat at ~1.5 cores for most of its
  9 minutes waiting on one straggler. Size the sample well above the core count or accept a long tail.

### Cost warning

The verify's cost is **unbounded by the same tail it is helping to fix** — this deck has single
rollouts measured at 7 h 08 m, and the stratification deliberately includes degenerate cells. Start
small (tens of cell-sides), read the progress lines, and scale up. It is a diagnostic you launched, so
it is yours to kill.

## 6. Which branch to optimise on

The user also asked: *"It is arguable whether we should perhaps optimize on the branch to leave out
work from other agents."*

The case for it is real: the retained journal's engine **is** `gen/fungus-candb-mulligan-2026-09-27` @
`57c36b5c`, so optimising there would make our narrowing the *only* digest delta, and any flip
unambiguously ours.

**Recommendation: optimise on the main line anyway.** Three reasons, in order of weight:

1. **The gen branch has no trustworthy correctness gate.** Two commits since the freeze rebaselined
   ground truth on all three tiers (`6b9f0cb2`, `88dd58db`). On that branch the regression suite would
   diff against a GT no current binary produces, so it would be red for reasons unrelated to us —
   exactly when we are making search-narrowing changes, the class of change that most needs a working
   gate. The user's constraint was *"(without breaking things)"*, and optimising without a regression
   gate is the opposite of that.
2. **Attribution no longer requires isolation, because the foreign contribution is measurable
   separately** (§7). Isolation was the only way to get attribution; it is not any more.
3. **The divergence must be crossed exactly once, and later is worse.** 35 engine commits / 8,756
   inserted lines already separate the two trees. Deferring that rebase makes it larger, and
   CLAUDE.md requires a rebuild, a byte-identity re-check and `check_gt_logs.py` after any rebase
   that replays engine changes. Two of those commits are also *perf* work
   (`8843b134`, `9ed41927` `MTG_BOTTOM_NAME_DEDUPE` default ON) that a branch would forgo.

The gen branch stays frozen as the **finish-as-is** escape route, which it already serves without
carrying any new work. Both routes remain open and nothing needs deciding to keep them so.

## 7. How much did the OTHER agents' work actually change this deck's play?

This is answerable without running a generation, because the equivalence-discovery cache stores a
`signature` per bucket class — a vector of probe win-turns, 400 per class, 8,800 in total — measured
identically by both engines. Diffing two caches is a free, stratified play-divergence measurement.

**Caveat that must travel with the number:** the signature is measured at *discovery* settings
(depth 5 / budget 20 ms), not at the generation's (depth 1 / budget 3 ms), and it is a win-turn
agreement rate on probe hands, not the keep/bottom flip rate. It is a strong proxy and a cheap one, not
a substitute for §5.

**A confound checked and cleared.** `scripts/mullgen.sh` runs generation with
`MTG_DECISION_WORK_X=1000`, while the engine default is `0` (disarmed). A first pass compared the
frozen cache against a HEAD cache built *without* that setting, conflating the 35 commits with the
work-ceiling change. Re-measured with both sides at `X=1000`. The ceiling turns out to be almost
perfectly inert on this deck — **1 of 8,800 probe entries** (0.01%), and the d1/b3 play digest is
`4b55aac85d0e0b77` either way — so the first pass was not materially confounded after all. Worth
having checked; not worth a retraction.

### The number

Frozen `57c36b5c` vs HEAD, **both at `MTG_DECISION_WORK_X=1000`**:

| | |
|---|---|
| probe entries compared | 8,800 (400 per class x 22) |
| entries that differ | **149 (1.69%)** |
| mean delta over all entries | **−0.0017 turns** (HEAD marginally faster) |
| delta histogram | −2: 2, −1: 81, +1: 62, +2: 4 |
| worst class | `Psychotrope Thallid` 5.8%, then `Brightcap Badger` 3.5%, `Peat Bog` / `Mycoloth` 2.5% |

**35 engine commits and 8,756 inserted lines of other agents' work move this deck's play on 1.7% of
probe rollouts, with a mean effect under two thousandths of a turn, and the divergence is nearly
symmetric** (81 down, 62 up, almost all ±1). Whatever those commits did, they did not do it to Fungus.

That is the fact behind §6 reason 2: isolating on the gen branch would buy attribution against a
baseline that is already close to zero. It also says retention across this particular gap is likely to
pass the real test in §5 — *likely*, not proven, because the signature is a d5/b20 win-turn agreement
rate and the thing that matters is the d1/b3 keep-flip rate.

One aside worth noting: `Psychotrope Thallid` is both the worst-diverging class here and the card the
slow-tail analysis found to be a **passenger** (lift 0.90x, not a driver of cost). Divergence and cost
are landing on the same card for different reasons.

## 8. Interaction with the cost work, because it changes the calculus

- A **pure scheduling or instrumentation** optimisation leaves the play digest untouched, so it needs
  none of this — the existing gate already permits it. That is the lucky case and it is worth checking
  for before reaching for this feature.
- Any optimisation that **narrows the search** (collapsing option groups, bounding the odometer,
  pruning fungible subsets) changes play by construction and therefore moves the digest. Those are
  exactly the candidates in `fungus-slow-rollout-diagnosis-2026-09-30.md` §2a. So the faster the
  engine gets, the more this feature is needed — **and the more likely the similarity test is to
  fail**, because a narrowing that changes nothing measurable is also a narrowing that bought nothing.

That tension is worth stating up front: **retention and search-narrowing are in conflict, and the
similarity test is where that conflict gets adjudicated.** If a narrowing passes the flip test, it was
safe *and* the old data is retainable; if it fails, we learn the narrowing was not free — which is
information we want either way. The test is therefore useful even when it refuses.

## 9. What this does NOT do

- It does not make the run reproducible. The user explicitly accepted that (*"reproducing this exactly
  is not crucial"*), and §4d records the provenance so the artifact is at least **honest** about it.
- It does not license cross-DECK or cross-`RolloutCfg` pooling. `RolloutCfgAllows`
  (depth/budget/max_turns) stays a hard refusal: those change what a rollout *means*, not just how it
  plays.
- It does not change the equivalence-discovery cache gate. A bucket structure from a different engine
  would be a correctness problem, not a sampling one — the cell indices would not correspond. **For
  Fungus this was checked and is fine:** all 22 classes are singletons with identical membership at
  both engines, so `bucket_fp` and `deck_fp` both match and the indices do correspond. Retention here
  really is only a sampling question.
- It does not teach `RunKeepMerge` about mixed chunks (§4d).

## 10. Status of the prize being protected

`logs/fungus_journal_backup/` holds the cancelled run's journal (350 MB, 5,583,349 records), gencache
and slow.log, verified byte-identical to what the run left behind — and re-verified against the live
copies in the deck directory. The engine that produced it is pinned by branch
`gen/fungus-candb-mulligan-2026-09-27` @ `57c36b5c` plus
`logs/Fungus_candidate-b-2026-09_mullgen/mtg-analyze.frozen` (md5 `bb389d836c…`) and
`cards.json.frozen` (md5 `2c4252fe…`).

The settings the journal requires for any resume, from its own header — a resume that misses one is
refused, and `MTG_DECISION_WORK_X=1000` is the easy one to forget because it lives in `mullgen.sh`,
not in the header:

```
commit 57c36b5c   play_digest 36a65944fd138cd0
depth 1   budget_ms 3   max_turns 8   R 30   max_mull 6
seed_base 1000000   equiv_seed 20260701   start_life 20   opp_heads 1
MTG_DECISION_WORK_X=1000        (set by scripts/mullgen.sh, engine default is 0)
```

Note the campaign ran seven `mtg-analyze` invocations across three different d1/b3 digests
(`f1f8288da6dff73e`, `e0ffdb608cd70b25`, `36a65944fd138cd0`); the journal header is the authoritative
one for this artifact.
