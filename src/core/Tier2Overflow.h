#pragma once
// TIER-2 OVERFLOW STORE: where a full tier-1 cache puts the entries it would otherwise lose.
// See docs/design/tier2-cache-overflow.md and docs/design/two-tier-search-cache.md.
//
// THE CONTRACT (USER 2026-11-03: "Tier 2 is essentially just to improve performance of heavier games
// when we have extra RAM. It changes no play."). Tier 1 (core/Tier1Ledger.h) is small, FIXED per thread
// and machine-independent, so what it holds is a function of the game alone. Tier 2 is sized from the
// machine's RAM and its byte budget is SHARED by every worker, so whether an entry is still in tier 2
// depends on the machine and on what the other threads hold. It is therefore allowed to hold an entry
// only when SERVING it is indistinguishable from recomputing it: same result, same side effects, and
// the same charge -- budget units AND priced work (core/WorkCounters.h; the priced decision ceiling reads
// it). A cache whose hit is FREE (it skips charged work) can overflow here only if its entry records
// what a recompute would charge at the hit site, which is NOT the same as what the first computation
// charged whenever the computation reads other warm caches (the measured cachecharge mode-2 overcharge,
// TurnSolver.cpp). The design note lists, per cache, which ones qualify.
//
// SHAPE. Storage is PER THREAD (each consumer cache keeps one Overflow<> in a thread_local), and only
// the BYTE BUDGET is shared (one Pool, atomics). Every cache wired so far is scoped to a game or to a
// decision on one worker (the mana cache is emptied per game on purpose, so a key hole cannot become a
// cross-game channel), so a shared MAP would buy no hit another thread could use, would cost a lock on
// every lookup, and would turn any residual key hole into cross-thread play. Lookups happen only on a
// tier-1 miss. A refused store is simply lost (the bounded fallback: tier 1 still clears, tier 2 never
// exceeds its budget).
//
// LEVERS. MTG_TIER2_OVERFLOW (DEFAULT ON; =0 restores each cache's tier-1-only behaviour byte for byte).
// MTG_TIER2_OVERFLOW_KB = the shared budget in KB (0 = unbounded; UNSET = membudget::DefaultTier2OverflowKb,
// its own slice of the RAM-derived cache budget, apart from the plan-cache pool). The purity test runs
// MTG_TIER2_OVERFLOW_KB=1 (every store refused but the first few hundred bytes) against a large value.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include "EnvFlags.h"

namespace tier2
{
enum Cat : int
{
    kMana = 0,   // payable-mana cache (SpellEffects): content-pure; a hit replays the solve's recorded work (no units)
    kCats
};
inline const char* CatName(int c)
{
    static const char* n[kCats] = { "mana" };
    return (c >= 0 && c < kCats) ? n[c] : "?";
}

// DEFAULT ON; MTG_TIER2_OVERFLOW=0 -> every wired cache behaves exactly as before (tier 1 only).
inline bool On() { static const bool v = EnvOn("MTG_TIER2_OVERFLOW", true); return v; }

struct CatStats
{
    std::atomic<long long> bytes{0}, peak{0};
    std::atomic<long long> stores{0}, refused{0}, lookups{0}, hits{0}, ctx_misses{0}, dropped{0};
};

// One shared byte budget. Gauges are relaxed atomics; Reserve is a CAS so the budget is never exceeded.
struct Pool
{
    long long budget = 0;   // bytes; <= 0 = unbounded
    std::atomic<long long> used{0}, peak{0};
    CatStats cat[kCats];

    explicit Pool(long long budget_bytes) : budget(budget_bytes) {}
    bool Reserve(int c, long long b)
    {
        long long cur = used.load(std::memory_order_relaxed);
        for (;;)
        {
            if (budget > 0 && cur + b > budget)
            { cat[c].refused.fetch_add(1, std::memory_order_relaxed); return false; }
            if (used.compare_exchange_weak(cur, cur + b, std::memory_order_relaxed)) { break; }
        }
        Bump(peak, cur + b);
        Bump(cat[c].peak, cat[c].bytes.fetch_add(b, std::memory_order_relaxed) + b);
        cat[c].stores.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    void Release(int c, long long b)
    {
        if (b == 0) { return; }
        used.fetch_sub(b, std::memory_order_relaxed);
        cat[c].bytes.fetch_sub(b, std::memory_order_relaxed);
    }
    static void Bump(std::atomic<long long>& hi, long long v)
    {
        long long p = hi.load(std::memory_order_relaxed);
        while (v > p && !hi.compare_exchange_weak(p, v, std::memory_order_relaxed)) {}
    }
};

// The process's pool (defined in Tier2Overflow.cpp: reads MTG_TIER2_OVERFLOW_KB / membudget).
Pool& GlobalPool();
long long DefaultBudgetBytes();
// "[tier2-overflow] ..." summary (empty when nothing was ever offered to tier 2).
std::string Report(const Pool& p);

// A per-thread overflow map over the CONSUMER's own map type, so a spilled tier-1 entry moves as a node
// handle (no copy, no allocation). Bytes(v) is the consumer's own deterministic size estimate.
template <class Map, long long (*Bytes)(const typename Map::mapped_type&)>
class Overflow
{
public:
    using key_type  = typename Map::key_type;
    using node_type = typename Map::node_type;

    Overflow(int cat, Pool* pool = nullptr) : m_cat(cat), m_pool(pool) {}
    ~Overflow() { Clear(); }
    Overflow(const Overflow&) = delete;
    Overflow& operator=(const Overflow&) = delete;

    Pool& P() { return m_pool != nullptr ? *m_pool : GlobalPool(); }

    // Offer one entry (node handle from the consumer's map). Replaces an entry under the same key.
    // Returns false (entry dropped) when the shared budget refuses it.
    bool Put(node_type&& nh)
    {
        if (nh.empty()) { return false; }
        const long long b = Bytes(nh.mapped());
        auto it = m_map.find(nh.key());
        if (it != m_map.end()) { Drop(it); }
        if (!P().Reserve(m_cat, b)) { return false; }
        m_bytes += b;
        m_map.insert(std::move(nh));
        return true;
    }
    // Move EVERY entry of a tier-1 map into tier 2 (the consumer then has an empty map). Entries the
    // budget refuses are dropped -- exactly what the wholesale clear did to all of them before.
    void SpillAll(Map& from)
    {
        while (!from.empty()) { Put(from.extract(from.begin())); }
    }
    // Lookup (counts a lookup; the caller counts the hit once it has validated the entry).
    typename Map::iterator Find(const key_type& k)
    {
        P().cat[m_cat].lookups.fetch_add(1, std::memory_order_relaxed);
        return m_map.find(k);
    }
    typename Map::iterator End() { return m_map.end(); }
    // Remove an entry and hand it back (to promote it into tier 1).
    node_type Take(typename Map::iterator it)
    {
        const long long b = Bytes(it->second);
        m_bytes -= b;
        P().Release(m_cat, b);
        return m_map.extract(it);
    }
    void NoteHit()      { P().cat[m_cat].hits.fetch_add(1, std::memory_order_relaxed); }
    void NoteCtxMiss()  { P().cat[m_cat].ctx_misses.fetch_add(1, std::memory_order_relaxed); }
    // Empty this thread's store (game end); the bytes return to the shared pool.
    void Clear()
    {
        if (m_map.empty()) { return; }
        P().cat[m_cat].dropped.fetch_add(static_cast<long long>(m_map.size()), std::memory_order_relaxed);
        P().Release(m_cat, m_bytes);
        m_bytes = 0;
        m_map.clear();
    }
    std::size_t Size() const { return m_map.size(); }
    long long Bytes_() const { return m_bytes; }
    // Exchange the stored entries (and their byte count) with another store of the same category and pool
    // -- the bytes stay reserved in the shared pool, they only change owner. The mana oracle's isolation
    // scope stashes a game's tier 2 this way (ManaCacheObserverSwap).
    void SwapContents(Overflow& o) { m_map.swap(o.m_map); std::swap(m_bytes, o.m_bytes); }

private:
    void Drop(typename Map::iterator it)
    {
        const long long b = Bytes(it->second);
        m_bytes -= b;
        P().Release(m_cat, b);
        m_map.erase(it);
    }
    int       m_cat;
    Pool*     m_pool;
    Map       m_map;
    long long m_bytes = 0;
};
}
