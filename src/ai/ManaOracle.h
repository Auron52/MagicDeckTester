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
#include <optional>
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
        // ENGINE STEP (the rollback census): this payment / tap-ahead is made by the ENGINE's own
        // heuristic, exactly as an unhooked apply makes it -- so a script of k engine steps and then the
        // oracle's options is "keep the engine's first k mana decisions, redo everything after".
        bool     engine = false;
        std::string what;        // display only: the bill this option paid ("{1}{U}", "tap-ahead")
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
        // Past the script's end, keep making ENGINE steps instead of stopping at a frontier: the pure
        // replay of the engine's own line, whose first failing step the census measures from.
        bool                   all_engine = false;
        int                    first_fail = -1;      // the first engine-step PAYMENT that failed (step index)
        long long              engine_tapaheads = 0; // engine tap-aheads run inside engine steps (expected)
        // Parallel to the steps served (script entries consumed): the human-sequence step each was made
        // in (t_seq_step at the time; -1 outside a human sequence) -- where a viewer pre-tap must sit.
        std::vector<int>       pay_steps;
    };

    extern thread_local Hook* t_hook;
    // The human "**" sequence step being applied (TurnSolver's v2 walk: the index of the queued step,
    // the land included, that is being dispatched); -1 outside one. Written by the walk, read only by the
    // oracle's hook, so a payment can be placed where the viewer's pre-tap would have to sit.
    extern thread_local int   t_seq_step;

    // Is the next mana decision on the oracle's copy `s` an ENGINE step? (False when `s` is not the
    // oracle's copy -- every normal apply.) The engine-only machinery that stands down on the oracle's
    // copy -- the joint prepay, the land-Aura host hold, the heuristic tap-ahead -- runs for an engine
    // step, so the replay is the engine's line and not a variant of it.
    bool NextIsEngineStep(const GameState& s);
    // A tap-ahead site's step: true (and the step consumed) when it is an engine step, so the caller
    // runs the engine's own tap-ahead; false -> the caller serves ScriptedTapAhead as before.
    bool TakeEngineStep(const GameState& s, int count);
    // Owns the taps the engine makes inside an engine step (they are the engine's, not unscripted).
    // Inert unless `active`.
    struct EngineStepScope
    {
        Hook*            h = nullptr;
        GameState*       st = nullptr;
        std::vector<int> untapped;
        long long        ta0 = 0;
        EngineStepScope(GameState& s, bool active);
        ~EngineStepScope();
        EngineStepScope(const EngineStepScope&) = delete;
        EngineStepScope& operator=(const EngineStepScope&) = delete;
    };
    // ONE engine step made of SEVERAL payments: apply_one's land-Aura host hold pays once with the host
    // reserved and, on failure, once more without -- one decision, so one step. Takes the step when
    // `active`, stands the hook down until End(ok), and records a failed span as the step's failure.
    struct EngineStepSpan
    {
        Hook*                h = nullptr;
        std::size_t          step = 0;
        std::optional<EngineStepScope> es;
        EngineStepSpan(GameState& s, bool active);
        // `made` = false: the span turned out to decide nothing (a declined joint prepay) -- the step is
        // handed back, so the numbering counts only mana decisions.
        void End(bool ok, bool made = true);
        ~EngineStepSpan() { if (h != nullptr) { End(true); } }
        EngineStepSpan(const EngineStepSpan&) = delete;
        EngineStepSpan& operator=(const EngineStepSpan&) = delete;
    };
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
    // numbered untapped copies are tapped), except a copy the plan being realised taps or sacrifices
    // itself, which is its own source. A tap set that makes more than the bill is listed once per
    // leftover, and a set holding a multi-unit source also with one more free tap (its surplus can
    // float into a later payment). Each option is VERIFIED by performing it on a scratch copy
    // and paying float-only, so a listed option always pays. `unmodeled` collects what the
    // enumeration could not drive (e.g. a restricted-mana source the manual-tap routes refuse).
    std::vector<PayOption> MinimalPayments(const GameState& state, const ManaCost& cost, bool for_creature,
                                           std::vector<std::string>* unmodeled, bool creature_ability = false);

    // AIEngine::TakeTurn's entry hook: audit this real decision (no-op unless On()).
    void AuditDecision(const GameState& state, bool is_pre_combat);

    // THE VIEWER'S "CAN THE SEARCH FIND THIS LINE" CHECK (USER 2026-10-27: "a user using the viewer
    // should be able to tell: 'Can the search actually find this line' while using it"). For one plan:
    // does the engine's own apply of it -- the payments the search scores -- realise every action, or does
    // another menu entry with the same actions; if neither, can the board pay for it at all and with
    // which taps, and how far back the engine's own mana decisions have to be re-made to get there (the
    // rollback census). MANA ONLY: whether the search's own enumeration offers the line is not asked.
    // Runs whether or not MTG_MANA_ORACLE is on, quietly (RevealLogPause), on copies of `state`.
    struct LinePayment
    {
        std::string              what;      // the bill ("{1}{U}"; "tap-ahead"); "" = the engine's own payment
        bool                     engine = false;  // kept as the engine makes it (a census prefix step)
        bool                     fail = false;    // no payment exists for this one
        std::vector<std::string> taps;      // viewer pre-tap tokens: tap=<name>#<num>:<face>[+<aura>]
        std::string              text;      // the same taps for a person ("Conservatory for G, ...")
        std::string              keep_floating;   // floating units set aside for a later payment
        int                      step = -1; // the human-sequence step it was made in (-1 = not one)
    };
    struct LineAudit
    {
        bool        audited = false;      // false: not checked (`partial` says why)
        bool        engine_full = false;  // the search's apply of THIS plan realises every action
        int         sibling = -1;         // ...else a menu entry (index) with the same actions that does
        std::string dropped;              // what the search's apply drops ("cast X, act Y")
        // Only when neither of the above: the oracle's own answer.
        bool        payable = false;      // some payment realises every action...
        std::string other_order;          // ...in this plan's order (empty), or only in this one ("A > B > C")
        std::vector<LinePayment> pays;    // ...paying payment by payment like this
        // The census (this plan's order only; empty when only another order pays).
        std::string rollback;             // census bucket ("depth0", "depth1", ..., "capped", ...)
        int         keep = -1;            // keep the engine's first `keep` mana decisions, re-make the rest
        int         failed = -1;          // decision #failed+1 is the engine payment that failed
        bool        later = false;        // a lost cast is not one the plan declared (a continuation's)
        bool        pay_rollback = false; // MTG_PAY_ROLLBACK (forced on) recovers it
        std::string partial;              // non-empty: no verdict (capped / a choice the oracle does not drive)
    };
    // `menu` (optional): the frame's menu, for the same-actions sibling check; `plan` may be one of its
    // entries. `node_cap` bounds each payment search (applies) so a validation stays interactive.
    // `deep` = false: the engine and sibling checks only (`partial` says the oracle was not asked).
    LineAudit AuditLine(const GameState& state, bool pre, const TurnSolver::Plan& plan,
                        const std::vector<TurnSolver::Plan>* menu = nullptr, int node_cap = 400, bool deep = true);
}
