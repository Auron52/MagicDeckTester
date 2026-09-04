# Batch pool contamination (2026-08-26) — dormant defect, hardening shipped

**Status:** DORMANT (2026-08-29), hardening adopted 2026-09-01. The defect was proven real once,
could not be reproduced three days later under exhaustive reconstruction of every persisted input,
and the suspected carrier is now closed off in code. This doc exists so the next wild occurrence
starts from evidence, not from scratch.

## What was observed (proven, same-day differential)

During the M2_RECONSIDER held-out read, `mtg --batch` results were **not pool-composition-
invariant**: all 12 mirrorwing overnight cells (d0/d3/d5) read FASTER in large pools than
standalone/GT, while every other deck in the same pool stayed byte-identical to GT. Two different
poisoned pools produced **byte-identical wrong digests** (deterministic, not a race in the games
themselves), and the flip was controlled the same day on the same disk:

- pool at default profile-cache cap 3 → poisoned;
- same pool at `MTG_BATCH_PROFILE_CACHE=16` (no evictions) → clean, every cell == GT.

So the profile LRU cache's **eviction path** (BatchRunner.cpp `ProfileCache`, default cap 3) was
the carrier. Reload *content* was separately proven fine (cap 1, maximum reload thrash, clean);
co-tenant game **volume** was required, their profile loads alone were not.

## Why it is dormant, not fixed

On 2026-08-29 every persisted input was reconstructed exactly — the Aug-26 snapshot binary
(`test/logs/overnight/mtg.run`), the saved manifest, pinned `cards.json`, the exact sidecar set,
at 8/16/32 threads — and every run came back byte-clean. The engine is deterministic given its
pool state; what cannot be reconstructed is that day's machine state. The best surviving
mechanism candidate, unverifiable post-hoc:

> `AttachExhaustiveSidecar` re-ran `std::filesystem::exists()` per attach, `.gz` first then
> `.json`. On 9p under heavy load, a **transient `exists(.gz`) failure** would silently route that
> attach to the `.json` fallback — whose pre-12:57 bincache (possibly stale from the Aug-24
> keepmodel regeneration) was destroyed at 2026-08-26 12:57 by a probe regenerating it. The
> current `.json` table is content-equal to the `.gz`, so the flip is harmless today — which is
> exactly why it can no longer be caught in the act.

## Hardening shipped (2026-09-01)

1. **Sidecar resolution is memoized per profile path** (`AttachExhaustiveSidecar`,
   `src/ai/MulliganProfileIO.h`): the `.gz`-vs-`.json` choice is made once per process under a
   lock and can never flip between attaches. This also freezes sidecar *presence* for the process
   lifetime, which is what a measurement run wants (a generation dropping a sidecar mid-batch
   must not change arms already running).
2. **The `.json` fallback warns loudly** (once per path). Decks ship the sidecar gzipped; this
   path firing means a hand-placed `.json` or a transient `.gz` miss — either way, worth a line.
3. **The four silent catch blocks in profile loading now print `[profload] SWALLOWED: ...`**
   (base profile, keep constraints, eval sidecar, value sidecar). A parse failure silently
   degrading a deck to defaults mid-pool is exactly the shape of bug this incident looked like.
4. **`MTG_BATCH_STATE_DUMP=<substr>`** (default off; value = job-name substring filter): at every
   job switch whose name matches, the worker prints a content fingerprint of the profile handed
   to its engine plus the ambient per-thread config (exhaustive-keep pointer/sizes/bottoming,
   value/eval model sums, card_scores checksum, heurarm hash, salts, numbering pointer, depth/
   budget). **If the poison reappears in any wild run**: re-run the pool with
   `MTG_BATCH_STATE_DUMP=<affected deck>` on both a clean and a poisoned configuration and diff
   the lines — whichever field differs is the contaminated engine input.

## Mitigation (proven) if it ever reappears before a root cause

`MTG_BATCH_PROFILE_CACHE=16` (or any cap ≥ the pool's distinct profiles) removes evictions and
flipped poisoned→clean in the same-day differential. Cost is RSS (~167 MB per cached profile).

## History link

This class is almost certainly the real carrier behind the older mirrorwing batch-vs-standalone
divergence (the ab0799ce mystery): the same games reappear at their old "batch fiction" turns.
The mirrorwing overnight GT is standalone-honest and remains valid.

## 2026-09-01 (same day): the instrument caught a NEW, different instance

Hours after the hardening shipped, a mixed-arm pooled A/B (per-job heurarm flag
`MTG_FILTER_FEED_STRICT`) produced control-arm results that failed to match GT,
**nondeterministically** — unlike the 2026-08-26 event, which was deterministic.
`MTG_BATCH_STATE_DUMP` localised it in one run: every worker's per-job inputs were clean
(ctl hf=0 / str hf=2, identical profile fingerprints), yet results moved → the leak was at game
time, not job-switch time. Cause: the payable-mana cache (`g_mana_cache`, SpellEffects.cpp) is
thread_local, **outlives batch job switches by design** (its entries are pure functions of the
key), and the new lever changed the backtracker's answer without entering the key — so workers
replayed one arm's solves inside the other arm's games. Fix: `ManaCacheKey` mixes the lever in.

**Rule for future levers:** a heurarm (per-job) lever that changes the answer of any memoised
computation must be hashed into that memo's key. See `filter-feed-strict.md` §5.

## 2026-09-05: third sighting (smoke pool, antilife2hg g97) — standalone-clean, tripwires silent

A smoke-tier run (68 jobs, working tree = the Melira tutor-rank provider diff, a change whose
code is unreachable for every suite deck) moved exactly one game: `antilife2hg_smoke_d3_s1001`
gi=97, same win turn (5), play digest `7316339c60025267` → `28720f01f6eee86b`. Standalone
reconstruction of that one job on the same binary (twice, exact manifest fields) reproduces the
committed GT digest both times, and NONE of the c9351d7e tripwires fired (no `[profload]
SWALLOWED`, no `.json`-fallback warning) — so the 2026-08-26 carrier is ruled out and this looks
like the 2026-09-01 shape instead: a rare, nondeterministic in-pool flip, presumably another
thread_local memo replayed across a job boundary under one particular worker interleaving, with
no per-job lever involved this time (regression pools run a homogeneous env). Artifacts
preserved: `logs/incident_20260905_antilife2hg/` (the .wins, manifest, batch.log/err).
Response per the standing rule: standalone repro is the verdict — GT untouched, no full-tier
re-run, the coincident provider commit exonerated (also structurally: its code cannot execute
without a Melira deck in the pool). If a fourth sighting lands, run the pool with
`MTG_BATCH_STATE_DUMP=antilife` and start diffing worker job-histories clean-vs-flipped.

## 2026-09-05 (same day, cont.): sightings 4-5 + DIFFERENTIAL — the payable-mana cache is the carrier

Two further smoke runs each flipped exactly one antilife game (play digest only, same win
turn): `antilife_smoke_d0_s1001` gi=97 — **d0, no search: the greedy path itself moved** —
then `antilife2hg_smoke_d3_s1001` gi=23. Three consecutive flipping runs, all antilife, one
game each, every one standalone-clean (x2, == GT). The instrumented run (MTG_BATCH_STATE_DUMP
=antilife) shows all four antilife jobs' per-worker inputs CLEAN and identical (profile
fingerprints, ekB/ekN, life/heads) — the 2026-09-01 signature: the leak is at game time via a
thread_local memo that survives job switches. DIFFERENTIAL: the very next smoke with
**MTG_MANA_CACHE=0 came back 68/68 ALL PASS**. The cache is designed byte-identical on/off, so
this is evidence (not yet proof — flips are per-run probabilistic) that `g_mana_cache`
(SpellEffects.cpp, thread_local, uint64-keyed, never cleared across jobs) is again the carrier
— this time NOT via a per-job heurarm lever (regression pools run a homogeneous env), so the
suspect is an unkeyed input or key collision. Artifacts:
`logs/incident_20260905_antilife2hg/` (three flipped .wins, manifest, statedump stderr).
MITIGATION available meanwhile: `MTG_MANA_CACHE=0` on pooled regression runs (results
identical by design, just slower). OPEN: audit ManaCacheKey's inputs against everything
TapForCost/TapForCostDirect actually reads (provider ManaSourceRank? job-scoped profile
knobs?), and why antilife specifically.
