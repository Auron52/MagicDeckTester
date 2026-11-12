// Fiery Islet's "{1}, {T}, Sacrifice Fiery Islet: Draw a card" when the {1} is paid by cracking a Treasure that sits
// AHEAD of the Islet on the battlefield. The payment erases the Treasure, so the Islet's index taken before it points
// one past the Islet: the executor (AIEngine::PerformDigFull) and both rollout sites (TurnSolver ApplyPlan's DigDraw)
// sacrificed the NEXT permanent and left the tapped Islet in play (Pirates s30049175 T4: Daring Buccaneer died, the
// mana oracle read the stranded tapped Islet as "a mana choice the oracle does not drive", 174 UNMODELED on the
// due6-20261112 gate). Every site now re-finds the source by card number after the payment (the two TurnSolver
// sites are the same three lines; this test drives the executor's).
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <string>

namespace
{

const CardDefinition& DefSD(const std::string& name)
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void PutSD(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefSD(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

// Treasure first, then the Islet, then a creature the payment must not touch; one card to draw.
GameState TreasureAheadOfIslet()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    CreateTreasureTokens(s, 0, 1, /*fresh_hold_exempt=*/true);
    s.battlefield.back().entered_this_turn = false;   // an old Treasure: no fresh-hold doctrine in play
    PutSD(s, "Fiery Islet", 22);
    PutSD(s, "Daring Buccaneer", 14);
    Card top = DefSD("Mountain").card;
    top.m_number = 38;
    s.players[0].library.push_back(top);
    return s;
}

int CountNamed(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

void CheckIsletSacrificed(const GameState& s)
{
    CHECK(CountNamed(s, "Fiery Islet") == 0);
    CHECK(CountNamed(s, "Daring Buccaneer") == 1);
    CHECK(CountNamed(s, "Treasure Token") == 0);
    CHECK(s.players[0].hand.size() == 1);
    bool islet_in_gy = false;
    for (const Card& c : s.players[0].graveyard) { if (c.m_name.str() == "Fiery Islet") { islet_in_gy = true; } }
    CHECK(islet_in_gy);
}

}   // namespace

TEST_CASE("Fiery Islet sac-draw paid by a Treasure ahead of it sacrifices the Islet (executor)")
{
    GameState s = TreasureAheadOfIslet();
    REQUIRE(s.battlefield.size() == 3);
    REQUIRE(s.battlefield[0].card.m_name.str() == "Treasure Token");
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    const AIEngine::DigOutcome o = MtgTestSeam::PerformDigFull(eng, s, "Fiery Islet", true, -1, -1, [](GameState&) {});
    CHECK(o != AIEngine::DigOutcome::Failed);
    CheckIsletSacrificed(s);
}
