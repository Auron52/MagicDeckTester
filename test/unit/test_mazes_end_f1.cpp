// Maze's End F1 (2026-11-18): the four structural defects the Stage 5d claude-play sweep (seed 61001) found.
//   gi6 -- a draw trigger resolved INSIDE a search-put ability, before its "then shuffle" (CR 603.3).
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/LandPlay.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameEngine.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/LandSearch.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
void LoadF1Cards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& FDef(const char* name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    return *d;
}

Card FCard(const char* name, int number)
{
    Card c = FDef(name).card;
    c.m_number = number;
    return c;
}

int FPut(GameState& s, const char* name, int number, bool tapped = false)
{
    Permanent p;
    p.card = FCard(name, number);
    p.controller_index = 0;
    p.owner_index = 0;
    p.tapped = tapped;
    s.battlefield.push_back(p);
    return number;
}

GameState FBase()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    s.game_seed           = 61007;
    return s;
}

const Permanent* FFind(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return &p; } }
    return nullptr;
}

bool FInHand(const GameState& s, int num)
{
    for (const Card& c : s.players[0].hand) { if (c.m_number == num) { return true; } }
    return false;
}

// The card a draw would take AFTER `taken` leaves the library and the library is shuffled by the next
// search shuffle -- computed on a copy, through the same ShuffleAfterSearch the resolver uses.
int TopAfterTakeAndShuffle(const GameState& s, int taken)
{
    GameState ref = s;
    Library& lib = ref.players[0].library;
    std::vector<Card> keep;
    while (!lib.empty())
    {
        Card c = lib.DrawTop();
        if (c.m_number != taken) { keep.push_back(c); }
    }
    for (const Card& c : keep) { lib.push_back(c); }
    ShuffleAfterSearch(ref, 0);
    return ref.players[0].library.front().m_number;
}
}   // namespace

TEST_CASE("Maze's End F1 (gi6): Guild Summit's Gate draw waits for Maze's End's \"then shuffle\" (CR 603.3)")
{
    LoadF1Cards();
    GameState s = FBase();
    FPut(s, "Guild Summit", 600);
    FPut(s, "Forest", 603);
    FPut(s, "Forest", 604);
    const int maze = FPut(s, "Maze's End", 605);
    for (int k = 0; k < 14; ++k) { s.players[0].library.push_back(FCard("Forest", 900 + k)); }
    s.players[0].library.push_back(FCard("Boros Guildgate", 606));
    const int pre_shuffle_top = s.players[0].library.front().m_number;
    const int expected = TopAfterTakeAndShuffle(s, 606);
    REQUIRE(expected != pre_shuffle_top);   // the seed must make the two orders distinguishable
    const std::size_t hand0 = s.players[0].hand.size();
    const std::string target = "Boros Guildgate";
    {
        const SacNamedPinScope pin(0, &target);
        REQUIRE(PermAbilitySourceLive(s, 0, maze, PermAbilityMode::TapTutorPut, nullptr));
        ApplyPermAbility(s, 0, maze, PermAbilityMode::TapTutorPut);
    }
    REQUIRE(FFind(s, 606) != nullptr);
    CHECK(s.players[0].hand.size() == hand0 + 2);   // Maze's End back + ONE Summit draw
    CHECK(FInHand(s, expected));                    // drawn from the SHUFFLED library
    CHECK_FALSE(FInHand(s, pre_shuffle_top));       // never the known pre-shuffle top
    CHECK(g_triggered_draw_deferral == nullptr);    // the scope closed
}

TEST_CASE("Maze's End F1 (gi6): a land drop's Guild Summit draw is still immediate (no search, no scope)")
{
    LoadF1Cards();
    GameState s = FBase();
    s.players[0].lands_played_this_turn = 0;
    FPut(s, "Guild Summit", 600);
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(FCard("Forest", 900 + k)); }
    const int top = s.players[0].library.front().m_number;
    s.players[0].hand.push_back(FCard("Azorius Guildgate", 601));
    REQUIRE(PlayLandFromHand(s, 0, FDef("Azorius Guildgate"), LandPlayOptions{}));
    CHECK(FInHand(s, top));
}

TEST_CASE("Maze's End F1 (gi6): nested search-put scopes on one state defer to the OUTERMOST; a clone draws now")
{
    LoadF1Cards();
    GameState s = FBase();
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(FCard("Forest", 900 + k)); }
    GameState clone = s;
    {
        SearchPutTriggerScope outer(s);
        {
            SearchPutTriggerScope inner(s);
            TriggeredDraw(s, 0, 1, false);
        }
        CHECK(s.players[0].hand.empty());          // the inner scope did not flush
        TriggeredDraw(clone, 0, 1, false);          // another state: not deferred
        CHECK(clone.players[0].hand.size() == 1);
    }
    CHECK(s.players[0].hand.size() == 1);           // the outer scope flushed
    CHECK(s.players[0].cards_drawn_this_turn == 1);
}
