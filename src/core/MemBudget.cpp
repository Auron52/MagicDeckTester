#include "MemBudget.h"
#include "HardwareConcurrency.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#elif defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#if defined(_MSC_VER)
#pragma comment(lib, "psapi.lib")   // GetProcessMemoryInfo (K32 alias on modern SDKs; harmless)
#endif
#endif

namespace membudget
{
namespace
{
    long long ReadMemTotalMb()
    {
#if defined(__linux__)
        if (std::FILE* f = std::fopen("/proc/meminfo", "r"))
        {
            char line[256];
            long long kb = 0;
            while (std::fgets(line, sizeof(line), f))
            {
                if (std::sscanf(line, "MemTotal: %lld kB", &kb) == 1) { break; }
                kb = 0;
            }
            std::fclose(f);
            return kb / 1024;
        }
        return 0;
#elif defined(_WIN32)
        MEMORYSTATUSEX st;
        st.dwLength = sizeof(st);
        if (GlobalMemoryStatusEx(&st))
        { return static_cast<long long>(st.ullTotalPhys / (1024ULL * 1024ULL)); }
        return 0;
#else
        return 0;
#endif
    }

    // cache_mb per the header's derivation (0 when the budget is unbounded/unknown).
    long long CacheMb()
    {
        static const long long v = []{
            const long long budget = BudgetMb();
            if (budget <= 0) { return 0LL; }
            const long long reserve = static_cast<long long>(Workers()) * 250LL;
            long long c = budget - reserve;
            if (c < 2048) { c = 2048; }
            return c;
        }();
        return v;
    }

    // Deliberately LEAKED (heap, never destroyed): the watchdog is a detached thread that may be
    // inside MemReport() while main() returns and static destructors run. A function-local static
    // would be destroyed under it; a leaked object outlives the process.
    std::mutex& ReportMutex() { static std::mutex* m = new std::mutex; return *m; }
    std::vector<std::function<std::string()>>& Reporters()
    { static auto* v = new std::vector<std::function<std::string()>>; return *v; }
    std::function<std::string()>& CapContext()
    { static auto* f = new std::function<std::string()>; return *f; }

    std::string Gb(long long bytes)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2fG", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
        return buf;
    }
}

long long MemTotalMb()
{
    static const long long v = ReadMemTotalMb();
    return v;
}

int Workers()
{
    static const int v = concurrency_util::AffinityCpuCount();
    return v;
}

long long BudgetMb()
{
    static const long long v = []{
        if (const char* e = std::getenv("MTG_MEM_BUDGET_MB"))
        { return static_cast<long long>(std::strtoll(e, nullptr, 10)); }
        return MemTotalMb() / 2;
    }();
    return v;
}

std::size_t DefaultTtCapEntries()
{
    const long long c = CacheMb();
    if (c <= 0) { return 0; }
    const long long pw_kb = c * 1024LL / Workers();
    return static_cast<std::size_t>(pw_kb * 1024LL / 3 / 64);
}

long long DefaultFslPoolKb()
{
    const long long c = CacheMb();
    return c <= 0 ? 0LL : c * 1024LL * 2 / 5;
}

std::size_t DefaultFslCapEntries()
{
    return CacheMb() <= 0 ? std::size_t{0} : std::size_t{2000000};
}

long long DefaultPlanCacheKb()
{
    return CacheMb() <= 0 ? 0LL : static_cast<long long>(Workers()) * 102400LL;
}

long long CurrentRssBytes()
{
#if defined(__linux__)
    if (std::FILE* f = std::fopen("/proc/self/statm", "r"))
    {
        long long size = 0, resident = 0;
        const int n = std::fscanf(f, "%lld %lld", &size, &resident);
        std::fclose(f);
        if (n == 2) { return resident * static_cast<long long>(sysconf(_SC_PAGESIZE)); }
    }
    return 0;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
    { return static_cast<long long>(pmc.WorkingSetSize); }
    return 0;
#else
    return 0;
#endif
}

long long RssCapBytes()
{
    static const long long v = []{
        const double gb = [] {
            if (const char* e = std::getenv("MTG_RSS_CAP_GB")) { return std::strtod(e, nullptr); }
            return static_cast<double>(MemTotalMb()) * 0.75 / 1024.0;
        }();
        if (gb <= 0.0) { return 0LL; }
        return static_cast<long long>(gb * 1024.0 * 1024.0 * 1024.0);
    }();
    return v;
}

std::string Describe()
{
    char buf[384];
    std::snprintf(buf, sizeof(buf),
                  "mem: total=%lldMB budget=%lldMB workers=%d -> tt_cap=%zu/table fsl_pool=%lldMB"
                  " fsl_cap=%zu plan_cache=%lldMB rss_cap=%s (MTG_MEM_BUDGET_MB / MTG_RSS_CAP_GB;"
                  " each pool's own env var overrides)",
                  MemTotalMb(), BudgetMb(), Workers(), DefaultTtCapEntries(),
                  DefaultFslPoolKb() / 1024, DefaultFslCapEntries(), DefaultPlanCacheKb() / 1024,
                  RssCapBytes() > 0 ? Gb(RssCapBytes()).c_str() : "off");
    return buf;
}

void RegisterMemReporter(std::function<std::string()> f)
{
    std::lock_guard<std::mutex> lk(ReportMutex());
    Reporters().push_back(std::move(f));
}

std::string MemReport()
{
    std::string out;
    std::lock_guard<std::mutex> lk(ReportMutex());
    for (const auto& r : Reporters())
    {
        const std::string s = r();
        if (s.empty()) { continue; }
        if (!out.empty()) { out += ' '; }
        out += s;
    }
    return out;
}

void SetCapContext(std::function<std::string()> f)
{
    std::lock_guard<std::mutex> lk(ReportMutex());
    CapContext() = std::move(f);
}

void StartRssWatchdog(const char* tag)
{
    static std::once_flag once;
    std::call_once(once, [tag] {
        const long long cap = RssCapBytes();
        if (cap <= 0 || CurrentRssBytes() <= 0) { return; }   // off, or unreadable here
        const std::string name = tag ? tag : "mtg";
        std::thread([cap, name] {
            bool warned = false;
            for (;;)
            {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                const long long rss = CurrentRssBytes();
                if (rss <= 0) { continue; }
                if (!warned && rss * 4 >= cap * 3)
                {
                    warned = true;
                    std::fprintf(stderr, "[rss-cap] %s: rss=%s is past 3/4 of the cap %s (%s)\n",
                                 name.c_str(), Gb(rss).c_str(), Gb(cap).c_str(), MemReport().c_str());
                    std::fflush(stderr);
                }
                if (rss < cap) { continue; }
                std::fflush(stdout);
                std::string ctx;
                {
                    std::lock_guard<std::mutex> lk(ReportMutex());
                    if (CapContext()) { ctx = CapContext()(); }
                }
                std::fprintf(stderr,
                             "[rss-cap] %s: rss=%s EXCEEDS the cap %s (MTG_RSS_CAP_GB) -- aborting this"
                             " process so the box survives; results already streamed are on disk."
                             " pools: %s\n%s",
                             name.c_str(), Gb(rss).c_str(), Gb(cap).c_str(), MemReport().c_str(),
                             ctx.c_str());
                std::fflush(stderr);
                std::_Exit(137);
            }
        }).detach();
    });
}
}
