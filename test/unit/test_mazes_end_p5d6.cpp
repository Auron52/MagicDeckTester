// Maze's End P5d-6 (2026-11-20; docs/design/analysis-Maze's End.md "P5d-6"): the USER's Q1 / Q2 rulings as provider
// ORDER / RESTRICT levers, all DEFAULT OFF. Q2: the Manabond discard-value rule (MazesEndProvider::CrossingDumpYesOrder:
// 2 = "no" pruned, -1 = "no" first, 0 = engine default). Q1: TapTutorPutEval's lands-short tier under
// MTG_MAZE_FETCH_TIER_FULL (C2) and MTG_MAZE_FETCH_RANK_COND.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>
#include <vector>

namespace
{
void LoadCards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card C(const char* name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Card c = d->card;
    c.m_number = number;
    return c;
}

void Put(GameState& s, const char* name, int number)
{
    Permanent p;
    p.card = C(name, number);
    p.controller_index = 0;
    p.owner_index = 0;
    s.battlefield.push_back(p);
}

struct Arm
{
    int slot;
    Arm(int sl, int v) : slot(sl) { heurarm::t_arm[sl] = static_cast<std::int8_t>(v); }
    ~Arm() { heurarm::t_arm[slot] = -1; }
};

const char* kGates[] = { "Azorius Guildgate", "Boros Guildgate", "Dimir Guildgate", "Golgari Guildgate",
                         "Gruul Guildgate", "Izzet Guildgate", "Orzhov Guildgate", "Rakdos Guildgate",
                         "Selesnya Guildgate", "Sea Gate", "Manor Gate", "Cliffgate" };

// Our end step with Manabond and Maze's End out; `board_gates` Gates in play; hand = `hand_gates` new-name Gates + `extra`.
GameState Board(const DecisionProvider& prov, int board_gates, int hand_gates, const std::vector<const char*>& extra,
                const std::vector<const char*>& also_out = {})
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number = 4;
    s.phase = Phase::Ending;
    s.m_provider = &prov;
    Put(s, "Manabond", 700);
    Put(s, "Maze's End", 701);
    int n = 0;
    for (int k = 0; k < board_gates; ++k) { Put(s, kGates[n++], 710 + k); }
    for (const char* o : also_out) { Put(s, o, 730 + n++); }
    for (int k = 0; k < hand_gates; ++k) { s.players[0].hand.push_back(C(kGates[n++], 750 + k)); }
    int m = 0;
    for (const char* e : extra) { s.players[0].hand.push_back(C(e, 770 + m++)); }
    return s;
}

int Order(const MazesEndProvider& prov, const GameState& s)
{
    int lands = 0;
    for (const Card& c : s.players[0].hand) { lands += c.IsLand() ? 1 : 0; }
    return prov.CrossingDumpYesOrder(s, 0, lands);
}
}   // namespace

TEST_CASE("Maze's End P5d-6 Q2: the Manabond discard-value rule (CrossingDumpYesOrder)")
{
    LoadCards();
    const MazesEndProvider prov;
    {
        const GameState s = Board(prov, 2, 1, { "Gates Ablaze" });
        CHECK(Order(prov, s) == 0);                                   // lever OFF: engine default
    }
    const Arm on(heurarm::MAZE_MANABOND_DISCARD, 1);
    CHECK(Order(prov, Board(prov, 2, 1, { "Gates Ablaze" })) == 2);   // a goldfish no-op is free: "no" pruned
    CHECK(Order(prov, Board(prov, 2, 1, { "Sylvan Scrying" })) == -1);   // 1 land + tutor: cast it first
    CHECK(Order(prov, Board(prov, 2, 2, { "Sylvan Scrying" })) == 2);    // 2 lands, one tutor: high-impact
    CHECK(Order(prov, Board(prov, 2, 2, { "Sylvan Scrying", "Sylvan Scrying" })) == -1);   // "consider again"
    CHECK(Order(prov, Board(prov, 2, 3, { "Sylvan Scrying", "Archdruid's Charm" })) == 2); // 3 lands
    CHECK(Order(prov, Board(prov, 7, 2, { "Sylvan Scrying", "Sylvan Scrying" })) == 2);    // sets up the win
    CHECK(Order(prov, Board(prov, 2, 1, { "Amulet of Vigor" })) == -1);  // kept card for one land
    CHECK(Order(prov, Board(prov, 2, 2, { "Amulet of Vigor" })) == 0);   // kept card for two: searched, default
    CHECK(Order(prov, Board(prov, 2, 2, { "Exploration", "Exploration" })) == 0);   // one Exploration kept
    CHECK(Order(prov, Board(prov, 2, 2, { "Exploration", "Exploration" }, { "Exploration" })) == 2);
    CHECK(Order(prov, Board(prov, 2, 2, { "Guild Summit", "Guild Summit" })) == 0);   // one Summit kept
    CHECK(Order(prov, Board(prov, 2, 2, { "Guild Summit" }, { "Guild Summit" })) == 2); // an extra Summit is free
    CHECK(Order(prov, Board(prov, 2, 2, { "Burgeoning" }, { "Exploration" })) == 2);  // Burgeoning droppable
    {   // no Maze's End ours anywhere: the tutor is how the deck finds it -- held even for three lands
        GameState s = Board(prov, 2, 3, { "Sylvan Scrying" });
        s.battlefield.erase(s.battlefield.begin() + 1);   // Maze's End (701)
        CHECK(Order(prov, s) == -2);                         // batch 3 (USER Q4): "yes" pruned
        const Arm tp(heurarm::MAZE_MANABOND_TUTOR_PRUNE, 0);
        CHECK(Order(prov, s) == -1);                         // sub-lever off: "no" only ordered first
    }
    {   // the same hand with Maze's End IN HAND (not on the battlefield): ours, so the 3-land yes frees the tutor
        GameState s = Board(prov, 2, 3, { "Sylvan Scrying" });
        s.players[0].hand.push_back(s.battlefield[1].card);
        s.battlefield.erase(s.battlefield.begin() + 1);
        CHECK(Order(prov, s) == 2);
    }
    {   // no Maze's End anywhere, no tutor in hand: nothing held, so the prune does not fire
        GameState s = Board(prov, 2, 1, { "Gates Ablaze" });
        s.battlefield.erase(s.battlefield.begin() + 1);
        CHECK(Order(prov, s) == 2);
    }
    {
        const Arm nb(heurarm::MAZE_DISCARD_BURGEONING, 0);
        CHECK(Order(prov, Board(prov, 2, 2, { "Burgeoning" }, { "Exploration" })) == 0);
    }
}

TEST_CASE("Maze's End P5d-6 Q1: the fetch's lands-short tier (TapTutorPutEval)")
{
    LoadCards();
    const MazesEndProvider prov;
    const CardDefinition* maze = CardDatabase::Instance().Lookup("Maze's End");
    REQUIRE(maze != nullptr);
    auto board = [&](int gates, bool summit)
    {
        GameState s;
        s.active_player_index = 0;
        s.turn_number = 4;
        s.phase = Phase::PreCombatMain;
        s.m_provider = &prov;
        Put(s, "Maze's End", 701);
        for (int k = 0; k < gates; ++k) { Put(s, kGates[k], 710 + k); }
        if (summit) { s.players[0].hand.push_back(C("Guild Summit", 790)); }
        return s;
    };
    // An empty hand cannot fill this turn's drop plus next turn's: the lands-short tier.
    CHECK(prov.TapTutorPutEval(board(3, false), *maze, "Simic Guildgate") == 1000);   // shipped: "win out"
    {
        const Arm c2(heurarm::MAZE_FETCH_TIER_FULL, 1);
        CHECK(prov.TapTutorPutEval(board(3, false), *maze, "Simic Guildgate") == 100);
    }
    const Arm cond(heurarm::MAZE_FETCH_RANK_COND, 1);
    CHECK(prov.TapTutorPutEval(board(3, false), *maze, "Simic Guildgate") == 100);    // early: one spell's worth
    CHECK(prov.TapTutorPutEval(board(3, true), *maze, "Simic Guildgate") == 99);      // Guild Summit first
    CHECK(prov.TapTutorPutEval(board(7, true), *maze, "Simic Guildgate") == 1000);    // near the win
}
