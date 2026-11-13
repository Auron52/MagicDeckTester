// Unit tests for MTG_LEAFLESS_READS_LEAFED (P17/P19, logs/hydras-research/P19-leafless-read.md): a LEAFLESS node
// answers from a LEAFED (rollout) pass's game-line-cache entry for the same (state, depth), restricted to its
// window. Each case runs one rollout pass at `depth` to fill the game line cache, then the leafless pass twice on
// that same cache -- once with the read pinned OFF (a cold leafless search) and once ON -- and checks that the two
// answers agree and that the ON pass did less work. The decisive case is the one where the rollout's answer lies
// PAST the leafless window: the leafless pass must still say "no win".
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "deck/DeckLoader.h"

#include <string>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(
            ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

constexpr int kMaxTurns = 8;

// Turn 6, our pre-combat main with the land drop spent, a library of Swamps (every draw is a land), the Vampires
// provider, three Swamps and a summoning-sick Legion Lieutenant (2 power, no haste): it first attacks on turn 7.
// Falkenrath Gorger in hand needs red and stays uncastable. So the kill turn is set by the opponent's life alone:
// 2 -> turn 7, 4 -> turn 8.
GameState Board(int opp_life)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = opp_life;
    s.players[0].lands_played_this_turn = 1;
    s.m_provider = &SelectDecisionProvider(DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Vampires/Vampire.cod")));
    for (int k = 0; k < 20; ++k)
    {
        Card c = Def("Swamp").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    for (int k = 0; k < 3; ++k)
    {
        Permanent p;
        p.card = Def("Swamp").card; p.card.m_number = 50 + k;
        p.controller_index = 0; p.owner_index = 0;
        s.battlefield.push_back(p);
    }
    Permanent lt;
    lt.card = Def("Legion Lieutenant").card; lt.card.m_number = 1;
    lt.controller_index = 0; lt.owner_index = 0; lt.entered_this_turn = true;
    s.battlefield.push_back(lt);
    Card g = Def("Falkenrath Gorger").card; g.m_number = 10;
    s.players[0].hand.push_back(g);
    return s;
}

}   // namespace

TEST_CASE("Leafless reads leafed: a rollout win PAST the leafless window reads as no-win, with less work")
{
    EnsureCardsLoaded();
    // depth 2 from turn 6: the window is turns 6-7, the kill is on turn 8 -- only the rollout leaf sees it.
    const TurnSolver::LeaflessReadProbe r = TurnSolver::LeaflessReadProbeForTest(Board(4), 2, kMaxTurns, false);
    CHECK(r.leafed_win == 8);                 // the rollout pass found the turn-8 kill beyond its window
    CHECK(r.cold_win == kMaxTurns + 1);       // a cold leafless pass sees no in-window win
    CHECK(r.read_win == r.cold_win);          // ...and reading the rollout pass's entries changes nothing
    CHECK(r.cold_reads == 0);                 // =0: never read across leaf kinds
    CHECK(r.read_reads > 0);                  // =1: the read actually fired
    CHECK(r.read_leaves < r.cold_leaves);     // ...and saved leaf visits
}

TEST_CASE("Leafless reads leafed: a next-turn kill is the same answer with the read on or off")
{
    EnsureCardsLoaded();
    // depth 3 from turn 6: the kill is on turn 7. Both passes settle it without a leaf (and this board needs no read),
    // so this only pins that the read changes nothing where it has nothing to add.
    const TurnSolver::LeaflessReadProbe r = TurnSolver::LeaflessReadProbeForTest(Board(2), 3, kMaxTurns, false);
    CHECK(r.leafed_win == 7);
    CHECK(r.cold_win == 7);
    CHECK(r.read_win == 7);
    CHECK(r.cold_reads == 0);
}

TEST_CASE("Leafless reads leafed: an IN-window rollout win is read as the leafless answer")
{
    EnsureCardsLoaded();
    // depth 3 from turn 6: the window is turns 6-8 and holds the turn-8 kill, so the rollout pass's entry is an
    // in-window win and the leafless pass returns it.
    const TurnSolver::LeaflessReadProbe r = TurnSolver::LeaflessReadProbeForTest(Board(4), 3, kMaxTurns, false);
    CHECK(r.leafed_win == 8);
    CHECK(r.cold_win == 8);
    CHECK(r.read_win == 8);
    CHECK(r.cold_reads == 0);
    CHECK(r.read_reads > 0);
    CHECK(r.read_leaves <= r.cold_leaves);
}
