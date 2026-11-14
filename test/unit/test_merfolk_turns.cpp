#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

namespace
{
void LoadMerfolkCards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

void AddPermanent(GameState& state, const char* name, int number, uint16_t chosen = 0)
{
    const CardDefinition* def = CardDatabase::Instance().Lookup(name);
    REQUIRE(def != nullptr);
    Permanent p;
    p.card = def->card;
    p.card.m_number = number;
    p.controller_index = 0;
    p.owner_index = 0;
    p.chosen_subtype_id = chosen;
    state.battlefield.push_back(p);
}
}

TEST_CASE("Three Tree City counts the chosen type of each source copy")
{
    LoadMerfolkCards();
    GameState state;
    state.active_player_index = 0;
    AddPermanent(state, "Island", 101);
    AddPermanent(state, "Island", 102);
    const auto& types = SubtypeRegistry::Instance();
    AddPermanent(state, "Three Tree City", 103, types.Id("Goblin"));
    AddPermanent(state, "Three Tree City", 104, types.Id("Merfolk"));
    AddPermanent(state, "Goblin Lackey", 105);
    AddPermanent(state, "Goblin Lackey", 106);
    AddPermanent(state, "Goblin Lackey", 107);
    AddPermanent(state, "Rishadan Dockhand", 108);
    const CardDefinition* city = CardDatabase::Instance().Lookup("Three Tree City");
    REQUIRE(city != nullptr);
    CHECK(ScaledManaCreatureCount(state, &state.battlefield[2]) == 3);
    CHECK(ScaledManaCreatureCount(state, &state.battlefield[3]) == 1);
    CHECK(ScaledManaNetYield(state, *city, &state.battlefield[2]) == 1);
    CHECK(ScaledManaNetYield(state, *city, &state.battlefield[3]) == 0);
}

TEST_CASE("Seahunter searches Merfolk permanents and rejects Merfolk spells")
{
    LoadMerfolkCards();
    const CardDefinition* prophets = CardDatabase::Instance().Lookup("Wanderwine Prophets");
    const CardDefinition* dockhand = CardDatabase::Instance().Lookup("Rishadan Dockhand");
    REQUIRE(dockhand != nullptr);
    CHECK(SeahunterLibraryCandidate(dockhand->card));
    Card instant = dockhand->card;
    instant.RemoveType(CardType::Creature);
    instant.AddType(CardType::Instant);
    CHECK_FALSE(SeahunterLibraryCandidate(instant));
    if (prophets != nullptr) { CHECK(SeahunterLibraryCandidate(prophets->card)); }
}

TEST_CASE("Seahunter puts the pinned library Merfolk through the shared battlefield path")
{
    LoadMerfolkCards();
    const CardDefinition* hunter = CardDatabase::Instance().Lookup("Seahunter");
    const CardDefinition* dockhand = CardDatabase::Instance().Lookup("Rishadan Dockhand");
    const CardDefinition* island = CardDatabase::Instance().Lookup("Island");
    REQUIRE(hunter != nullptr);
    REQUIRE(dockhand != nullptr);
    REQUIRE(island != nullptr);
    GameState state;
    state.active_player_index = 0;
    Card merfolk = dockhand->card;
    merfolk.m_number = 201;
    Card land = island->card;
    land.m_number = 202;
    state.players[0].library.push_back(land);
    state.players[0].library.push_back(merfolk);

    PerformTutorToBattlefield(state, 0, hunter->params, 1,
                              {"Rishadan Dockhand"}, "Seahunter");
    REQUIRE(state.battlefield.size() == 1);
    CHECK(state.battlefield[0].card.m_number == 201);
    CHECK(state.battlefield[0].entered_this_turn);
    CHECK(state.players[0].library.size() == 1);

    PerformTutorToBattlefield(state, 0, hunter->params, 1,
                              {kTutorDeclineTarget}, "Seahunter");
    CHECK(state.battlefield.size() == 1);
    CHECK(state.players[0].library.size() == 1);
}

TEST_CASE("Emperor Mihail II sees the private top only under its controller's permission")
{
    LoadMerfolkCards();
    GameState state;
    state.active_player_index = 0;
    const CardDefinition* merfolk = CardDatabase::Instance().Lookup("Rishadan Dockhand");
    REQUIRE(merfolk != nullptr);
    Card top = merfolk->card;
    top.m_number = 301;
    state.players[0].library.push_back(top);
    CHECK(PlayableTopMerfolkNumber(state) == 0);
    AddPermanent(state, "Emperor Mihail II", 302);
    CHECK(MayCastTopMerfolk(state));
    CHECK(PlayableTopMerfolkNumber(state) == 301);
    CHECK_FALSE(LibraryTopVisible(state));
    state.battlefield[0].controller_index = 1;
    CHECK(PlayableTopMerfolkNumber(state) == 0);
}

TEST_CASE("Emperor Mihail II pays each cast trigger independently")
{
    LoadMerfolkCards();
    GameState state;
    state.active_player_index = 0;
    AddPermanent(state, "Emperor Mihail II", 401);
    const CardDefinition* merfolk = CardDatabase::Instance().Lookup("Rishadan Dockhand");
    REQUIRE(merfolk != nullptr);
    state.floating_mana.colorless = 2;
    state.scripted_mihail_pay_mask = 1; // pay first, decline second
    FireOnCastTriggers(state, *merfolk);
    FireOnCastTriggers(state, *merfolk);
    CHECK(state.scripted_mihail_trigger_ordinal == 2);
    CHECK(state.floating_mana.colorless == 1);
    int tokens = 0;
    for (const Permanent& p : state.battlefield)
    { if (p.is_token && CardHasSubtype(p.card, "Merfolk")) { ++tokens; } }
    CHECK(tokens == 1);

    GameState two;
    two.active_player_index = 0;
    AddPermanent(two, "Emperor Mihail II", 411);
    AddPermanent(two, "Emperor Mihail II", 412);
    two.floating_mana.colorless = 2;
    two.scripted_mihail_pay_mask = 2; // decline first copy, pay second
    FireOnCastTriggers(two, *merfolk);
    CHECK(two.scripted_mihail_trigger_ordinal == 2);
    tokens = 0;
    for (const Permanent& p : two.battlefield)
    { if (p.is_token && CardHasSubtype(p.card, "Merfolk")) { ++tokens; } }
    CHECK(tokens == 1);
}
