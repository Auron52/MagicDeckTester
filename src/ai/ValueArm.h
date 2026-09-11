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
    // RESERVED single pass (MTG_ESC_SINGLE_RESERVE, with esc_single): the probe's start gate keeps room for ONE
    // heuristic pass at each depth it admits (tree(k) + R x leaves(k), leaves extrapolated from the previous
    // pass), and the single pass then runs on the REMAINING budget, overrun-guarded, instead of unlimited.
    // On an overrun a partial line that rated a win is kept (anytime), else the heuristic escalation runs.
    int         constant_alpha_relaxed = -1;   // -1 unset | 0 strict | 1 a constant-leaf ladder keeps the relaxed value alpha (MTG_CONSTANT_ALPHA_RELAXED)
    int         esc_single_reserve = -1;
    // CEILING on the FIT single pass's depth (MTG_ESC_FIT_DEPTH_CAP): "never run the rollout deeper than
    // where the ladder would have got to". Distinct from value_play.escalation_cap, which drives the
    // COLD-PREDICTOR path (eff_single_deck) and is inert under FIT. Per-job for this file's usual reason:
    // the question is a depth SWEEP (1/2/3/off), and an env static pins one depth per process, so four
    // arms would cost four batch invocations and four tails.
    int         esc_fit_depth_cap  = -1;    // -1 unset | 0 no cap | >0 the ceiling
    // Ceiling on the LADDER's escalation depth (MTG_ESC_DEPTH_CAP). Paired with esc_fit_depth_cap so a
    // MATCHED-DEPTH comparison is possible: `esc_depth_cap=D` vs `esc_fit_depth_cap=D` end at the same
    // depth, so whatever separates them is the ladder's machinery (iterative deepening, the incumbent,
    // the crossover take decision) and not its depth. That pairing is the whole point of having both
    // per-job: the two arms must run at the same seeds in the same pool to be comparable.
    int         esc_depth_cap      = -1;    // -1 unset | 0 no cap | >0 the ceiling
    // CONSTANT-LEAF EXHAUSTION MULTIPLE (MTG_CONSTANT_EXHAUST_MULT, default 1): a leafless pass stops at
    // used >= mult x the decision budget. The MODEL pass has no such stop (it runs to the proportional overrun
    // ceiling, 25x, because its truncated line is still rated); a leafless pass past exhaustion can only pay off
    // by PROVING a win in what remains of its main loops (the optional wave phases stop at exhaustion anyway).
    // 1 = stop at the budget (byte-identical to the 2026-09-10c exhaustion stop); >1 lets it run on that far.
    double      constant_exhaust_mult = -1.0;   // <=0 unset    // -1 unset | 0 unlimited pass | 1 reserved | 2 FIT: unreserved probe, then the rollout pass at the deepest depth whose estimated cost fits (MTG_ESC_SINGLE_FIT)
    // EMULATED-GATE LADDER (MTG_LADDER_EMULATED): warm-up passes on the value leaf, the committing pass
    // on the heuristic rollout, at the depth the FULL HEURISTIC ladder would have committed -- its start
    // gate is replayed on reconstructed heuristic pass costs (value cost + R x leaves). User design
    // 2026-09-09. Shape for an A/B: value_model=false + value_profile=<model> + ladder_emulated=true.
    int         ladder_emulated   = -1;     // -1 unset | 0 off | 1 on     (MTG_LADDER_EMULATED)
    // Commit a VERIFIED warm-up win directly (filled in if truncated) instead of replaying it on the
    // heuristic. -1 unset => ON for a no-leaf (constant) model, OFF otherwise (MTG_LADDER_EMUL_DIRECT).
    int         ladder_emul_direct = -1;
    // COMMITTING leaf of the emulated ladder: -1 unset (deck shape / env) | 0 heuristic rollout | 1 the
    // deck's value model at the depth the VALUE ladder would commit, then the hybrid's trust escalation
    // (MTG_LADDER_EMUL_COMMIT_MODEL). WARM-UP leaf: -1 unset | 0 the model | 1 no leaf at all
    // (MTG_LADDER_EMUL_WARM_NONE) -- the model stays attached for the committing pass.
    int         ladder_emul_commit    = -1;
    int         ladder_emul_warm_none = -1;
    double      ladder_emul_margin = -1.0;  // <=0 unset                   (MTG_LADDER_EMUL_MARGIN; <1 biases borderline passes to the heuristic)
    // Depths 1..N always play the heuristic (exact pass costs into the replayed gate, at the price of
    // their warm-up rollouts -- cheap where the ladder's growth is steep). -1 unset => env (default 0).
    int         ladder_emul_hfirst = -1;    // -1 unset | >=0 explicit     (MTG_LADDER_EMUL_HFIRST)
    // ORDER-FREE WIN REUSE for every pass (MTG_MEMO_WIN_ORDERFREE): an FSLineCache WIN entry whose zone
    // order differs yields its win turn with an empty continuation instead of a full re-search. Makes
    // the tree leaf-independent (heuristic == value warm-up) and skips the committing pass's own
    // order-miss re-searches; the line ends at that node (the engine re-searches past a line's end).
    int         memo_win_orderfree = -1;    // -1 unset | 0 off | 1 on     (MTG_MEMO_WIN_ORDERFREE)
    // ORDER-FREE REUSE WAVE (MTG_FSL_OF_WAVE / MTG_OF_WAVE_SHARE; see FSLineStoreOfHint in
    // TurnSolver.cpp). `of_wave_share` is the share of the REMAINING budget a node's re-search must
    // be worth before it may take a permuted twin's answer instead -- the gate that makes the reuse
    // decay to zero as the budget grows. Here rather than env-only for the reason this file states
    // above: sizing it needs one arm per share value, and env knobs are process-wide, so an env-only
    // sweep costs one `mtg --batch` per arm and strands cores on every invocation's tail.
    int         of_wave        = -1;        // -1 unset | 0 off | 1 on     (MTG_FSL_OF_WAVE)
    double      of_wave_share  = -1.0;      // <0 unset                    (MTG_OF_WAVE_SHARE)
    // FIRST-VERIFIED-WIN HORIZON EXIT (MTG_FS_HORIZON_EXIT). Same reason as every other entry here:
    // measuring a PRUNE means running both arms, and the standing bar is to measure a prune
    // UNBUDGETED -- which is expensive enough that it must not also pay a per-arm batch tail.
    int         fs_horizon_exit = -1;       // -1 unset | 0 off | 1 on     (MTG_FS_HORIZON_EXIT)
    int         memo_orderfree_verified_only = -1;   // -1 unset | 0 all entries | 1 verified wins only (MTG_MEMO_ORDERFREE_VERIFIED_ONLY)
    // SPLIT KEYS (MTG_FSL_SPLIT_KEYS): WIN entries under an ORDER-EXACT key, NO-WINs canonical --
    // the change that makes order-free reuse sound BY CONSTRUCTION. Here so the whole soundness
    // ladder (legacy unsound -> guard -> split keys -> split keys + wave) can be measured as ONE
    // pooled batch instead of four env-pinned invocations, each stranding cores on its own tail.
    int         fsl_split_keys = -1;        // -1 unset | 0 off (single canonical key) | 1 on
    // FROZEN cost-per-probe-leaf R for the adopted per-deck escalation cap (sidecar
    // value_play.escalation_r). Here for the usual reason: R is a PER-DECK constant, so sweeping it
    // via MTG_ESC_DECK_R pins one value process-wide and forces one `mtg --batch` per arm -- and the
    // alternative (a scratch deck dir per arm) copies multi-hundred-MB sidecar trees per value.
    double      esc_deck_r        = -1.0;   // <=0 unset                   (MTG_ESC_DECK_R)
    double      trust_slack       = -1.0;   // <=0 unset                   (kTrustPathSlack override)
    // Empty => unset (fall back to env, then to the deck-adjacent <stem>.value.json auto-detect).
    // "none"/"off"/"0" => explicitly NO sidecar, which is how an H-arm job asks for the pure
    // heuristic leaf on a deck that ships a model. Note this must be explicit: sidecar PRESENCE is
    // what activates the hybrid, so "just don't set MTG_VALUE_MODEL" is not the same thing.
    std::string value_profile;
};

inline thread_local Arm t_arm;
// PER-DECK search shape from the sidecar's value_play (set by AIEngine around each decision, RAII):
//   t_deck_ladder    0 = escalation (the hybrid) | 1 = emulated ladder committing on the heuristic
//                    | 2 = emulated ladder committing on the value model (then the hybrid's trust escalation).
//   t_deck_warm_none 1 = the ladder's warm-up passes run with no leaf (value_play.leaf "none" while the
//                    model stays attached for the committing pass).
//   t_deck_key       the profile identity (MulliganProfile::value_source) keying per-deck learned state.
// The per-job arm still wins over all of them (an A/B must be able to pin the shape on any deck).
inline thread_local int         t_deck_ladder = 0;
inline thread_local int         t_deck_warm_none = 0;
// 1 = value_play.commit "model" (both ladders: the emulated ladder's committing pass, or the escalation
// ladder's value pass at its last depth before the heuristic escalation).
inline thread_local int         t_deck_commit_model = 0;
// value_play.ladder "single": -1 unset | 2 = the FIT single pass (see ValuePlay). Same resolution order as the
// other shape keys: the per-job arm wins, then the deck, then the env default.
inline thread_local int         t_deck_single = -1;
// value_play.alpha for a leafless probe: -1 unset | 0 strict | 1 relaxed.
inline thread_local int         t_deck_alpha_relaxed = -1;
// value_play.exhaust_mult: <=0 unset.
inline thread_local double      t_deck_exhaust_mult = 0.0;
inline thread_local std::string t_deck_key;

// Reset to "use the env default for everything". Called by a worker before a job with no arm block,
// so a previous job's arm cannot leak into it through the reused thread.
inline void Clear() { t_arm = Arm{}; }
}
