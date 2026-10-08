# The play-digest resume gate cannot see a heuristic whose denominator is the ROLLOUT

**Status: FOUND 2026-10-08, worked around by hand, NOT fixed in tooling.** Found the hard way: a
keep-generation run resumed a journal it should have refused, and would have shipped a table whose
cells were labelled under two different discard rules.

## What happened

The kitty cleanup-discard buckets were corrected mid-generation
(`docs/design/kitty-discard-buckets-are-stale-for-v3.md`, `MTG_KE_BUCKET_V3`). The engine was
rebuilt, `mullgen.sh run` was relaunched, and both protective gates **passed and let the old work
through**:

```
[preflight] banked journal: R=30, K=15, depth=1, budget_ms=3, max_mull=6, play_digest=ef59301a8b0f85e1
[preflight] fields checkable without discovery MATCH recipe `fast` -- the journal can be resumed
[keepgen] equivalence discovery: CACHE HIT (15 buckets from ...gencache.json) -- skipped
[keepgen] RESUME(journal): reloaded 10988 cell-sides from ...raw.json.journal -> continuing
```

Both gates key on **`play_digest`**, and the play digest **did not change** — so by their test this
was the same engine.

## Why the gate was wrong without being buggy

The play digest is a battery of **64 games of ordinary play**. The cleanup discard fires about
**once per 120 games in play** — so the battery contains roughly *half a firing*. The digest
genuinely could not see the change.

But the workload that *builds the table* is not ordinary play. The rule's own comment
(`DecisionProviders.cpp`) states its real denominator:

> Its denominator is the ROLLOUT — 425/game normal, 591/game forced-keep — **and above all the
> mulligan generator**, where a land-light hand cannot deploy and so crosses seven cards every turn.

So the ratio between the two workloads is roughly **three orders of magnitude**. A change invisible
to the gate is loud in the thing the gate is protecting.

**The failure mode is silent and it corrupts the artifact, not the run:** 10,988 cell-sides (20.6% of
size-7) carried labels produced under the old rule, and the remaining ~79% would have been produced
under the new one. Nothing downstream would flag that: the table validates, the A/Bs run, the
`bottoming_enabled` check passes, and the keep policy is simply fitted to a rule that was never
consistently in force.

## The general shape, so this is recognisable next time

> **A resume/reuse gate that fingerprints PLAY is unsound for any heuristic whose firing rate in
> play is far below its firing rate in the generator.**

Everything with that shape is affected, not just this rule. The marker is a provider hook that
ordinary play reaches rarely but a trial-game-heavy generator reaches constantly — cleanup discard is
the clearest case (it needs hand > 7 at cleanup, which deployment normally prevents and a land-light
mulligan trial guarantees).

This is the same lesson as [digest equality can mean BROKEN], one level up: there, equal digests
masked a change that emitted zero variants; here, an equal digest masks a change the digest's own
sample cannot reach. **Digest equality is evidence about the sampled workload only.**

## The hand workaround that was used

Rename the journal, its rolling `.bak1`/`.bak2`, **and the discovery `gencache.json`** aside (never
delete — a journal is banked compute), then relaunch so every cell is computed under one rule:

```
mv <raw>.journal        <raw>.journal.PRE-<change>-<ts>
mv <raw>.journal.bak1   <raw>.journal.PRE-<change>-<ts>.bak1
mv <raw>.journal.bak2   <raw>.journal.PRE-<change>-<ts>.bak2
mv <stem>.keepmodel.gencache.json <stem>.keepmodel.gencache.PRE-<change>-<ts>.json
```

The `gencache` matters as much as the journal and is the easier one to forget: equivalence discovery
is itself measured by probe rollouts, which hit the same hook, so a CACHE HIT carries buckets
discovered under the old rule into the new run.

## The fix worth building (not built)

**There is already a hook for it, and it is empty.** The gencache's `meta` block carries a
`policy_fp` field (and a `commit` field), both of which were **blank** on this run:

```
meta.play_digest: ef59301a8b0f85e1
meta.policy_fp:                      <- empty
meta.commit:                         <- empty
```

Populating `policy_fp` with a fingerprint of the **active provider-heuristic configuration** — the
`heurarm` flag vector plus the resolved provider name — would make both gates reject automatically on
exactly this class of change, with no play-digest sensitivity required. It is strictly a *refusal*
fingerprint, so a false mismatch costs a regeneration and never a wrong artifact; the current failure
costs a wrong artifact silently, which is the worse direction.

A weaker alternative — widening the play-digest battery until it reaches rare hooks — does not work:
the battery would have to grow by ~1000x to sample this rule even a few times, and every other rare
hook has its own factor.

Related: `docs/design/kitty-discard-buckets-are-stale-for-v3.md`,
`.claude/skills/mulligan-profile.md` (the journal/recipe rules this sits beside),
`src/analyzer/ExhaustiveKeep.cpp` (the resume gate and `MTG_JOURNAL_BACKUP_S`).
