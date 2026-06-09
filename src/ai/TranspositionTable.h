#pragma once
#include <cstdint>
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
// It is NOT reused across decisions or games:
//   - The library is keyed by its remaining SIZE (plus the top card as cheap
//     insurance). Within a single decision every rollout draws from the top of the
//     same root library, so remaining size uniquely identifies remaining content.
//     Across games that invariant breaks (different shuffles), so a persistent table
//     would mis-hit. Per-decision scope also keeps it single-threaded by
//     construction — no locking, and thread-invariant results are preserved.
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

    // Returns a pointer to the cached win turn, or nullptr on a miss.
    const int* Lookup(const Key& k) const
    {
        std::unordered_map<Key, int, KeyHash>::const_iterator it = m_map.find(k);
        return it == m_map.end() ? nullptr : &it->second;
    }

    // Records a win turn for a key. The same key always maps to the same value
    // (pure function), so a redundant store is a harmless no-op.
    void Store(const Key& k, int win_turn) { m_map.emplace(k, win_turn); }

    std::size_t Size() const { return m_map.size(); }

private:
    std::unordered_map<Key, int, KeyHash> m_map;
};
