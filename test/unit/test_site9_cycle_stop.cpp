// MTG_SITE9_CYCLE_STOP -- the site-9 re-open chain stops on a state it has already been at
// (src/ai/EngineFlags.h, Site9CycleStopEnabled). Boars seed 900115 gi115 T4: Lightning Greaves' equip {0}
// moved between two Flinthoof Boars re-keyed the post-entry activation snapshot on every re-open, so the
// delta rule never stopped and MTG_BP_DEV_SEQ walked ~2^32 deviation sequences on the identical board
// (origin: no finish in 43 min, 411 MB and growing; with the stop: 45 s, 59 MB, win turn 5).
//
// These pin the helper both twins share, TurnSolver::Site9ChainRepeats:
//   * a repeat is a repeat of the FULL mid-turn identity: where the Equipment is attached and an
//     until-end-of-turn haste are both part of it, so A -> B -> A repeats but A -> B does not;
//   * building the key is play-neutral: no work unit, no priced work, no combat-pin witness read.
#include <doctest/doctest.h>

#include "ai/GameWorkMeter.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/WorkCounters.h"

#include <cstdint>
#include <string>
#include <vector>

namespace
{

void EnsureCardsS9c()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

void PutS9c(GameState& s, const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Permanent p;
    p.card             = d->card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

Permanent& FindS9c(GameState& s, int number)
{
    for (Permanent& p : s.battlefield) { if (p.card.m_number == number) { return p; } }
    FAIL("permanent not found");
    return s.battlefield.front();
}

// The seed-115 board: Birds of Paradise, Lightning Greaves on the first Flinthoof Boar, a second Boar.
GameState BoarsBoard()
{
    EnsureCardsS9c();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    PutS9c(s, "Birds of Paradise", 2);
    PutS9c(s, "Flinthoof Boar", 15);
    PutS9c(s, "Flinthoof Boar", 16);
    PutS9c(s, "Lightning Greaves", 37);
    FindS9c(s, 37).equipped_to = 15;
    return s;
}

}   // namespace

TEST_CASE("site-9 cycle stop: Greaves moved away and back is a repeat, moved once is not")
{
    GameState s = BoarsBoard();
    std::vector<std::uint64_t> chain;
    CHECK_FALSE(TurnSolver::Site9ChainRepeats(chain, s));   // the state the first occurrence opens on
    FindS9c(s, 37).equipped_to = 16;                         // continuation 1: equip {0} -> the other Boar
    CHECK_FALSE(TurnSolver::Site9ChainRepeats(chain, s));
    FindS9c(s, 37).equipped_to = 15;                         // continuation 2: ...and back
    CHECK(TurnSolver::Site9ChainRepeats(chain, s));
    CHECK(chain.size() == 2);                                // a repeat is not recorded twice
}

TEST_CASE("site-9 cycle stop: an until-end-of-turn haste is part of the identity")
{
    GameState s = BoarsBoard();
    std::vector<std::uint64_t> chain;
    CHECK_FALSE(TurnSolver::Site9ChainRepeats(chain, s));
    FindS9c(s, 16).temp_haste = true;                        // Flinthoof Boar's {R}: haste resolved
    CHECK_FALSE(TurnSolver::Site9ChainRepeats(chain, s));
    CHECK(TurnSolver::Site9ChainRepeats(chain, s));
}

TEST_CASE("site-9 cycle stop: the key costs no work unit, no priced work and no pin-witness read")
{
    GameState s = BoarsBoard();
    const long long          used0   = gamework::t_used;
    const long long          priced0 = workctr::t_priced;
    const unsigned long long cp0     = combatpin::t_reads;
    std::vector<std::uint64_t> chain;
    TurnSolver::Site9ChainRepeats(chain, s);
    TurnSolver::Site9ChainRepeats(chain, s);
    CHECK(gamework::t_used == used0);
    CHECK(workctr::t_priced == priced0);
    CHECK(combatpin::t_reads == cp0);
}
