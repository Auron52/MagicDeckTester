#include "Tier2Overflow.h"
#include "MemBudget.h"
#include <cstdlib>

namespace tier2
{
long long DefaultBudgetBytes()
{
    // MTG_TIER2_OVERFLOW_KB: explicit KB (0 = unbounded); unset = the RAM-derived slice.
    if (const char* e = std::getenv("MTG_TIER2_OVERFLOW_KB"); e != nullptr && *e != '\0')
    {
        const long long kb = std::strtoll(e, nullptr, 10);
        return kb > 0 ? (kb << 10) : 0;
    }
    return membudget::DefaultTier2OverflowKb() << 10;
}

Pool& GlobalPool()
{
    // Deliberately leaked: thread_local Overflow<> stores of exiting threads release into it, and the
    // RSS watchdog's reporter may read it while static destructors run.
    static Pool* p = new Pool(DefaultBudgetBytes());
    return *p;
}

static std::string Mb(long long b)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1fMB", static_cast<double>(b) / (1024.0 * 1024.0));
    return buf;
}

std::string Report(const Pool& p)
{
    long long offered = 0;
    for (int c = 0; c < kCats; ++c) { offered += p.cat[c].stores.load() + p.cat[c].refused.load(); }
    if (offered == 0) { return {}; }
    std::string s = "[tier2-overflow] on=" + std::to_string(On() ? 1 : 0)
                  + " budget=" + (p.budget > 0 ? Mb(p.budget) : std::string("unbounded"))
                  + " peak=" + Mb(p.peak.load());
    for (int c = 0; c < kCats; ++c)
    {
        const CatStats& k = p.cat[c];
        if (k.stores.load() + k.refused.load() + k.lookups.load() == 0) { continue; }
        s += std::string(" | ") + CatName(c) + ": stores=" + std::to_string(k.stores.load())
           + " refused=" + std::to_string(k.refused.load()) + " lookups=" + std::to_string(k.lookups.load())
           + " hits=" + std::to_string(k.hits.load()) + " ctx_misses=" + std::to_string(k.ctx_misses.load())
           + " dropped_at_game_end=" + std::to_string(k.dropped.load()) + " peak=" + Mb(k.peak.load());
    }
    return s;
}

namespace
{
// Exit summary, like [determinism] / [game-line-cache]: printed only when something was offered to tier 2.
struct Reporter
{
    ~Reporter()
    {
        const std::string s = Report(GlobalPool());
        if (!s.empty()) { std::fprintf(stderr, "%s\n", s.c_str()); }
    }
};
Reporter g_reporter;
// Heartbeat / RSS-cap token: "t2=used/budget".
[[maybe_unused]] const bool g_registered = []{
    membudget::RegisterMemReporter([]() -> std::string {
        const Pool& p = GlobalPool();
        if (p.peak.load(std::memory_order_relaxed) == 0) { return {}; }
        return "t2=" + Mb(p.used.load(std::memory_order_relaxed)) + "/"
             + (p.budget > 0 ? Mb(p.budget) : std::string("inf"));
    });
    return true; }();
}
}
