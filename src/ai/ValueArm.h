#pragma once
#include <string>

// PER-THREAD value-leaf ARM settings, so ONE pooled batch can run BOTH arms of the depth matrix.
//
// The matrix measures two arms of the same deck at the same depth: H (pure heuristic leaf) and V
// (learned value leaf). Both were selected purely by process ENVIRONMENT -- MTG_VALUE_MODEL,
// MTG_VALUE_MIN_DEPTH, MTG_LADDER_VALUE_LEAF, MTG_VALUE_STARTGATE_ALPHA, MTG_VALUE_PROFILE -- and
// each is read once into a process-wide `static const`. One process could therefore only ever BE
// one arm, which forced the matrix driver to spawn a separate `mtg --batch` per (deck, arm) and
// again per wave. The cost of that is not theoretical: the cheap V arm cannot backfill cores while
// the expensive H arm drains its tail, so a run measured 2026-08-10 sat at ~4.8 of 20 threads for
// hours, and every wave boundary is a full synchronisation point where the single slowest game in
// the whole arm gates all remaining work.
//
// These overrides move the arm from the environment onto the JOB: the batch worker sets them before
// building a job's engine, and every read site consults them before falling back to its env static.
// UNSET everywhere (the sentinels below) means "use the env default", so single runs, the
// regression harness, and every pre-existing manifest are byte-identical -- the override is opt-in
// per job and absent jobs never touch it.
//
// thread_local rather than a global: a batch worker owns its thread for the duration of a game and
// the search does not spawn threads (parallelism is at the game level), so per-thread is per-game.
// This is the same pattern the escalation beam already uses (`g_esc_beam_width`).
namespace valuearm
{
struct Arm
{
    int         value_model       = -1;     // -1 unset | 0 off | 1 on     (MTG_VALUE_MODEL)
    int         value_min_depth   = -2;     // -2 unset | >=-1 explicit    (MTG_VALUE_MIN_DEPTH)
    int         ladder_value_leaf = -1;     // -1 unset | 0 off | 1 on     (MTG_LADDER_VALUE_LEAF)
    double      startgate_alpha   = -1.0;   // <=0 unset                   (MTG_VALUE_STARTGATE_ALPHA)
    // Empty => unset (fall back to env, then to the deck-adjacent <stem>.value.json auto-detect).
    // "none"/"off"/"0" => explicitly NO sidecar, which is how an H-arm job asks for the pure
    // heuristic leaf on a deck that ships a model. Note this must be explicit: sidecar PRESENCE is
    // what activates the hybrid, so "just don't set MTG_VALUE_MODEL" is not the same thing.
    std::string value_profile;
};

inline thread_local Arm t_arm;

// Reset to "use the env default for everything". Called by a worker before a job with no arm block,
// so a previous job's arm cannot leak into it through the reused thread.
inline void Clear() { t_arm = Arm{}; }
}
