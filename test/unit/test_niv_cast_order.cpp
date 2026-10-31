// The USER's NivMizzetCombo cast order (MTG_NIV_CAST_ORDER, DEFAULT ON; USER 2026-10-31):
//
//   land (searched) -> Sol Ring -> Izzet Signet -> Niv-Mizzet, the Firemind / Niv-Mizzet, Parun (name tie)
//   -> Curiosity when its host is a Niv -> Tandem Lookout when a Niv is on the battlefield or earlier in the
//   plan -> Brainstorm -> Ponder -> Preordain -> Gitaxian Probe -> Remand (with its target, as the last draw)
//   -> [no Niv: Curiosity -> Tandem Lookout] -> Generator Servant (cast).
//
// These pin the ranks (by params), the plan-level Lookout slot (ArrangeCastOrder), Curiosity's host test
// (CastOrderActionRank), the opt-ins, and that the lever off restores the generic tiers (Sol Ring and the
// Signet tied, which is what let hand order cast the Signet first).
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsNo()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefNo(const std::string& name)
{
    EnsureCardsNo();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

const NivMizzetComboProvider& NivProvNo()
{
    static const NivMizzetComboProvider p;
    return p;
}

GameState NivState()
{
    EnsureCardsNo();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.m_provider          = &NivProvNo();
    return s;
}

Action CastNo(const std::string& name, int hand_number = 0)
{
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = name;
    a.def       = &DefNo(name);
    a.cost      = DefNo(name).card.m_mana_cost;
    (void)hand_number;
    return a;
}

struct ArmNo
{
    heurarm::Slot slot;
    ArmNo(heurarm::Slot s, bool on) : slot(s) { heurarm::t_arm[s] = on ? 1 : 0; }
    ~ArmNo() { heurarm::t_arm[slot] = -1; }
};

std::vector<std::string> SortedNames(const GameState& s, const std::vector<Action>& acts)
{
    std::vector<int> ord;
    for (int i = 0; i < static_cast<int>(acts.size()); ++i) { ord.push_back(i); }
    std::stable_sort(ord.begin(), ord.end(), [&](int x, int y) { return CastOrderLess(s, acts[x], acts[y]); });
    ApplyDeckCastArrangement(s, acts, ord);
    std::vector<std::string> out;
    for (int i : ord) { out.push_back(acts[static_cast<std::size_t>(i)].card_name.str()); }
    return out;
}

}   // namespace

TEST_CASE("Niv cast order: the USER's ranks, by params")
{
    ArmNo arm(heurarm::NIV_CAST_ORDER, true);
    const GameState s = NivState();
    const NivMizzetComboProvider& p = NivProvNo();
    auto r = [&](const char* n) { return p.CastOrderRank(s, DefNo(n)); };
    CHECK(r("Sol Ring") < r("Izzet Signet"));
    CHECK(r("Izzet Signet") < r("Niv-Mizzet, the Firemind"));
    CHECK(r("Niv-Mizzet, the Firemind") == r("Niv-Mizzet, Parun"));
    CHECK(r("Niv-Mizzet, the Firemind") < r("Brainstorm"));
    CHECK(r("Brainstorm") < r("Ponder"));
    CHECK(r("Ponder") < r("Preordain"));
    CHECK(r("Preordain") < r("Gitaxian Probe"));
    CHECK(r("Gitaxian Probe") < r("Remand"));
    // No Niv out: Curiosity and Lookout after every draw, the Servant last.
    CHECK(r("Remand") < r("Curiosity"));
    CHECK(r("Curiosity") < r("Tandem Lookout"));
    CHECK(r("Tandem Lookout") < r("Generator Servant"));
    CHECK(p.CastOrderTotalByName());
    CHECK(p.OrderOpaqueCastsByRank());
}

TEST_CASE("Niv cast order: a plan's line, Lookout after a same-plan Niv, Curiosity on a Niv host")
{
    ArmNo arm(heurarm::NIV_CAST_ORDER, true);
    GameState s = NivState();
    // A Niv in hand (card number 31) that Curiosity enchants in the same plan.
    Card niv = DefNo("Niv-Mizzet, Parun").card;
    niv.m_number = 31;
    s.players[0].hand.push_back(niv);
    Action cur = CastNo("Curiosity");
    cur.enchant_target = 31;
    const std::vector<Action> acts = { CastNo("Generator Servant"), CastNo("Ponder"), CastNo("Tandem Lookout"),
                                       cur, CastNo("Brainstorm"), CastNo("Izzet Signet"),
                                       CastNo("Niv-Mizzet, Parun"), CastNo("Sol Ring") };
    const std::vector<std::string> want = { "Sol Ring", "Izzet Signet", "Niv-Mizzet, Parun", "Curiosity",
                                            "Tandem Lookout", "Brainstorm", "Ponder", "Generator Servant" };
    CHECK(SortedNames(s, acts) == want);

    // Without the Niv in the plan the enablers drop behind the draws.
    const std::vector<Action> no_niv = { CastNo("Tandem Lookout"), CastNo("Ponder"), CastNo("Curiosity") };
    const std::vector<std::string> want2 = { "Ponder", "Curiosity", "Tandem Lookout" };
    CHECK(SortedNames(s, no_niv) == want2);
}

TEST_CASE("Niv cast order: lever off restores the generic tiers (Sol Ring and the Signet tie)")
{
    ArmNo arm(heurarm::NIV_CAST_ORDER, false);
    const GameState s = NivState();
    const NivMizzetComboProvider& p = NivProvNo();
    CHECK(p.CastOrderRank(s, DefNo("Sol Ring")) == p.CastOrderRank(s, DefNo("Izzet Signet")));
    CHECK_FALSE(p.CastOrderTotalByName());
    CHECK_FALSE(p.OrderOpaqueCastsByRank());
    // The tie keeps plan order: a Signet listed first is cast first.
    const std::vector<Action> acts = { CastNo("Izzet Signet"), CastNo("Sol Ring") };
    const std::vector<std::string> want = { "Izzet Signet", "Sol Ring" };
    CHECK(SortedNames(s, acts) == want);
}
