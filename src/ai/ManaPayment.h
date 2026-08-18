#pragma once
// THE single-attempt mana payment (backlog C1, unification step 2). The executor and the rollout
// each carried a twin ~380-line copy of this function kept in sync by comment discipline -- and
// every historical
// divergence between them was a real bug (coloured-pip EffectiveProduces, Karoo two-colour
// credit, storage burst; see docs/design/rollout-executor-lockstep.md). Both now delegate here.
//
// What actually differed between the twins, now parameters:
//   available        - the executor's turn-scoped accounting pool, decremented as sources tap;
//                      nullptr for the rollout, which keeps no such pool. NOTE: accounting is
//                      deliberately NOT rolled back on a failed attempt (preserved executor
//                      behaviour -- the TapForCost wrapper snapshots/restores it around the
//                      reserved attempt instead).
//   honor_legacy_cco - the rollout passes true so the MTG_LEGACY_CCO_PAY measurement hatch can
//                      re-enable the old (rules-violating) EffectiveProduces payment in the
//                      scarcity path; the executor passes false (it never had that hatch).
//
// One former divergence was resolved rather than parameterized: in the legacy (MTG_TAP_LEGACY)
// step-1 path the executor still read EffectiveProduces where the rollout read the payment-legal
// ProducesForPayment -- the executor was the unfixed twin of the 6bb2791 coloured-pip fix,
// reachable only under that opt-in hatch. Unified on ProducesForPayment (default config
// byte-identical: the path never runs with MTG_TAP_LEGACY unset).
#include "../core/GameState.h"
#include "TurnSolver.h"   // Action
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

bool TapForCostSharedOnce(GameState& state, const ManaCost& cost_in, bool for_creature,
                          std::uint64_t reserved_mask, ManaPool* available,
                          bool honor_legacy_cco);

// THE effective spell cost (C1 unit 2): spectacle, splice-onto-Arcane combining, affinity,
// Medallion-style colour reduction, and Hinata's per-target discount (fixed-cost spells only --
// {X} spells apply the discount where the chosen X is known). Was a byte-identical twin pair
// (AIEngine::EffectiveCost / TurnSolver's file-static EffectiveCost); both now delegate here.
ManaCost EffectiveSpellCost(const CardDefinition& def, const GameState& state, int copies = 1);

// THE cast-order comparator and its opaque-set guard (C1 unit 3): provider RANK first, then
// cheapest-first among mana accelerants by the action's ACTUAL cost; the reorder is skipped
// entirely for sets containing a mid-turn re-solve breakpoint (OrderingOpaque -- the search owns
// that ordering). Was a byte-identical twin pair (TurnSolver statics / AIEngine's CastOrderLessAI
// and OrderingOpaqueAI kept in lockstep by comment); both now share these definitions.
bool CastOrderLess(const GameState& state, const Action& a, const Action& b);
bool OrderingOpaque(const std::string& name);

// The same comparator with the two SORT KEYS supplied by the caller. CastOrderLess is this
// function with the keys read from the provider; the ladder below passes an EFFECTIVE key instead,
// so a spell walked down its range sorts at its new position without a second comparator to keep
// in lockstep with this one.
bool CastOrderLessRanked(const GameState& state, const Action& a, int key_a,
                                                 const Action& b, int key_b);

// THE cast sort key: the provider RANK, with the card's natural main PHASE folded in as the
// tie-break beneath it.
//
//   USER 2026-08-18: "Generally, m1 cards should be first in the order if there is no reason to
//   do it otherwise."
//
// The rank IS "a reason to do it otherwise", so it stays the primary key and the phase only
// separates cards the rank ties -- Ignoble Hierarch (m1) ahead of Birds of Paradise (m2), both
// rank-10 dorks. This is also the only way the m1/m2 classification reaches play at all today:
// the pre-combat Main2 FILTER is opt-in per provider (ClassifiesMainPhases) and no provider opts
// in, so without this the phase column is advisory. Gate: MTG_ORDER_M1_FIRST. Off -> the phase
// term is constant and the key is the rank scaled, which orders identically to the bare rank.
int  CastOrderKey(const GameState& state, const CardDefinition* def, int rank);
bool OrderM1FirstEnabled();   // MTG_ORDER_M1_FIRST

// ---- The cast-order RANGE and its fallback ladder (step 2 of the USER's design) ---------------
// docs/design/cast-order-ideal-with-ranges.md:
//
//   "We only search order in order to ensure our ideal order can be cast and choose a less ideal
//    order if not. ... It should be from ideal -> cost-efficient."
//
// A spell that affects affordability does not get a position, it gets a RANGE. `ideal` is where
// the ordering principles want it (draw first, reducer before the reduced, enabler before the
// payoff); `cost_efficient` is the un-promoted rank, which is the order that is known to pay
// because it is the order the engine has always used. ideal == cost_efficient for every card
// with no promotion, and that is every card when MTG_IDEAL_ORDER is off -> the ladder is inert.
struct CastOrderRange
{
    int  ideal          = 0;
    int  cost_efficient = 0;
    bool Ranged() const { return ideal < cost_efficient; }
};
CastOrderRange CastOrderRangeOf(const GameState& state, const CardDefinition& def);
bool           CastOrderRangeEnabled();   // MTG_ORDER_RANGE

// Step 3: does the OPAQUE apply path order its casts, instead of keeping plan order? This is what
// gives the range a domain -- every promoted card in the database is OrderingOpaque (verified over
// cards.json: 11 promoted cards, 0 of them clean), so with the opaque path in plan order the
// ladder above can never fire. Retiring the bail-out does NOT mean routing these sets down the
// clean branch: the breakpoint / staging machinery lives in the opaque one and must stay. Only the
// ORDER of the non-enabler casts changes. MTG_ORDER_OPAQUE, default off.
bool OpaqueCastOrderEnabled();

// THE fallback ladder. `order` holds indices into `actions` already sorted the way the caller
// sorts today; on return it is the IDEAL order, with each ranged spell walked toward its
// cost-efficient end only as far as paying for the line requires. Never reorders a spell that has
// no range, never makes a payable line unpayable (its terminal rung is the cost-efficient order),
// and returns `order` untouched when the lever is off, when no spell in the set has a range, or
// when the set's costs cannot be projected (see the definition).
void ApplyCastOrderRangeLadder(const GameState& state, const std::vector<Action>& actions,
                               std::vector<int>& order);

// THE ENABLER/WIPE RECHECK -- the one line in the suite that a cast-ORDER cannot express.
//
//   USER 2026-08-18: "One tricky thing about this deck is that Tainted Remedy + Reverent Silence
//   + Tainted Remedy + Reverent Silence is an actual play that interferes with the order a bit."
//   ... "So essentially we are allowed to recheck Tainted Remedy + Reverent Silence at the end
//   of the list."
//
// Reverent Silence's alt cost gifts the opponent 6 life, which a Tainted Remedy flips into 6
// damage -- and its effect then DESTROYS that Remedy. So a second Silence needs a second Remedy
// resolved after the first wipe. A rank is a total order on card TYPES and cannot alternate two
// of them, so a rank sort emits Remedy, Remedy, Silence, Silence: the first wipe kills BOTH
// Remedies and the second Silence gifts 6 life UNBACKED (the guards then decline it, and the
// deck loses 6 damage it was entitled to).
//
// The USER's resolution is a second PASS rather than a cleverer rank: run the order once, then
// re-offer the enabler/wipe pair at the end for the copies that are left. It is a permutation of
// casts the plan already holds -- nothing is added, nothing is dropped. Gate: MTG_ORDER_RECHECK.
void ApplyEnablerWipeRecheck(const GameState& state, const std::vector<Action>& actions,
                             std::vector<int>& order);

// MTG_ORDER_RECHECK. Shared with the ENUMERATION guard (SubsetHasUnbackedAltPayload), which may
// only admit a replacement-enabler subset when this ordering pass is there to alternate it.
bool OrderRecheckEnabled();

// THE accounting mana pool (C1 unit 4): everything the active player's untapped sources could
// produce this phase, plus the turn-scoped floating reserve. Was a byte-identical twin pair
// (TurnSolver's file-static BuildPool / AIEngine::BuildAvailableMana); both sides now call this.
// `skip`, when non-null, EXCLUDES that one permanent's contribution -- the "what would I still be
// able to pay if this source were tapped?" question. Used by the combat hold-back rule to tell a
// dork whose mana is actually funding a play from one whose tap nothing needs. nullptr (the default,
// and every pre-existing call site) is byte-identical to the original.
// (core/SpellEffects.h forward-declares this WITHOUT the default -- the default may be given only
// once, and this header is the canonical declaration everything else includes.)
ManaPool AvailableManaPool(const GameState& state, const Permanent* skip = nullptr);

// THE public payment entry (C1 unit 5). Mana-source RESERVATION ("leaving sources up"): FIRST try
// to pay while HOLDING the special sources (dorks / {C}-manlands / depletion) untapped; only if the
// cost cannot be met without them fall through to the normal payment. Slack-only, so it is weakly
// dominant. MTG_NO_RESERVE -> mask 0 -> a single normal attempt. Was a twin pair
// (AIEngine::TapForCost / TurnSolver's TapForCostDirect) differing only in the `available`
// accounting snapshot; parameters as in TapForCostSharedOnce (executor: &pool,false;
// rollout: nullptr,true).
bool TapForCostShared(GameState& state, const ManaCost& cost_in, bool for_creature,
                      ManaPool* available, bool honor_legacy_cco);

// PLAN-SCOPED source reservation -- the card numbers of battlefield sources the plan being applied
// must hold untapped for a LATER cast in the same plan. It rides the SAME reserve-then-fallback
// retry as ReservableSpecialMask (try holding them; if the cost cannot be met, pay normally), so it
// can only ever change WHICH sources pay, never whether a cost is payable.
//
// The one producer today is the same-turn mana-unlock equip (TurnSolver::ManaUnlockColorReserve):
// the plan casts a dork, equips haste onto it and spends its mana on a later cast -- but the dork's
// colours are not the later cast's colours, so the per-cast greedy would happily spend the line's
// ONLY red source on a generic pip of the ENABLERS and strand the payoff (FiveColour seed 3 turn 3:
// Bloom Tender makes {W}{B}{G}, Mana Cannons needs {R} off the lone Steam Vents). The whole-turn
// batch pre-pay cannot cover this: it declines, because before the unlock the turn's combined cost
// is unaffordable. See docs/design/mana-source-reservation.md.
//
// Empty for every other plan -> byte-identical. Set/cleared by PlanSourceReserveScope.
extern thread_local std::vector<int> g_plan_reserved_sources;

// RAII for g_plan_reserved_sources: sets it for the plan's cast section and restores the previous
// value on scope exit (nested plan applications -- a rollout inside an apply -- restore correctly).
// Release() drops the reservation early, which the unlock hoist calls the moment the dork is hasted:
// from then on the reserved source is exactly what the payoff wants to tap.
class PlanSourceReserveScope
{
public:
    explicit PlanSourceReserveScope(std::vector<int> nums)
        : m_prev(std::move(g_plan_reserved_sources))
    { g_plan_reserved_sources = std::move(nums); }
    ~PlanSourceReserveScope() { g_plan_reserved_sources = std::move(m_prev); }
    static void Release() { g_plan_reserved_sources.clear(); }
    PlanSourceReserveScope(const PlanSourceReserveScope&)            = delete;
    PlanSourceReserveScope& operator=(const PlanSourceReserveScope&) = delete;
private:
    std::vector<int> m_prev;
};

// Post-spell mana sinks (C1 unit 5): animate manlands (Mutavault) / tap-and-pay token abilities
// (Sliver Hive), run pre-combat so the creatures can attack. Twin pairs
// (AIEngine::{AnimateLands,ActivateTapTokens} / TurnSolver's Simulate{AnimateLands,TapTokens})
// with one PRESERVED structural divergence each around the affordability gate -- see the
// definitions. `available` = the executor's accounting pool, nullptr for the rollout (which also
// selects the rollout's MTG_LEGACY_CCO_PAY hatch, the poolless caller being the only one with it).
void AnimateLandsShared(GameState& state, ManaPool* available);
void ActivateTapTokensShared(GameState& state, ManaPool* available);
