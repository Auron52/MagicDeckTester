#pragma once
// TIER-1 CACHE LEDGER: one per-thread byte account for every search cache whose HIT IS FREE (it skips
// work a miss would charge to the search budget). See docs/design/two-tier-search-cache.md.
//
// WHY ONE LEDGER. A free hit is a play input under a finite budget: it decides how many units a
// computation costs, and the iterative-deepening start gate reads those units. So the question "was
// this entry still resident?" must have the same answer on every machine, at every thread count, and
// whatever ran earlier on the worker. That holds when residency depends only on (a) a FIXED byte
// budget and (b) this thread's own game -- which is what a per-thread ledger with a constant limit
// gives. The caches it replaces were bounded by machine RAM / worker count (the TT cap, the m1 memo's
// byte cap) or by a GLOBAL pool other threads also draw on (FSL, plan pool).
//
// STAGE 1 (this header as first landed) is ACCOUNTING ONLY: every cache reports its bytes, and the
// ledger records each game's peak, total and per cache (MTG_TIER1_STATS). Nothing here changes what
// any cache admits, so play is byte-identical. The per-game peak distribution is the input that sizes
// the tier-1 budget (USER 2026-11-03: "50-100 MB per thread ... choose a number that fits most games").
//
// Bytes are each cache's own APPROXIMATION of its footprint (map node + payload), the same numbers the
// caches already used for their own caps. They are deterministic (a function of the entries, never of
// the allocator), which is what lets them become an admission currency in stage 2.
#include <cstdint>

namespace tier1
{
enum Cat : int
{
    kTT = 0,      // TranspositionTable win + no-win maps (all live tables on the thread)
    kFsl,         // FSLineCache (per decision)
    kPlanEnum,    // enummemo promoted plan vectors (m1/m2 enumeration memo)
    kPlanBp,      // bpcache continuation lists
    kM1Memo,      // rollout pre-combat solve memo
    kSolveMemo,   // greedy Solve memo + searched second-main memo
    kGameLc,      // game-persistent FS line cache
    kMana,        // payable-mana cache (SpellEffects)
    kCats
};
inline const char* CatName(int c)
{
    static const char* n[kCats] = { "tt", "fsl", "plan_enum", "plan_bp", "m1memo", "solvememo", "gamelc", "mana" };
    return (c >= 0 && c < kCats) ? n[c] : "?";
}

inline thread_local long long t_cur[kCats]  = {};
inline thread_local long long t_total       = 0;
inline thread_local long long t_peak_total  = 0;
inline thread_local long long t_peak[kCats] = {};
// Composition of the total at the moment it peaked (which caches the peak is made of).
inline thread_local long long t_at_peak[kCats] = {};

inline void Add(int c, long long delta)
{
    t_cur[c] += delta;
    t_total  += delta;
    if (t_cur[c] > t_peak[c]) { t_peak[c] = t_cur[c]; }
    if (t_total > t_peak_total)
    {
        t_peak_total = t_total;
        for (int k = 0; k < kCats; ++k) { t_at_peak[k] = t_cur[k]; }
    }
}
// For a cache that keeps its own running byte count: report the new value, the ledger takes the delta.
inline void Set(int c, long long now) { Add(c, now - t_cur[c]); }

// Game start (TurnSolver::ClearPerGameCaches, after the per-game caches are emptied): the peak restarts
// from whatever is still live (per-decision tables of an enclosing caller, if any).
inline void BeginGame()
{
    t_peak_total = t_total;
    for (int k = 0; k < kCats; ++k) { t_peak[k] = t_cur[k]; t_at_peak[k] = t_cur[k]; }
}
}
