// Unit cover for MTG_PAYABLE_ORDER (USER 2026-09-27, Pirates). Hand Malcolm, the Eyes {U}{R}, Corsair
// Captain {2}{U}, Daring Buccaneer {R} ("reveal a Pirate card from your hand or pay {2}"), five lands.
// Six mana of cost; the Corsair's enter-trigger Treasure is the sixth. The reviewed order (Malcolm,
// Buccaneer revealing the Corsair, Corsair) brings the Treasure in last and strands a cast; the maker
// hoist (Corsair, Malcolm, Buccaneer) leaves the Buccaneer nothing to reveal -> {2}{R}. Corsair,
// Buccaneer (revealing Malcolm), Malcolm casts all three. USER: "it really is just for the 'fail to
// pay' case, that this is worth doing" / "If we have mana for all of them, then there is no need to
// change the order."
//
// What is pinned here:
//   1. lever ON, five lands: the enumerator OFFERS the three-cast plan, the shared decider realises
//      Corsair -> Buccaneer -> Malcolm, and the rollout apply resolves all three with the Treasure
//      spent and the Buccaneer paid {R} (it revealed Malcolm);
//   2. lever ON, six lands (mana for all): the reviewed order is kept unchanged;
//   3. lever OFF: exactly as before -- the three-cast plan is not offered and the decider is the
//      old reveal-blind maker hoist (Corsair, Malcolm, Buccaneer).
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/EffectHandler.h"
#include "core/SpellEffects.h"

#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsPo()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefPo(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void PutPo(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefPo(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

Card HandPo(const std::string& name, int number)
{
    Card c     = DefPo(name).card;
    c.m_number = number;
    return c;
}

int CountNamedPo(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

// The two levers under test, forced for this thread and restored on scope exit.
struct PoArms
{
    PoArms(bool order_on, bool payable_on)
    {
        heurarm::t_arm[heurarm::PIRATES_CAST_ORDER] = order_on ? 1 : 0;
        heurarm::t_arm[heurarm::PAYABLE_ORDER]      = payable_on ? 1 : 0;
    }
    ~PoArms()
    {
        heurarm::t_arm[heurarm::PIRATES_CAST_ORDER] = -1;
        heurarm::t_arm[heurarm::PAYABLE_ORDER]      = -1;
    }
};

const DecisionProvider& Pirates()
{
    static const PiratesProvider prov;
    return prov;
}

// T4, pre-combat: Islands + two Mountains untapped (5 lands = Island x3; 6 = Island x4), hand
// Malcolm, Corsair Captain, Daring Buccaneer (no Metallic Mimic -- it would not be a Pirate in hand
// anyway), a library of Islands.
GameState MalcolmTurn(int lands)
{
    EnsureCardsPo();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.m_provider = &Pirates();
    for (int k = 0; k < lands - 2; ++k) { PutPo(s, "Island", 300 + k); }
    PutPo(s, "Mountain", 320);
    PutPo(s, "Mountain", 321);
    s.players[0].hand.push_back(HandPo("Malcolm, the Eyes", 10));
    s.players[0].hand.push_back(HandPo("Corsair Captain", 11));
    s.players[0].hand.push_back(HandPo("Daring Buccaneer", 12));
    for (int k = 0; k < 10; ++k)
    {
        Card c;
        c.m_name = "Island";
        c.RehashName();
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

const TurnSolver::Plan* FindAllThree(const std::vector<TurnSolver::Plan>& plans)
{
    for (const TurnSolver::Plan& p : plans)
    {
        int n = 0;
        for (const Action& a : p.actions) { if (a.kind == Action::Kind::CastFromHand) { ++n; } }
        if (n == 3) { return &p; }
    }
    return nullptr;
}

// The apply's clean-set order (stable CastOrderLess sort), then the shared decider.
std::vector<std::string> RealisedNames(const GameState& s, const TurnSolver::Plan& p, bool decide)
{
    std::vector<int> order;
    for (int i = 0; i < static_cast<int>(p.actions.size()); ++i)
    { if (p.actions[i].kind == Action::Kind::CastFromHand) { order.push_back(i); } }
    std::stable_sort(order.begin(), order.end(), [&](int x, int y)
    { return CastOrderLess(s, p.actions[x], p.actions[y]); });
    if (decide) { ApplyPayableCastOrder(s, p.actions, order); }
    std::vector<std::string> names;
    for (int i : order) { names.push_back(p.actions[i].card_name.str()); }
    return names;
}

const std::vector<std::string> kDefault = { "Malcolm, the Eyes", "Daring Buccaneer", "Corsair Captain" };
const std::vector<std::string> kIdeal   = { "Corsair Captain", "Daring Buccaneer", "Malcolm, the Eyes" };
const std::vector<std::string> kHoist   = { "Corsair Captain", "Malcolm, the Eyes", "Daring Buccaneer" };

}   // namespace

TEST_CASE("Payable order ON, five lands: {Malcolm, Corsair, Buccaneer} is offered and realised Corsair -> Buccaneer -> Malcolm")
{
    PoArms arms(/*order_on=*/true, /*payable_on=*/true);
    const GameState s = MalcolmTurn(5);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* all3 = FindAllThree(plans);
    REQUIRE_MESSAGE(all3 != nullptr, "the three-cast line on five lands was not enumerated");

    CHECK(RealisedNames(s, *all3, /*decide=*/false) == kDefault);   // the reviewed order is the default
    CHECK(RealisedNames(s, *all3, /*decide=*/true)  == kIdeal);     // ...which cannot pay: nearest payable
    CHECK(TurnSolver::CanonicalNonSacCastOrder(s, *all3) == kIdeal); // the viewer reports the realised order

    // The rollout apply realises it: three bodies, the Treasure spent, all five lands tapped -- so the
    // Buccaneer paid {R} (a {2}{R} Buccaneer would not fit: 3 + 1 + 2 = 6 = 5 lands + the Treasure).
    GameState after = s;
    TurnSolver::ApplyPlan(after, *all3, /*is_pre_combat=*/true);
    CHECK(CountNamedPo(after, "Corsair Captain") == 1);
    CHECK(CountNamedPo(after, "Daring Buccaneer") == 1);
    CHECK(CountNamedPo(after, "Malcolm, the Eyes") == 1);
    CHECK(CountNamedPo(after, "Treasure Token") == 0);
    CHECK(after.players[0].hand.empty());
    int tapped = 0;
    for (const Permanent& p : after.battlefield) { if (p.card.IsLand() && p.tapped) { ++tapped; } }
    CHECK(tapped == 5);
    // Malcolm cast third: he was not on the battlefield for the second spell -> no Clue.
    CHECK(CountNamedPo(after, "Clue Token") == 0);
}

TEST_CASE("Payable order ON, six lands: mana for all three -> the reviewed order is kept")
{
    PoArms arms(true, true);
    const GameState s = MalcolmTurn(6);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* all3 = FindAllThree(plans);
    REQUIRE(all3 != nullptr);
    CHECK(RealisedNames(s, *all3, /*decide=*/true) == kDefault);
    GameState after = s;
    TurnSolver::ApplyPlan(after, *all3, true);
    CHECK(CountNamedPo(after, "Corsair Captain") == 1);
    CHECK(CountNamedPo(after, "Daring Buccaneer") == 1);
    CHECK(CountNamedPo(after, "Malcolm, the Eyes") == 1);
    CHECK(after.players[0].hand.empty());
}

TEST_CASE("Payable order OFF: the five-land three-cast line is not offered; the decider is the old maker hoist")
{
    // Grab the plan's actions from the lever-ON enumeration, then judge them with the lever OFF.
    TurnSolver::Plan all3_copy;
    {
        PoArms arms(true, true);
        const GameState s = MalcolmTurn(5);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        const TurnSolver::Plan* all3 = FindAllThree(plans);
        REQUIRE(all3 != nullptr);
        all3_copy = *all3;
    }
    PoArms arms(true, false);
    const GameState s = MalcolmTurn(5);
    CHECK(FindAllThree(TurnSolver::EnumerateMainPlans(s, true)) == nullptr);
    // Reveal-blind: Corsair first projects payable at the Buccaneer's plan-start {R}, so the hoist fires.
    CHECK(RealisedNames(s, all3_copy, /*decide=*/true) == kHoist);
    std::vector<int> a, b;
    for (int i = 0; i < static_cast<int>(all3_copy.actions.size()); ++i)
    { if (all3_copy.actions[i].kind == Action::Kind::CastFromHand) { a.push_back(i); } }
    std::stable_sort(a.begin(), a.end(), [&](int x, int y)
    { return CastOrderLess(s, all3_copy.actions[x], all3_copy.actions[y]); });
    b = a;
    ApplyPayableCastOrder(s, all3_copy.actions, a);
    ApplyEtbTreasureFundingOrder(s, all3_copy.actions, b);
    CHECK(a == b);
    CHECK(TurnSolver::CanonicalNonSacCastOrder(s, all3_copy) == kDefault);   // display unchanged when off
}

TEST_CASE("Payable order ON, five lands: the executor's own cast path pays the realised order in full")
{
    // The executor reprices every cast LIVE off the hand (CastSpellFromHand), independently of the
    // rollout's apply_one -- so walk the decider's order through it: Corsair (its Treasure enters on
    // resolution), Buccaneer revealing Malcolm at {R}, Malcolm off the last land + the Treasure.
    PoArms arms(true, true);
    GameState s = MalcolmTurn(5);
    // Keep the enumeration ALIVE: FindAllThree returns a pointer INTO it. Passing the temporary
    // straight in left a dangling pointer -- it happened to survive locally and read an empty plan on
    // both CI runners.
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* all3 = FindAllThree(plans);
    REQUIRE(all3 != nullptr);
    const std::vector<std::string> ord = RealisedNames(s, *all3, true);
    REQUIRE(ord == kIdeal);
    AIEngine eng;
    for (const std::string& name : ord)
    {
        auto it = std::find_if(s.players[0].hand.begin(), s.players[0].hand.end(),
                               [&](const Card& c) { return c.m_name.str() == name; });
        REQUIRE(it != s.players[0].hand.end());
        ManaPool avail = AvailableManaPool(s);
        const std::size_t before = s.stack.size();
        MtgTestSeam::CastSpellFromHand(eng, s, *it, avail, 0);
        REQUIRE_MESSAGE(s.stack.size() == before + 1, "executor could not cast ", name);
        const StackEntry entry = s.stack.back();
        s.stack.pop_back();
        EffectHandler::Resolve(s, entry, DefPo(name));
    }
    CHECK(CountNamedPo(s, "Corsair Captain") == 1);
    CHECK(CountNamedPo(s, "Daring Buccaneer") == 1);
    CHECK(CountNamedPo(s, "Malcolm, the Eyes") == 1);
    CHECK(CountNamedPo(s, "Treasure Token") == 0);
    CHECK(s.players[0].hand.empty());
}
