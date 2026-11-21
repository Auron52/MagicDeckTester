# `mtg --batch` prepares its jobs on one core (deferred, found 2026-11-21)

## Why it matters

The USER (2026-11-21): *"Nothing should be single-threaded if we can avoid it..."*. Before a batch plays its first
game, `src/runner/BatchRunner.cpp` parses every manifest job serially:
`for (const json& jspec : manifest["jobs"]) { jobs.push_back(ParseJob(jspec, profile_cache)); }`.
While it does, the other cores sit idle.

**Measured:** the Maze's End batch-3 A/B manifest (13,217 single-game jobs, 3 arms × clairvoyant/blind × every row
of the two Maze's End decks) spent about 4 minutes in this loop before any worker started. The batch heartbeat does
not cover this time, because the workers do not exist yet.

## What `ParseJob` does per job

Each step below runs once per job, even though an A/B manifest holds only a handful of distinct
(deck, profile, flags) combinations:

1. Parses the decklist file.
2. `ProfileCache::get` on the profile path. This is an LRU cache, so a repeated path should hit it. Check that it
   does, since a miss means re-parsing a large profile JSON.
3. `ResolvePlaySettings`.
4. `SelectDecisionProvider` / `DetectDecisionProvider`. Detection scans the deck's card params.
5. `WarnRequiredHeuristics`, under the job's arm.
6. `GoldFishRunner::DeckUsesSecondMain`, under the job's arm.
7. Writes one `[play]` stderr line.

## Fix (not yet done)

1. **Profile it first.** Run `mtg --batch` on a large manifest with `perf record` under `./build.sh profile`. Do not
   guess which step dominates.
2. **Then either memoise or parallelise.** Memoise the per-(deck path, profile path, flags) work: steps 1, 3, 4, 5
   and 6 depend only on that key and the manifest fields. Or parallelise the loop across `nproc` threads, keeping
   job order. Since `ProfileCache` is shared, that needs a thread-safe cache or per-thread caches.
3. **Prove it's byte-identical.** Every job's digest must be unchanged. Run smoke and check the `[play]` lines are
   identical apart from their order.

Neither option changes play, so the fix is ungated for quality. It still goes through the push queue, because it
touches `src/`.
