#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"

#include <array>

TEST_CASE("passive opponent land cycle has three lands and the suite's modal nonbasic buckets")
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);

    Decklist deck;
    const CardDefinition* mountain = CardDatabase::Instance().Lookup("Mountain");
    REQUIRE(mountain != nullptr);
    for (int i = 0; i < 20; ++i) { deck.mainboard.push_back(mountain->card); }

    std::array<int, 4> bucket_counts{};
    std::array<std::array<bool, 4>, 2> parity_buckets{};
    for (int gi = 0; gi < 10; ++gi)
    {
        // The game index, rather than seed modulo ten, controls logged runs and batch replays.
        const GameState state = GoldFishRunner::SetupGame(deck, 1000, gi);
        int basic = 0, nonbasic = 0;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != 1) { continue; }
            REQUIRE(p.card.IsLand());
            if (IsBasicLandCard(p.card)) { ++basic; }
            else { ++nonbasic; }
        }
        CHECK(basic + nonbasic == 3);
        CHECK(NonbasicLandDamage(state, 1, 2) == 2 * nonbasic);
        REQUIRE(nonbasic >= 0);
        REQUIRE(nonbasic <= 3);
        ++bucket_counts[nonbasic];
        parity_buckets[gi % 2][nonbasic] = true;
    }
    const std::array<int, 4> expected_counts{2, 2, 3, 3};
    CHECK(bucket_counts == expected_counts);
    for (const auto& half : parity_buckets)
    {
        for (bool seen : half) { CHECK(seen); }
    }

    // A standalone analyzer probe still gets a deterministic bucket from its seed.
    const GameState probe = GoldFishRunner::SetupGame(deck, 1003);
    const GameState indexed = GoldFishRunner::SetupGame(deck, 1003, 3);
    CHECK(NonbasicLandDamage(probe, 1, 2) == NonbasicLandDamage(indexed, 1, 2));
}

TEST_CASE("decks observing opponent lands develop the seeded opponent mana base")
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);

    const Decklist mill = DeckLoader::LoadFromFile("decks/Mill/Mill.cod");
    const Decklist burn = DeckLoader::LoadFromFile("decks/Midrange Burn/Midrange Burn.cod");
    const Decklist hinata = DeckLoader::LoadFromFile("decks/Hinata/Hinata.cod");
    REQUIRE(GoldFishRunner::DeckNeedsOpponentLandPlay(mill));
    REQUIRE(GoldFishRunner::DeckNeedsOpponentLandPlay(burn));
    REQUIRE(GoldFishRunner::DeckNeedsOpponentLandPlay(hinata));
    CHECK_FALSE(GoldFishRunner::DeckTouchesOpponentZones(burn));
    CHECK_FALSE(GoldFishRunner::DeckTouchesOpponentZones(hinata));
    const GameState mill_state = GoldFishRunner::SetupGame(mill, 1, 0);

    for (const Decklist* deck : { &burn, &hinata })
    {
        const GameState current = GoldFishRunner::SetupGame(*deck, 1, 0);
        CHECK(current.opponent_library_dealt);
        CHECK(current.opponent_plays_lands);
        CHECK(current.players[1].hand.size() == 7u);
        CHECK(current.players[1].library.size() == 53u);
        for (int i = 0; i < 7; ++i)
        { CHECK(current.players[1].hand[i].m_name == mill_state.players[1].hand[i].m_name); }
        CHECK(current.battlefield.empty());

        const GameState legacy = GoldFishRunner::SetupGame(*deck, 1, 0, true);
        CHECK_FALSE(legacy.opponent_library_dealt);
        CHECK_FALSE(legacy.opponent_plays_lands);
        CHECK(legacy.battlefield.size() == 3u);
    }

    GameState burn_state = GoldFishRunner::SetupGame(burn, 1, 0);
    burn_state.players[1].hand.clear();
    burn_state.players[1].hand.push_back(CardDatabase::Instance().Lookup("Watery Grave")->card);
    ResolvePassiveOpponentLandTurn(burn_state);
    CHECK(NonbasicLandDamage(burn_state, 1, 2) == 2);

    GameState hinata_state = GoldFishRunner::SetupGame(hinata, 1, 0);
    const CardDefinition* opus = CardDatabase::Instance().Lookup("Magma Opus");
    REQUIRE(opus != nullptr);
    const int before = HinataAvailableTargets(*opus, hinata_state);
    hinata_state.players[1].hand.clear();
    hinata_state.players[1].hand.push_back(CardDatabase::Instance().Lookup("Island")->card);
    ResolvePassiveOpponentLandTurn(hinata_state);
    CHECK(HinataAvailableTargets(*opus, hinata_state) == before + 1);
}
