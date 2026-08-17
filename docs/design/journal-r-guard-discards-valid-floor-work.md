# The journal's R guard discards floor work that is byte-identical

**Status:** deferred, diagnosed, not implemented. Applies to the journal replay guard in
`src/analyzer/ExhaustiveKeep.cpp` (~line 2185). Raised by the user, 2026-08-16.

> "This shouldn't be a thing. The first few cells should be identical between the two." — user, on
> the claim that a `recommend`(R40) -> `fast`(R30) hand-off would discard the floor pass.

The user is right, and the file says so two lines above the guard.

## The contradiction, in one file

`ExhaustiveKeep.cpp:2155` states the seeding contract:

> rollouts are seeded from `(seed_base, r, w, pd)` and the accumulators use `+=` only, so a reloaded
> cell holds byte-identical samples to an uninterrupted run

**R is not in the seed tuple.** Rollout `r` of a given cell is therefore the same rollout whether the
cap is 30 or 40 -- the cap only decides how many of them a cell is allowed to receive.

`ExhaustiveKeep.cpp:2185` nonetheless requires it to match:

```cpp
matched = m.value("bucket_fp", 0ULL) == jfp.bucket
       && m.value("deck_fp",   0ULL) == jfp.deck
       && m.value("seed_base", ~0ULL) == cfg.seed
       && m.value("K", -1) == K
       && m.value("max_mull", -1) == cfg.max_mull
       && m.value("equiv_seed", 0ULL) == cfg.equiv_seed
       && m.value("R", -1LL) == static_cast<long long>(cfg.rollouts)   // <-- this one
       && RolloutCfgAllows(...);
```

A mismatch prints `RESUME(journal): ... fingerprint MISMATCH -- ignoring` and starts fresh.

## Why R does not belong in that list

The guard's stated justification is *"else the partial indexes a different table"*. That is a SHAPE
argument, and it is correct for every other member:

| field | changes the table's shape/indexing? |
|---|---|
| `bucket_fp`, `deck_fp` | yes -- different buckets/deck => different cells |
| `K` | yes -- `comps` is indexed by bucket composition |
| `max_mull` | yes -- different hand sizes in the table |
| `seed_base`, `equiv_seed` | yes -- different rollout stream / different bucketing |
| **`R`** | **no -- it is a cap on `cnt`, nothing else** |

R got lumped in with genuine shape guards without a shape argument to justify it.

## The legitimate concern, and the smaller fix that keeps it

There IS one real property at stake: byte-identity of the finished profile. Loading a cell recorded
at `n=40` into an R=30 run gives that cell more samples than an uninterrupted R30 run would have
produced, and the journal stores `(sum, sumsq, cnt)` AGGREGATES, so `n=40` cannot be truncated back
to 30 after the fact.

That argues for a per-entry filter, not a whole-journal veto:

* drop `R` from the header match;
* when replaying, skip any entry with `n > cfg.rollouts` (the replay loop already has the sibling
  test `if (n <= t.cnt[i][pd]) { continue; }`, so this is one more condition in the same place);
* keep `RolloutCfgAllows` as-is -- depth/budget genuinely do make it a different run.

Byte-identity is preserved: every loaded cell has `n <= R`, its samples are the `r = 0..n-1` prefix
of the same seeded stream this run would draw, and the remaining rollouts continue from `r = n`.

## Why it matters in practice

The case it costs is exactly the documented hand-off. A `recommend` floor pass writes cells at
`n=1-2` (`floor R=1`, or `R=2` under `complete`), so **every** journal entry is far below any cap
that would follow it -- all of it is loadable, and today all of it is thrown away the moment the
recipe changes R. On Mirrorwing post-mana-fix that floor pass is ~32 minutes of 32 cores.

It also removes a trap from the recipe hand-off the mulligan-profile skill documents
(`recommend -> complete` keeps R40 and resumes; `recommend -> fast` silently does not), where the
difference is invisible except as a one-line warning in a multi-hour log.

## Related

Same family as the equivalence-cache and prior-carry fingerprints: a guard that is correct in
intent, over-broad in scope, and expensive precisely when the pipeline hands one stage to the next.
Compare [[edge-table-pairwise-mutual-completion]], where a global filter likewise charges every
comparison for a constraint only some of them need.
