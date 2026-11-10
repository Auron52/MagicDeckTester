#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "ai/TurnSolver.h"
#include "core/EffectHandler.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>

namespace
{
Card StudyCard(const char* name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Card c = d->card;
    c.m_number = number;
    return c;
}

GameState StudyState()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 2;
    s.players[0].hand.push_back(StudyCard("Island", 1));
    s.players[0].hand.push_back(StudyCard("Mountain", 2));
    s.players[0].hand.push_back(StudyCard("Swamp", 3));
    s.players[0].library.push_back(StudyCard("Forest", 4));
    s.players[0].library.push_back(StudyCard("Plains", 5));
    return s;
}
}

TEST_CASE("Careful Study draws before its two searched discards")
{
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    const CardDefinition* def = CardDatabase::Instance().Lookup("Careful Study");
    REQUIRE(def != nullptr);
    CHECK(def->params.draw == 2);
    CHECK(def->params.post_draw_discard == 2);
    CHECK(def->params.put_back_from_hand == 2);

    GameState base = StudyState();
    StackEntry entry;
    entry.type = StackEntry::EntryType::Spell;
    entry.source = StudyCard("Careful Study", 6);
    entry.controller_index = 0;

    GameState a = base;
    g_scripted_putback_choice = 0;
    EffectHandler::Resolve(a, entry, *def);
    CHECK(a.players[0].hand.size() == 3);
    CHECK(a.players[0].library.empty());
    CHECK(a.players[0].graveyard.size() == 3); // two discards plus the resolved Study
    CHECK(a.players[0].graveyard.back().m_name == "Careful Study");
    CHECK(g_scripted_putback_choice == -1);

    GameState b = base;
    DrawIntoHand(b, 0, 2, HandEntryReason::Draw);
    const int last_choice = static_cast<int>(PutBackCandidates(b, 0, 2).size()) - 1;
    REQUIRE(last_choice > 1);
    b = base;
    g_scripted_putback_choice = last_choice;
    EffectHandler::Resolve(b, entry, *def);
    CHECK(b.players[0].hand.size() == 3);
    CHECK(b.players[0].library.empty());
    CHECK(b.players[0].graveyard.size() == 3);
    CHECK(b.players[0].graveyard.back().m_name == "Careful Study");
    CHECK(g_scripted_putback_choice == -1);
    bool different = false;
    for (std::size_t i = 0; i < a.players[0].hand.size(); ++i)
    { different |= a.players[0].hand[i].m_number != b.players[0].hand[i].m_number; }
    CHECK(different);
}

TEST_CASE("Looting viewer choices use one post-draw hand for both discards")
{
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    GameState s = StudyState();
    DrawIntoHand(s, 0, 2, HandEntryReason::Draw);
    int calls = 0;
    DiscardChooser chooser = [&](const GameState& current, int, const std::vector<int>& legal, int)
    {
        CHECK(NonCleanupDiscardContext() == "Faithless Looting");
        CHECK(NonCleanupDiscardIsEffect());
        CHECK(NonCleanupDiscardRemaining() == 2 - calls);
        CHECK(NonCleanupDiscardAiSet().size() == static_cast<std::size_t>(2 - calls));
        CHECK(current.players[0].hand.size() == 5);
        const int answer = calls == 0 ? 1 : 3;
        CHECK(std::find(legal.begin(), legal.end(), answer) != legal.end());
        ++calls;
        return answer;
    };
    DiscardChooser* saved = g_play_discard_chooser;
    g_play_discard_chooser = &chooser;
    ResolvePutBackFromHand(s, 0, 2, "Faithless Looting", true);
    g_play_discard_chooser = saved;
    CHECK(calls == 2);
    CHECK(s.players[0].graveyard.size() == 2);
    CHECK(s.players[0].graveyard[0].m_number == 4);
    CHECK(s.players[0].graveyard[1].m_number == 2);
}

TEST_CASE("Faithless Looting flashback uses {2}{R} and exiles only the graveyard cast")
{
    CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
    const CardDefinition* def = CardDatabase::Instance().Lookup("Faithless Looting");
    REQUIRE(def != nullptr);
    REQUIRE(def->params.flashback_cost.has_value());
    CHECK(def->params.flashback_cost->ToString() == "{2}{R}");

    GameState s;
    s.active_player_index = 0;
    s.turn_number = 3;
    s.game_seed = 100;
    s.players[0].life = 20;
    s.players[1].life = 20;
    s.players[0].lands_played_this_turn = 1;
    for (int i = 0; i < 3; ++i)
    {
        Permanent land;
        land.card = StudyCard("Mountain", 10 + i);
        land.controller_index = 0;
        land.owner_index = 0;
        s.battlefield.push_back(land);
    }
    s.players[0].graveyard.push_back(StudyCard("Faithless Looting", 20));
    s.players[0].library.push_back(StudyCard("Island", 30));
    s.players[0].library.push_back(StudyCard("Forest", 31));
    for (int i = 0; i < 8; ++i)
    { s.players[0].library.push_back(StudyCard("Plains", 40 + i)); }

    const auto plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* flash = nullptr;
    for (const auto& p : plans)
    {
        if (p.actions.size() == 1 && p.actions[0].kind == Action::Kind::CastFromGraveyard
            && p.actions[0].card_name == "Faithless Looting")
        { flash = &p; break; }
    }
    REQUIRE(flash != nullptr);
    CHECK(flash->actions[0].cost.ManaValue() == 3);
    CHECK(flash->actions[0].discard_lands == 0);
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *flash, true);
    bool in_exile = false;
    for (const Card& c : roll.exile)
    { if (c.m_name == "Faithless Looting" && c.m_number == 20) { in_exile = true; } }
    CHECK(in_exile);
    CHECK(roll.players[0].graveyard.empty() == false); // discarded cards, never the flashed Looting

    GameState hand = s;
    hand.players[0].graveyard.clear();
    StackEntry entry;
    entry.type = StackEntry::EntryType::Spell;
    entry.source = StudyCard("Faithless Looting", 21);
    entry.controller_index = 0;
    EffectHandler::Resolve(hand, entry, *def);
    bool hand_cast_in_graveyard = false;
    for (const Card& c : hand.players[0].graveyard)
    { if (c.m_name == "Faithless Looting" && c.m_number == 21) { hand_cast_in_graveyard = true; } }
    CHECK(hand_cast_in_graveyard);
}
