#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

struct Action;

// PLAN CONTEXT: what else this turn's plan is going to do, visible to a DecisionProvider while it
// is being asked about ONE action inside that plan.
//
// Why it exists. A provider is handed a GameState, which describes everything that has ALREADY
// happened -- and for a decision made mid-plan that is only half the information. The other half is
// the plan itself: which land is being played, what else gets cast this turn, and how much mana is
// left afterwards. GoblinsProvider::TutorCandidates currently hand-rolls guesses at exactly that
// (entering_fodder, buff_targets' hand component, hand_has_play, haste_avail, mana_now/mana_next),
// and each of those is a tuned stand-in for something the plan already knows exactly.
//
// This also fixes the state mismatch documented at the tutor axis fan-out in TurnSolver.cpp: that
// fan-out ranks every plan against one shared turn-start state rather than the state its own plan
// creates. Correcting the state ALONE measured worse (+18 held-out, MTG_TUTOR_AXIS_POSTLAND), and
// the diagnosis was that it made one input accurate while every other input stayed calibrated to
// the old, wrong one. The point of a plan context is to let the whole cluster move together instead.
//
// Deliberately a raw view, not a copy: it is set on a hot enumeration path and must cost nothing.
// The pointers are owned by the caller and valid only for the duration of the Scope below.
struct PlanContext
{
    const std::vector<Action>* actions   = nullptr;  // the plan's action list
    std::size_t                index     = 0;        // which action is being decided
    const std::string*         land      = nullptr;  // land this plan plays (empty = holds it)
    bool                       land_done = false;    // true if `state` already has that land in play

    // Actions that come AFTER the one being decided -- "the rest of the plan". Defined out-of-line
    // in the provider TU (PlanContextRest, below) so this header stays free of TurnSolver.h.
};

// The rest of the plan as a [begin, end) range. Returns {nullptr, nullptr} when no plan is in scope.
// Declared here and defined in PlanContext.cpp so callers that DO include TurnSolver.h can walk it
// without this header pulling in the Action definition for everyone else.
std::pair<const Action*, const Action*> PlanContextRest(const PlanContext* pc);

// Null when no plan is in scope (real resolution, human play, or a provider call made outside plan
// enumeration). A provider MUST treat null as "no extra information" and behave exactly as before.
const PlanContext* CurrentPlanContext();

// RAII setter. Nests safely: restores whatever was in scope before.
class PlanContextScope
{
public:
    explicit PlanContextScope(const PlanContext* pc);
    ~PlanContextScope();
    PlanContextScope(const PlanContextScope&)            = delete;
    PlanContextScope& operator=(const PlanContextScope&) = delete;

private:
    const PlanContext* m_prev;
};

// PLAN TRAITS: what this turn's committed plan DOES, distilled for the mana payment layer
// (docs/design/mana-order-and-reserve-overhaul.md, layer 3). Where PlanContext above is a raw view
// for per-action decisions on the ENUMERATION path, PlanTraits is a one-shot summary computed at
// plan APPLICATION time (TurnSolver::ComputePlanTraits, called by both apply paths -- rollout
// ApplyPlanDirect and executor AIEngine::TakeTurn, through the one shared builder so the two cannot
// drift) and installed over the whole payment (prepay + casts) via PlanTraitsScope. Consumers:
// the prepay reserve ladder (BatchPrepayMainCasts), the per-payment one-shot hold, and
// ManaSourceRank's scaler bias. All consumers treat a null CurrentPlanTraits() as "no extra
// information" -- behave exactly as the static rules always did -- so real resolution, human play
// and every path outside a plan apply are byte-identical by construction.
struct PlanTraits
{
    bool main2                 = false;  // built in the POST-combat main: combat already happened,
                                         // so a body held back buys nothing this turn (goldfish)
    bool plan_has_own_pump     = false;  // a plan cast targets an OWN creature (target_own_creature
                                         // pump or a solo_target_trick)
    bool copy_magnet_live      = false;  // board has a copies_solo_targeted_spells permanent
                                         // (Zada / Mirrorwing): every own creature is a copy target
    bool bodies_are_multipliers= false;  // own-pump in plan AND magnet live -> the trick pays per
                                         // untapped body, so bodies out-value their mana this turn
    int  pump_target_card      = 0;      // projected pump target's card.m_number (0 = none): the
                                         // SAME picker the apply's auto-target uses, evaluated
                                         // pre-payment -- replaces the old every-turn greatest-
                                         // power-attacker proxy with a plan-gated exact hold
    bool casts_scaler_food     = false;  // plan casts a creature matching a board subtype-scaler's
                                         // subtype (Priest of Titania + Elves in the plan): the
                                         // scaler's burst GROWS if it taps after those casts
    bool attack_matters        = false;  // pre-combat main and an eligible attacker exists: a held
                                         // body converts to damage this turn
    int  mana_casts            = 0;      // plan casts that pay real mana (nonfree, ManaValue>0):
                                         // the PER-PAYMENT one-shot hold is only sound when this
                                         // is <=1 -- on a multi-cast turn the per-cast greedy
                                         // holding a one-shot strands the joint line (the retired
                                         // MTG_RESERVE failure mode; traced on MW gi75/gi141 at
                                         // UNBOUNDED budget), so multi-cast turns rely on the
                                         // whole-turn prepay ladder alone

    // --- M2-PAYLOAD RESERVE inputs (MTG_M2_PAYLOAD_RESERVE; overhaul ledger "fc96 s4") ---
    // The pre-combat payment is otherwise blind to the POST-combat payload sitting in hand
    // (fc96: DTL's lands-first Hellkite payment leaves 6 post-combat where Unite needs 7,
    // while an equally legal payment leaves 7 -- so the payment choice, not the plan, forecloses
    // the m2 kill). These three summaries let PayloadReserveMask (ManaPayment.cpp) see it.
    int  cast_color_mask       = 0;      // (1<<Color) bits of the plan's PERMANENT casts: they sit
                                         // on the battlefield by the m2, so a domain source's
                                         // post-cast yield/colours widen by exactly this set
                                         // (fc96: Hellkite's WUBRG lifts Faeburrow from 3 to 5)
    int  cast_mv_total         = 0;      // summed mana value of the real-mana casts: what the m1
                                         // will spend, for the payload-fits-the-pool precheck
    static constexpr int kMaxCastNames = 8;
    const std::string* cast_names[kMaxCastNames] = {};  // interned-name pointers of the plan's
    int  cast_name_count       = 0;      // casts (payload exclusion: a hand card the plan itself
                                         // casts is not an m2 payload; copies beyond the cast
                                         // count still qualify)

    // --- SCARCE-COLOR HOLD inputs (MTG_SCARCE_COLOR_HOLD; overhaul ledger "mw326 DTL") ---
    // Summed COLOURED cost pips of the real-mana casts, indexed by Color (W,U,B,R,G). When the
    // whole-turn prepay declines (e.g. the joint cost is only payable via a mid-turn Treasure
    // mint), the per-cast greedy pays each cast blind to the NEXT cast's colour needs -- mw326:
    // DTL's lands-first order paid Gold Rush {1}{G} with Gruul Turf, the board's ONLY {R} source,
    // stranding Mirrorwing's {R}{R}. ScarceColorHoldMask (ManaPayment.cpp) uses these totals to
    // hold scarce providers of colours the plan's OTHER casts still need.
    int  cast_pips[5]          = {};
    // The plan casts something that can INTRODUCE a new cast mid-turn -- a Treasure mint
    // (creates_treasures opens the deferred breakpoint) or a flood-engine draw (DigDraw /
    // DrawUntilNonland, the batch-pay drawsafe class). Those follow-on casts are invisible to
    // every plan-scope reserve AND to the cast_pips totals above, so the sole-colour-provider
    // rank tier (ManaSourceRankBase) fires only under this flag: it must protect a colour no
    // list can name. Everywhere else the rank stays untouched -- the first build applied the
    // demotion unconditionally and churned slivers/antilife/dragonstorm trains wholesale
    // (uniform 4->5 blocks) by reordering every payment including rollout interiors.
    bool mid_turn_casts        = false;
};

// Null when no plan apply is in scope (or every consumer lever is off -- the builder is not run).
const PlanTraits* CurrentPlanTraits();

// RAII setter, PlanContextScope's twin. Nests safely.
class PlanTraitsScope
{
public:
    explicit PlanTraitsScope(const PlanTraits* pt);
    ~PlanTraitsScope();
    PlanTraitsScope(const PlanTraitsScope&)            = delete;
    PlanTraitsScope& operator=(const PlanTraitsScope&) = delete;

private:
    const PlanTraits* m_prev;
};
