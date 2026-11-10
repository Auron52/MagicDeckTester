#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "ai/TurnSolver.h"
#include "ai/AIEngine.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>

namespace
{
void LoadGodCards()
{
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
}

Card Named(const char* name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Card c = d->card;
    c.m_number = number;
    return c;
}

const Permanent& Numbered(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield)
    { if (p.card.m_number == number) { return p; } }
    static Permanent missing;
    REQUIRE_MESSAGE(false, "missing permanent #", number);
    return missing;
}

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 3;
    s.players[0].life = 20;
    s.players[1].life = 20;
    return s;
}
}

TEST_CASE("Replenish: a God returned last sees co-entering creatures after full-batch devotion")
{
    LoadGodCards();
    GameState s = Fresh();
    // The two {R}{R} Eidolons plus Purphoros's own {R} make devotion five.
    // Sequential PutCardOntoBattlefield would miss both Purphoros triggers.
    s.players[0].graveyard.push_back(Named("Eidolon of the Great Revel", 1));
    s.players[0].graveyard.push_back(Named("Eidolon of the Great Revel", 2));
    s.players[0].graveyard.push_back(Named("Purphoros, God of the Forge", 3));
    s.players[0].graveyard.push_back(Named("Careful Study", 4));

    PerformReplenish(s, 0, "Replenish");

    CHECK(s.battlefield.size() == 3);
    CHECK(Numbered(s, 3).card.IsCreature());
    CHECK(DevotionTo(s, 0, "R") == 5);
    CHECK(s.players[1].life == 16); // two other creatures, two damage each
    REQUIRE(s.players[0].graveyard.size() == 1);
    CHECK(s.players[0].graveyard.front().m_name == "Careful Study");
}

TEST_CASE("Replenish: Purphoros cannot trigger on its own entry, and unattached Auras stay")
{
    LoadGodCards();
    GameState s = Fresh();
    s.players[0].graveyard.push_back(Named("Purphoros, God of the Forge", 11));
    s.players[0].graveyard.push_back(Named("Rancor", 12));

    PerformReplenish(s, 0, "Replenish");

    CHECK(s.players[1].life == 20);
    CHECK(s.battlefield.size() == 1);
    CHECK_FALSE(Numbered(s, 11).card.IsCreature());
    REQUIRE(s.players[0].graveyard.size() == 1);
    CHECK(s.players[0].graveyard.front().m_name == "Rancor");
}

TEST_CASE("Purphoros: the printed cost, team pump, and two-colour hybrid devotion")
{
    LoadGodCards();
    const CardDefinition* purphoros = CardDatabase::Instance().Lookup("Purphoros, God of the Forge");
    REQUIRE(purphoros != nullptr);
    CHECK(purphoros->card.m_mana_cost.ToString() == "{3}{R}");
    REQUIRE(purphoros->params.team_pump_cost.has_value());
    CHECK(purphoros->params.team_pump_cost->ToString() == "{2}{R}");
    CHECK(purphoros->params.team_pump_power == 1);

    GameState s = Fresh();
    Permanent god;
    god.card = Named("Purphoros, God of the Forge", 21);
    god.controller_index = 0;
    god.owner_index = 0;
    s.battlefield.push_back(god);
    Permanent eidolon;
    eidolon.card = Named("Eidolon of the Great Revel", 22);
    eidolon.controller_index = 0;
    eidolon.owner_index = 0;
    s.battlefield.push_back(eidolon);
    RefreshDevotionCreatures(s);
    CHECK(ApplyActivatePump(s, 0, 21, 2, 1) == 1);
    CHECK(Numbered(s, 22).temp_power_bonus == 1);
    CHECK(Numbered(s, 21).temp_power_bonus == 0); // not a creature at devotion three

    // A precombat activation must be a searched plan option: pumping before a
    // Xenagos trigger changes the power that trigger reads.
    for (int i = 0; i < 3; ++i)
    {
        Permanent mountain;
        mountain.card = Named("Mountain", 40 + i);
        mountain.controller_index = 0;
        mountain.owner_index = 0;
        s.battlefield.push_back(mountain);
    }
    bool offered = false;
    for (const Action& a : TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true))
    {
        if (a.kind == Action::Kind::ActivatePump && a.sac_source_id == 21
            && a.gy_exile_mode == 2 && a.chosen_x == 1)
        { offered = true; }
    }
    CHECK(offered);

    // The viewer's combat prompt needs an attacker and payable mana after
    // Purphoros enters. A seed sweep that merely casts it may never reach
    // this state, so exercise the live chooser directly.
    GameState combat = Fresh();
    combat.battlefield = s.battlefield;
    for (Permanent& p : combat.battlefield) { p.temp_power_bonus = 0; }
    bool asked = false;
    FirebreatheChooser choose_pump = [&](const GameState&, int,
                                         const std::vector<int>&, int max_count) {
        asked = true;
        CHECK(max_count >= 1);
        return 1;
    };
    FirebreatheChooser* previous_pump = g_play_firebreathe_chooser;
    g_play_firebreathe_chooser = &choose_pump;
    AIEngine ai;
    std::vector<int> attackers{1};
    ai.Firebreathe(combat, attackers);
    g_play_firebreathe_chooser = previous_pump;
    CHECK(asked);
    CHECK(Numbered(combat, 22).temp_power_bonus == 1);

    GameState hybrid = Fresh();
    Permanent shusher;
    shusher.card = Named("Vexing Shusher", 30); // {R/G}{R/G}
    shusher.controller_index = 0;
    shusher.owner_index = 0;
    hybrid.battlefield.push_back(shusher);
    CHECK(DevotionTo(hybrid, 0, "R") == 2);
    CHECK(DevotionTo(hybrid, 0, "G") == 2);
    CHECK(DevotionTo(hybrid, 0, "RG") == 2); // each hybrid symbol counts once for the pair
}
