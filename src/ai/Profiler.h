#pragma once

// Deterministic, low-distortion hot-path instrumentation.
//
// Enabled only when MTG_PROFILE is defined (cmake -DMTG_PROFILE=ON). In a normal
// build every PROF_* macro expands to nothing, so the engine pays ZERO overhead
// and the repo stays pristine — this header exists purely for the profiling build.
//
// Why counters rather than a sampling profiler: the search is fully deterministic
// (deterministic SearchBudget, fixed seeds), so call/clone/node COUNTS are exactly
// reproducible run to run — no sampling noise, no symbol/elevation setup. The
// counts are bumped once per coarse event (a GameState clone, a SimulateToEnd
// call, an EnumeratePlans call), never in the innermost per-turn-step loop, so the
// instrumentation cost is negligible next to the work it measures (a counter ++
// sits beside a full ~60-card GameState deep copy).
//
// Threading: each worker accumulates into thread_local storage with no atomics or
// locks on the hot path; FlushThread() folds a worker's totals into the global
// aggregate once, at the end of its lambda. Per-game cost is recorded under a
// mutex once per game (not per decision), which is far off the hot path.

#ifdef MTG_PROFILE

#include <cstdint>
#include <mutex>
#include <vector>
#include <ostream>
#include <algorithm>
#include <iomanip>

namespace prof
{
    struct Counters
    {
        uint64_t gamestate_copies = 0;  // explicit GameState deep clones in the search
        uint64_t tt_lookups       = 0;  // TranspositionTable::Lookup calls
        uint64_t tt_hits          = 0;  // ... that hit
        uint64_t tt_stores        = 0;  // TranspositionTable::Store calls (WINS ONLY -- see SimulateToEnd)
        uint64_t tt_nowin         = 0;  // ... rollouts that returned no-win and were therefore NOT stored
        uint64_t tt_nowin_stored  = 0;  // ... no-wins stored under MTG_TT_NOWIN_CACHE (nothing truncated)
        uint64_t tt_nowin_hit     = 0;  // bound-qualified no-win entries that answered a query
        uint64_t enumerate_calls  = 0;  // EnumeratePlans invocations
        uint64_t plans_generated  = 0;  // candidate plans produced by EnumeratePlans
        uint64_t applyplan_calls  = 0;  // ApplyPlanDirect invocations
        uint64_t search_nodes     = 0;  // rollout turn-steps (budget units) consumed

        // FSLineCache / bound-qualified no-win memo (see FSLineEntry in TurnSolver.cpp). These
        // answer the question a wall-clock A/B cannot: under a BOUNDED play budget, does the memo
        // ever get to store anything, or does the truncation guard suppress every store?
        uint64_t fsline_lookups      = 0;  // FSLineWin interior-node memo probes
        uint64_t fsline_win_hit      = 0;  // ... returned a stored WIN
        uint64_t fsline_nowin_hit    = 0;  // ... returned a stored NO-WIN (the new saving)
        uint64_t fsline_nowin_stale  = 0;  // ... found a NO-WIN whose bound was too narrow -> re-search
        uint64_t fsline_nowin_result = 0;  // nodes that finished with no win (store candidates)
        uint64_t fsline_nowin_stored = 0;  // ... that were actually stored (nothing truncated below)

        // WHERE does the tree actually branch? Node entries bucketed by the game turn and by the
        // remaining search depth, plus the candidate count each node loops over. "1.35M nodes" says
        // nothing about shape; these say which turn and which ply hold the width.
        uint64_t fsw_by_turn[12]  = {0};   // FSLineWin entries, indexed by state.turn_number
        uint64_t fsw_by_depth[12] = {0};   // ... indexed by remaining depth
        uint64_t fsw_nodes        = 0;     // nodes that reached the candidate loop
        uint64_t fsw_cands        = 0;     // candidates those nodes looped over (=> mean branching)

        void Add(const Counters& o)
        {
            gamestate_copies += o.gamestate_copies;
            tt_lookups       += o.tt_lookups;
            tt_hits          += o.tt_hits;
            tt_stores        += o.tt_stores;
            tt_nowin         += o.tt_nowin;
            tt_nowin_stored  += o.tt_nowin_stored;
            tt_nowin_hit     += o.tt_nowin_hit;
            enumerate_calls  += o.enumerate_calls;
            plans_generated  += o.plans_generated;
            applyplan_calls  += o.applyplan_calls;
            search_nodes     += o.search_nodes;
            fsline_lookups      += o.fsline_lookups;
            fsline_win_hit      += o.fsline_win_hit;
            fsline_nowin_hit    += o.fsline_nowin_hit;
            fsline_nowin_stale  += o.fsline_nowin_stale;
            fsline_nowin_result += o.fsline_nowin_result;
            fsline_nowin_stored += o.fsline_nowin_stored;
            for (int i = 0; i < 12; ++i) { fsw_by_turn[i] += o.fsw_by_turn[i]; fsw_by_depth[i] += o.fsw_by_depth[i]; }
            fsw_nodes += o.fsw_nodes;
            fsw_cands += o.fsw_cands;
        }
    };

    struct GamePerf
    {
        int      index  = 0;
        uint64_t nodes  = 0;
        double   millis = 0.0;
    };

    // Per-DECISION cost. A game's total nodes says nothing about WHERE they went; on a
    // heavy-tailed deck a single main-phase decision can hold the entire cost of a game
    // (treasure_hunt seed 9010 gi 1). Recording (turn, nodes) once per top-level decision
    // is off the hot path and turns "this game is slow" into "this turn's decision is slow".
    struct DecisionPerf
    {
        int      turn  = 0;
        int      pre_combat = 0;
        uint64_t nodes = 0;
    };

    // Per-thread hot-path accumulators (no synchronisation).
    inline thread_local Counters t_counters;
    inline thread_local uint64_t t_game_nodes = 0;  // nodes for the in-flight game

    // Global aggregate, written once per thread (FlushThread) / once per game.
    inline std::mutex                g_mutex;
    inline Counters                  g_total;
    inline std::vector<GamePerf>     g_games;
    inline std::vector<DecisionPerf> g_decisions;

    inline Counters& Thread() { return t_counters; }

    inline void AddGameNodes(uint64_t n) { t_counters.search_nodes += n; t_game_nodes += n; }
    inline void ResetGame()              { t_game_nodes = 0; }

    inline void RecordDecision(int turn, bool pre_combat, uint64_t nodes)
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_decisions.push_back(DecisionPerf{turn, pre_combat ? 1 : 0, nodes});
    }

    inline void RecordGame(int index, double millis)
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_games.push_back(GamePerf{index, t_game_nodes, millis});
    }

    inline void FlushThread()
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_total.Add(t_counters);
        t_counters = Counters{};
    }

    inline void Report(std::ostream& os)
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        const Counters& c = g_total;
        os << "\n=== PROFILE (deterministic counters) ===\n";
        os << "GameState deep copies : " << c.gamestate_copies << "\n";
        os << "Search nodes (steps)  : " << c.search_nodes << "\n";
        os << "EnumeratePlans calls  : " << c.enumerate_calls
           << "   plans: " << c.plans_generated;
        if (c.enumerate_calls) { os << "   (" << (double)c.plans_generated / c.enumerate_calls << " plans/call)"; }
        os << "\n";
        os << "ApplyPlanDirect calls : " << c.applyplan_calls << "\n";
        os << "TT lookups            : " << c.tt_lookups
           << "   hits: " << c.tt_hits;
        if (c.tt_lookups) { os << "   (" << (100.0 * c.tt_hits / c.tt_lookups) << "% hit)"; }
        os << "\n";
        os << "TT nowin stored       : " << c.tt_nowin_stored
           << "   nowin hits: " << c.tt_nowin_hit << "\n";
        os << "TT stores             : " << c.tt_stores
           << "   nowin NOT stored: " << c.tt_nowin;
        if (c.tt_lookups) { os << "   (" << (100.0 * c.tt_stores / c.tt_lookups) << "% of lookups stored)"; }
        os << "\n";
        if (c.search_nodes)
        {
            os << "copies / node         : " << (double)c.gamestate_copies / c.search_nodes << "\n";
        }
        if (c.fsline_lookups)
        {
            os << "FSLine memo probes    : " << c.fsline_lookups
               << "   win hits: " << c.fsline_win_hit
               << "   nowin hits: " << c.fsline_nowin_hit
               << "   nowin stale: " << c.fsline_nowin_stale << "\n";
            os << "FSLine nowin results  : " << c.fsline_nowin_result
               << "   stored: " << c.fsline_nowin_stored;
            if (c.fsline_nowin_result)
            {
                os << "   (" << (100.0 * c.fsline_nowin_stored / c.fsline_nowin_result)
                   << "% stored, rest suppressed by truncation)";
            }
            os << "\n";
        }

        if (c.fsw_nodes)
        {
            os << "\n--- search tree shape ---\n";
            os << "FSLineWin nodes       : " << c.fsw_nodes
               << "   candidates: " << c.fsw_cands
               << "   (" << (double)c.fsw_cands / c.fsw_nodes << " branching)\n";
            os << "by game turn   :";
            for (int i = 0; i < 12; ++i) { if (c.fsw_by_turn[i]) { os << "  t" << i << "=" << c.fsw_by_turn[i]; } }
            os << "\nby depth left  :";
            for (int i = 0; i < 12; ++i) { if (c.fsw_by_depth[i]) { os << "  d" << i << "=" << c.fsw_by_depth[i]; } }
            os << "\n";
        }

        // Per-DECISION heavy-tail: which single decision holds the game's cost?
        if (!g_decisions.empty())
        {
            std::vector<DecisionPerf> ds = g_decisions;
            std::sort(ds.begin(), ds.end(),
                      [](const DecisionPerf& a, const DecisionPerf& b) { return a.nodes > b.nodes; });
            uint64_t tot = 0;
            for (const DecisionPerf& d : ds) { tot += d.nodes; }
            os << "\n--- per-decision cost (" << ds.size() << " decisions, " << tot << " nodes) ---\n";
            for (size_t i = 0; i < ds.size() && i < 12; ++i)
            {
                os << "  turn " << ds[i].turn << (ds[i].pre_combat ? " pre " : " post")
                   << " : " << ds[i].nodes << " nodes"
                   << "  (" << (tot ? 100.0 * ds[i].nodes / tot : 0.0) << "%)\n";
            }
        }

        // Per-game heavy-tail: how concentrated is the cost?
        if (!g_games.empty())
        {
            std::vector<GamePerf> games = g_games;
            std::sort(games.begin(), games.end(),
                      [](const GamePerf& a, const GamePerf& b) { return a.nodes > b.nodes; });

            uint64_t total_nodes = 0;
            double   total_ms    = 0.0;
            for (const GamePerf& g : games) { total_nodes += g.nodes; total_ms += g.millis; }

            os << "\n--- per-game cost (" << games.size() << " games) ---\n";
            os << "total nodes: " << total_nodes << "   total game-ms (summed): " << total_ms << "\n";

            auto pct_nodes = [&](double frac)
            {
                size_t k = (size_t)(frac * games.size());
                uint64_t acc = 0;
                for (size_t i = 0; i < k && i < games.size(); ++i) { acc += games[i].nodes; }
                return total_nodes ? 100.0 * acc / total_nodes : 0.0;
            };
            os << "top  1% of games hold " << pct_nodes(0.01) << "% of nodes\n";
            os << "top  5% of games hold " << pct_nodes(0.05) << "% of nodes\n";
            os << "top 10% of games hold " << pct_nodes(0.10) << "% of nodes\n";

            os << "heaviest games (index : nodes : ms):\n";
            for (size_t i = 0; i < games.size() && i < 10; ++i)
            {
                os << "  #" << games[i].index << " : " << games[i].nodes
                   << " : " << games[i].millis << "\n";
            }
            double median = games[games.size() / 2].nodes;
            os << "median game nodes: " << median
               << "   max/median: " << (median > 0 ? games[0].nodes / median : 0.0) << "x\n";
        }
        os << "=========================================\n";
    }
}

#define PROF_INC(field)        (++::prof::Thread().field)
#define PROF_ADD(field, n)     (::prof::Thread().field += (n))
#define PROF_ADD_NODES(n)      (::prof::AddGameNodes((uint64_t)(n)))
#define PROF_RESET_GAME()      (::prof::ResetGame())
#define PROF_RECORD_GAME(i, ms) (::prof::RecordGame((i), (ms)))
#define PROF_RECORD_DECISION(t, pre, n) (::prof::RecordDecision((t), (pre), (uint64_t)(n)))
#define PROF_FLUSH_THREAD()    (::prof::FlushThread())
#define PROF_REPORT(os)        (::prof::Report(os))

#else  // !MTG_PROFILE — all no-ops, zero overhead.

#define PROF_INC(field)         ((void)0)
#define PROF_ADD(field, n)      ((void)0)
#define PROF_ADD_NODES(n)       ((void)0)
#define PROF_RESET_GAME()       ((void)0)
#define PROF_RECORD_GAME(i, ms) ((void)0)
#define PROF_RECORD_DECISION(t, pre, n) ((void)0)
#define PROF_FLUSH_THREAD()     ((void)0)
#define PROF_REPORT(os)         ((void)0)

#endif // MTG_PROFILE
