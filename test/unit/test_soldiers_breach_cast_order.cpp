// Unit cover for two USER-APPROVED cast orders (2026-10-14; docs/design/soldiers-breaching-cast-order-2026-10-15.md):
//
//   Soldiers (MTG_SOLDIERS_ORDER; USER: "Soldiers' order also looks good."):
//     land > Champion of the Parish > Recruiter of the Guard > Ranger-Captain of Eos > Thalia's Lieutenant
//     > Aether Vial > Recruitment Officer > Esper Sentinel > Thalia, Guardian of Thraben > Coppercoat Vanguard
//     > Cathar Commando > Jirina > Harbin > Brutal Cathar > Field Marshal > General Kudro > King Darien XLVIII
//     > Rick (slots 6-17 order-free; the measured 10-04 sequence keeps it total).
//   BreachingDragonstorm (MTG_BREACH_ORDER; USER: "your order is fine"):
//     land > Breaching Dragonstorm > Boarding Party > Annoyed Altisaur > Maelstrom Wanderer > Creative
//     Technique > Call Forth the Tempest > Sakashima's Protege, with OrderOpaqueCastsByRank so the rank
//     applies to this all-opaque deck at all.
//
// And the USER's cross-deck ruling (2026-10-14): "Let's fully order the cards in each list inasmuch as
// possible. I don't like the randomness of choosing the order." So, per deck:
//   1. the rank lays the cards out in exactly the approved list (lever on);
//   2. the shared comparator is TOTAL over the deck's distinct names -- for every pair exactly one of
//      the two orders is "less" -- with a card the rank does not know falling back to its NAME;
//   3. permuting the hand (the plan vector) never changes the realised order, Vial puts included;
//   4. lever off is the old order (VialProvider's generic tiers / Breaching's plan order).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <algorithm>
#include <random>
#include <string>
#include <vector>

namespace
{

void EnsureCardsSB()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefSB(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void PutSB(GameState& s, const std::string& name, int number, int charge = 0)
{
    Permanent p;
    p.card             = DefSB(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    p.charge_counters  = charge;
    s.battlefield.push_back(p);
}

// Force one lever for this thread, restored on scope exit.
struct ArmSB
{
    heurarm::Slot slot;
    ArmSB(heurarm::Slot s, bool on) : slot(s) { heurarm::t_arm[slot] = on ? 1 : 0; }
    ~ArmSB() { heurarm::t_arm[slot] = -1; }
};

// A pre-combat main with the land drop made and `lands` (name, count) untapped -- enough mana that the
// payable-order fallback never has a reason to move anything -- and the given provider.
GameState BoardSB(const DecisionProvider& prov, const std::vector<std::pair<std::string, int>>& lands)
{
    EnsureCardsSB();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 9;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.m_provider = &prov;
    int num = 300;
    for (const auto& [name, n] : lands) { for (int k = 0; k < n; ++k) { PutSB(s, name, num++); } }
    for (int k = 0; k < 20; ++k)
    {
        Card c = DefSB(lands.front().first).card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

// A canonical-route plan (searched_order false) casting `names` in VECTOR order; a name in `puts` is an
// Aether Vial put instead of a hand cast.
TurnSolver::Plan PlanSB(const std::vector<std::string>& names, const std::vector<std::string>& puts = {})
{
    TurnSolver::Plan plan;
    plan.land_decided = true;
    for (const std::string& n : names)
    {
        Action a;
        const bool put = std::find(puts.begin(), puts.end(), n) != puts.end();
        a.kind      = put ? Action::Kind::ActivateVial : Action::Kind::CastFromHand;
        a.card_name = n;
        a.def       = &DefSB(n);
        plan.actions.push_back(a);
    }
    plan.searched_order = false;
    return plan;
}

Action CastSB(const std::string& name)
{
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = name;
    a.def       = &DefSB(name);
    return a;
}

// Identity, reverse, and 40 fixed-seed shuffles of `v`.
std::vector<std::vector<std::string>> PermutationsSB(const std::vector<std::string>& v)
{
    std::vector<std::vector<std::string>> out;
    out.push_back(v);
    out.emplace_back(v.rbegin(), v.rend());
    std::mt19937 rng(20261015u);
    for (int k = 0; k < 40; ++k)
    {
        std::vector<std::string> p = v;
        std::shuffle(p.begin(), p.end(), rng);
        out.push_back(p);
    }
    return out;
}

// Every ordered pair of DISTINCT names: exactly one direction compares less (the order is total).
void CheckTotalSB(const GameState& s, const std::vector<std::string>& names)
{
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        for (std::size_t j = 0; j < names.size(); ++j)
        {
            if (i == j) { continue; }
            const Action a = CastSB(names[i]);
            const Action b = CastSB(names[j]);
            const bool ab = CastOrderLess(s, a, b);
            const bool ba = CastOrderLess(s, b, a);
            CHECK_MESSAGE(ab != ba, "tie between ", names[i], " and ", names[j]);
        }
    }
}

const std::vector<std::string> kSoldiersOrder = {
    "Champion of the Parish", "Recruiter of the Guard", "Ranger-Captain of Eos", "Thalia's Lieutenant",
    "Aether Vial", "Recruitment Officer", "Esper Sentinel", "Thalia, Guardian of Thraben",
    "Coppercoat Vanguard", "Cathar Commando", "Jirina, Dauntless General", "Harbin, Vanguard Aviator",
    "Brutal Cathar", "Field Marshal", "General Kudro of Drannith", "King Darien XLVIII",
    "Rick, Steadfast Leader",
};

const std::vector<std::string> kBreachOrder = {
    "Breaching Dragonstorm", "Boarding Party", "Annoyed Altisaur", "Maelstrom Wanderer",
    "Creative Technique", "Call Forth the Tempest", "Sakashima's Protege",
};

const SoldiersProvider& SoldiersProvSB()
{
    static const SoldiersProvider p;
    return p;
}

const BreachingDragonstormProvider& BreachProvSB()
{
    static const BreachingDragonstormProvider p;
    return p;
}

GameState SoldiersBoardSB()
{
    // Unclaimed Territory pays any colour for a creature spell and {C} for Aether Vial.
    return BoardSB(SoldiersProvSB(), {{"Unclaimed Territory", 45}});
}

GameState BreachBoardSB()
{
    return BoardSB(BreachProvSB(), {{"Mountain", 16}, {"Forest", 16}, {"Island", 16}});
}

}   // namespace

TEST_CASE("Soldiers cast order: the rank lays the deck out in the USER-approved list")
{
    ArmSB arm(heurarm::SOLDIERS_ORDER, true);
    const GameState s = SoldiersBoardSB();
    const SoldiersProvider& prov = SoldiersProvSB();
    for (std::size_t i = 1; i < kSoldiersOrder.size(); ++i)
    {
        CAPTURE(kSoldiersOrder[i - 1]);
        CAPTURE(kSoldiersOrder[i]);
        CHECK(prov.CastOrderRank(s, DefSB(kSoldiersOrder[i - 1])) < prov.CastOrderRank(s, DefSB(kSoldiersOrder[i])));
    }
    CHECK(prov.CastOrderTotalByName());
    // The order-free block (slots 7, 9-17) is cheapest first: its 10-04 sequence never puts a dearer card
    // ahead of a cheaper one, so mana value -- a param -- decides it wherever it can.
    int prev_mv = -1;
    for (const char* n : {"Esper Sentinel", "Coppercoat Vanguard", "Cathar Commando", "Jirina, Dauntless General",
                          "Harbin, Vanguard Aviator", "Brutal Cathar", "Field Marshal", "General Kudro of Drannith",
                          "King Darien XLVIII", "Rick, Steadfast Leader"})
    {
        const int mv = DefSB(n).card.m_mana_cost.ManaValue();
        CHECK_MESSAGE(mv >= prev_mv, n);
        prev_mv = mv;
    }
}

TEST_CASE("Soldiers cast order: TOTAL -- no two different cards tie, an unknown card falls back to its name")
{
    ArmSB arm(heurarm::SOLDIERS_ORDER, true);
    const GameState s = SoldiersBoardSB();
    std::vector<std::string> names = kSoldiersOrder;
    // Two cards the Soldiers rank does not know (another deck's creatures) tie on rank: the NAME breaks it.
    names.push_back("Goblin Guide");
    names.push_back("Monastery Swiftspear");
    CheckTotalSB(s, names);
    CHECK(SoldiersProvSB().CastOrderRank(s, DefSB("Goblin Guide"))
          == SoldiersProvSB().CastOrderRank(s, DefSB("Monastery Swiftspear")));
    CHECK(CastOrderLess(s, CastSB("Goblin Guide"), CastSB("Monastery Swiftspear")));
    CHECK_FALSE(CastOrderLess(s, CastSB("Monastery Swiftspear"), CastSB("Goblin Guide")));
    // ...and after every card the deck does know.
    CHECK(CastOrderLess(s, CastSB("Rick, Steadfast Leader"), CastSB("Goblin Guide")));
}

TEST_CASE("Soldiers cast order: permuting the hand never changes the realised order (casts and Vial puts)")
{
    ArmSB arm(heurarm::SOLDIERS_ORDER, true);
    GameState s = SoldiersBoardSB();
    PutSB(s, "Aether Vial", 50, 2);
    for (const std::vector<std::string>& perm : PermutationsSB(kSoldiersOrder))
    {
        CHECK(TurnSolver::RealisedNonSacCastOrder(s, PlanSB(perm)) == kSoldiersOrder);
        // Vial-put the MV-2 Humans instead: a put is sequenced at its creature's slot (USER 2026-10-06).
        CHECK(TurnSolver::RealisedNonSacCastOrder(
                  s, PlanSB(perm, {"Thalia's Lieutenant", "Coppercoat Vanguard", "Cathar Commando"}))
              == kSoldiersOrder);
    }
    // Two copies of one card stay together, in place.
    std::vector<std::string> dup = {"Field Marshal", "Champion of the Parish", "Field Marshal", "Esper Sentinel"};
    CHECK(TurnSolver::RealisedNonSacCastOrder(s, PlanSB(dup))
          == std::vector<std::string>{"Champion of the Parish", "Esper Sentinel", "Field Marshal", "Field Marshal"});
}

TEST_CASE("Soldiers cast order: lever off is VialProvider's generic order, ties kept in plan order")
{
    ArmSB arm(heurarm::SOLDIERS_ORDER, false);
    const GameState s = SoldiersBoardSB();
    static const VialProvider vial;
    for (const std::string& n : kSoldiersOrder)
    { CHECK(SoldiersProvSB().CastOrderRank(s, DefSB(n)) == vial.CastOrderRank(s, DefSB(n))); }
    CHECK_FALSE(SoldiersProvSB().CastOrderTotalByName());
    // Every creature ties at the generic 10, so plan order decides -- the "randomness" the order removes.
    CHECK(TurnSolver::RealisedNonSacCastOrder(s, PlanSB({"Esper Sentinel", "Champion of the Parish"}))
          == std::vector<std::string>{"Esper Sentinel", "Champion of the Parish"});
}

TEST_CASE("BreachingDragonstorm cast order: the rank lays the deck out in the USER-approved list, and applies")
{
    ArmSB arm(heurarm::BREACH_ORDER, true);
    const GameState s = BreachBoardSB();
    const BreachingDragonstormProvider& prov = BreachProvSB();
    for (std::size_t i = 1; i < kBreachOrder.size(); ++i)
    {
        CAPTURE(kBreachOrder[i - 1]);
        CAPTURE(kBreachOrder[i]);
        CHECK(prov.CastOrderRank(s, DefSB(kBreachOrder[i - 1])) < prov.CastOrderRank(s, DefSB(kBreachOrder[i])));
    }
    // Every nonland card here is ORDER-OPAQUE (cascade / Creative Technique / Breaching Dragonstorm), so
    // without the opt-in no rank is ever read and the deck casts in plan (= hand) order.
    for (const std::string& n : kBreachOrder) { CHECK_MESSAGE(OrderingOpaque(n), n); }
    CHECK(prov.OrderOpaqueCastsByRank());
    CHECK(OpaqueCastOrderActive(s));
    CHECK(prov.CastOrderTotalByName());
}

TEST_CASE("BreachingDragonstorm cast order: TOTAL, and permuting the hand never changes the realised order")
{
    ArmSB arm(heurarm::BREACH_ORDER, true);
    const GameState s = BreachBoardSB();
    std::vector<std::string> names = kBreachOrder;
    names.push_back("Goblin Guide");
    names.push_back("Monastery Swiftspear");
    CheckTotalSB(s, names);
    for (const std::vector<std::string>& perm : PermutationsSB(kBreachOrder))
    { CHECK(TurnSolver::RealisedNonSacCastOrder(s, PlanSB(perm)) == kBreachOrder); }
}

TEST_CASE("BreachingDragonstorm cast order: lever off is the generic rank and plan order")
{
    ArmSB arm(heurarm::BREACH_ORDER, false);
    const GameState s = BreachBoardSB();
    const BreachingDragonstormProvider& prov = BreachProvSB();
    static const GenericProvider generic;
    for (const std::string& n : kBreachOrder)
    { CHECK(prov.CastOrderRank(s, DefSB(n)) == generic.CastOrderRank(s, DefSB(n))); }
    CHECK_FALSE(prov.OrderOpaqueCastsByRank());
    CHECK_FALSE(prov.CastOrderTotalByName());
    CHECK_FALSE(OpaqueCastOrderActive(s));
}
