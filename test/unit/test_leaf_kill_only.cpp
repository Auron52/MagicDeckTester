// Unit tests for MTG_LEAF_KILL_ONLY (Vampires search-cost round 2, agent R; docs/design/vampires-search-cost-2.md):
// on the turn a horizon rollout's cutoff ends, only a THIS-TURN kill can change its answer, so the per-turn
// solves there skip every candidate's next-turn rollout. Each case runs the same rollout with the scope
// armed (default) and with the heurarm slot pinned to 0, and checks the answers agree and the scope armed
// exactly where the rule says.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/SearchBudget.h"
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

// Turn 6, our pre-combat main with the land drop spent; a library of Swamps (every draw is a land, so the
// future is predictable); the Vampires provider.
GameState Fresh(int opp_life)
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
    return s;
}

void Put(GameState& s, const std::string& name, int number, bool sick = false)
{
    Permanent p;
    p.card              = Def(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
}

void Hand(GameState& s, const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

struct ArmScope   // pin one heurarm slot for the scope
{
    heurarm::Slot s;
    explicit ArmScope(heurarm::Slot slot, int v) : s(slot) { heurarm::t_arm[slot] = static_cast<std::int8_t>(v); }
    ~ArmScope() { heurarm::t_arm[s] = -1; }
};

constexpr int kMaxTurns = 8;

struct Both { int on; int off; TurnSolver::KillOnlyTally t_on; TurnSolver::KillOnlyTally t_off; };

Both Run(const GameState& s, int cutoff, bool second_main = true)
{
    Both b{};
    b.on = TurnSolver::SimulateToEndForTest(s, 1, kMaxTurns, cutoff, second_main, &b.t_on);
    {
        ArmScope off(heurarm::LEAF_KILL_ONLY, 0);
        b.off = TurnSolver::SimulateToEndForTest(s, 1, kMaxTurns, cutoff, second_main, &b.t_off);
    }
    return b;
}

// A board with several castable plans this turn and no way to kill a 20-life opponent today.
GameState NoKillBoard()
{
    GameState s = Fresh(20);
    for (int k = 0; k < 4; ++k) { Put(s, "Swamp", 50 + k); }
    Put(s, "Blackcleave Cliffs", 60);
    Put(s, "Bloodghast", 1);
    Hand(s, "Falkenrath Gorger", 10);
    Hand(s, "Legion Lieutenant", 11);
    Hand(s, "Markov Baron", 12);
    Hand(s, "Stromkirk Condemned", 13);
    return s;
}

}   // namespace

TEST_CASE("Leaf kill-only: at a slack-0 leaf with no kill this turn, both arms return no-win and the scope skips rollouts")
{
    EnsureCardsLoaded();
    const GameState s = NoKillBoard();
    const Both b = Run(s, /*cutoff=*/6);
    CHECK(b.on == kMaxTurns + 1);
    CHECK(b.off == kMaxTurns + 1);
    CHECK(b.t_on.armed == 1);       // armed on the one turn the cutoff leaves
    CHECK(b.t_on.skipped > 0);      // ...and it actually skipped candidate rollouts
    CHECK(b.t_off.armed == 0);      // =0: never armed
    CHECK(b.t_off.skipped == 0);
}

TEST_CASE("Leaf kill-only: a this-turn kill is found under the scope exactly as without it")
{
    EnsureCardsLoaded();
    GameState s = NoKillBoard();
    s.players[1].life = 2;          // Bloodghast's attack alone is lethal
    const Both b = Run(s, /*cutoff=*/6);
    CHECK(b.off == 6);
    CHECK(b.on == 6);
    CHECK(b.t_on.armed == 1);
}

TEST_CASE("Leaf kill-only: a kill only NEXT turn is no-win at cutoff 6 and the same win at cutoff 7 (armed on turn 7 only)")
{
    EnsureCardsLoaded();
    GameState s = Fresh(2);
    for (int k = 0; k < 3; ++k) { Put(s, "Swamp", 50 + k); }
    Put(s, "Legion Lieutenant", 1, /*sick=*/true);   // summoning-sick (no haste): lethal next turn only
    Hand(s, "Falkenrath Gorger", 10);          // uncastable (no red), keeps the candidate list honest
    {
        const Both b = Run(s, /*cutoff=*/6);
        CHECK(b.on == kMaxTurns + 1);
        CHECK(b.off == kMaxTurns + 1);
        CHECK(b.t_on.armed == 1);
    }
    {
        const Both b = Run(s, /*cutoff=*/7);
        CHECK(b.off == 7);
        CHECK(b.on == b.off);
        CHECK(b.t_on.armed == 1);   // turn 6 is not the last turn (slack 1); turn 7 is
    }
}

TEST_CASE("Leaf kill-only: a full-horizon rollout and a deck without a searched second main never arm")
{
    EnsureCardsLoaded();
    const GameState s = NoKillBoard();
    {
        // cutoff == max_turns: the natural horizon exit grades the final state, so the pick matters.
        const Both b = Run(s, /*cutoff=*/kMaxTurns);
        CHECK(b.on == b.off);
        CHECK(b.t_on.armed == 0);
    }
    {
        const Both b = Run(s, /*cutoff=*/6, /*second_main=*/false);
        CHECK(b.on == b.off);
        CHECK(b.t_on.armed == 0);
    }
}

TEST_CASE("Leaf kill-only: an own-death deck (Vampires arms it) arms the scope and agrees with the full solve")
{
    EnsureCardsLoaded();
    GameState s = NoKillBoard();
    s.own_death_live = true;
    {
        const Both b = Run(s, /*cutoff=*/6);
        CHECK(b.on == kMaxTurns + 1);
        CHECK(b.off == kMaxTurns + 1);
        CHECK(b.t_on.armed == 1);
        CHECK(b.t_on.skipped > 0);
    }
    s.players[1].life = 2;
    {
        const Both b = Run(s, /*cutoff=*/6);
        CHECK(b.off == 6);
        CHECK(b.on == 6);
    }
}

TEST_CASE("Spent-budget leaf memo: a repeated no-win leaf after the budget is spent costs nothing the second time")
{
    EnsureCardsLoaded();
    GameState s = NoKillBoard();
    s.players[1].life = 60;             // out of reach by the horizon: every cutoff is a no-win
    for (int cutoff : { 6, 7, kMaxTurns })
    {
        CAPTURE(cutoff);
        const TurnSolver::SpentTwice on = TurnSolver::SimulateToEndSpentTwiceForTest(s, 1, kMaxTurns, cutoff, true, 100);
        TurnSolver::SpentTwice off;
        {
            ArmScope o(heurarm::EXH_LEAF_MEMO, 0);
            off = TurnSolver::SimulateToEndSpentTwiceForTest(s, 1, kMaxTurns, cutoff, true, 100);
        }
        CHECK(on.first == kMaxTurns + 1);
        CHECK(on.second == on.first);       // the memo answers exactly what the recompute returns
        CHECK(off.second == off.first);
        CHECK(on.first == off.first);
        CHECK(on.units_first == off.units_first);
        CHECK(off.units_second > 0);        // =0: the spent pass recomputes the leaf
        CHECK(on.units_second == 0);        // on: the second call is the memo's
    }
}

TEST_CASE("Kill-only m2 no-reach: Vampires vouches no post-combat kill, so the kill-only turn skips its m2 search")
{
    EnsureCardsLoaded();
    const long long kBig = 1LL << 40;   // finite (so units are counted), never spent
    for (int opp : { 20, 2 })
    {
        CAPTURE(opp);
        GameState s = NoKillBoard();
        s.players[1].life = opp;        // 2: Bloodghast's attack kills in COMBAT, which the cut leaves alone
        TurnSolver::KillOnlyTally on, off;
        const int w_on = TurnSolver::SimulateToEndForTest(s, 1, kMaxTurns, 6, true, &on, kBig);
        int w_off = 0;
        {
            ArmScope o(heurarm::KO_M2_NO_REACH, 0);
            w_off = TurnSolver::SimulateToEndForTest(s, 1, kMaxTurns, 6, true, &off, kBig);
        }
        CHECK(w_on == w_off);
        CHECK(on.armed == 1);
        if (opp == 20) { CHECK(on.units < off.units); }   // the m2 candidates are not applied
        else           { CHECK(w_on == 6); }
    }
}

TEST_CASE("Edge m2 no-reach: the full-line search skips an edge node's second main and returns the same line")
{
    EnsureCardsLoaded();
    GameState s = NoKillBoard();
    s.players[1].life = 7;              // reachable within the horizon, so the search finds a win and edges appear
    struct R { TurnSolver::SearchLine line; long long edges = 0; long long units = 0; };
    auto run = [&](int arm)
    {
        ArmScope a(heurarm::FSL_EDGE_NO_REACH, arm);
        R r;
        SearchBudget b(1LL << 40);      // finite (so units are counted), never spent
        const long long e0 = TurnSolver::FslEdgeNoReachCountForTest();
        r.line  = TurnSolver::FullSearchLine(s, 3, kMaxTurns, /*second_main=*/true, nullptr, &b);
        r.edges = TurnSolver::FslEdgeNoReachCountForTest() - e0;
        r.units = b.Used();
        return r;
    };
    const R on = run(1), off = run(0);
    CHECK(on.line.win_turn <= kMaxTurns);
    CHECK(on.line.win_turn == off.line.win_turn);
    CHECK(on.line.phases.size() == off.line.phases.size());
    for (std::size_t i = 0; i < on.line.phases.size() && i < off.line.phases.size(); ++i)
    {
        CHECK(on.line.phases[i].is_pre_combat == off.line.phases[i].is_pre_combat);
        CHECK(on.line.phases[i].plan.actions.size() == off.line.phases[i].plan.actions.size());
    }
    CHECK(off.edges == 0);
    CHECK(on.edges > 0);
    CHECK(on.units < off.units);
}
