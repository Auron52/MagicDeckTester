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
#include <cstdlib>
#include <atomic>
#include "EnvFlags.h"

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

// ---------------------------------------------------------------- STAGE 2: ADMISSION (MTG_TIER1)
// Each tier-1 cache gets a FIXED per-thread allocation, identical on every machine. A cache asks
// Fits() before storing and refuses (or applies its own wholesale-clear policy) when the allocation
// is full. Because the allocation is a constant and the ledger counts only this thread's entries --
// which are cleared per game -- what is resident, and therefore every free hit, is a function of the
// game alone: the same at any thread count, on any machine, after any earlier game on the worker.
// The bounds it replaces were derived from machine RAM / worker count (the TT cap, the m1 memo's
// byte cap, the mana cache's byte cap) or drawn from a GLOBAL pool (FSL).
// MTG_TIER1=0 restores those legacy bounds (A/B control). MTG_TIER1_<CAT>_MB overrides one
// allocation (0 = unbounded). Allocation 0 by default = not a tier-1 admission category (the plan
// caches use the shared pool; the solve memo keeps its portable entry-count cap).
// Defaults: the per-cache max over the regression tier's 32,874 searched games (2026-11-03) with
// headroom -- tt 5.0 MB, fsl 15.4, m1memo 8.3, gamelc 15.6, mana 46.0 (p99.9 6.7).
inline bool On() { static const bool v = EnvOn("MTG_TIER1", true); return v; }
inline long long AllocBytes(int c)
{
    static const long long* v = []{
        static long long a[kCats] = {};
        const long long def_mb[kCats] = { 8, 16, 0, 0, 8, 0, 24, 16 };   // tt fsl plan_enum plan_bp m1memo solvememo gamelc mana
        const char* env[kCats] = { "MTG_TIER1_TT_MB", "MTG_TIER1_FSL_MB", nullptr, nullptr, "MTG_TIER1_M1MEMO_MB",
                                   nullptr, "MTG_TIER1_GAMELC_MB", "MTG_TIER1_MANA_MB" };
        for (int c = 0; c < kCats; ++c)
        {
            long long mb = def_mb[c];
            if (env[c] != nullptr && std::getenv(env[c]) != nullptr) { mb = std::strtoll(std::getenv(env[c]), nullptr, 10); }
            a[c] = mb > 0 ? (mb << 20) : 0;
        }
        return a; }();
    return v[c];
}
inline std::atomic<long long> g_refused[kCats];
// True = this cache may add `add` portable bytes. Allocation 0 = unbounded.
inline bool Fits(int c, long long add)
{
    const long long a = AllocBytes(c);
    if (a <= 0 || t_cur[c] + add <= a) { return true; }
    g_refused[c].fetch_add(1, std::memory_order_relaxed);
    return false;
}

// PORTABLE SIZE CONSTANTS. Admission must count the same number on Linux and Windows, so tier-1 sizes
// are element COUNTS (size(), never capacity()) times these fixed constants -- the Linux sizeof()s at
// the time of writing (2026-11-03), so the ledger still reads as approximate bytes.
constexpr long long kActionB    = 544;   // sizeof(Action)
constexpr long long kPlanB      = 552;   // sizeof(TurnSolver::Plan)
constexpr long long kPhasePlanB = 568;   // sizeof(TurnSolver::PhasePlan)
constexpr long long kFslEntryB  = 56;    // sizeof(FSLineEntry)
constexpr long long kMapNodeB   = 64;    // an unordered_map node + bucket share

// Game start (TurnSolver::ClearPerGameCaches, after the per-game caches are emptied): the peak restarts
// from whatever is still live (per-decision tables of an enclosing caller, if any).
inline void BeginGame()
{
    t_peak_total = t_total;
    for (int k = 0; k < kCats; ++k) { t_peak[k] = t_cur[k]; t_at_peak[k] = t_cur[k]; }
}
}
