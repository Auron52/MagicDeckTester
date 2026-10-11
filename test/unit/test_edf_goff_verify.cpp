// MTG_EDF_GOFF_VERIFY_UNPROJECTED -- Solve's EDF go-off cut verifies the maximal blink candidate by applying
// it even when the lethal projection does not see the kill.
//
// The board is EDF gi 1676 (seed 1651677, d0) turn 4: Yavimaya Coast and Conservatory, each carrying a
// Wild Growth; Cloud of Faeries and Eldrazi Displacer in play (a live loop, net +1 an iteration);
// hand Fertile Ground x2, Emiel, Trace of Abundance; opponent on 19; Living Wish on top of the library
// and Essence Depleter in the sideboard. The blink apply banks, investigates with Conservatory, draws
// Living Wish, wishes for Depleter, finds its {B} off a Fertile Ground cast mid-loop and drains -- a
// kill ExtraLethalDamage / ProjectsAlternateWin do not model (the wish is in the LIBRARY), so the cut
// logged "projected=0", never applied it, and the walk spent the turn's mana on two Fertile Grounds.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsGv()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const EldraziFlickerProvider& ProvGv()
{
    static const EldraziFlickerProvider p;
    return p;
}

Card PhGv(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

GameState Make1676(bool wish_on_top)
{
    EnsureCardsGv();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = 19;
    s.deck_has_land_aura  = true;
    s.m_provider          = &ProvGv();
    int num = 1;
    for (const char* nm : { "Fertile Ground", "Fertile Ground", "Emiel the Blessed", "Trace of Abundance" })
    { s.players[0].hand.push_back(PhGv(nm, num++)); }
    if (wish_on_top) { s.players[0].library.push_back(PhGv("Living Wish", num++)); }
    for (int i = 0; i < 30; ++i) { s.players[0].library.push_back(PhGv("Brushland", num++)); }
    for (const char* nm : { "Azorius Chancery", "Adarkar Wastes", "Cloud of Faeries", "Eldrazi Displacer",
                            "Mariposa Military Base", "Vexing Shusher", "Essence Depleter",
                            "Dimensional Infiltrator" })
    { s.players[0].sideboard.push_back(PhGv(nm, num++)); }
    auto put = [&](const char* nm, int host) -> int
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(nm)->card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        p.aura_attached_to  = host;
        s.battlefield.push_back(p);
        return p.card.m_number;
    };
    const int coast = put("Yavimaya Coast", 0);
    put("Wild Growth", coast);
    const int cons = put("Conservatory", 0);
    put("Cloud of Faeries", 0);
    put("Wild Growth", cons);
    put("Eldrazi Displacer", 0);
    s.players[0].lands_played_this_turn = 1;
    return s;
}

TurnSolver::Plan SolveGv(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::EDF_GOFF_VERIFY_UNPROJECTED] = static_cast<std::int8_t>(arm);
    const TurnSolver::Plan p = TurnSolver::Solve(s, /*is_pre_combat=*/true,
        TurnSolver::GreedyPermit(TurnSolver::GreedySite::D0Runner, 0));
    heurarm::t_arm[heurarm::EDF_GOFF_VERIFY_UNPROJECTED] = -1;
    return p;
}

bool BlinkGoOff(const TurnSolver::Plan& p)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::ActivateBlink && a.chosen_x > 3) { return true; } }
    return false;
}

}   // namespace

TEST_CASE("EDF d0: an unprojected go-off that kills on application is taken (MTG_EDF_GOFF_VERIFY_UNPROJECTED)")
{
    const GameState s = Make1676(/*wish_on_top=*/true);
    const TurnSolver::Plan off = SolveGv(s, 0);
    const TurnSolver::Plan on  = SolveGv(s, 1);

    CHECK_MESSAGE(!off.wins_this_turn, "precondition: the projection does not see this kill");
    CHECK(on.wins_this_turn);
    CHECK(BlinkGoOff(on));
    // ...and it is a real kill: replaying the returned plan ends the game.
    GameState copy = s;
    TurnSolver::ApplyPlan(copy, on, /*is_pre_combat=*/true);
    CHECK(copy.players[1].life <= 0);
}

// The verify is a CHECK, not a preference: with no finisher reachable the go-off does not kill, and the
// lever must leave Solve's pick exactly as it was.
TEST_CASE("EDF d0: a go-off that does not kill on application changes nothing (MTG_EDF_GOFF_VERIFY_UNPROJECTED)")
{
    const GameState s = Make1676(/*wish_on_top=*/false);
    const TurnSolver::Plan off = SolveGv(s, 0);
    const TurnSolver::Plan on  = SolveGv(s, 1);
    CHECK(on.wins_this_turn == off.wins_this_turn);
    REQUIRE(on.actions.size() == off.actions.size());
    for (std::size_t i = 0; i < on.actions.size(); ++i)
    {
        CHECK(on.actions[i].card_name.str() == off.actions[i].card_name.str());
        CHECK(on.actions[i].chosen_x == off.actions[i].chosen_x);
    }
}
