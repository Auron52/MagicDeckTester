#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"

#include <array>

TEST_CASE("legacy passive opponent land cycle has three lands and the suite's modal nonbasic buckets")
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
        // The three-placeholder board survives only for references recorded before every deck's
        // opponent played lands (--legacy-passive-opponent-lands).
        const GameState state = GoldFishRunner::SetupGame(deck, 1000, gi, true);
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
    const GameState probe = GoldFishRunner::SetupGame(deck, 1003, -1, true);
    const GameState indexed = GoldFishRunner::SetupGame(deck, 1003, 3, true);
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

TEST_CASE("every deck's passive opponent plays a seeded real deck's lands (USER 2026-11-17)")
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);

    // A deck with no opponent-zone card at all: before 2026-11-17 its opponent had three
    // placeholder lands and no library.
    Decklist plain;
    const CardDefinition* forest = CardDatabase::Instance().Lookup("Forest");
    REQUIRE(forest != nullptr);
    for (int i = 0; i < 20; ++i) { plain.mainboard.push_back(forest->card); }
    REQUIRE_FALSE(GoldFishRunner::DeckTouchesOpponentZones(plain));
    REQUIRE_FALSE(GoldFishRunner::DeckUsesDiverseOpponentPool(plain));

    const Decklist mill = DeckLoader::LoadFromFile("decks/Mill/Mill.cod");
    for (uint64_t seed : { 1ull, 2ull, 37ull })
    {
        const GameState s = GoldFishRunner::SetupGame(plain, seed, 0);
        CHECK(s.opponent_library_dealt);
        CHECK(s.opponent_plays_lands);
        CHECK(s.battlefield.empty());
        CHECK(s.players[1].hand.size() == 7u);
        CHECK(s.players[1].library.size() == 53u);
        // The same seeded pool list and shuffle as Mill's opponent.
        const GameState m = GoldFishRunner::SetupGame(mill, seed, 0);
        REQUIRE(m.players[1].hand.size() == s.players[1].hand.size());
        for (size_t i = 0; i < s.players[1].hand.size(); ++i)
        { CHECK(s.players[1].hand[i].m_name == m.players[1].hand[i].m_name); }
    }

    // Its land turn puts a REAL land (with its own mana ability) under the opponent's control.
    GameState s = GoldFishRunner::SetupGame(plain, 1, 0);
    s.players[1].hand.clear();
    s.players[1].hand.push_back(CardDatabase::Instance().Lookup("Watery Grave")->card);
    ResolvePassiveOpponentLandTurn(s);
    REQUIRE(s.battlefield.size() == 1u);
    CHECK(s.battlefield[0].controller_index == 1);
    CHECK(s.battlefield[0].card.m_name == "Watery Grave");
    CHECK(s.battlefield[0].tapped);   // a passive opponent declines the shock payment

    // The legacy replay flag restores the placeholder board for old references.
    const GameState legacy = GoldFishRunner::SetupGame(plain, 1, 0, true);
    CHECK_FALSE(legacy.opponent_library_dealt);
    CHECK_FALSE(legacy.opponent_plays_lands);
    CHECK(legacy.battlefield.size() == 3u);
}
