# Where the engine's time actually goes (perf, 2026-08-16)

> **CORRECTION (same day).** The 18.4% attributed to the cleanup-discard ranking below is WRONG, and
> so is the recommendation built on it. `perf` is severely throttled in this container: a 120-game run
> that should yield ~24,000 samples produced **~1,000**, and a single 3.6 s game at `-F 4999` produced
> **270**. Two independent deterministic measurements put the ranking at **~1.5%**, not 18.4%:
> (a) running it TWICE per call (byte-identical, so the delta is its exact cost) moved a degenerate
> game from 3.26 s to 3.31 s — minima of 5 alternating pairs; (b) 4,087 calls x ~10 us = ~0.04 s of a
> 3.26 s game. Treat every perf percentage in this file as indicative only, and prefer deterministic
> counters or a double-the-work A/B. The subsystem ORDERING is probably still right; the magnitudes
> are not.

Recorded because a full day was spent optimising the mana tap backtracker before anyone measured
that it is **~1% of engine cost** (`docs/design/mana-prune-tight-bounds.md` §8). Step 0 of any perf
task is this document's method, not a component's internal ratio.

**Method.** `bash build.sh profile` (Release codegen + symbols), then
`perf record -g --call-graph dwarf -F 997` on the real workload, written to `/tmp` (perf cannot
write to `/workspaces` — 9p). Read the **inclusive** (`--children`) view for subsystem shares and
the flat (`--no-children`) view for what to actually change. Beware two traps seen here:

* **Startup swamps a short run.** Hinata's first profile was 71.6% `DeserializeExhaustiveKeep` at
  120 games — the one-time exhaustive-keep sidecar load. It is still 37.6% at 700 games. Any deck
  with a keep sidecar needs a long run before the in-game profile means anything.
* **`[unknown] 0xffffffffffffffff`** at ~90% is the dwarf unwinder's root, not a real frame.

## FiveColour, mulligan-gen settings (d3 / 3 ms) — the 220–440 h workload

Inclusive:

| subsystem | inclusive |
|---|---|
| `EnumeratePlansWithLand(Uncached)` | **25.0%** |
| `SimulateEndAndStartNextTurn` | **24.8%** |
| **`CleanupDiscardRankingWithOrder`** | **18.4%** |
| `GameState::GameState(const&)` | 12.5% |
| `GameEngine::MainPhase` | 10.9% |

Flat (self) — what to actually edit:

| symbol | self |
|---|---|
| `GameState::GameState(const&)` | 9.0% |
| allocator: `operator new` 6.8 + `delete` 2.1 + `free` 2.1 + `malloc` 1.2 + memmove 1.2 | **13.4%** |
| `BuildSimKey` (+ its lambda) | 6.7% |
| cleanup discard: ranking 4.4 + insertion sort 2.0 + `CleanupDiscardManaValue` 1.3 | **7.6%** |
| `CollectActions` | 3.7% |
| `RevealLogPause::RevealLogPause()` | 2.7% |

## Hinata, same settings (700 games, startup amortised)

`EnumeratePlansWithLand` 14.0%, `SimulateEndAndStartNextTurn` 11.1%, `operator new` 12.7%.
**The cleanup-discard ranking does not appear above 4%.**

So the 18.4% is **FiveColour-specific**, and the reason is the deck: it holds expensive five-colour
cards it cannot yet cast, so the hand exceeds seven often and the cleanup shed fires far more than
on a deck that empties its hand. It is the right target for the FiveColour gen workload and NOT a
general engine win — exactly the distinction this session got wrong once already.

## Candidate work, all BYTE-IDENTICAL (no ground-truth risk)

Unlike the tap-order experiment, none of these change a decision, so they need no rebaseline and
carry no play risk — verify with the standing smoke digest check.

1. **Cleanup-discard ranking (FiveColour 18.4%).** Three compounding defects, none algorithmic:
   * it is recomputed *per card shed* — the `while (hand_count() > 7)` loop calls
     `CleanupDiscardCandidates` again for every discard, re-ranking the whole hand each time;
   * it returns a fully ranked `vector<int>` when the caller reads only index 0 (except when a plan
     pinned `scripted_discard_choice`), so the sort is usually wasted;
   * it allocates two vectors (`out`, `taken`) per call, feeding the 13.4% allocator bill.
   A top-1 fast path plus `thread_local` scratch buffers addresses all three without touching the
   ranking's semantics. Care: indices shift after each erase, so "compute once, reuse" needs the
   remap — the safest first cut is the top-1 path plus scratch reuse, leaving the loop alone.
2. **`RevealLogPause` (2.7%). — DONE 2026-08-16.** It already has a no-op fast path, but computes it
   by reading **28 thread-local pointers** on every construction, and it is constructed millions of
   times per search game. One `g_play_hooks_installed` counter maintained at the ~28 install sites
   replaces 28 loads with one. Mechanical, but every install site must be covered.

   **Shipped as a STICKY `thread_local bool`, not a counter.** The install-site audit found that all
   26 chooser/sink hooks are installed from exactly one function (`ClaudePlayHarness::Install`,
   src/main.cpp) — except `g_play_soulfire_chooser`, installed in `RunClaudePlay` itself and
   **missing from `ClearClaudePlayChoosers`** (a real pre-existing leak, fixed in the same series).
   That single overwrite-without-a-paired-clear is exactly what would make an increment/decrement
   counter read STALE LOW, the one unsound direction — it would let a human chooser fire inside the
   search. A sticky flag that is never cleared can only ever be stale HIGH, which merely takes
   today's full save/null/restore path. So the sticky form is not a simplification, it is the sound
   one. Invariant: *if `!g_play_hooks_installed` then all 26 hooks are null* — monotone, so the
   restoring destructor writes back nulls whenever the flag is false.

   `g_reveal_logger` and `g_real_resolution` stay LIVE reads on purpose. `g_reveal_logger` is set
   per-game by `GameEngine::RunGame`'s `RevealScope`, so folding it into a sticky flag would
   permanently kill the fast path on every logged/digest run — strictly worse than today, where the
   outer pause nulls it and the hot nested pauses still take the fast path.

   Hatches: `MTG_PAUSE_HOOK_FLAG=0` restores the 26-pointer scan; `MTG_PAUSE_HOOK_AUDIT=1` runs the
   scan AND aborts if the flag ever disagrees in the unsafe direction. **Verified**: audit-mode smoke
   is clean over all 36 cases *and* the 196 reference replays — which matter because
   `test/viewer_protocol_check.py` drives `--claude-play`, the only path where a hook is ever
   non-null, so it is the one test that actually exercises install-site exhaustiveness. All 36
   digests identical across flag-on / flag-off / audit arms.
3. **Allocator churn (13.4%).** Largely downstream of 1, of `GameState` copies, and of small
   per-call vectors in hot helpers. Worth re-profiling after 1 rather than attacking blind.

## What NOT to do

The mana tap backtracker. It is ~1% of cost; its infeasibility half is already at its 1.6-nodes-per-
entry floor; and a flow-guided tap order that cut nodes 9.5–13.6x moved total time by ~1% and was
refused by the held-out seeds on play quality (`483adf1`).
