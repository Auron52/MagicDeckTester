// The land-Aura host hold in the payable-order walks (MTG_PO_LAND_AURA_HOLD) --
// docs/design/mana-completeness-oracle.md 13.7. Fungus s3003 gi148 T3: Forest, Hickory Woodlot (two depletion
// counters) and Concordant Crossroads; Wild Growth + Undercellar Myconid in hand. Under Crossroads the hasted
// Myconid is a same-turn producer, which brings the plan into the payable-order walk. The given order -- Wild
// Growth on the Forest first, paid by the Woodlot, then Forest + its bonus pay the Myconid -- pays; the walk read
// it off the aggregate pool (3 for 4), called it unpayable and moved the Myconid first, so the Myconid tapped
// itself for Wild Growth and missed the attack the T4 kill needed.
#include <doctest/doctest.h>

#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <cstdint>
#include <string>
#include <vector>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Permanent& Put(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = Def(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

void Hand(GameState& s, const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

constexpr int kForest = 20;

GameState Board()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.deck_has_land_aura  = true;
    for (int k = 0; k < 20; ++k)
    {
        Card c = Def("Forest").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    Put(s, "Forest", kForest);
    Put(s, "Concordant Crossroads", 13);
    Permanent& woodlot = Put(s, "Hickory Woodlot", 30);
    Counter dep;
    dep.type  = Counter::Type::Depletion;
    dep.count = 2;
    woodlot.counters.Add(dep);
    Hand(s, "Wild Growth", 36);
    Hand(s, "Undercellar Myconid", 54);
    return s;
}

struct ArmPin
{
    heurarm::Slot slot;
    std::int8_t   prev;
    ArmPin(heurarm::Slot s_, bool on) : slot(s_), prev(heurarm::t_arm[s_]) { heurarm::t_arm[s_] = on ? 1 : 0; }
    ~ArmPin() { heurarm::t_arm[slot] = prev; }
};

}   // namespace

TEST_CASE("Land-Aura host hold: {Wild Growth -> Forest, hasted Myconid} keeps Wild Growth first")
{
    EnsureCardsLoaded();
    const GameState s = Board();
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* plan = nullptr;
    int wg = -1, myc = -1;
    for (const TurnSolver::Plan& p : plans)
    {
        int w = -1, m = -1, casts = 0;
        for (int i = 0; i < static_cast<int>(p.actions.size()); ++i)
        {
            const Action& a = p.actions[static_cast<std::size_t>(i)];
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            ++casts;
            if (a.card_name.str() == "Wild Growth" && a.enchant_target == kForest) { w = i; }
            if (a.card_name.str() == "Undercellar Myconid") { m = i; }
        }
        if (w >= 0 && m >= 0 && casts == 2) { plan = &p; wg = w; myc = m; break; }
    }
    REQUIRE(plan != nullptr);
    // The hasted Myconid carries the same-turn credit (MTG_HASTE_DORK_CAST_CREDIT) -- what brings the walk in.
    REQUIRE(plan->actions[static_cast<std::size_t>(myc)].rock_mana.Total() > 0);

    std::vector<int> order = { wg, myc };
    ApplyPayableCastOrder(s, plan->actions, order);
    CHECK(order == std::vector<int>{ wg, myc });

    // The lever off: the walk ignores the bonus and permutes the Myconid first.
    ArmPin off(heurarm::PO_LAND_AURA_HOLD, false);
    std::vector<int> off_order = { wg, myc };
    ApplyPayableCastOrder(s, plan->actions, off_order);
    CHECK(off_order == std::vector<int>{ myc, wg });
}
