// {X} sizing with SAME-PLAN scaled-dork growth + the Wirewood Lodge burst. USER 2026-10-06,
// references/suboptimal/SelesnyaLifegain/claude_s4_gi3.json T6: "I was only able to do X=8 on T6 when
// I had enough on board for up to X=10." Board: Blossoming Sands, Brushland, Wirewood Lodge, Llanowar
// Elves, two Elvish Archdruids; hand Llanowar Elves + Genesis Wave. Cast the Elf first (4 Elves):
// Sands 1 + Brushland 1 + Llanowar 1 + Archdruids 4 + 4 + the Lodge burst re-tapping an Archdruid
// (4, less its {G}) = 14, minus the Elf's {G} = 13 = {X}{G}{G}{G} at X = 10. The X range used to be
// sized on the un-grown board (X <= 8) and the burst's growth was credited nowhere.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/GameSetup.h"
#include "core/HeuristicDefaults.h"
#include "ai/TurnSolver.h"

#include <string>
#include <vector>

namespace
{

const CardDefinition& DefDG(const std::string& name)
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void PutDG(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card = DefDG(name).card;
    p.card.m_number = number;
    p.controller_index = 0;
    p.owner_index = 0;
    s.battlefield.push_back(p);
}

GameState LodgeBoard()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 6;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    PutDG(s, "Blossoming Sands", 1);
    PutDG(s, "Brushland", 2);
    PutDG(s, "Wirewood Lodge", 3);
    PutDG(s, "Llanowar Elves", 4);
    PutDG(s, "Elvish Archdruid", 5);
    PutDG(s, "Elvish Archdruid", 6);
    for (const char* n : {"Llanowar Elves", "Genesis Wave"})
    {
        Card c = DefDG(n).card; c.m_number = 10 + static_cast<int>(s.players[0].hand.size());
        s.players[0].hand.push_back(c);
    }
    for (int k = 0; k < 30; ++k)
    { Card c = DefDG("Forest").card; c.m_number = 900 + k; s.players[0].library.push_back(c); }
    return s;
}

int WaveX(const TurnSolver::Plan& p, bool& with_elf)
{
    int x = -1; with_elf = false;
    for (const Action& a : p.actions)
    {
        if (a.kind != Action::Kind::CastFromHand) { continue; }
        if (a.card_name.str() == "Genesis Wave") { x = a.chosen_x; }
        if (a.card_name.str() == "Llanowar Elves") { with_elf = true; }
    }
    return x;
}

}   // namespace

TEST_CASE("Genesis Wave X counts same-plan Elf growth and the Lodge burst: X=10 offered and paid")
{
    GameState s = LodgeBoard();
    int best_with_elf = -1, best_alone = -1;
    const TurnSolver::Plan* pick = nullptr;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    for (const TurnSolver::Plan& p : plans)
    {
        bool elf = false;
        const int x = WaveX(p, elf);
        if (x < 0) { continue; }
        if (elf && x > best_with_elf) { best_with_elf = x; pick = &p; }
        if (!elf && x > best_alone) { best_alone = x; }
    }
    CHECK(best_with_elf == 10);   // was 8
    CHECK(best_alone == 8);       // the Wave alone keeps its own un-grown max: 11 = lands 2 + Elf 1 + Archdruids 3+3 + burst (3-1)
    REQUIRE(pick != nullptr);
    GameState after = s;
    TurnSolver::ApplyPlan(after, *pick, true);
    // X = 10 reveals ten Forests: all ten land on the battlefield (MV 0 <= X).
    int forests = 0;
    for (const Permanent& p : after.battlefield) { if (p.card.m_name.str() == "Forest") { ++forests; } }
    CHECK(forests == 10);
}
