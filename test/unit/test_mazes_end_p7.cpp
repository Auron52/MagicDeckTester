// Maze's End deck, P7 (2026-11-18; docs/design/analysis-Maze's End.md "P7"): the engine's CAST / LAND DOMINANCE
// FOLD (TurnSolver.cpp CastDominanceFold) under the Maze's End provider's premises (MazesEndProvider::
// CastDominanceLive / CastDominanceClass). A plan is dropped for its twin plus one more cast of Exploration /
// Burgeoning / Manabond / Amulet of Vigor (fold A), and a land-less plan of only such casts (Amulet aside) for its
// twin plus a land (fold B). Not live with a Treasure-making Gate untapped or in hand, or a Gateway Plaza in hand;
// Guild Summit vetoes. MTG_MAZE_CAST_DOMINANCE / MTG_MAZE_LAND_DOMINANCE, both DEFAULT ON.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "mtg_test_seam.h"

#include <set>
#include <string>
#include <vector>

namespace
{
void LoadP7Cards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& D(const char* name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    return *d;
}

Card C(const char* name, int number)
{
    Card c = D(name).card;
    c.m_number = number;
    return c;
}

void Put(GameState& s, const char* name, int number, bool tapped = false)
{
    Permanent p;
    p.card = C(name, number);
    p.controller_index = 0;
    p.owner_index = 0;
    p.tapped = tapped;
    s.battlefield.push_back(p);
}

struct Arm
{
    int slot;
    Arm(int sl, int v) : slot(sl) { heurarm::t_arm[sl] = static_cast<std::int8_t>(v); }
    ~Arm() { heurarm::t_arm[slot] = -1; }
};

// The turn-4 position of the census dump (seed 30001): Baldur's Gate, Amulet of Vigor, Golgari Guildgate in play;
// two Exploration, Burgeoning and four lands in hand.
GameState T4(const DecisionProvider& prov)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.m_provider          = &prov;
    s.deck_has_crossing_land_put = true;
    s.players[0].lands_played_this_turn = 0;
    Put(s, "Baldur's Gate", 1);
    Put(s, "Amulet of Vigor", 2);
    Put(s, "Golgari Guildgate", 3);
    s.players[0].hand.push_back(C("Exploration", 10));
    s.players[0].hand.push_back(C("Exploration", 11));
    s.players[0].hand.push_back(C("Gond Gate", 12));
    s.players[0].hand.push_back(C("Burgeoning", 13));
    s.players[0].hand.push_back(C("Manor Gate", 14));
    s.players[0].hand.push_back(C("Maze's End", 15));
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(C(k % 2 ? "Izzet Guildgate" : "Sylvan Scrying", 30 + k)); }
    return s;
}

// Each base plan (bp_choice < 0) as "land|cast,cast" (cast names in plan order).
std::set<std::string> Menu(const GameState& s)
{
    std::set<std::string> out;
    for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
    {
        if (p.bp_choice >= 0) { continue; }
        std::string k = p.land_to_play + "|";
        for (const Action& a : p.actions) { k += a.card_name.str() + ","; }
        out.insert(k);
    }
    return out;
}
}   // namespace

TEST_CASE("Maze's End P7: the cast / land dominance fold drops plans a sibling dominates")
{
    LoadP7Cards();
    const MazesEndProvider prov;
    const GameState s = T4(prov);
    CHECK(prov.CastDominanceLive(s));
    CHECK(prov.CastDominanceClass(s, D("Exploration")) == 1);
    CHECK(prov.CastDominanceClass(s, D("Burgeoning")) == 1);
    CHECK(prov.CastDominanceClass(s, D("Manabond")) == 1);
    CHECK(prov.CastDominanceClass(s, D("Amulet of Vigor")) == 2);
    CHECK(prov.CastDominanceClass(s, D("Guild Summit")) == -1);
    CHECK(prov.CastDominanceClass(s, D("Sylvan Scrying")) == 0);
    CHECK(prov.CastDominanceClass(s, D("Gond Gate")) == 0);

    std::set<std::string> off_menu;
    {
        const Arm off(heurarm::MAZE_CAST_DOMINANCE, 0);
        CHECK_FALSE(prov.CastDominanceLive(s));
        off_menu = Menu(s);
    }
    const std::set<std::string> on_menu = Menu(s);
    // Restrict-only: every plan the fold keeps is on the unfolded menu, and it removes something here.
    for (const std::string& k : on_menu) { CHECK(off_menu.count(k) == 1); }
    CHECK(on_menu.size() < off_menu.size());
    // The unfolded menu holds the dominated shapes; the folded one does not.
    CHECK(off_menu.count("|") == 1);                    // pass
    CHECK(off_menu.count("Manor Gate|") == 1);          // a land alone, while "+ Exploration" is affordable
    CHECK(on_menu.count("|") == 0);
    CHECK(on_menu.count("Manor Gate|") == 0);
    CHECK(on_menu.count("Gond Gate|") == 0);
    // A land-less plan of class-1 casts goes for its land twin (fold B) ...
    if (off_menu.count("|Exploration,") == 1) { CHECK(on_menu.count("|Exploration,") == 0); }
    {
        // ... and stays with fold B off (fold A alone), as does every maximal plan.
        const Arm b_off(heurarm::MAZE_LAND_DOMINANCE, 0);
        const std::set<std::string> a_menu = Menu(s);
        if (off_menu.count("|Exploration,") == 1) { CHECK(a_menu.count("|Exploration,") == 1); }
        for (const std::string& k : on_menu) { CHECK(a_menu.count(k) == 1); }
    }
    // Every maximal plan of the unfolded menu survives: the best two-cast lines are all still there.
    for (const std::string& k : off_menu)
    {
        if (k.find("Exploration,Exploration,") != std::string::npos || k.find("Exploration,Burgeoning,") != std::string::npos)
        { CHECK(on_menu.count(k) == 1); }
    }
}

TEST_CASE("Maze's End P7: the fold's mana premise -- not live with a Treasure Gate or a Gateway Plaza")
{
    LoadP7Cards();
    const MazesEndProvider prov;
    {
        GameState s = T4(prov);
        Put(s, "Heap Gate", 40);                        // untapped: the last-chance Treasure could use held mana
        CHECK_FALSE(prov.CastDominanceLive(s));
    }
    {
        GameState s = T4(prov);
        Put(s, "Heap Gate", 40, /*tapped=*/true);       // tapped: it cannot make a Treasure before our untap
        CHECK(prov.CastDominanceLive(s));
    }
    {
        GameState s = T4(prov);
        s.players[0].hand.push_back(C("Heap Gate", 41));
        CHECK_FALSE(prov.CastDominanceLive(s));
    }
    {
        GameState s = T4(prov);
        s.players[0].hand.push_back(C("Gateway Plaza", 42));   // Burgeoning / Manabond could put it off-main
        CHECK_FALSE(prov.CastDominanceLive(s));
    }
}
