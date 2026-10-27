#pragma once

// MANA COMPLETENESS ORACLE -- MTG_MANA_ORACLE (default OFF). docs/design/mana-completeness-oracle.md.
//
// USER 2026-10-26: "what I want to make sure is that the search never misses an option due to mana,
// even if we largely use heuristics." At every real main-phase decision the oracle realises each
// menu plan two ways -- the way the engine pays for it, and every minimal payment there is -- and
// checks the two inclusions that together say "the same list of plans":
//
//   I1  current  <= complete   every outcome the engine reaches is one the oracle reaches too
//                              (else ORACLE-GAP: the oracle is incomplete there, fix the ORACLE)
//   I2  complete <= current    every outcome the oracle reaches is covered by the engine's
//                              (else a MISS -- APPLY-DROP / DOMINATED -- fix the ENGINE)
//
// HOW THE COMPLETE SIDE PAYS. The plan is applied by the engine itself (TurnSolver::ApplyPlan), so
// land position, activations, continuations and every sub-decision pin run exactly as in search. Only
// the PAYMENTS are taken over: each outermost payment on the oracle's own state copy is served from a
// SCRIPT -- the taps to make (through the viewer's manual-tap routes, ApplyHumanPreTap, i.e. the
// engine's own mechanic for each source) and the floating units to set aside -- and then paid from the
// floating pool ONLY (t_float_only), so the payer cannot substitute a heuristic tap of its own. The
// first payment past the end of the script is the FRONTIER: its state and cost are captured and the
// payment fails, which ends that probe. The oracle then enumerates every minimal payment of the
// captured cost on the captured board and re-applies the plan once per payment, script extended.
//
// Nothing here is reachable when the flag is off: t_hook is null and t_float_only false on every
// thread, and both are only ever set by the oracle around its own applies on its own copies.

#include "../core/Card.h"
#include "../core/ManaPool.h"
#include "TurnSolver.h"

#include <cstdint>
#include <string>
#include <vector>

struct GameState;

namespace manaoracle
{
    // 0 = off; 1 = summary + the first few flags per class; 2 = every flag; 3 = + per-plan trace.
    int  Level();
    inline bool On() { return Level() > 0; }

    // One way to pay one payment: the taps to make (in order), and the floating units to set aside
    // for the duration of the payment so the payer cannot spend them (restored afterwards). The
    // set-aside is what makes WHICH floating unit pays a choice: Generator Servant's haste-tagged {C}
    // held back from a noncreature spell, an off-colour unit kept for the next cast.
    struct PayOption
    {
        std::vector<TurnSolver::PreTap> taps;
        // Parallel to `taps`: 1 = tap through the oracle's RESTRICTED route (OracleCreatureTap) into
        // the creature-only floating pool, for a creature spell's payment. Empty = all manual-tap routes.
        std::vector<char>               creature_route;
        ManaPool stash;          // general-float units set aside (typed WUBRGC; never wild)
        int      stash_haste = 0;// ...of which haste-tagged {C} (a subset of stash.colorless)
        bool     fail = false;   // no payment exists: fail this one (the engine drops the cast) and go on
    };

    // The payment script for ONE oracle apply. Consulted by TapForCostShared for the OUTERMOST payment
    // made on `target` (the oracle's own copy); payments on any other state object -- a scratch probe,
    // a payability check -- run through the normal pipeline untouched.
    struct Hook
    {
        const GameState*       target = nullptr;
        std::vector<PayOption> script;
        std::size_t            next   = 0;       // payments served so far
        bool                   ended  = false;   // the frontier was reached (or a scripted tap failed)
        std::string            tap_error;        // non-empty: a scripted tap was refused (an oracle defect)
        // The frontier payment: the board it was asked on and what it asked for.
        bool                   frontier = false;
        GameState*             f_state  = nullptr;   // owned by the caller (copied into at the frontier)
        ManaCost               f_cost;
        bool                   f_for_creature = false;
        bool                   f_creature_ability = false;  // paying a creature's activated ability
        int                    f_tapahead = 0;          // > 0: the frontier is a TAP-AHEAD (up to N sources)
        bool                   f_lands_only = true;     // ...of lands only (an ETB "untap N lands")
        // Every newly tapped mana source the script tapped (Card::m_number), for the unscripted-tap check.
        std::vector<int>       tapped_by_script;
    };

    extern thread_local Hook* t_hook;
    // OBSERVE MODE for the enumerator's mana gates (ENUM-GAP): while true, every per-subset mana
    // rejection in EnumeratePlans lets the subset through and the odometer's mana bounds are off, so
    // the menu it builds is "every plan the NON-mana rules allow". Set only by the oracle, around its
    // own EnumerateMainPlans call on its own thread; false everywhere else.
    extern thread_local bool  t_gate_observe;
    // Pay from the floating pools only: the payer drains them as usual and then FAILS (atomically)
    // instead of tapping anything. Set by the oracle around scripted payments and payment probes.
    extern thread_local bool  t_float_only;

    // The oracle-only tap the viewer's manual tap deliberately lacks: a RESTRICTED source -- a
    // creature_mana_only one (Somberwald Sage) or a colored_creature_only land's coloured face (Cavern
    // of Souls, Unclaimed Territory) -- tapped through the engine's own TapSourceIntoFloat into the
    // creature-only floating pool, which only a creature spell can spend (the payer drains it first
    // for one). Returns "" on success, else why not.
    std::string OracleCreatureTap(GameState& state, const TurnSolver::PreTap& t);

    // apply_one's TAP-AHEAD site on the oracle's copy (an untap ritual / an ETB "untap N lands" about
    // to be cast): serve the next script entry -- the sources to tap into the float ahead of the untap
    // -- or, past the script's end, capture the frontier. Replaces the engine's heuristic tap-ahead.
    void ScriptedTapAhead(GameState& state, int count, bool lands_only);

    // TapForCostShared's oracle branch (ManaPayment.cpp). Returns the payment's result.
    bool ScriptedPay(GameState& state, const ManaCost& cost, bool for_creature, ManaPool* available,
                     bool honor_legacy_cco);

    // Every MINIMAL way to pay `cost` on `state`: tap sets in which no tap can be dropped, crossed
    // with every distinct set-aside of the floating pool. Identical sources fold (only the lowest-
    // numbered untapped copies are tapped). Each option is VERIFIED by performing it on a scratch copy
    // and paying float-only, so a listed option always pays. `unmodeled` collects what the
    // enumeration could not drive (e.g. a restricted-mana source the manual-tap routes refuse).
    std::vector<PayOption> MinimalPayments(const GameState& state, const ManaCost& cost, bool for_creature,
                                           std::vector<std::string>* unmodeled, bool creature_ability = false);

    // AIEngine::TakeTurn's entry hook: audit this real decision (no-op unless On()).
    void AuditDecision(const GameState& state, bool is_pre_combat);
}
