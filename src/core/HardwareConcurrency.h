#pragma once
// Worker-thread count resolution shared by every parallel runner (batch, goldfish,
// analyzer, divergence diagnostic).
//
// std::thread::hardware_concurrency() reads the number of online CPUs at the
// instant it is called. In WSL2 / dev containers that count can be transiently
// low at process launch (we observed it return 3 on a 24-CPU box, crippling an
// overnight regression to 3 of 24 cores), even though the process affinity mask
// already permits all CPUs. sched_getaffinity reports the CPUs this process may
// run on -- the same value `nproc` honors -- and is stable across the run, so we
// prefer it and fall back to hardware_concurrency only when it is unavailable.

#include <ostream>
#include <string>
#include <thread>

#if defined(__linux__)
#include <sched.h>
#include <unistd.h>
#endif

namespace concurrency_util
{

// Number of CPUs this process may actually run on (>= 1). Affinity-based on Linux
// (stable; matches `nproc`), hardware_concurrency elsewhere, 1 as last resort.
inline int AffinityCpuCount()
{
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) == 0)
    {
        int n = CPU_COUNT(&set);
        if (n >= 1) { return n; }
    }
#endif
    int hw = static_cast<int>(std::thread::hardware_concurrency());
    return hw >= 1 ? hw : 1;
}

// Resolve a worker-thread count from a requested value. requested <= 0 means
// "auto" -> AffinityCpuCount(); a positive request is honored verbatim.
inline int ResolveWorkerThreads(int requested)
{
    return requested > 0 ? requested : AffinityCpuCount();
}

// One-line visibility into how the worker count was resolved, so an
// under-parallelized run can never hide again. Example:
//   [batch] workers=24 (hw_concurrency=3, affinity=24, requested=auto)
inline void LogWorkerThreads(std::ostream& os, const char* tag, int requested, int resolved)
{
    os << "[" << tag << "] workers=" << resolved
       << " (hw_concurrency=" << std::thread::hardware_concurrency()
       << ", affinity=" << AffinityCpuCount()
       << ", requested=" << (requested > 0 ? std::to_string(requested) : std::string("auto"))
       << ")\n";
}

} // namespace concurrency_util
