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
    // PATH-TO-TRUST start gate (MTG_ESC_TO_TRUST) and its bounded leniency (kTrustPathSlack). Here
    // for exactly the reason the block above exists: both are process-wide statics, so without a
    // per-job override an A/B of them needs one `mtg --batch` per arm -- which strands cores on each
    // invocation's tail and re-introduces the wave pattern this file was written to remove.
    int         esc_to_trust      = -1;     // -1 unset | 0 off | 1 on     (MTG_ESC_TO_TRUST)
    // SINGLE HEURISTIC PASS AT THE PROBE'S COMMITTED DEPTH (MTG_ESC_SINGLE_AT_COMMITTED): the value-leaf
    // ladder finds the depth D it can afford, then ONE heuristic pass at exactly D runs to completion
    // and its line is always taken. No d1..D heuristic re-ladder, no crossover fall-back, no
    // starvation. User paradigm 2026-09-08 ("don't search further; heuristic at the final depth").
    int         esc_single        = -1;     // -1 unset | 0 off | 1 on     (MTG_ESC_SINGLE_AT_COMMITTED)
    // EMULATED-GATE LADDER (MTG_LADDER_EMULATED): warm-up passes on the value leaf, the committing pass
    // on the heuristic rollout, at the depth the FULL HEURISTIC ladder would have committed -- its start
    // gate is replayed on reconstructed heuristic pass costs (value cost + R x leaves). User design
    // 2026-09-09. Shape for an A/B: value_model=false + value_profile=<model> + ladder_emulated=true.
    int         ladder_emulated   = -1;     // -1 unset | 0 off | 1 on     (MTG_LADDER_EMULATED)
    // Commit a VERIFIED warm-up win directly (filled in if truncated) instead of replaying it on the
    // heuristic. -1 unset => ON for a no-leaf (constant) model, OFF otherwise (MTG_LADDER_EMUL_DIRECT).
    int         ladder_emul_direct = -1;
    double      ladder_emul_margin = -1.0;  // <=0 unset                   (MTG_LADDER_EMUL_MARGIN; <1 biases borderline passes to the heuristic)
    // Depths 1..N always play the heuristic (exact pass costs into the replayed gate, at the price of
    // their warm-up rollouts -- cheap where the ladder's growth is steep). -1 unset => env (default 0).
    int         ladder_emul_hfirst = -1;    // -1 unset | >=0 explicit     (MTG_LADDER_EMUL_HFIRST)
    // ORDER-FREE WIN REUSE for every pass (MTG_MEMO_WIN_ORDERFREE): an FSLineCache WIN entry whose zone
    // order differs yields its win turn with an empty continuation instead of a full re-search. Makes
    // the tree leaf-independent (heuristic == value warm-up) and skips the committing pass's own
    // order-miss re-searches; the line ends at that node (the engine re-searches past a line's end).
    int         memo_win_orderfree = -1;    // -1 unset | 0 off | 1 on     (MTG_MEMO_WIN_ORDERFREE)
    int         memo_orderfree_verified_only = -1;   // -1 unset | 0 all entries | 1 verified wins only (MTG_MEMO_ORDERFREE_VERIFIED_ONLY)
    double      trust_slack       = -1.0;   // <=0 unset                   (kTrustPathSlack override)
    // Empty => unset (fall back to env, then to the deck-adjacent <stem>.value.json auto-detect).
    // "none"/"off"/"0" => explicitly NO sidecar, which is how an H-arm job asks for the pure
    // heuristic leaf on a deck that ships a model. Note this must be explicit: sidecar PRESENCE is
    // what activates the hybrid, so "just don't set MTG_VALUE_MODEL" is not the same thing.
    std::string value_profile;
};

inline thread_local Arm t_arm;
// PER-DECK search shape from the sidecar's value_play (set by AIEngine around each decision, RAII):
//   t_deck_ladder 0 = escalation (the hybrid) | 1 = emulated-gate ladder.
//   t_deck_key    the profile identity (MulliganProfile::value_source) keying per-deck learned state.
// The per-job arm still wins over both (an A/B must be able to pin the shape on any deck).
inline thread_local int         t_deck_ladder = 0;
inline thread_local std::string t_deck_key;

// Reset to "use the env default for everything". Called by a worker before a job with no arm block,
// so a previous job's arm cannot leak into it through the reused thread.
inline void Clear() { t_arm = Arm{}; }
}
