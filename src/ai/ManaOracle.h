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

#include <atomic>
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
        // The SPELL being paid for (PayingSpellCard() at the frontier; the cast site's scope), so the payment table
        // can apply a subtype restriction -- Giada, Font of Hope's {W} "only to cast an Angel spell".
        bool                   f_has_spell = false;
        Card                   f_spell;
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
        // SAFE (RealPayScope, the REAL board): a scripted entry is used only while it fits -- its bill is the one
        // asked for and its taps and float-only payment succeed; at the first that does not, the board is put
        // back and this and every later payment is the engine's own (`abandoned`). Past the script's end, the
        // engine's too. Never a frontier, never a failed payment the engine would not have failed.
        bool                   safe      = false;
        bool                   abandoned = false;
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
    // False only for a SAFE hook (RealPayScope) whose entry did not fit: the board is put back, the hook
    // gives up its script, and the caller runs the engine's own tap-ahead -- as it does for every later step.
    bool ScriptedTapAhead(GameState& state, int count, bool lands_only);

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
    // `whole_float` (the refine's fast form): no set-aside enumeration -- the payment drains the floating pool as
    // the payer does, so each tap set is one option.
    // `spell` (optional): the spell being paid for, set as the paying-spell scope (SpellSubtypePayScope) for the
    // table and every verification -- a subtype-restricted source (Giada) is offered only to a spell of its subtype.
    std::vector<PayOption> MinimalPayments(const GameState& state, const ManaCost& cost, bool for_creature,
                                           std::vector<std::string>* unmodeled, bool creature_ability = false,
                                           bool whole_float = false, const Card* spell = nullptr);

    // PAY REFINE (MTG_PAY_REFINE; TapForCostShared, ManaPayment.cpp). `pre` / `post`: the board before and after
    // ONE outermost payment of `cost` the engine just made. Returns true and `out` = the payment to make instead
    // when one DOMINATES it: it taps a subset of the sources the engine's payment tapped (any faces -- the
    // minimal payments of the bill on a board where only those are untapped), costs no more life, energy or
    // haste-tagged {C}, and the sources it gives back can still make every unit the engine's payment left
    // floating (`post` is reachable from it by mana abilities alone) -- and is strictly better somewhere. Among
    // several, the one giving back the most sources, then the most life, then energy. Never a source the
    // engine's payment left untapped, so never a guess about the rest of the turn. Works on copies only.
    bool RefinePayment(const GameState& pre, const GameState& post, const ManaCost& cost, bool for_creature,
                       bool creature_ability, PayOption& out);
    // An option's taps, through the routes MinimalPayments verified them with (ApplyHumanPreTap /
    // OracleCreatureTap). False = a tap was refused (the state may be part-tapped: the caller restores).
    bool PerformPayTaps(GameState& state, const PayOption& o);

    // THE EXECUTOR'S PAYMENT SCRIPT (MTG_REAL_PAY_SCRIPT; AIEngine::TakeTurn, real play only). For the plan the
    // executor is about to apply on `state`: when the engine's own apply of it (the search's, TurnSolver::ApplyPlan)
    // pays every action but is DOMINATED at the line level, the oracle's realisation of the same plan that
    // dominates it -- made of the engine's own taps, CoreKey and line equal, replayed and compared on the real
    // board -- as a payment script. False: keep the engine's payments -- for a DROP too (the search scored the
    // plan by that apply, the drop included; paying the dropped cast plays a line nobody evaluated), a line that
    // draws, reveals or casts a card the plan did not name (its continuation is searched on cards no payment could
    // have known), a combo route, an armed damage board. `what` (optional): why. Bounded by MTG_REAL_PAY_NODES
    // applies (default 600).
    // `attack_pin` (optional, MTG_REAL_PAY_ATTACK_PIN): when a script is returned for a PRE-COMBAT main, the card
    // numbers of our creatures on the engine's apply board, +n = DECLARED as an attacker there, -n = not -- the
    // combat the search scored this line with. The script leaves a superset of sources untapped, and an attack heuristic that reads untapped mana
    // (FiveColourProvider::ShouldAttackWith's hold) can re-decide the combat on that board; the executor replays
    // this set instead (AIEngine::DeclareAttackers). Left empty with `*attack_pin_ok` false when not computable.
    bool PlanPaymentScript(const GameState& state, const TurnSolver::Plan& plan, bool pre,
                           std::vector<PayOption>& script, std::string* what = nullptr,
                           std::vector<int>* attack_pin = nullptr, bool* attack_pin_ok = nullptr);
    // Serve `script` on the REAL board `s` for the scope's life, as a SAFE hook (Hook::safe): the executor's
    // outermost payments on `s` take its entries in order while they fit, the engine's own payment otherwise.
    // Real declarations the scored-combat pin changed (MTG_REAL_PAY_ATTACK_PIN; printed with MTG_PAY_REFINE_STATS).
    extern std::atomic<long long> g_real_pay_atk_pin_changed;
    struct RealPayScope
    {
        Hook  h;
        Hook* saved = nullptr;
        RealPayScope(GameState& s, std::vector<PayOption> script);
        ~RealPayScope();
        RealPayScope(const RealPayScope&)            = delete;
        RealPayScope& operator=(const RealPayScope&) = delete;
    };

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

    // THE COMPLETENESS SELF-CHECK for one plan the engine realises in full (AuditPlan's I1, whose miss is an
    // ORACLE-GAP): does the oracle's own payment search reach an outcome on the engine's line that covers the
    // engine's end board? For the unit tests that pin the shapes the search once missed. Quiet; copies only.
    bool CoversEngineOutcome(const GameState& state, bool pre, const TurnSolver::Plan& plan);

    // THE ENUM-GAP VERDICT for one plan a mana gate kept off `menu` (AuditDecision's per-plan check), for the unit
    // tests that pin its false-positive rules: the class it would report ("ENUM-GAP", "HIDDEN-INFO", a
    // "DOMINATED-*"), or "" for none. `observed` (optional): the rest of the observe-mode enumeration -- the plans
    // a realisation that skipped one of this plan's actions may stand for. `detail` (optional): the flag's text.
    // Quiet; copies only.
    std::string EnumGapVerdict(const GameState& state, bool pre, const TurnSolver::Plan& plan,
                               const std::vector<TurnSolver::Plan>& menu,
                               const std::vector<TurnSolver::Plan>* observed = nullptr, std::string* detail = nullptr);
}
