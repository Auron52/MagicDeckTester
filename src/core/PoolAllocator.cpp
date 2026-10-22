// Thread-local segregated free-list allocator over malloc/free (replaces the global
// operator new/delete jemalloc would otherwise supply).
//
// DEFAULT ON. Opt out with MTG_POOL_ALLOC=0 (any other value / unset => on). ON: small
// allocations (<= kMaxPooled) are served from per-thread, per-size-class free-lists, caching
// freed blocks for reuse without going back to jemalloc -- a minimal pool that measured ~2.9%
// faster (callgrind Ir) than jemalloc's tcache on the search's alloc/free-heavy per-node
// GameState deep-copy pattern. OFF: every block is a plain header'd malloc, freed straight back.
//
// Correctness: a caching layer OVER malloc/free -- it only changes WHERE memory lives, never any
// value, so results stay byte-identical (verified by the win-dump digest gate). EVERY block (on OR
// off) carries a kAlign-byte header recording its size-class (or kBigClass for a passthrough
// block), and delete ALWAYS dispatches on that header. So a block's alloc policy travels with the
// block: one allocated while the gate was off (e.g. during early static init, before g_pool_on's
// dynamic init flips it true) frees correctly even after the gate turns on. There is no headerless
// block and thus no new/delete-policy mismatch across the static-init boundary. Persistent
// allocations are safe: a freed pooled block is retained on a free-list and reused, up to a
// per-thread RETENTION CAP (below); past the cap it goes straight back to malloc.
//
// THE RETENTION CAP (MTG_POOL_ALLOC_CAP_MB, default 64 MB per thread; =0 = unbounded, the old
// behaviour). Without it a thread kept EVERY small block it ever freed, so its footprint was its
// own all-time high-water mark of live small blocks, summed over threads -- not the ~81 MB the
// ordinary search needs (measured at 12 threads), but whatever the worst enumeration on each thread
// ever held. NivMizzetCombo, 2026-10-22: the Brainstorm put-back axis materialises up to millions of
// whole-Plan clones in ONE rollout-turn candidate list (AppendSubdecisionAxes' `Plan v = p`, 99.4% of
// a 6 GB thread's live heap at the peak, jemalloc heap profile), a 1-action plan's Action vector is a
// 496-byte block, and every such list ratcheted that thread's pool up for good: the mulligan settings
// screen's d1 b1 scorer arm climbed monotonically to the 35 GB RSS cap in under a minute at 24
// threads, while the SAME run with MTG_POOL_ALLOC=0 fell back to 1-6 GB between lists and finished.
// The cap keeps the reuse the pool exists for (the hot per-node GameState churn lives far below it)
// and hands the rest back. It changes only where memory lives, never a value: byte-identical.
//
// Over-aligned types use the aligned operator new/delete(align_val_t) overloads, which we do NOT
// override -> they stay on the default allocator (self-consistent pair, never reaching our header).
//
// Revert = delete this file + its CMake line; nothing else references it.

#include <cstdlib>
#include <cstdint>
#include <new>

namespace
{
    // Cache the gate once (a per-alloc getenv would dwarf the allocation itself). Default ON;
    // MTG_POOL_ALLOC=0 opts out. Zero-initialized (false) until this dynamic init runs -- allocs in
    // that early window are header'd passthrough blocks and free correctly once the gate flips (see top).
    const bool g_pool_on = []{
        const char* e = std::getenv("MTG_POOL_ALLOC");
        return e == nullptr || e[0] != '0';
    }();

    constexpr std::size_t kAlign     = 16;      // returned-pointer alignment (>= max_align_t)
    constexpr std::size_t kQuantum   = 16;      // size-class granularity
    constexpr std::size_t kMaxPooled = 512;     // sizes above this bypass the pool
    constexpr int         kClasses   = static_cast<int>(kMaxPooled / kQuantum) + 1;  // 0..32
    constexpr int         kBigClass  = -1;      // header marker for a malloc-passthrough block

    // Header (kAlign bytes, keeps the user pointer aligned): the size-class index, or kBigClass.
    struct Header { int cls; };

    // Per-thread free-list heads. Each list stores USER (payload) pointers; a free block holds the
    // next-pointer in its payload (unused while free), leaving the size-class header (before the
    // payload) intact across the free/realloc cycle. Payload is >= kQuantum (16) bytes, so it always
    // fits a void*.
    thread_local void* g_free[kClasses] = {};
    // Bytes (header included) currently parked on THIS thread's free-lists, and the cap on them
    // (see THE RETENTION CAP at the top). The cap is value-carrying, so it keeps the raw getenv
    // parse (and no allocation can happen in here: this is operator new's own TU). Zero-initialized
    // until this dynamic init runs, and 0 means unbounded, so the early static-init window keeps the
    // old retain-everything behaviour for the handful of blocks freed in it.
    thread_local std::size_t g_free_bytes = 0;
    const std::size_t g_free_cap = []{
        const char* e = std::getenv("MTG_POOL_ALLOC_CAP_MB");
        const unsigned long long mb = (e != nullptr && *e != '\0') ? std::strtoull(e, nullptr, 10) : 64ULL;
        return static_cast<std::size_t>(mb) << 20;
    }();

    inline int SizeClass(std::size_t n)
    {
        return static_cast<int>((n + (kQuantum - 1)) / kQuantum);   // 1..kClasses-1 for pooled sizes
    }

    inline void* PoolAlloc(std::size_t n)
    {
        if (n == 0) { n = 1; }
        // Pool off, or too big to pool: a header'd malloc block, freed straight back on delete.
        if (!g_pool_on || n > kMaxPooled)
        {
            void* raw = std::malloc(kAlign + n);
            if (!raw) { return nullptr; }
            static_cast<Header*>(raw)->cls = kBigClass;
            return static_cast<char*>(raw) + kAlign;
        }
        const int cls = SizeClass(n);
        const std::size_t block = static_cast<std::size_t>(cls) * kQuantum;
        if (void* pl = g_free[cls])                           // free-list hit: pop a payload
        {
            g_free[cls] = *static_cast<void**>(pl);           // next-pointer lives in the payload
            g_free_bytes -= kAlign + block;
            return pl;                                        // header (before pl) still valid
        }
        void* raw = std::malloc(kAlign + block);              // miss: fresh block from jemalloc
        if (!raw) { return nullptr; }
        static_cast<Header*>(raw)->cls = cls;
        return static_cast<char*>(raw) + kAlign;
    }

    inline void PoolFree(void* p) noexcept
    {
        if (!p) { return; }
        void* raw = static_cast<char*>(p) - kAlign;
        const int cls = static_cast<Header*>(raw)->cls;       // header records the alloc policy
        if (cls == kBigClass) { std::free(raw); return; }     // passthrough block -> straight back
        const std::size_t bytes = kAlign + static_cast<std::size_t>(cls) * kQuantum;
        // Over this thread's retention cap: a pooled block is still a plain malloc block (its header
        // only names the class), so it goes straight back like a passthrough one.
        if (g_free_cap != 0 && g_free_bytes + bytes > g_free_cap) { std::free(raw); return; }
        *static_cast<void**>(p) = g_free[cls];                // stash next-pointer IN the payload
        g_free[cls] = p;                                      // retain (list holds payload pointers)
        g_free_bytes += bytes;
    }
}

// ---- replaced global operator new/delete -------------------------------------------------
// Every block routes through PoolAlloc/PoolFree so the header is always present and delete always
// dispatches on it (see the header-invariant note at top). g_pool_on only selects size-classed vs
// passthrough INSIDE PoolAlloc; it is never a new/delete gate, so the two never disagree.

void* operator new(std::size_t n)
{
    void* p = PoolAlloc(n);
    if (!p) { throw std::bad_alloc(); }
    return p;
}
void* operator new[](std::size_t n) { return ::operator new(n); }

void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return PoolAlloc(n); }
void* operator new[](std::size_t n, const std::nothrow_t& t) noexcept { return ::operator new(n, t); }

void operator delete(void* p) noexcept { PoolFree(p); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }
