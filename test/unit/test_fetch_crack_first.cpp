// Fetch-crack fix (Boars, 2026-11-14): a BATTLEFIELD fetchland's land pays the same main's casts.
//
// The defect: a battlefield land-search activation (Arid Mesa / Wooded Foothills / Windswept Heath) ran in
// the plan's TRAILING pass, after every cast, and the subset walk priced the casts on the pre-crack board,
// where the uncracked fetch produces nothing. "Crack, then cast with the fetched land" was not a plan at any
// budget, in either main, and the d0 runner's greedy walk could not reach it either (Creature Giving d3 b0
// s30015 gi14 T1: Heath -> untapped Breeding Pool, Birds of Paradise stranded in hand). The fix makes the
// crack a ROOT of the walk (EnumeratePlansCrackFirst / TurnSolver::SolveCrackFirst): the casts are
// enumerated on the cracked board and the plan runs the crack in the PRE-CAST pass, the same encoding both
// worlds already realise through TurnSolver::ApplyLandSearchActs.
//
// Shape used here: the opponent at 3, Arid Mesa on the battlefield, Lava Spike ({R}, 3 to a player) in hand
// and a Mountain in the library. The only mana is the fetched Mountain, so the kill exists only crack-first.
#include <doctest/doctest.h>
#include "ai/AIEngine.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "core/EffectHandler.h"
#include <string>
#include <vector>

namespace
{
const CardDefinition& FDef(const char* name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, name);
    return *d;
}
Card FCard(const char* name, int number)
{
    Card c = FDef(name).card;
    c.m_number = number;
    return c;
}
void FPut(GameState& s, const char* name, int number)
{
    Permanent p;
    p.card = FCard(name, number);
    p.controller_index = p.owner_index = 0;
    s.battlefield.push_back(p);
}
// Opponent at 3; Arid Mesa (10) on our battlefield, Lava Spike (20) in hand, a Mountain (30) on top of a
// library of Plains. `extra_land` adds an untapped Mountain (11): the Spike is then payable WITHOUT the crack.
GameState FBoard(bool pre_combat, bool extra_land = false)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.phase               = pre_combat ? Phase::PreCombatMain : Phase::PostCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 3;
    s.players[0].lands_played_this_turn = 1;   // the drop is spent: only the battlefield fetch can make mana
    FPut(s, "Arid Mesa", 10);
    if (extra_land) { FPut(s, "Mountain", 11); }
    s.players[0].hand.push_back(FCard("Lava Spike", 20));
    s.players[0].library.push_back(FCard("Mountain", 30));
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(FCard("Plains", 40 + k)); }
    return s;
}
bool InGraveyard(const GameState& s, const char* name)
{
    for (const Card& c : s.players[0].graveyard) { if (c.m_name == name) { return true; } }
    return false;
}
bool CastsSpike(const TurnSolver::Plan& p)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name == "Lava Spike") { return true; } }
    return false;
}
bool CracksFirst(const TurnSolver::Plan& p)
{
    if (p.actions.empty() || !TurnSolver::IsLandSearchActivation(p.actions.front())) { return false; }
    return p.land_search_act_slot == TurnSolver::kLandSlotFirst
        || (TurnSolver::LandSearchActFirstMask(p.land_search_act_slot) & 1) != 0;
}
void ResolveStack(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}
}   // namespace

TEST_CASE("Fetch crack-first: the enumeration offers crack, THEN cast with the fetched land (both mains)")
{
    for (const bool pre : { true, false })
    {
        CAPTURE(pre);
        const GameState s = FBoard(pre);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, pre);
        const TurnSolver::Plan* hit = nullptr;
        for (const TurnSolver::Plan& p : plans) { if (CastsSpike(p) && CracksFirst(p)) { hit = &p; break; } }
        REQUIRE(hit != nullptr);
        // ...and only crack-first: no plan casts the Spike with the crack in the trailing pass.
        for (const TurnSolver::Plan& p : plans) { if (CastsSpike(p)) { CHECK(CracksFirst(p)); } }
        // The ROLLOUT world (ApplyPlan -> ApplyPlanDirect) realises it: Mesa sacrificed, one life paid, the
        // fetched Mountain tapped for the Spike, the opponent dead.
        GameState t = s;
        TurnSolver::ApplyPlan(t, *hit, pre);
        CHECK(InGraveyard(t, "Arid Mesa"));
        CHECK(InGraveyard(t, "Lava Spike"));
        CHECK(t.players[0].life == 19);
        CHECK(t.players[1].life <= 0);
    }
}

TEST_CASE("Fetch crack-first: the searched executor (AIEngine::TakeTurn, d2) cracks and casts in main 1 and main 2")
{
    for (const bool pre : { true, false })
    {
        CAPTURE(pre);
        GameState s = FBoard(pre);
        AIEngine eng(MulliganProfile::DefaultProfile(), 2, 20);
        eng.SetSearchPostCombat(true);
        eng.TakeTurn(s, pre, ResolveStack);
        CHECK(InGraveyard(s, "Arid Mesa"));
        CHECK(InGraveyard(s, "Lava Spike"));
        CHECK(s.players[1].life <= 0);
    }
}

TEST_CASE("Fetch crack-first: the d0 runner and the horizon greedy walk the cracked board too")
{
    for (const bool pre : { true, false })
    {
        CAPTURE(pre);
        // The greedy walk itself (the horizon leaf's / d0 runner's Solve).
        {
            const GameState s = FBoard(pre);
            const TurnSolver::Plan g =
                TurnSolver::Solve(s, pre, TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0));
            CHECK(CastsSpike(g));
            CHECK(CracksFirst(g));
            GameState t = s;
            TurnSolver::ApplyPlan(t, g, pre);
            CHECK(t.players[1].life <= 0);
        }
        // ...and the d0 EXECUTOR (AIEngine at depth 0).
        {
            GameState s = FBoard(pre);
            AIEngine eng(MulliganProfile::DefaultProfile(), 0, 20);
            eng.SetSearchPostCombat(true);
            eng.TakeTurn(s, pre, ResolveStack);
            CHECK(InGraveyard(s, "Lava Spike"));
            CHECK(s.players[1].life <= 0);
        }
    }
}

TEST_CASE("Fetch crack-first: a cast the board already pays keeps the greedy's plain line")
{
    // With an untapped Mountain the Spike needs no fetched mana: the greedy takes the crack-first plan only
    // when it scores STRICTLY better, so this turn plays exactly as before the fix.
    const GameState s = FBoard(true, /*extra_land=*/true);
    const TurnSolver::Plan g =
        TurnSolver::Solve(s, true, TurnSolver::GreedyPermit(TurnSolver::GreedySite::HorizonLeaf, 0));
    CHECK(CastsSpike(g));
    CHECK_FALSE(CracksFirst(g));
}

TEST_CASE("Fetch crack-first: Windswept Heath -> untapped land pays Birds of Paradise (the Creature Giving gi14 shape)")
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 1;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    FPut(s, "Windswept Heath", 10);
    s.players[0].hand.push_back(FCard("Birds of Paradise", 20));
    s.players[0].library.push_back(FCard("Forest", 30));
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(FCard("Plains", 40 + k)); }
    const TurnSolver::Plan* hit = nullptr;
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    for (const TurnSolver::Plan& p : plans)
    {
        bool birds = false;
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::CastFromHand && a.card_name == "Birds of Paradise") { birds = true; } }
        if (birds && CracksFirst(p)) { hit = &p; break; }
    }
    REQUIRE(hit != nullptr);
    GameState t = s;
    TurnSolver::ApplyPlan(t, *hit, true);
    CHECK(InGraveyard(t, "Windswept Heath"));
    bool birds_out = false;
    for (const Permanent& p : t.battlefield) { if (p.card.m_name == "Birds of Paradise") { birds_out = true; } }
    CHECK(birds_out);
}
