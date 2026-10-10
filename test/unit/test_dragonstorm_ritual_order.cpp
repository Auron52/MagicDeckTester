// Unit cover for the three Dragonstorm executor-order levers (docs/design/executor-ritual-payable-order.md),
// found on dragonstorm_smoke_d0_s1001 gi533 (seed 1534, d0): the T5 plan {Desperate Ritual, Seething Song,
// Rite of Flame, Pyretic Ritual, Apex of Power} pays exactly 10 in rank order (rituals first), but Apex
// makes the set ORDER-OPAQUE, so the executor cast it in raw HAND order -- Desperate, Seething, Apex --
// and Apex met 8 of its 10. The T5 kill became T8.
//
// The board here is the same shape on five Mountains (5 + the rituals' net 5 = 10):
//   rank order  Rite (5->6), Desperate (->7), Pyretic (->8), Seething (->10), Apex (10)   pays
//   hand order  Desperate (5->6), Seething (->8), Apex (8 < 10)                           fails
//
// What is pinned:
//   1. MTG_DRAGONSTORM_ORDER_OPAQUE: the provider opts into OrderOpaqueCastsByRank, so the rollout apply
//      (ApplyPlanDirect -- the executor's lockstep twin, same shared order functions) casts the opaque
//      set in rank order and Apex resolves; off (with the payable lever off too) it fails in hand order.
//   2. MTG_PAYABLE_ORDER_RITUALS: the payable-order rule takes an unpayable ritual order into its domain
//      and returns the nearest payable permutation; an order that already pays is returned untouched;
//      off -> the maker / reveal domain only, the order is not looked at. Alone it rescues the apply.
//   3. MTG_EXEC_FAILED_CAST_NO_RESOLVE: the d0 executor, which (with both order levers off) still casts
//      Apex in hand order and fails it, no longer runs a breakpoint re-solve for the unpaid cast.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsDro()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefDro(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void PutDro(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefDro(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

Card CardDro(const std::string& name, int number)
{
    Card c     = DefDro(name).card;
    c.m_number = number;
    return c;
}

int InHandDro(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

const char* kApex  = "Apex of Power";
const char* kRite  = "Rite of Flame";
const char* kPyr   = "Pyretic Ritual";
const char* kDesp  = "Desperate Ritual";
const char* kSeeth = "Seething Song";
const char* kScour = "Scourge of Valkas";

// The four levers this file reads, forced for this thread and restored on scope exit.
struct DroArms
{
    DroArms(bool opaque_rank, bool payable_rituals, bool failed_no_resolve = true)
    {
        heurarm::t_arm[heurarm::DRAGONSTORM_ORDER_OPAQUE]    = opaque_rank ? 1 : 0;
        heurarm::t_arm[heurarm::PAYABLE_ORDER_RITUALS]       = payable_rituals ? 1 : 0;
        heurarm::t_arm[heurarm::EXEC_FAILED_CAST_NO_RESOLVE] = failed_no_resolve ? 1 : 0;
        heurarm::t_arm[heurarm::PAYABLE_ORDER]               = 1;   // the adopted rule itself
    }
    ~DroArms()
    {
        heurarm::t_arm[heurarm::DRAGONSTORM_ORDER_OPAQUE]    = -1;
        heurarm::t_arm[heurarm::PAYABLE_ORDER_RITUALS]       = -1;
        heurarm::t_arm[heurarm::EXEC_FAILED_CAST_NO_RESOLVE] = -1;
        heurarm::t_arm[heurarm::PAYABLE_ORDER]               = -1;
    }
};

const DragonstormProvider& DsProvDro()
{
    static const DragonstormProvider p;
    return p;
}

// T5, pre-combat, land drop already made: five untapped Mountains, the hand in the gi533 HAND ORDER
// (Desperate, Seething, Apex, Rite, Pyretic -- plus `extra` at the end), a library of Mountains for
// Apex's exile.
GameState RitualTurn(const std::vector<std::string>& extra = {})
{
    EnsureCardsDro();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.m_provider = &DsProvDro();
    for (int k = 0; k < 5; ++k) { PutDro(s, "Mountain", 300 + k); }
    int num = 10;
    for (const char* n : { kDesp, kSeeth, kApex, kRite, kPyr }) { s.players[0].hand.push_back(CardDro(n, num++)); }
    for (const std::string& n : extra) { s.players[0].hand.push_back(CardDro(n, num++)); }
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(CardDro("Mountain", 900 + k)); }
    return s;
}

int CastCount(const TurnSolver::Plan& p)
{
    int n = 0;
    for (const Action& a : p.actions) { if (a.kind == Action::Kind::CastFromHand) { ++n; } }
    return n;
}

// The enumerated {four rituals, Apex} plan (no splice), with its actions re-laid in HAND order -- the
// raw plan order an opaque set was executed in.
TurnSolver::Plan RitualApexPlanInHandOrder(const GameState& s)
{
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* hit = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        if (CastCount(p) != 5) { continue; }
        bool ok = true;
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            if (a.splice_count != 0 || a.card_name.str() == kScour) { ok = false; break; }
        }
        if (ok) { hit = &p; break; }
    }
    REQUIRE_MESSAGE(hit != nullptr, "the {4 rituals, Apex} plan was not enumerated on five Mountains");
    TurnSolver::Plan out = *hit;
    std::vector<Action> casts, rest;
    for (const Action& a : out.actions) { (a.kind == Action::Kind::CastFromHand ? casts : rest).push_back(a); }
    std::vector<Action> laid;
    for (const char* n : { kDesp, kSeeth, kApex, kRite, kPyr })
    {
        auto it = std::find_if(casts.begin(), casts.end(), [&](const Action& a) { return a.card_name.str() == n; });
        REQUIRE(it != casts.end());
        laid.push_back(*it);
    }
    laid.insert(laid.end(), rest.begin(), rest.end());
    out.actions = laid;
    out.searched_order = false;
    return out;
}

std::vector<int> CastIdx(const TurnSolver::Plan& p)
{
    std::vector<int> v;
    for (int i = 0; i < static_cast<int>(p.actions.size()); ++i)
    { if (p.actions[i].kind == Action::Kind::CastFromHand) { v.push_back(i); } }
    return v;
}

std::vector<std::string> Names(const TurnSolver::Plan& p, const std::vector<int>& ord)
{
    std::vector<std::string> v;
    for (int i : ord) { v.push_back(p.actions[i].card_name.str()); }
    return v;
}

// Does `ord` pay, walked against the board + the rituals' float in that order? (An independent
// aggregate walk: Mountains are wild-enough red here -- every cost and every float is red / generic.)
bool WalkPays(const GameState& s, const TurnSolver::Plan& p, const std::vector<int>& ord)
{
    int pool = AvailableManaPool(s).Total();
    for (int i : ord)
    {
        const Action& a = p.actions[i];
        if (pool < a.cost.ManaValue()) { return false; }
        pool += a.ritual_float - a.cost.ManaValue();
    }
    return true;
}

}   // namespace

TEST_CASE("Dragonstorm opts into the opaque rank order only under MTG_DRAGONSTORM_ORDER_OPAQUE")
{
    const GameState s = RitualTurn();
    {
        DroArms arms(/*opaque_rank=*/true, /*payable_rituals=*/false);
        CHECK(DsProvDro().OrderOpaqueCastsByRank());
        CHECK(OpaqueCastOrderActive(s));
    }
    {
        DroArms arms(false, false);
        CHECK_FALSE(DsProvDro().OrderOpaqueCastsByRank());
        CHECK_FALSE(OpaqueCastOrderActive(s));   // (MTG_ORDER_OPAQUE is unset in the test env)
    }
}

TEST_CASE("Opaque {4 rituals, Apex} in hand order: Apex fails with both order levers off, resolves with the rank order")
{
    const GameState s0 = RitualTurn();
    TurnSolver::Plan plan;
    {
        DroArms arms(true, true);
        plan = RitualApexPlanInHandOrder(s0);
    }
    // The set IS order-opaque (Apex re-solves), so it never takes the clean rank-sorted branch by itself.
    CHECK(OrderingOpaque(kApex));

    {   // BEFORE: hand order, Apex at 8 of 10 -> it stays in hand.
        DroArms arms(/*opaque_rank=*/false, /*payable_rituals=*/false);
        GameState after = s0;
        TurnSolver::ApplyPlan(after, plan, /*is_pre_combat=*/true);
        CHECK(InHandDro(after, kApex) == 1);
    }
    {   // MTG_DRAGONSTORM_ORDER_OPAQUE alone: rank order, Apex paid and resolved.
        DroArms arms(true, false);
        GameState after = s0;
        TurnSolver::ApplyPlan(after, plan, true);
        CHECK(InHandDro(after, kApex) == 0);
        for (const char* n : { kRite, kDesp, kPyr, kSeeth }) { CHECK(InHandDro(after, n) == 0); }
    }
    {   // MTG_PAYABLE_ORDER_RITUALS alone rescues the hand order too.
        DroArms arms(false, true);
        GameState after = s0;
        TurnSolver::ApplyPlan(after, plan, true);
        CHECK(InHandDro(after, kApex) == 0);
    }
}

TEST_CASE("Payable order takes an unpayable ritual order into its domain (MTG_PAYABLE_ORDER_RITUALS)")
{
    const GameState s = RitualTurn();
    TurnSolver::Plan plan;
    {
        DroArms arms(true, true);
        plan = RitualApexPlanInHandOrder(s);
    }
    const std::vector<int> hand_order = CastIdx(plan);
    REQUIRE(Names(plan, hand_order) == std::vector<std::string>{ kDesp, kSeeth, kApex, kRite, kPyr });
    REQUIRE_FALSE(WalkPays(s, plan, hand_order));

    {   // ON: the nearest payable permutation of the same five casts.
        DroArms arms(false, true);
        std::vector<int> ord = hand_order;
        ApplyPayableCastOrder(s, plan.actions, ord);
        CHECK(ord != hand_order);
        CHECK(WalkPays(s, plan, ord));
        std::vector<int> a = ord, b = hand_order;
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        CHECK(a == b);                                   // the same casts, only reordered
        CHECK(Names(plan, ord).back() == kApex);         // the payoff after every ritual that funds it
    }
    {   // OFF: no maker, no reveal cast -> outside the domain, untouched.
        DroArms arms(false, false);
        std::vector<int> ord = hand_order;
        ApplyPayableCastOrder(s, plan.actions, ord);
        CHECK(ord == hand_order);
    }
    {   // ON, an order that already pays (the rank order) is returned untouched.
        DroArms arms(false, true);
        std::vector<int> ord = hand_order;
        std::stable_sort(ord.begin(), ord.end(), [&](int x, int y)
        { return CastOrderLess(s, plan.actions[x], plan.actions[y]); });
        REQUIRE(WalkPays(s, plan, ord));
        const std::vector<int> before = ord;
        ApplyPayableCastOrder(s, plan.actions, ord);
        CHECK(ord == before);
    }
}

TEST_CASE("Payable order rituals: a two-ritual chain that cannot pay as given is reordered (two Mountains)")
{
    // Seething Song {2}{R} first cannot be paid off two Mountains; Rite of Flame first floats RR, and then
    // Seething is 1 + 2 = 3. Only the order changes.
    EnsureCardsDro();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.m_provider = &DsProvDro();
    PutDro(s, "Mountain", 300);
    PutDro(s, "Mountain", 301);
    std::vector<Action> acts(2);
    acts[0].kind = Action::Kind::CastFromHand; acts[0].card_name = kSeeth; acts[0].def = &DefDro(kSeeth);
    acts[0].cost = DefDro(kSeeth).card.m_mana_cost; acts[0].ritual_float = 5;
    acts[1].kind = Action::Kind::CastFromHand; acts[1].card_name = kRite; acts[1].def = &DefDro(kRite);
    acts[1].cost = DefDro(kRite).card.m_mana_cost; acts[1].ritual_float = 2;
    {
        DroArms arms(false, true);
        std::vector<int> ord = { 0, 1 };
        ApplyPayableCastOrder(s, acts, ord);
        CHECK(ord == std::vector<int>{ 1, 0 });
    }
    {
        DroArms arms(false, false);
        std::vector<int> ord = { 0, 1 };
        ApplyPayableCastOrder(s, acts, ord);
        CHECK(ord == std::vector<int>{ 0, 1 });
    }
}

TEST_CASE("Executor d0: an UNPAID draw-engine cast opens no breakpoint re-solve (MTG_EXEC_FAILED_CAST_NO_RESOLVE)")
{
    // Both order levers OFF, so the d0 executor realises the {4 rituals, Apex} plan in hand order and Apex
    // fails at 8 of 10 -- the gi533 shape. The rollout's apply_one returns on a failed payment before any
    // arming; the executor's uncommitted branch armed resolve_draw_breakpoint anyway, and here (nothing
    // but the failed Apex to re-solve towards, re-cast first in hand order every time) it spins to the
    // depth cap. With the lever the pass just carries on: Rite and Pyretic are cast, the d0 second pass
    // is still requested, and that pass casts Apex off the full 10.
    auto resolver = [](GameState& st)
    {
        while (!st.stack.empty())
        {
            const StackEntry e = st.stack.back();
            st.stack.pop_back();
            const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
            if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
        }
    };
    for (const bool lever : { false, true })
    {
        CAPTURE(lever);
        DroArms arms(/*opaque_rank=*/false, /*payable_rituals=*/false, /*failed_no_resolve=*/lever);
        GameState s = RitualTurn();
        AIEngine eng;   // depth 0: the d0 runner
        const long long before = MtgTestSeam::BpResolves(eng);
        const bool second_pass = eng.TakeTurn(s, /*is_pre_combat_main=*/true, resolver);
        const long long resolves = MtgTestSeam::BpResolves(eng) - before;
        CHECK(InHandDro(s, kApex) == 1);   // the hand-order Apex failed in both arms
        if (lever)
        {
            CHECK(resolves == 0);
            CHECK(second_pass);
            CHECK(InHandDro(s, kRite) == 0);
            CHECK(InHandDro(s, kPyr) == 0);
            CHECK(AvailableManaPool(s).Total() == 10);
            eng.TakeTurn(s, true, resolver);
            CHECK(InHandDro(s, kApex) == 0);
        }
        else
        {
            CHECK(resolves > 0);   // the phantom breakpoint the lever removes
        }
    }
}
