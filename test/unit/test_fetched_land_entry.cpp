// Fetched lands must expose the same optional entry choice as a hand-played land.
// Oracle witness: Cyclone seed1900008, T2 Scalding Tarn40 -> Steam Vents45, life18.
#include <doctest/doctest.h>
#include "ai/LandPlay.h"
#include "cards/CardDatabase.h"
#include "core/GameLogger.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

namespace
{
void EntryCards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}
const CardDefinition& EntryDef(const char* name)
{
    const auto* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    return *d;
}
Card EntryCard(const char* name, int number)
{
    Card c = EntryDef(name).card;
    c.m_number = number;
    return c;
}
GameState FetchFrame()
{
    EntryCards();
    GameState s;
    s.active_player_index = 0;
    s.game_seed = 1900008;
    s.turn_number = 2;
    s.players[0].life = 18;
    s.players[1].life = 20;
    Permanent island;
    island.card = EntryCard("Island", 20);
    island.owner_index = island.controller_index = 0;
    s.battlefield.push_back(island);
    s.players[0].hand.push_back(EntryCard("Scalding Tarn", 40));
    s.players[0].library.push_back(EntryCard("Steam Vents", 45));
    s.players[0].library.push_back(EntryCard("Island", 19));
    s.players[0].library.push_back(EntryCard("Mountain", 30));
    return s;
}
struct EntryHook
{
    LandEntryChooser* saved = g_play_land_entry_chooser;
    bool saved_speculating = g_tap_speculating;
    explicit EntryHook(LandEntryChooser* chooser)
    { g_play_land_entry_chooser = chooser; g_tap_speculating = false; }
    ~EntryHook()
    { g_play_land_entry_chooser = saved; g_tap_speculating = saved_speculating; }
};
void FetchSteam(GameState& s)
{
    LandPlayOptions opts;
    opts.fetch_target = "Steam Vents";
    opts.honor_entry_chooser = true;
    REQUIRE(PlayLandFromHand(s, 0, EntryDef("Scalding Tarn"), opts));
}
const Permanent& Steam(const GameState& s)
{
    for (const auto& p : s.battlefield) if (p.card.m_number == 45) { return p; }
    FAIL("the fetched physical Steam Vents is missing");
    return s.battlefield.front();
}
void SameFetch(const GameState& a, const GameState& b)
{
    CHECK(a.players[0].life == b.players[0].life);
    CHECK(a.players[0].lands_played_this_turn == b.players[0].lands_played_this_turn);
    CHECK(a.players[0].hand.size() == b.players[0].hand.size());
    REQUIRE(a.players[0].library.size() == b.players[0].library.size());
    for (std::size_t i = 0; i < a.players[0].library.size(); ++i)
    {
        CHECK(a.players[0].library[i].m_number == b.players[0].library[i].m_number);
        CHECK(a.players[0].library[i].m_name == b.players[0].library[i].m_name);
    }
    REQUIRE(a.battlefield.size() == b.battlefield.size());
    for (std::size_t i = 0; i < a.battlefield.size(); ++i)
    {
        CHECK(a.battlefield[i].card.m_number == b.battlefield[i].card.m_number);
        CHECK(a.battlefield[i].card.m_name == b.battlefield[i].card.m_name);
        CHECK(a.battlefield[i].tapped == b.battlefield[i].tapped);
    }
    REQUIRE(a.players[0].graveyard.size() == b.players[0].graveyard.size());
    for (std::size_t i = 0; i < a.players[0].graveyard.size(); ++i)
    { CHECK(a.players[0].graveyard[i].m_number == b.players[0].graveyard[i].m_number); }
}
} // namespace

TEST_CASE("Fetched land entry: Cyclone Tarn -> Steam asks once after the fetch cost and applies the selected payment")
{
    for (bool pay : {false, true})
    {
        GameState s = FetchFrame();
        int calls = 0;
        LandEntryChooser chooser = [&](const GameState& at, int controller, const std::string& source,
                                        int life, const std::vector<std::string>& reveal, bool heuristic) {
            ++calls;
            CHECK(controller == 0);
            CHECK(source == "Steam Vents");
            CHECK(life == 2);
            CHECK(reveal.empty());
            CHECK(heuristic);   // retain the old fetched-shock default
            CHECK(at.players[0].life == 17);   // Tarn's cost paid; shock's cost not paid yet
            CHECK(at.battlefield.size() == 1); // the replacement choice precedes entry
            return pay;
        };
        EntryHook hook(&chooser);
        FetchSteam(s);
        CHECK(calls == 1);
        CHECK(s.players[0].life == (pay ? 15 : 17));
        CHECK(Steam(s).tapped == !pay);
        CHECK(s.players[0].lands_played_this_turn == 1);
        CHECK(s.players[0].hand.empty());
        REQUIRE(s.players[0].graveyard.size() == 1);
        CHECK(s.players[0].graveyard[0].m_number == 40);
        CHECK(s.players[0].library.size() == 2);
    }
}

TEST_CASE("Fetched land entry: null hook and accepting the heuristic have identical physical entry and shuffle")
{
    GameState unattended = FetchFrame();
    { EntryHook hook(nullptr); FetchSteam(unattended); }
    CHECK(unattended.players[0].life == 15);
    CHECK_FALSE(Steam(unattended).tapped);
    GameState accepted = FetchFrame();
    int calls = 0;
    LandEntryChooser chooser = [&](const GameState&, int, const std::string&, int,
                                    const std::vector<std::string>&, bool heuristic) {
        ++calls;
        return heuristic;
    };
    { EntryHook hook(&chooser); FetchSteam(accepted); }
    CHECK(calls == 1);
    SameFetch(unattended, accepted);
}

TEST_CASE("Fetched land entry: preview pause and speculative payment never consume a live choice")
{
    GameState unattended = FetchFrame();
    { EntryHook hook(nullptr); FetchSteam(unattended); }
    int calls = 0;
    LandEntryChooser chooser = [&](const GameState&, int, const std::string&, int,
                                    const std::vector<std::string>&, bool) { ++calls; return false; };
    EntryHook hook(&chooser);
    GameState preview = FetchFrame();
    { RevealLogPause pause; FetchSteam(preview); }
    CHECK(g_play_land_entry_chooser == &chooser);
    CHECK(calls == 0);
    SameFetch(unattended, preview);
    GameState speculative = FetchFrame();
    g_tap_speculating = true;
    FetchSteam(speculative);
    g_tap_speculating = false;
    CHECK(calls == 0);
    SameFetch(unattended, speculative);
}

TEST_CASE("Fetched land entry: a forced-tapped search and predecided modes never prompt or pay shock life")
{
    EntryCards();
    int calls = 0;
    LandEntryChooser chooser = [&](const GameState&, int, const std::string&, int,
                                    const std::vector<std::string>&, bool) { ++calls; return true; };
    EntryHook hook(&chooser);
    for (LandEntryTap mode : {LandEntryTap::ForcedTapped, LandEntryTap::PreDecidedTapped,
                              LandEntryTap::PreDecidedUntapped})
    {
        GameState s;
        s.active_player_index = 0;
        s.players[0].life = 18;
        EnterLand(s, EntryDef("Steam Vents"), 45, false, true, mode);
        CHECK(s.players[0].life == 18);
        CHECK(Steam(s).tapped == (mode != LandEntryTap::PreDecidedUntapped));
    }
    // Real land-search route, rather than only the helper's enum: Titan puts its chosen shock tapped.
    GameState search;
    search.active_player_index = 0;
    search.players[0].life = 18;
    search.players[0].library.push_back(EntryCard("Steam Vents", 45));
    PerformLandSearch(search, 0, LandSearchSpecOf(EntryDef("Primeval Titan").params), "Primeval Titan", -1);
    CHECK(search.players[0].life == 18);
    CHECK(Steam(search).tapped);
    CHECK(calls == 0);
}
