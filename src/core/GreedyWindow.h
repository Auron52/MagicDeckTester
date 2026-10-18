#pragma once
// THE NO-GREEDY WINDOW GUARD (moved out of src/ai/TurnSolver.h verbatim, 2026-10-18, Vampires A1) so a
// shared CORE resolver -- the madness trigger (SpellEffects.h ResolvePendingMadness) -- can ask the same
// "is search depth left?" question the search's own sites ask, without core including the search header.
// The doctrine and the history are documented where the guard is used: src/ai/TurnSolver.h (the block
// note above its #include of this file) and docs/design/no-greedy-in-search-window.md.
#include <cstdio>
#include <cstdlib>

namespace greedywindow
{
inline constexpr int kNoSearch = -1;          // no search frame on the stack -> greedy is the play path
inline thread_local int t_depth = kNoSearch;  // innermost search frame's REMAINING depth

// RAII. Unconditional (not behind a flag): one thread-local int store per search frame, which is a
// decision-scale write, and a guard that can be configured off is the thing the user asked to end.
struct Frame
{
    int prev;
    explicit Frame(int d) : prev(t_depth) { t_depth = d; }
    ~Frame() { t_depth = prev; }
};

[[noreturn]] inline void Trip(const char* site, int depth)
{
    std::fprintf(stderr,
        "\n*** FATAL: GREEDY PLAN CHOICE INSIDE THE SEARCH WINDOW ***\n"
        "    site: %s\n"
        "    the innermost search frame still has depth %d remaining, so this decision had search\n"
        "    budget available and took a one-shot heuristic instead.\n"
        "\n"
        "    This is a DOCTRINE VIOLATION, not a tuning question (USER 2026-09-05 / 2026-10-01):\n"
        "    greedy is permitted only BEYOND the search horizon, for this-turn combo go-off\n"
        "    heuristics, mana allocation, and non-dork attack decisions.\n"
        "\n"
        "    Fix it by routing this site through the search, or -- WITH USER APPROVAL ONLY -- by not\n"
        "    considering the phase at all. Do NOT add a flag to bypass this check; it is deliberately\n"
        "    fatal because this class of regression has been removed three times and returned three\n"
        "    times. See docs/design/no-greedy-in-search-window.md and\n"
        "    docs/design/greedy-in-the-searched-window-status.md.\n\n",
        site, depth);
    std::fflush(stderr);
    // abort(), not an exception: a throw can be caught by a worker-thread boundary and demoted to a
    // per-job error, which is exactly the "invisible until someone looks" failure this replaces.
    std::abort();
}

// THE ONE PREDICATE both checks reduce to, kept pure so test_greedy_window.cpp can prove the gate
// FIRES (a gate nobody has seen trip may be a silent no-op): a greedy pick is inside the window when
// the CALLER's claimed remaining depth is positive OR the innermost open search frame still holds
// depth. Either alone has a blind spot -- a claim is only as honest as the call site that makes it,
// and a frame only exists where a host opened one -- so the permit checks both.
inline bool InWindow(int claimed_depth)
{
    return claimed_depth > 0 || t_depth > 0;
}

inline void Require(const char* site, int claimed_depth = 0)
{
    if (InWindow(claimed_depth)) { Trip(site, claimed_depth > 0 ? claimed_depth : t_depth); }
}
}   // namespace greedywindow
