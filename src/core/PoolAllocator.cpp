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
// allocations are safe: a freed pooled block is retained on a free-list (reused, never returned to
// the OS -- footprint is the live high-water mark, measured ~81 MB at 12 threads, not cumulative).
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
        if (void* pl = g_free[cls])                           // free-list hit: pop a payload
        {
            g_free[cls] = *static_cast<void**>(pl);           // next-pointer lives in the payload
            return pl;                                        // header (before pl) still valid
        }
        const std::size_t block = static_cast<std::size_t>(cls) * kQuantum;
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
        *static_cast<void**>(p) = g_free[cls];                // stash next-pointer IN the payload
        g_free[cls] = p;                                      // retain (list holds payload pointers)
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
