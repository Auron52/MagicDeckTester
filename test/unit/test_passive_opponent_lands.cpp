#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/HeuristicDefaults.h"
#include "core/OpponentDeck.h"
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

TEST_CASE("an opponent fetch shuffles on its own ordinal and never moves our reshuffles")
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);

    Decklist plain;
    const CardDefinition* forest = CardDatabase::Instance().Lookup("Forest");
    REQUIRE(forest != nullptr);
    for (int i = 0; i < 40; ++i) { plain.mainboard.push_back(forest->card); }

    GameState with_fetch = GoldFishRunner::SetupGame(plain, 7, 6);
    GameState without = with_fetch;
    // The opponent plays a fetch with a basic to find: it cracks and shuffles THEIR library.
    with_fetch.players[1].hand.clear();
    with_fetch.players[1].hand.push_back(CardDatabase::Instance().Lookup("Windswept Heath")->card);
    with_fetch.players[1].library.push_back(forest->card);
    without.players[1].library.push_back(forest->card);
    ResolvePassiveOpponentLandTurn(with_fetch);
    REQUIRE(with_fetch.opponent_searched_library_this_turn);
    CHECK(with_fetch.opponent_search_count == 1);
    CHECK(with_fetch.search_count == without.search_count);

    // Our next reshuffle realises the same library order either way.
    ShuffleAfterSearch(with_fetch, 0);
    ShuffleAfterSearch(without, 0);
    REQUIRE(with_fetch.players[0].library.size() == without.players[0].library.size());
    for (size_t i = 0; i < with_fetch.players[0].library.size(); ++i)
    { CHECK(with_fetch.players[0].library[i].m_number == without.players[0].library[i].m_number); }

    // A legacy replay keeps the shared ordinal it was recorded under.
    GameState legacy = GoldFishRunner::SetupGame(plain, 7, 6, true);
    const uint64_t before = legacy.search_count;
    ShuffleAfterSearch(legacy, 1);
    CHECK(legacy.search_count == before + 1);
    CHECK(legacy.opponent_search_count == 0);
}

TEST_CASE("2HG deals both opponent heads a pool list; the cheaper one is the targeted head (USER 2026-11-17)")
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);

    Decklist plain;
    const CardDefinition* forest = CardDatabase::Instance().Lookup("Forest");
    REQUIRE(forest != nullptr);
    for (int i = 0; i < 40; ++i) { plain.mainboard.push_back(forest->card); }

    struct HeadsScope
    {
        int saved = gamesetup::t_setup.opponent_heads;
        explicit HeadsScope(int h) { gamesetup::t_setup.opponent_heads = h; }
        ~HeadsScope() { gamesetup::t_setup.opponent_heads = saved; }
    };

    {
        HeadsScope one(1);
        const GameState s = GoldFishRunner::SetupGame(plain, 1234, 0);
        CHECK_FALSE(opponentdeck::HasSecondHead(s));
        CHECK(s.opponent_slot == 1234 % opponentdeck::Pool().size());
        CHECK(s.opponent2_hand.empty());
    }

    HeadsScope two(2);
    int swapped = 0;
    for (uint64_t seed = 1; seed <= 60; ++seed)
    {
        const GameState s = GoldFishRunner::SetupGame(plain, seed, 0);
        REQUIRE(opponentdeck::HasSecondHead(s));
        CHECK(s.opponent_slot != s.opponent2_slot);
        CHECK(opponentdeck::ListTotalManaValue(s.opponent_slot)
              <= opponentdeck::ListTotalManaValue(s.opponent2_slot));
        CHECK(s.players[1].hand.size() == 7u);
        CHECK(s.opponent2_hand.size() == 7u);
        CHECK(s.players[1].library.size() + 7u == 60u);
        CHECK(s.opponent2_library.size() + 7u == 60u);
        for (const Card& c : s.opponent2_hand) { CHECK(c.m_number >= opponentdeck::kNumberBase2); }
        if (s.opponent_slot != seed % opponentdeck::Pool().size()) { ++swapped; }
    }
    CHECK(swapped > 0);   // the cheaper-list rule does pick the second slot sometimes

    // Both heads play a land on the opponent's turn and draw at the end of ours.
    GameState s = GoldFishRunner::SetupGame(plain, 7, 0);
    s.players[1].hand.clear();
    s.players[1].hand.push_back(CardDatabase::Instance().Lookup("Island")->card);
    s.opponent2_hand.clear();
    s.opponent2_hand.push_back(CardDatabase::Instance().Lookup("Mountain")->card);
    ResolvePassiveOpponentLandTurn(s);
    REQUIRE(s.battlefield.size() == 2u);
    CHECK(s.battlefield[0].card.m_name == "Island");
    CHECK(s.battlefield[1].card.m_name == "Mountain");
    CHECK(s.battlefield[1].controller_index == 1);
    const std::size_t lib2 = s.opponent2_library.size();
    opponentdeck::EndOfTurnDraw(s);
    CHECK(s.opponent2_hand.size() == 1u);
    CHECK(s.opponent2_library.size() + 1 == lib2);

    // The second head's fetch pays the team's life and shuffles only its own library.
    GameState f = GoldFishRunner::SetupGame(plain, 7, 0);
    f.players[1].hand.clear();
    f.opponent2_hand.clear();
    f.opponent2_hand.push_back(CardDatabase::Instance().Lookup("Windswept Heath")->card);
    f.opponent2_library.push_back(forest->card);
    const int life = f.players[1].life;
    const uint64_t ours = f.search_count;
    ResolvePassiveOpponentLandTurn(f);
    CHECK(f.players[1].life == life - 1);
    CHECK(f.opponent_searched_library_this_turn);
    CHECK(f.opponent2_search_count == 1);
    CHECK(f.opponent_search_count == 0);
    CHECK(f.search_count == ours);

    // A legacy replay never deals a second head.
    const GameState legacy = GoldFishRunner::SetupGame(plain, 7, 0, true);
    CHECK_FALSE(opponentdeck::HasSecondHead(legacy));
}

TEST_CASE("opponent lands enter as one front block, so the battlefield order is independent of which turn we cast on")
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    REQUIRE(opponentdeck::OpponentLandsFrontOn());   // DEFAULT ON (MTG_OPP_LANDS_FRONT)

    Decklist plain;
    const CardDefinition* forest = CardDatabase::Instance().Lookup("Forest");
    const CardDefinition* island = CardDatabase::Instance().Lookup("Island");
    const CardDefinition* swamp = CardDatabase::Instance().Lookup("Swamp");
    REQUIRE(forest != nullptr);
    REQUIRE(island != nullptr);
    REQUIRE(swamp != nullptr);
    for (int i = 0; i < 20; ++i) { plain.mainboard.push_back(forest->card); }

    auto ours = [&](GameState& s, int number) {
        Permanent p;
        p.card = forest->card;
        p.card.m_number = number;
        p.controller_index = 0;
        p.owner_index = 0;
        s.battlefield.push_back(p);
    };
    auto opp_land_turn = [&](GameState& s, const CardDefinition* land, int number) {
        s.players[1].hand.clear();
        Card c = land->card;
        c.m_number = number;
        s.players[1].hand.push_back(c);
        ResolvePassiveOpponentLandTurn(s);
    };

    // Line A: we cast X and Y on the same turn, then the opponent plays two lands.
    // Line B: X, opponent land, Y, opponent land. The same position after the second opponent turn.
    GameState a = GoldFishRunner::SetupGame(plain, 1, 0);
    GameState b = a;
    REQUIRE(a.battlefield.empty());
    ours(a, 101); ours(a, 102);
    opp_land_turn(a, island, 901);
    opp_land_turn(a, swamp, 902);
    ours(b, 101);
    opp_land_turn(b, island, 901);
    ours(b, 102);
    opp_land_turn(b, swamp, 902);

    // The opponent's lands lead, in their own arrival order; ours follow in theirs.
    const std::array<int, 4> expected{901, 902, 101, 102};
    REQUIRE(a.battlefield.size() == expected.size());
    REQUIRE(b.battlefield.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
    {
        CHECK(a.battlefield[i].card.m_number == expected[i]);
        CHECK(b.battlefield[i].card.m_number == expected[i]);
        CHECK(a.battlefield[i].controller_index == b.battlefield[i].controller_index);
        CHECK(a.battlefield[i].card.m_name == b.battlefield[i].card.m_name);
    }
    CHECK(a.battlefield[0].controller_index == 1);
    CHECK(a.battlefield[1].controller_index == 1);
}
