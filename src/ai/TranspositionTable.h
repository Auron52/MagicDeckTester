#pragma once
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <unordered_map>

// Per-decision memoization of SimulateToEnd(state, depth) -> win turn.
//
// The lookahead search is exhaustive and CLAIRVOYANT (it plans against the real,
// fixed library order — see project-clairvoyant-search). Two key consequences make
// SimulateToEnd a pure, memoizable function of (game state, rollout depth):
//
//   1. Its result does NOT depend on the search budget. The rollout sub-search runs
//      every iterative-deepening pass to completion (enforce_budget=false); the
//      budget is only counted, never used to truncate a rollout. So the win turn is
//      a function of the state and depth alone.
//   2. Its result does NOT depend on the branch-and-bound cutoff. The cutoff only
//      makes the function early-return max_turns+1 for lines that do not win by the
//      cutoff; a returned REAL win turn (<= max_turns) is the true earliest win
//      regardless of the cutoff used. We therefore cache only real win turns, and a
//      cache hit that returns the true win turn yields caller decisions identical to
//      the cutoff-clamped path (all comparisons are against the running best).
//
// SCOPE: one top-level AI decision. A table is created at the enforcing
// SolveWithLookahead call and threaded through the whole recursion, then destroyed.
// It is NOT reused across decisions or games (single-threaded by construction, no
// locking, thread-invariant results):
//   - The library is keyed by its remaining SIZE (plus the top card as cheap
//     insurance). Within a single decision every rollout draws from the top of the
//     same root library, so remaining size uniquely identifies remaining content.
//     Across games that invariant breaks (different shuffles), so a persistent table
//     would mis-hit.
//
// MEASURED (2026-07-16): making this table GAME-persistent (MTG_LEAF_CACHE, reuse rollout
// leaves across a game's decisions) is SOUND but does not help QUALITY. A verify-on-hit
// harness (MTG_LEAF_VERIFY) found 0 stale hits over 283,014 verified hits -- with
// search-shuffle on, BuildSimKey folds the full ordered library + search_count, so the key
// is exact across decisions too. It does not pay because the cross-decision reuse is small
// (~0.14pp added hit rate; recurring rollout leaves are mostly intra-decision, already
// captured by the per-call table) AND because a hit frees budget, so the deterministic
// start-gate spends the saving on a DEEPER search rather than less wall time (LP-neutral).
// The MTG_TT_STATS / MTG_LEAF_VERIFY instruments are kept for future reuse questions. See
// docs/design/escalation-interior-reuse.md.
//
// The table kills the cross-candidate redundancy (different opening plays that
// transpose to the same later state) and the cross-pass redundancy (iterative
// deepening re-deriving the same shallow sub-rollouts), making depth-5 search
// tractable on decision-dense states. See project-search-optimizations.
class TranspositionTable
{
public:
    // 128-bit key: two independently-mixed 64-bit hashes over the future-determining
    // state. Two independent words make collisions (a false hit that would silently
    // corrupt a result) astronomically unlikely while keeping lookups int-cheap.
    struct Key
    {
        uint64_t h1 = 0;
        uint64_t h2 = 0;
        bool operator==(const Key& o) const { return h1 == o.h1 && h2 == o.h2; }
    };

    struct KeyHash
    {
        std::size_t operator()(const Key& k) const { return static_cast<std::size_t>(k.h1); }
    };

    // Optional reuse instrument (MTG_TT_STATS): total lookups/hits across ALL tables
    // (static). A persistent leaf cache's CROSS-decision reuse shows up as the hit delta
    // vs the per-call baseline (intra-decision hits are identical in both runs). Off by
    // default => the two counter bumps are skipped, so the hot Lookup is unchanged.
    static bool StatsOn()
    {
        static const bool on = std::getenv("MTG_TT_STATS") != nullptr;
        return on;
    }
    static std::atomic<unsigned long long>& Lookups() { static std::atomic<unsigned long long> v{0}; return v; }
    static std::atomic<unsigned long long>& Hits()    { static std::atomic<unsigned long long> v{0}; return v; }

    // Returns a pointer to the cached win turn, or nullptr on a miss.
    const int* Lookup(const Key& k) const
    {
        std::unordered_map<Key, int, KeyHash>::const_iterator it = m_map.find(k);
        const bool hit = (it != m_map.end());
        if (StatsOn())
        {
            Lookups().fetch_add(1, std::memory_order_relaxed);
            if (hit) { Hits().fetch_add(1, std::memory_order_relaxed); }
        }
        return hit ? &it->second : nullptr;
    }

    // Records a win turn for a key. The same key always maps to the same value
    // (pure function), so a redundant store is a harmless no-op.
    void Store(const Key& k, int win_turn) { m_map.emplace(k, win_turn); }

    std::size_t Size() const { return m_map.size(); }

    // Drop all entries (the game-persistent leaf cache is cleared per game).
    void Clear() { m_map.clear(); }

private:
    std::unordered_map<Key, int, KeyHash> m_map;
};

// Print the aggregate lookup/hit totals once at exit when MTG_TT_STATS is set.
namespace tt_detail
{
    struct StatsReporter
    {
        ~StatsReporter()
        {
            if (!TranspositionTable::StatsOn()) { return; }
            const unsigned long long l = TranspositionTable::Lookups().load();
            const unsigned long long h = TranspositionTable::Hits().load();
            std::fprintf(stderr, "[tt-stats] lookups=%llu hits=%llu (%.2f%% hit)\n",
                         l, h, l ? (100.0 * static_cast<double>(h) / static_cast<double>(l)) : 0.0);
        }
    };
    inline StatsReporter g_stats_reporter;
}
