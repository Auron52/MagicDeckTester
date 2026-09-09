#pragma once
#include "../core/EnvFlags.h"
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
        static const bool on = EnvOn("MTG_TT_STATS");
        return on;
    }
    static std::atomic<unsigned long long>& Lookups() { static std::atomic<unsigned long long> v{0}; return v; }
    static std::atomic<unsigned long long>& Hits()    { static std::atomic<unsigned long long> v{0}; return v; }
    // Peak entries in any single table (MTG_TT_STATS). The per-decision tables dominate RSS on
    // decision-dense decks (antilife escalation), so this shows the memory driver directly.
    static std::atomic<std::size_t>& PeakSize() { static std::atomic<std::size_t> v{0}; return v; }
    // Same MTG_TT_STATS gate, for the NO-WIN half (see LookupNoWin/StoreNoWin). Lookups()/
    // Hits() above only ever see the WIN map, so with the no-win cache armed the reported hit rate
    // was a statement about a minority of the traffic -- and "what fraction of rollouts does the
    // memo actually remove" is the whole question when deciding whether a state memo pays.
    static std::atomic<unsigned long long>& NoWinLookups() { static std::atomic<unsigned long long> v{0}; return v; }
    static std::atomic<unsigned long long>& NoWinHits()    { static std::atomic<unsigned long long> v{0}; return v; }
    static std::atomic<unsigned long long>& NoWinStores()  { static std::atomic<unsigned long long> v{0}; return v; }

    // Result-NEUTRAL store cap (MTG_TT_CAP = max entries per table; 0/unset = unlimited = byte-identical).
    // The table is a pure memoization of SimulateToEnd (a miss just recomputes the same value), so refusing
    // to store past the cap only trades recompute time for bounded memory -- decisions are unchanged. Early
    // (shallow, high-reuse) leaves are stored first and kept; only the deep long-tail is dropped. This bounds
    // the antilife escalation's ~6GB single-decision footprint. Read once (static) so the hot path stays cheap.
    static std::size_t Cap()
    {
        static const std::size_t cap = []{
            const char* e = std::getenv("MTG_TT_CAP");
            return e ? static_cast<std::size_t>(std::strtoull(e, nullptr, 10)) : std::size_t{0};
        }();
        return cap;
    }

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
    // (pure function), so a redundant store is a harmless no-op. A result-neutral cap
    // (MTG_TT_CAP) bounds memory: past the cap we simply stop storing (future lookups
    // recompute -> identical answers, just slower). Off by default (cap==0) => the two
    // extra reads short-circuit and the store is unchanged (byte-identical).
    void Store(const Key& k, int win_turn)
    {
        static const std::size_t cap = Cap();
        if (cap && m_map.size() >= cap) { return; }
        m_map.emplace(k, win_turn);
        if (StatsOn())
        {
            std::size_t sz = m_map.size(), prev = PeakSize().load(std::memory_order_relaxed);
            while (sz > prev && !PeakSize().compare_exchange_weak(prev, sz, std::memory_order_relaxed)) {}
        }
    }

    // BOUND-QUALIFIED NO-WINS (MTG_TT_NOWIN_CACHE; see SimulateToEnd). A win is cutoff-independent
    // and lives in m_map above. A NO-WIN is not: SimulateToEndImpl aborts with `if (turn >
    // cutoff_turn) return max_turns+1`, so the result means only "no win at turn <= cutoff_turn".
    // Store that cutoff alongside it and reuse the entry only for a query asking no more. Kept in a
    // SEPARATE map so the win path above is untouched -- with the flag off nothing here is ever
    // called and behaviour is byte-identical.
    // A no-win rollout leaves MORE behind than its return value, and a memo that replays only the
    // return value is not the same function. Two side effects are load-bearing at the consumer:
    //   * the GRADED LEAF QUANTITY (leafeval::t_tb / t_life). A no-win is the case the leaf grade
    //     exists for -- it is what ranks two equally-hopeless candidates -- so dropping it on a hit
    //     silently reorders the argmax (measured on EDF seed 8008: leaf-eval publishes 416 -> 343
    //     and flips 15 -> 11 when the bound alone is replayed).
    //   * the CONDEMNATION-DROP delta (g_condemn_drops), which the escalation window reads as this
    //     candidate's "filter-touched" flag. Same class as enummemo::Entry / solvememo::Entry's
    //     replayed side counters (audit §6.1) -- and solved the same way, by storing the delta.
    // The engine's own types are not visible here, so the leaf grade is carried as two opaque
    // long longs (leafeval::kInvalid is just a value) and the drop delta as a count.
    struct NoWinEntry
    {
        int       bound = 0;          // "no win at turn <= bound"
        long long leaf_tb   = 0;      // leafeval::t_tb   as the memoized body left it
        long long leaf_life = 0;      // leafeval::t_life as the memoized body left it
        unsigned  condemn_drops = 0;  // g_condemn_drops delta over the memoized body
    };

    const NoWinEntry* LookupNoWin(const Key& k) const
    {
        std::unordered_map<Key, NoWinEntry, KeyHash>::const_iterator it = m_nowin.find(k);
        const bool hit = (it != m_nowin.end());
        if (StatsOn())
        {
            NoWinLookups().fetch_add(1, std::memory_order_relaxed);
            if (hit) { NoWinHits().fetch_add(1, std::memory_order_relaxed); }
        }
        return hit ? &it->second : nullptr;
    }

    // A wider refutation supersedes a narrower one; never narrows an existing bound. The whole
    // entry moves together: the leaf grade a run publishes depends on how far that run got, so a
    // wider bound's grade must not be spliced onto a narrower bound (see SimulateToEnd's
    // full-run argument for which queries may read it at all).
    void StoreNoWin(const Key& k, const NoWinEntry& e)
    {
        static const std::size_t cap = Cap();
        if (cap && m_nowin.size() >= cap) { return; }
        if (StatsOn()) { NoWinStores().fetch_add(1, std::memory_order_relaxed); }
        std::unordered_map<Key, NoWinEntry, KeyHash>::iterator it = m_nowin.find(k);
        if (it == m_nowin.end())            { m_nowin.emplace(k, e); }
        else if (it->second.bound < e.bound) { it->second = e; }
    }

    std::size_t Size() const { return m_map.size(); }

    // Drop all entries (the game-persistent leaf cache is cleared per game).
    void Clear() { m_map.clear(); m_nowin.clear(); }

private:
    std::unordered_map<Key, int, KeyHash> m_map;
    // key -> the refutation (cutoff it was proved under + the body's replayable side effects)
    std::unordered_map<Key, NoWinEntry, KeyHash> m_nowin;
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
            std::fprintf(stderr, "[tt-stats] lookups=%llu hits=%llu (%.2f%% hit) peak_entries=%zu cap=%zu\n",
                         l, h, l ? (100.0 * static_cast<double>(h) / static_cast<double>(l)) : 0.0,
                         TranspositionTable::PeakSize().load(), TranspositionTable::Cap());
            const unsigned long long nl = TranspositionTable::NoWinLookups().load();
            const unsigned long long nh = TranspositionTable::NoWinHits().load();
            const unsigned long long ns = TranspositionTable::NoWinStores().load();
            if (nl + ns > 0)
            {
                std::fprintf(stderr,
                             "[tt-stats] nowin_lookups=%llu nowin_hits=%llu (%.2f%% hit) nowin_stores=%llu\n",
                             nl, nh, nl ? (100.0 * static_cast<double>(nh) / static_cast<double>(nl)) : 0.0,
                             ns);
            }
        }
    };
    inline StatsReporter g_stats_reporter;
}
