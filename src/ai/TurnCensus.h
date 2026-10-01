#pragma once

// PER-DECISION WORK CENSUS -- MTG_TURN_CENSUS=<path>. Unset = OFF = byte-identical.
//
// WHY. Every work instrument in this engine is a WHOLE-RUN aggregate: `[rollout-stats]` prints one
// number per counter for the entire process, and `cost.py` divides totals by games. That answers
// "how much" and can never answer "WHERE" -- which turns, on which boards, and whether the caching
// and cost-pruning machinery actually engaged on the turns that cost the most. A deck whose mean
// turn is cheap and whose 99th-percentile turn is 10,000x the mean reads, in the aggregate, exactly
// like a deck that is uniformly mediocre; the abandonment-rate finding (memory
// matrix-cost-is-abandonment-rate: ~90% of a Snow cell's wall is games pinned at the ceiling) says
// this engine is emphatically the first kind. Sizing a lever against a mean is therefore sizing it
// against a number no real decision ever had.
//
// USER 2026-09-30: *"it's crucial that we look at real turns... a list of something like 1000
// slowest turns in terms of branching and how our caching/cost pruning deals with them... we need
// to make sure we are doing only the work that is necessary."*
//
// WHAT. One row per REAL-PLAY decision root (the same two roots DecisionWorkMeter arms at -- see
// SolveWithLookahead's `decision_root` and FullSearchLine's `fs_decision_root`), carrying the
// DELTA of every work/cache/prune/condemnation counter across that one decision, plus the
// deterministic unit cost and the wall time. Sort by units and the tail IS the list of slowest
// turns, each one already annotated with what the memo, the pruner and the condemner did to it.
//
// WHY SNAPSHOT-AND-DIFF RATHER THAN NEW PER-TURN COUNTERS. The 124 counters this file diffs are
// the SAME objects `[rollout-stats]` prints, read through one generated table (see
// MTG_TURN_CENSUS_FIELDS in TurnSolver.cpp). A parallel set of thread-local per-turn counters
// would need an increment beside every existing one and would drift from them on the first edit
// that adds a counter to only one of the two sets -- the cross-tab-keyed-on-a-struct-field defect
// from the snow work, in a new place. Diffing the originals cannot drift by construction: a
// counter added to the aggregate appears here as soon as it is added to the field table, and one
// that is never bumped reads as a hard zero rather than as a plausible-looking number.
//
// THE PRICE OF THAT CHOICE, AND HOW IT IS ENFORCED RATHER THAN DOCUMENTED. Those counters are
// PROCESS-GLOBAL atomics, so a delta is only attributable to one decision if no other thread is
// playing a game at the same time -- i.e. the census is a SINGLE-THREADED instrument. That is not
// left to a note in a skill file, because "run it with one worker" is exactly the kind of caveat
// that survives one session and then quietly produces a beautifully-formatted table of garbage.
// Instead every row carries a `contended` column: the outermost scope bumps a global live count,
// and any row written while a second decision was in flight ANYWHERE in the process is stamped
// contended=1 and a one-time warning goes to stderr. A contaminated run therefore reports itself
// in its own output, and the analyzer refuses to rank contended rows.
//
// Per-game wall clock is unaffected by thread count, but the census's own cost is not free: it
// forces MTG_ROLLOUT_STATS (below), which is the documented counter gate. Rank by `units`, which
// is deterministic and instrumentation-independent; `wall_us` is a secondary read and is only
// comparable within one single-threaded run.
//
// MTG_TURN_CENSUS_MIN_UNITS=<n> emits only decisions at or above n units (default 0 = every
// decision). The point of the tool is the tail, and a full overnight tier would otherwise write
// tens of millions of rows to say so.
//
// NESTING. The two decision roots are mutually exclusive per decision (FullSearchLine's host is
// reached straight from TakeTurn, bypassing SolveWithLookahead), but that is a property of the
// call graph rather than of this file, so only the OUTERMOST census scope on a thread emits.
// A nested scope is inert -- it must not write a row whose deltas its parent will also claim.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "../core/EnvFlags.h"
#include "DecisionWorkMeter.h"

namespace turncensus
{

inline const char* PathOrNull()
{
    static const char* p = EnvPath("MTG_TURN_CENSUS");
    return p;
}

inline bool On() { return PathOrNull() != nullptr; }

inline long long MinUnits()
{
    static const long long n = EnvInt("MTG_TURN_CENSUS_MIN_UNITS", 0);
    return n;
}

// ---- WHO ASKED (the `site` column) ------------------------------------------------------------
// The `root` column names the HOST (1 = SolveWithLookahead, 2 = FullSearchLineHybrid). That is NOT
// the same question as who asked for the search, and a turn routinely holds SEVERAL roots: the
// executor's own decision, plus one full searched RE-SOLVE per unsearched breakpoint continuation
// (AIEngine.cpp's resolve_draw_breakpoint, "a COMMITTED line's unsearched breakpoint continuation
// is a REAL DECISION"), plus the mode-2 post-draw re-solve, plus the pod trailing-pass twin. Every
// one of those is handed a BRAND-NEW `DecisionBudget()`, so the per-decision budget is a per-ROOT
// allowance and a turn's true cost is the sum over roots -- which is exactly why a census keyed on
// the host cannot apportion it. (Same error class as reading a frame census as a volume census:
// memory two-hosts-not-one-measure-where-the-applies-are.)
//
// Default is kSiteMain, so an UNTAGGED root reads as the executor's own decision; only the
// re-solve sites carry a tag. That way a new re-solve call site added without a tag shows up as
// main-decision volume on a turn that has too many of them, rather than as a silent zero.
enum : int
{
    kSiteMain      = 1,   // the executor's own decision for this phase
    kSiteBpResolve = 2,   // unsearched breakpoint continuation, re-solved at deck settings
    kSiteM2        = 3,   // mode-2 post-draw re-solve (non-committed play only)
    kSitePodBp     = 4    // pod trailing-pass breakpoint twin
};
inline thread_local int t_site = kSiteMain;

// RAII tag. Unconditional (not gated on On()): it is one thread-local int per ROOT solve, i.e. a
// decision-scale write, and gating it would make the census's own column depend on a flag read.
struct SiteTag
{
    int prev;
    explicit SiteTag(int s) : prev(t_site) { t_site = s; }
    ~SiteTag() { t_site = prev; }
};

// ---- IS THIS THE GAME WE ARE PLAYING? (the `probe` column) -----------------------------------
// A SECOND, ORTHOGONAL axis, and the one that turned out to matter most on Snow. Some decisions
// are not made for the game in progress at all: `AIEngine::RolloutWinTurnFrom` plays a COMPLETE
// trial game to LABEL a decision -- one per legal bottoming subset (up to 35), the keep oracle, the
// Land's Edge probe. Those trial games play at the deck's real depth and budget unless the profile
// ships a `bottom_eval_*` override, so each one re-pays the whole game's search cost, and every
// root inside them lands in the census indistinguishable from real play (they are real roots:
// `m_in_rollout` is the EXECUTOR's flag, while the census root predicate reads TurnSolver's
// `g_rollout_nest`, which is 0 inside a trial game).
//
// A DEPTH COUNT, not a flag, and deliberately NOT folded into `site`: the two questions are
// independent -- "who asked for this root" and "is this root for the game being played" -- and a
// single column would have to pick one, which is how a probe game's roots came to read as ordinary
// main-phase volume in the first place.
inline thread_local int t_probe = 0;

struct ProbeTag
{
    ProbeTag()  { ++t_probe; }
    ~ProbeTag() { --t_probe; }
};

// Defined in TurnSolver.cpp, where the counters are file-static. Both come from ONE X-macro table
// (MTG_TURN_CENSUS_FIELDS) so a column's name and the counter it reads cannot disagree.
const std::vector<const char*>& FieldNames();
void                            FillCounters(std::vector<long long>& out);

// One line naming which optional counter families are LIVE on this run, written into the file as a
// `#` comment above the header. A census column whose gate is off reads as a hard 0, and a table of
// zeros is indistinguishable from "this never happens" -- so the file states its own instrument
// state rather than relying on the operator's memory of which flags were exported. The analyzer
// reads this line and refuses to draw a conclusion from a dark family.
const std::string& GateNote();

// Cross-thread contamination detector. See the header note: a contended row is not discarded (it
// is still true that the decision happened and cost that many units) but its counter deltas are
// not attributable, so it is stamped and the analyzer drops it from the ranking.
inline std::atomic<int>  g_live{0};
inline std::atomic<bool> g_contended_ever{false};
inline thread_local int  t_nest = 0;

// One row. Identity columns first (they are what you feed back to `--seed` to replay the turn),
// then the counter deltas in FieldNames() order.
inline void WriteRow(unsigned long long seed, int turn, int pre_combat, int root_kind, int depth,
                     long long budget_units, long long units, long long wall_us, int contended,
                     int site, int probe, unsigned long long skey,
                     const std::vector<long long>& deltas)
{
    static std::mutex mu;
    std::lock_guard<std::mutex> lk(mu);
    static std::FILE* f = []() -> std::FILE* {
        std::FILE* h = std::fopen(PathOrNull(), "w");
        if (h == nullptr)
        {
            std::fprintf(stderr, "[turn-census] CANNOT OPEN %s -- census disabled for this run\n",
                         PathOrNull());
            return nullptr;
        }
        std::fprintf(h, "# %s\n", GateNote().c_str());
        std::fprintf(h, "seed\tturn\tpre\troot\tdepth\tbudget\tunits\twall_us\tcontended"
                        "\tsite\tprobe\tskey");
        for (const char* n : FieldNames()) { std::fprintf(h, "\t%s", n); }
        std::fprintf(h, "\n");
        return h;
    }();
    if (f == nullptr) { return; }
    std::fprintf(f, "%llu\t%d\t%d\t%d\t%d\t%lld\t%lld\t%lld\t%d\t%d\t%d\t%llu",
                 seed, turn, pre_combat, root_kind, depth, budget_units, units, wall_us, contended,
                 site, probe, skey);
    for (long long d : deltas) { std::fprintf(f, "\t%lld", d); }
    std::fprintf(f, "\n");
    // Flushed per row: a census run is routinely killed once its tail is interesting enough, and a
    // buffered last-few-thousand rows is exactly the part worth having. Rows are decision-scale
    // (milliseconds apart at minimum), so the fflush is not on any hot path.
    std::fflush(f);
}

// RAII at a decision root. Snapshots on construction, writes the delta row on destruction.
struct Scope
{
    bool                                  active = false;   // this scope owns the row
    bool                                  counted = false;  // this scope bumped t_nest
    unsigned long long                    seed   = 0;
    int                                   turn = 0, pre = 0, root = 0, depth = 0;
    long long                             budget = 0;
    long long                             units0 = 0;
    std::chrono::steady_clock::time_point t0;
    int                                   contended = 0;
    int                                   site = kSiteMain;
    int                                   probe = 0;
    unsigned long long                    skey = 0;

    Scope(bool is_root, unsigned long long seed_, int turn_, int pre_combat, int root_kind,
          int depth_, long long budget_units, unsigned long long state_key = 0)
    {
        if (!is_root || !On()) { return; }
        counted = true;
        if (t_nest++ != 0) { return; }   // nested: parent owns this decision's row
        active = true;
        seed = seed_; turn = turn_; pre = pre_combat; root = root_kind; depth = depth_;
        budget = budget_units;
        site = t_site; probe = t_probe > 0 ? 1 : 0; skey = state_key;
        units0 = decisionwork::t_used;
        if (g_live.fetch_add(1, std::memory_order_acq_rel) != 0)
        {
            contended = 1;
            if (!g_contended_ever.exchange(true))
            {
                std::fprintf(stderr,
                             "[turn-census] *** CONTENDED: a second decision was in flight while"
                             " this one ran. The counter columns are PROCESS-GLOBAL, so on those"
                             " rows they are NOT this decision's work. Re-run single-threaded"
                             " (one batch worker / one goldfish thread). Affected rows carry"
                             " contended=1. ***\n");
            }
        }
        t0 = std::chrono::steady_clock::now();
        FillCounters(scratch());
    }

    ~Scope()
    {
        if (!active) { if (counted) { --t_nest; } return; }
        const long long wall_us =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - t0).count();
        const long long units = decisionwork::t_used - units0;
        // Read the live counters BEFORE releasing the live count, so a row that was clean stays
        // clean: another thread starting between the read and the decrement cannot contaminate a
        // delta that is already computed.
        std::vector<long long> now;
        FillCounters(now);
        if (g_live.load(std::memory_order_acquire) != 1) { contended = 1; }
        g_live.fetch_sub(1, std::memory_order_acq_rel);
        --t_nest;
        if (units < MinUnits()) { return; }
        std::vector<long long>& before = scratch();
        const std::size_t n = now.size() < before.size() ? now.size() : before.size();
        std::vector<long long> deltas(n, 0);
        for (std::size_t i = 0; i < n; ++i) { deltas[i] = now[i] - before[i]; }
        WriteRow(seed, turn, pre, root, depth, budget, units, wall_us, contended, site, probe,
                 skey, deltas);
    }

private:
    // One buffer per thread, reused: the snapshot is taken at every decision root and a fresh
    // allocation there would be the census paying for itself in the units it is trying to measure.
    static std::vector<long long>& scratch()
    {
        static thread_local std::vector<long long> v;
        return v;
    }
};

}   // namespace turncensus
