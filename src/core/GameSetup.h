#pragma once
#include <cstdlib>

// PER-JOB GAME-SETUP overrides. Today: starting life, which is what "Two-Headed Giant" means
// for a goldfish (user, 2026-08-19: "regular play (20 life) and 2HG (30 life)").
//
// 2HG's other rules -- a shared turn, a teammate's board, one team life total -- are not
// modelled and are NOT what this is: the passive opponent never acts, so the only thing that
// changes for a goldfish measuring turn-to-kill is HOW MUCH DAMAGE IS REQUIRED. A deck that
// wins a 20-life race on turn 5 may need a materially different curve to win a 30-life race,
// and that is the question this lets us ask. Anything that reads a teammate is out of scope.
//
// Why a per-JOB thread_local and not an environment variable: exactly the reason
// ai/ValueArm.h exists. A process-wide `static const` would force one `mtg --batch` per life
// total, which strands cores on each invocation's tail and re-introduces the wave pattern
// CLAUDE.md's pooling rule forbids. As a job field, the 20-life and 30-life arms of the same
// comparison share ONE pooled queue and one tail.
//
// The env fallback (MTG_START_LIFE) is still needed: the ANALYZER paths -- exhaustive keep
// generation, the keep-model trainer, equivalence discovery -- call SetupGame outside any
// batch job, and a 30-life keep table has to be generated under 30-life rollouts or it is
// fitted to the wrong race.
//
// Resolution order is the house pattern: per-job override -> env static -> default 20.
// UNSET everywhere means 20, so every pre-existing manifest and every analyzer run is
// byte-identical; the override is opt-in and an absent field never touches it.
namespace gamesetup
{
struct Setup
{
    int starting_life = -1;   // -1 unset | >0 explicit   (manifest "starting_life")
};

inline thread_local Setup t_setup;

// The resolved starting life for the game about to be set up.
inline int StartingLife()
{
    static const int s_env = []
    {
        const char* e = std::getenv("MTG_START_LIFE");
        const int   v = (e && *e) ? std::atoi(e) : 0;
        return v > 0 ? v : 20;
    }();
    return t_setup.starting_life > 0 ? t_setup.starting_life : s_env;
}
}   // namespace gamesetup
