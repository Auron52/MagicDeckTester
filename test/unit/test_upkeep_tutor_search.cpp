#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameEngine.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <algorithm>
#include <string>
#include <tuple>

namespace
{
void LoadUpkeepTutorCards()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card TutorCard(const char* name, int number)
{
    const auto* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Card c = d->card;
    c.m_number = number;
    return c;
}

void TutorPermanent(GameState& s, const char* name, int number, bool tapped = false, bool fresh = false)
{
    Permanent p;
    p.card = TutorCard(name, number);
    p.controller_index = p.owner_index = 0;
    p.tapped = tapped;
    p.entered_this_turn = fresh;
    s.battlefield.push_back(p);
}

bool HasTutorCard(const std::vector<Card>& cards, const std::string& name)
{
    return std::any_of(cards.begin(), cards.end(), [&](const Card& c) { return c.m_name.str() == name; });
}
}

TEST_CASE("Upkeep tutor search commits the World War Hulk into Craterhoof same-turn win")
{
    LoadUpkeepTutorCards();
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 1;
    s.phase = Phase::PreCombatMain;
    s.players[0].life = 20;
    s.players[1].life = 20;
    // Current-turn mana is exhausted. The tutor is payable only after the next untap.
    for (int i = 0; i < 6; ++i) { TutorPermanent(s, "Forest", 1 + i, true); }
    for (int i = 0; i < 3; ++i) { TutorPermanent(s, "Llanowar Elves", 10 + i, true, true); }
    s.players[0].hand.push_back(TutorCard("Worldly Tutor", 20));
    s.players[0].hand.push_back(TutorCard("World War Hulk", 21));
    s.players[0].library.push_back(TutorCard("Forest", 30));
    s.players[0].library.push_back(TutorCard("Hornet Queen", 32));
    s.players[0].library.push_back(TutorCard("Craterhoof Behemoth", 31));
    for (int i = 0; i < 10; ++i) { s.players[0].library.push_back(TutorCard("Forest", 40 + i)); }

    const auto line = TurnSolver::FullSearchLine(s, 2, 5, /*second_main=*/false, nullptr, nullptr);
    CHECK(line.win_turn == 2);
    REQUIRE_FALSE(line.phases.empty());
    const int pin = line.phases.front().plan.upkeep_tutor_choice;
    CHECK(pin > 1);  // Hornet Queen is the earlier candidate; search selects Craterhoof.
    GameState applied = s;
    TurnSolver::ApplyPlan(applied, line.phases.front().plan, true);
    CHECK(applied.scripted_upkeep_tutor == pin);
    REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(applied));
    CHECK(HasTutorCard(applied.players[0].hand, "Craterhoof Behemoth"));
    CHECK(HasTutorCard(applied.players[0].graveyard, "Worldly Tutor"));
    CHECK(applied.scripted_upkeep_tutor == -1);
}

TEST_CASE("Worldly and Enlightened Tutor upkeep casts match the searched rollout and real game")
{
    LoadUpkeepTutorCards();
    for (const auto& [tutor, source, target] :
         {std::tuple{"Worldly Tutor", "Forest", "Craterhoof Behemoth"},
          std::tuple{"Enlightened Tutor", "Plains", "Sol Ring"}})
    {
        GameState base;
        base.active_player_index = 0;
        base.turn_number = 1;
        base.phase = Phase::PostCombatMain;
        base.players[0].hand.push_back(TutorCard(tutor, 50));
        base.players[0].library.push_back(TutorCard("Forest", 51));
        base.players[0].library.push_back(TutorCard(target, 52));
        base.players[0].library.push_back(TutorCard("Forest", 53));
        TutorPermanent(base, source, 54, true);
        GameState probe = base;
        probe.battlefield[0].tapped = false;
        const auto options = GameEngine::UpkeepTutorOptions(probe);
        REQUIRE(options.size() == 2);
        CHECK(options[0].first == -1);
        CHECK(options[1].second == target);

        GameState rollout = base;
        rollout.scripted_upkeep_tutor = 1;
        REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(rollout));
        CHECK(HasTutorCard(rollout.players[0].hand, target));
        CHECK(HasTutorCard(rollout.players[0].graveyard, tutor));
        CHECK(rollout.scripted_upkeep_tutor == -1);

        GameState real = base;
        real.turn_number = 2;
        real.battlefield[0].tapped = false;
        real.scripted_upkeep_tutor = 1;
        AIEngine ai;
        GameEngine game(ai);
        game.PlayOutFrom(real, 2, GameEngine::ResumeAt::UpkeepTail);
        const bool target_reached = HasTutorCard(real.players[0].hand, target)
            || std::any_of(real.battlefield.begin(), real.battlefield.end(),
                           [&](const Permanent& p) { return p.card.m_name.str() == target; });
        CHECK(target_reached);
        CHECK(HasTutorCard(real.players[0].graveyard, tutor));
        CHECK(real.scripted_upkeep_tutor == -1);
    }
}
