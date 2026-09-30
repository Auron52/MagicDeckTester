# Retaining rollouts across a play-identity change (mixed-provenance generation)

**Status: DESIGN, not built.** Requested by the user 2026-09-30, after the Fungus candidate-B
mulligan generation was cancelled at 90.8 h with **12.56 M sub-table + 10.16 M size-7 rollouts** and
63.7% of cell-sides frozen, on an engine whose play digest has since moved (`36a65944fd138cd0` →
`4b55aac85d0e0b77`).

> *"I think it is a waste to fully throw away our results from the previously generated full rollouts.
> I think I would like to allow a way to retain full rollouts that were generated that have similar
> results when given user permission. So we would end up with a mixed run, like 15 R on this build and
> the rest from the other. Reproducing this exactly is not crucial for me… though we could keep track
> of how it was generated."*

## 1. What currently happens, and why it is right by default

`PlayIdentityAllows` (`ExhaustiveKeep.cpp:221`) refuses a resume whose `play_digest` differs, with:

> *"its rollouts are not this run's rollouts; resuming would pool two engines into one sidecar"*

That refusal exists because resume was once the **one reuse path with no such check**, and a gen
restarted after a play-logic change silently continued into the same accumulators — producing a raw
sidecar holding two engines' rollouts under fingerprints asserting they were poolable. Every other
reuse route (prior-raw, probe-carry, equiv-cache, merge) gates this. **This design must not remove
that default.** It adds a deliberate, narrow, recorded override.

Note the gate is already tolerant in the right way: it prefers the `play_digest` over `commit`
precisely so a docs/scheduling/instrumentation commit cannot strand a multi-day journal. What it
cannot currently express is *"the play changed, and I have checked that it does not matter here."*

## 2. The statistics this rests on, stated plainly

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
That makes §4 the load-bearing part of this design, not §3.

## 3. Mechanism

### 3a. Permission must name the digest

```
MTG_KEEP_RETAIN_FOREIGN=36a65944fd138cd0        # the digest being admitted, never "=1"
```

Naming the foreign digest explicitly is the point: a blanket boolean would be settable by habit and
would survive into unrelated runs, which is how the original hole behaved. An unset or mismatched
value keeps today's refusal exactly. This is a **USER decision**, like `MTG_ALLOW_UNTESTED_DECK` — an
agent that wants it reports and stops.

### 3b. Provenance is recorded per record, not per run

Add a provenance table to the journal/raw header and an index on each record:

```json
{"meta":{ ...,
  "provenance":[ {"id":0,"play_digest":"36a65944fd138cd0","commit":"57c36b5c","rollouts":22719704},
                 {"id":1,"play_digest":"4b55aac85d0e0b77","commit":"<new>","rollouts":0} ]}}
{"H":7,"i":294127,"p":0,"s":41,"q":247,"n":7,"f":1,"g":0}      <- g = provenance id
```

A record predating the field reads as `g=0` (the retained engine), which is correct for exactly the
case this feature serves. The finished `.profile.json` then carries the same block, so **any later
consumer can see the artifact is mixed** — the merge gate, an A/B, or a human. A mixed profile must
never present itself as single-engine; that is the failure this whole area keeps producing.

Where a cell-side ends up with samples from both engines, its `n` splits per provenance
(`n_by_prov`), because a single pooled `n` would hide the ratio that §2 says governs the bias.

### 3c. What may be retained

Only **completed** cell-sides (`f=1`, frozen or capped) and completed sub-table cells. A partially
sampled cell-side is worth little and is where mixing is least defensible — the new engine should
simply re-roll it. This also keeps the common case simple: the 63.7% already frozen is the prize.

## 4. The similarity test — the part that actually needs deciding

"Similar results" has to be operational, and there is a cheap, honest version: **re-roll a stratified
sample of retained cell-sides on the new engine and compare.**

1. Sample ~300–500 cell-sides, stratified across hand size, `pd`, and the retained mean win turn
   (degenerate cells must be represented — they are where engines diverge most).
2. Roll each to the same `R` the retained record used.
3. Report, per stratum and overall:
   * mean win-turn delta, with a paired CI;
   * the **fraction of cell-sides whose keep/bottom DECISION flips**, which is what the table is for
     and matters far more than the mean;
   * the worst per-cell delta.
4. Accept only on a stated threshold.

**The decision-flip rate is the right metric, not the mean.** A keep table is an argmax over
sub-compositions; a uniform +0.05-turn shift changes nothing, while a handful of sign flips near
ties changes the shipped policy. `leaf-eval` on this deck already reports **67.8% of leaf
evaluations as ties**, so this table is unusually tie-dense and therefore unusually sensitive to
small shifts — which argues for a *tighter* flip threshold here than one might pick generically.

**The threshold is the user's call and I am not choosing it for them.** A defensible default to argue
from: accept if the paired mean delta CI is within ±0.05 turns **and** the decision-flip rate is
under 1%; disclose and require explicit re-confirmation between 1% and 5%; refuse above 5%. The
sample itself costs ~500 rollouts — minutes, against the ~50 h the retention saves.

## 5. Interaction with the cost work, because it changes the calculus

The user also noted: *"if we get lucky some of the optimizations may apply there (without breaking
things)."* Two consequences, and they pull in opposite directions:

- A **pure scheduling or instrumentation** optimisation leaves the play digest untouched, so it needs
  none of this — the existing gate already permits it. That is the lucky case and it is worth
  checking for before reaching for this feature.
- Any optimisation that **narrows the search** (collapsing option groups, bounding the odometer,
  pruning fungible subsets) changes play by construction and therefore moves the digest. Those are
  exactly the candidates in `fungus-slow-rollout-diagnosis-2026-09-30.md` §2a. So the faster the
  engine gets, the more this feature is needed — **and the more likely the similarity test is to
  fail**, because a narrowing that changes nothing measurable is also a narrowing that bought nothing.

That tension is worth stating up front: **retention and search-narrowing are in conflict, and the
similarity test is where that conflict gets adjudicated.** If a narrowing passes the flip test, it was
safe *and* the old data is retainable; if it fails, we learn the narrowing was not free — which is
information we want either way. The test is therefore useful even when it refuses.

## 6. What this does NOT do

- It does not make the run reproducible. The user has explicitly accepted that
  (*"reproducing this exactly is not crucial"*), and §3b records the provenance so the artifact is at
  least **honest** about it. A mixed profile should be re-derivable in outline, never byte-for-byte.
- It does not license cross-DECK or cross-`RolloutCfg` pooling. `RolloutCfgAllows`
  (depth/budget/max_turns) stays a hard refusal: those change what a rollout *means*, not just how it
  plays.
- It does not change the equivalence-discovery cache gate. A bucket structure from a different engine
  is not a sampling question but a correctness one — the cell indices themselves would not correspond.

## 7. Status of the prize being protected

`logs/fungus_journal_backup/` holds the cancelled run's journal (350 MB, 5,583,350 records),
gencache and slow.log, verified byte-identical to what the run left behind. The engine that produced
it is pinned by branch `gen/fungus-candb-mulligan-2026-09-27` @ `57c36b5c` plus
`logs/Fungus_candidate-b-2026-09_mullgen/mtg-analyze.frozen` (md5 `bb389d836c…`) and
`cards.json.frozen` (md5 `2c4252fe…`). So **both routes remain open**: finish it as-is on the frozen
engine, or retain it into a new run via this design. Nothing needs deciding to keep both available.
