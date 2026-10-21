#pragma once
#include <cstddef>
#include <string>
#include <vector>

// THE COMBO OFF ROUTE'S RESOLUTION TAPE (MTG_AA_ROUTE_TAPE, DEFAULT ON; =0 = exactly the old behaviour).
//
// WHY. The AccomplishedAlchemist route (aaroute, DecisionProviders.cpp) proves a go-off by applying the engine's
// own plans link by link on a copy, records the path, and later REPLAYS those plans -- on a copy again ("proven
// here") and then on the live state. "The path is the plans, not their indices" was true one level deep only: a
// plan still carries RANKS that are resolved inside ApplyPlanDirect at apply time --
//   * a breakpoint continuation (Plan::bp_choice: the k-th entry of the list BpEnumEntryFor serves -- cache
//     state; an unbranched breakpoint resolves to EMPTY inside the route), and
//   * a tutor's target (Plan::tutor_choice, or the provider's front: the k-th of the ranking computed inside
//     PerformTutor at resolution).
// Re-resolving those against a different cache or calling context can land a different board (forced with
// MTG_AA_ROUTE_APPLY_CLEARBP: an eviction between two replays of one path re-derives the list), which is one
// way a route the offer proved fails to kill when applied -- the generation's "APPLY replay did not kill" lines.
// NOTE: the FREQUENT no-op route (~1 per game; seed 96046 gi45: 9) was a different hole, the sim key missing
// Permanent::chosen_color (MTG_KEY_CHOSEN_COLOR, TurnSolver.cpp); see analysis-AccomplishedAlchemist.md,
// "Route tape and the chosen-colour key (2026-10-21)".
//
// WHAT. While the walk applies a link, every such resolution is RECORDED in the order it happens -- the tutor's
// fetched card NAME, and each breakpoint's resolved continuation PLAN (plus whether the plan carried it) -- and
// stored with the link. A replay of the link PLAYS the tape back at the same resolution points instead of
// re-resolving, so it lands on the board the proof reached. Resolutions inside an enumeration, a derivation or a
// rollout that the link's apply happens to run (a provider ranking, a continuation list) are not the link's own:
// the tape is SUSPENDED there (Suspend), in both modes.
namespace routetape
{
enum Mode : int { kOff = 0, kRecord = 1, kPlay = 2 };
inline thread_local int  t_mode    = kOff;
inline thread_local int  t_suspend = 0;       // > 0: inside a nested enumeration / derivation / rollout
inline thread_local bool t_miss    = false;   // play: an event the tape could not supply (or did not match)

// A tutor that fetched nothing (declined, nothing legal, a guard whiffed).
inline const std::string& NoneName() { static const std::string s("\x01(none)"); return s; }
inline thread_local std::vector<std::string>*       t_tutor_rec  = nullptr;
inline thread_local const std::vector<std::string>* t_tutor_play = nullptr;
inline thread_local std::size_t                     t_tutor_pos  = 0;

inline bool Recording() { return t_mode == kRecord && t_suspend == 0; }
inline bool Playing()   { return t_mode == kPlay && t_suspend == 0; }

// RAII: everything under it is not the link's own resolution. A no-op while no tape is open.
struct Suspend
{
    bool on;
    Suspend() : on(t_mode != kOff) { if (on) { ++t_suspend; } }
    ~Suspend() { if (on) { --t_suspend; } }
    Suspend(const Suspend&) = delete;
    Suspend& operator=(const Suspend&) = delete;
};
}
