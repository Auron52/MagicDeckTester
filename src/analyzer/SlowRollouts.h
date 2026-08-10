#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <ostream>
#include <string>
#include <vector>

// ---- SlowTracker: the always-on degenerate-rollout detector -------------------------------------
//
// Mulligan-table generation runs rollouts in FOUR phases -- equivalence discovery, the play-digest
// battery, the fused sub-table batches and the continuous pool -- and a single pathological cell in
// any of them can turn a gen from hours into days. This is the shared instrument that makes that
// visible in EVERY phase, with no way to turn it off (docs/design/keepgen-no-off-switches.md).
//
// Two outputs:
//   * a LIVE stream (stderr + an optional append-only log file) for any rollout over the threshold,
//     carrying the seed/description that reproduces it -- this is what survives a kill;
//   * the N slowest seen so far, dumped at every heartbeat, at floor-complete and at end-of-run.
//
// Cost is one steady_clock read per rollout plus a relaxed atomic compare. The lock is taken only
// when a rollout beats the current N-th-slowest cutoff (rare after warm-up) or trips the stream
// threshold, so there is no per-rollout contention. Nothing here feeds a decision, a seed or an
// accumulator, so results stay byte-identical.
class SlowTracker
{
public:
    struct Rec { double ms; std::string what; };
    static constexpr std::size_t kTopN = 12;

    // Baked stream threshold. A rollout over this is degenerate on any deck this repo runs (healthy
    // ones are ~0.3-2 s even on heavy decks), so a quiet stream means a healthy gen.
    static constexpr long long kStreamMs = 30000;

    // MTG_KEEP_SLOW_MS is a LOWER-ONLY override, clamped into [1, kStreamMs]. 0, negative and
    // above-default values cannot disable or weaken the stream -- they leave the baked default.
    // ("=0 disables" is exactly how the Creature Giving scout ended up with no slow list at all.)
    static long long StreamMs()
    {
        static const long long v = []{
            const char* s = std::getenv("MTG_KEEP_SLOW_MS");
            if (!s || !*s) { return kStreamMs; }
            const long long raw = std::atoll(s);
            if (raw <= 0) { return kStreamMs; }
            return std::min<long long>(raw, kStreamMs);
        }();
        return v;
    }

    // Append-only sidecar for streamed lines, so an interrupted run leaves its evidence on disk.
    void SetLog(const std::string& path)
    { std::lock_guard<std::mutex> lk(m_mtx); m_log_path = path; }

    // `describe` is invoked only on the slow path, so building the description is free in the common
    // case. `tag` names the phase, so a stream line says WHERE the slow rollout happened.
    template <typename F>
    void Observe(double ms, const char* tag, F&& describe)
    {
        const bool topn   = ms > m_gate.load(std::memory_order_relaxed);
        const bool stream = ms >= StreamMs();
        if (!topn && !stream) { return; }        // fast path: the vast majority of rollouts
        const std::string what = describe();
        // The stderr write stays INSIDE the lock. Two workers streaming concurrently otherwise interleave
        // mid-line and the reader loses rollouts (measured: 6365 readable stderr lines for 6368 logged
        // ones). This is the rare path by construction, so serialising it costs nothing.
        std::lock_guard<std::mutex> lk(m_mtx);
        if (topn)
        {
            m_top.push_back({ ms, std::string(tag) + "  " + what });
            std::sort(m_top.begin(), m_top.end(),
                      [](const Rec& a, const Rec& b) { return a.ms > b.ms; });
            if (m_top.size() > kTopN) { m_top.resize(kTopN); }
            m_gate.store(m_top.size() >= kTopN ? m_top.back().ms : 0.0, std::memory_order_relaxed);
        }
        if (stream)
        {
            const std::string line = "[keepgen] SLOW-ROLLOUT " + std::to_string(static_cast<long long>(ms))
                                   + "ms  " + tag + "  " + what;
            if (!m_log_path.empty())
            { std::ofstream f(m_log_path, std::ios::app); if (f) { f << line << "\n"; } }
            std::cerr << line << "\n" << std::flush;
        }
    }

    // Dump the current top-N. Called from every heartbeat, at floor-complete and at end-of-run, so a
    // degenerate cell is visible whenever the operator looks -- not only if the run reaches the one
    // point that used to print it.
    void Dump(std::ostream& os, const char* when, std::size_t limit = kTopN)
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        if (m_top.empty()) { return; }
        const std::size_t n = std::min(limit, m_top.size());
        os << "  slowest rollouts " << when << " (top " << n << "; watch for degenerate cells):\n";
        const std::ios::fmtflags flags = os.flags();
        const std::streamsize    prec  = os.precision();
        os << std::fixed << std::setprecision(0);
        for (std::size_t i = 0; i < n; ++i)
        { os << "    " << m_top[i].ms << "ms  " << m_top[i].what << "\n"; }
        os.flags(flags); os.precision(prec);
        os << std::flush;
    }

private:
    std::mutex          m_mtx;
    std::vector<Rec>    m_top;
    std::atomic<double> m_gate{ 0.0 };
    std::string         m_log_path;
};

// ONE tracker per process. Discovery and the play-digest battery run before the generator's config
// exists, and all four phases answer the same question ("what is slow in this deck?"), so they share
// a single top-N rather than each keeping a list nobody prints.
inline SlowTracker& Slow() { static SlowTracker s; return s; }

// Time a rollout and report it. Returns whatever the callable returns.
template <typename F, typename D>
auto TimedRollout(const char* tag, D&& describe, F&& fn) -> decltype(fn())
{
    const auto t0 = std::chrono::steady_clock::now();
    auto       r  = fn();
    Slow().Observe(std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - t0).count(),
                   tag, std::forward<D>(describe));
    return r;
}
