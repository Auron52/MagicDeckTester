#pragma once
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <ostream>
#include <string>
#include "core/EnvFlags.h"

// ---- GameProgress: predict a long-running game/rollout instead of guessing at it ----------------
//
// WHY THIS EXISTS. `SlowTracker` reports a rollout once it FINISHES, so a rollout still running is
// invisible to every instrument in the repo. On 2026-10-03 one keep rollout held the Fungus
// candidate-b floor-completion barrier for over TEN HOURS and the only observable facts were
// negative: `roll7 0/s`, `frozen 0/343538`, journal age climbing. There was no way to say whether it
// was one minute or one day from finishing, so no ETA for the generation could be given at all.
// USER: *"we really have no idea how long it will be, but with this feature we would have a very
// good idea ... Ideally we would want to know what turn they are processing and how far they are in
// the search. However, we only need this on something like a once per half-an-hour basis."*
//
// WHAT IT PUBLISHES. Each worker thread owns a slot while it is inside a tracked unit of work. The
// slot carries the unit's label (the same reproducing description SlowTracker streams), when it
// started, and the progress fields a prediction needs:
//   turn        -- which game turn the search is on. Against a known horizon (max_turns, 8 here)
//                  this alone is most of the answer: turn 3 after ten hours is very different news
//                  from turn 8.
//   decisions   -- budgeted decision roots entered this unit. The denominator is empirical (a
//                  healthy candidate-b rollout is ~112k enumeration calls over ~8 turns), so a
//                  count far past that says "this is not a long game, it is a stuck one".
//   enum_calls  -- EnumeratePlanPositions invocations, the search's real unit of work.
//   odo_bound   -- the CURRENT decision's whole-odometer size, which the enumerator already
//                  computes up front. This is the one true denominator available: it says how wide
//                  the decision being worked on actually is.
//
// COST. One branch plus two relaxed atomic stores per decision, and the same per enumeration call.
// Measured shapes put that at ~112k enumeration calls per rollout, i.e. a few hundred microseconds
// per rollout in total. The label is written once per unit under its own mutex and read only by the
// reporter thread. NOTHING here feeds a decision, a seed, an accumulator or a budget, so play stays
// byte-identical -- the same guarantee SlowTracker carries.
//
// NOT DISABLEABLE, deliberately, per docs/design/keepgen-no-off-switches.md: `MTG_LONG_RUN_REPORT_S`
// is a LOWER-ONLY override of the reporting interval, exactly like `MTG_KEEP_SLOW_MS`. A run that
// cannot report its own stuck work is the condition this exists to end.
namespace gameprogress
{

constexpr int kMaxSlots = 256;          // >> any worker count this repo runs (24-32)

struct Slot
{
    std::atomic<bool>               active{ false };
    std::atomic<long long>          start_ms{ 0 };
    std::atomic<int>                turn{ 0 };
    std::atomic<long long>          decisions{ 0 };
    std::atomic<long long>          enum_calls{ 0 };
    std::atomic<unsigned long long> odo_bound{ 0 };
    std::mutex                      label_mtx;
    std::string                     label;
};

inline Slot                g_slots[kMaxSlots];
inline thread_local Slot*  t_slot = nullptr;

inline long long NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Seconds between reports. Default 1800 (the user's "once per half-an-hour"); a LOWER value is
// honoured, 0/negative/higher cannot weaken it.
inline long long ReportS()
{
    static const long long v = []{
        const int raw = EnvInt("MTG_LONG_RUN_REPORT_S", 1800);
        if (raw <= 0) { return 1800LL; }
        return static_cast<long long>(raw) < 1800LL ? static_cast<long long>(raw) : 1800LL;
    }();
    return v;
}

// RAII around one tracked unit of work. `describe` is invoked ONCE, on entry -- cheap enough at
// rollout granularity, and it is what makes a report reproducible rather than merely alarming.
class Scope
{
public:
    template <typename Describe>
    explicit Scope(Describe&& describe)
    {
        for (int i = 0; i < kMaxSlots; ++i)
        {
            bool expect = false;
            if (g_slots[i].active.compare_exchange_strong(expect, true, std::memory_order_acq_rel))
            {
                m_slot = &g_slots[i];
                break;
            }
        }
        if (m_slot == nullptr) { return; }          // registry full -> silently untracked
        m_slot->start_ms.store(NowMs(), std::memory_order_relaxed);
        m_slot->turn.store(0, std::memory_order_relaxed);
        m_slot->decisions.store(0, std::memory_order_relaxed);
        m_slot->enum_calls.store(0, std::memory_order_relaxed);
        m_slot->odo_bound.store(0, std::memory_order_relaxed);
        { std::lock_guard<std::mutex> lk(m_slot->label_mtx); m_slot->label = describe(); }
        m_prev  = t_slot;
        t_slot  = m_slot;
    }
    ~Scope()
    {
        if (m_slot == nullptr) { return; }
        t_slot = m_prev;
        m_slot->active.store(false, std::memory_order_release);
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Slot* m_slot = nullptr;
    Slot* m_prev = nullptr;
};

// ---- hot-path publishers (one branch + relaxed stores; no-ops when untracked) -------------------

inline void NoteDecision(int turn_number)
{
    if (Slot* s = t_slot)
    {
        s->turn.store(turn_number, std::memory_order_relaxed);
        s->decisions.fetch_add(1, std::memory_order_relaxed);
    }
}

inline void NoteEnumerate(unsigned long long odometer_bound)
{
    if (Slot* s = t_slot)
    {
        s->odo_bound.store(odometer_bound, std::memory_order_relaxed);
        s->enum_calls.fetch_add(1, std::memory_order_relaxed);
    }
}

// ---- the reporter ------------------------------------------------------------------------------
// Prints every tracked unit running longer than `min_s`. Returns how many it printed, so a caller
// can stay silent when nothing is stuck. Safe to call from a monitor/heartbeat thread.
inline int Report(std::ostream& os, long long min_s, const char* tag = "[long-run]")
{
    const long long now = NowMs();
    int printed = 0;
    for (int i = 0; i < kMaxSlots; ++i)
    {
        Slot& s = g_slots[i];
        if (!s.active.load(std::memory_order_acquire)) { continue; }
        const long long started = s.start_ms.load(std::memory_order_relaxed);
        const double    el_s    = static_cast<double>(now - started) / 1000.0;
        if (started == 0 || el_s < static_cast<double>(min_s)) { continue; }
        std::string label;
        { std::lock_guard<std::mutex> lk(s.label_mtx); label = s.label; }
        char buf[256];
        std::snprintf(buf, sizeof buf,
                      "%s %.1fh  turn=%d  decisions=%lld  enum_calls=%lld  cur_odometer=%llu  ",
                      tag, el_s / 3600.0, s.turn.load(std::memory_order_relaxed),
                      s.decisions.load(std::memory_order_relaxed),
                      s.enum_calls.load(std::memory_order_relaxed),
                      s.odo_bound.load(std::memory_order_relaxed));
        os << buf << label << "\n";
        ++printed;
    }
    if (printed > 0) { os << std::flush; }
    return printed;
}

}  // namespace gameprogress
