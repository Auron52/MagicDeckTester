// DEPLETION SBA LOCKSTEP (MTG_DEPLETION_SBA; TurnSolver.cpp DepletionSbaOn).
//
// "{T}, Remove a depletion counter: Add {U}{U}. If there are no depletion counters on Saprazzan Skerry,
// sacrifice it." The executor's CheckStateBasedActions sacrifices the emptied land after every
// resolution; the apply (search + rollout) used to sweep only once, at the END of the plan, so a later
// cast of the same plan resolved against a board that still held the dead land.
//
// NivMizzetCombo seed 6, T3 (claude_s6 hand-off "0,1,0,2,0,0,4,0,0"): Izzet Signet spends the Skerry's
// last counter, then Preordain scries Island + Cascade Bluffs. GenericProvider::ScryKeepOnTop keeps a
// land "only while fewer than two lands are in play": the executor (Skerry gone, Canal alone) kept both
// and drew the Island; the search counted the dead Skerry, bottomed both and drew Niv-Mizzet. Every
// continuation it scored was built on draws the real game never made, the recorded "play Saprazzan
// Skerry" could not replay, no land was played at all, and the committed T5 realised T7.
//
// Both worlds run here: the rollout (TurnSolver::ApplyPlan) and the executor (AIEngine::TakeTurn through
// the committed-plan seam, its stack resolved with the depletion sweep GameEngine::ResolveStack runs).
// The lever-OFF arm pins the divergence the lever closes.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "ai/AIEngine.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsDs()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefDs(const std::string& name)
{
    EnsureCardsDs();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Permanent& PutDs(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card              = DefDs(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = false;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

void HandDs(GameState& s, const std::string& name, int number)
{
    Card c = DefDs(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

void LibDs(GameState& s, const std::string& name, int number)
{
    Card c = DefDs(name).card;
    c.m_number = number;
    s.players[0].library.push_back(c);
}

// The seed-6 T3 board, reduced to what the divergence needs: Saprazzan Skerry on its LAST counter and
// Spirebluff Canal; Izzet Signet + Preordain in hand; Island, Cascade Bluffs, Niv-Mizzet on top. The
// turn's land is already spent, so nothing drawn can be played and the continuation has no mana.
GameState Board6()
{
    EnsureCardsDs();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.game_seed           = 6;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    Permanent& sk = PutDs(s, "Saprazzan Skerry", 50);
    sk.counters.Add(Counter::Type::Depletion, 1);
    PutDs(s, "Spirebluff Canal", 55);
    HandDs(s, "Izzet Signet", 26);
    HandDs(s, "Preordain", 38);
    LibDs(s, "Island", 301);
    LibDs(s, "Cascade Bluffs", 302);
    LibDs(s, "Niv-Mizzet, the Firemind", 303);
    for (int k = 0; k < 6; ++k) { LibDs(s, "Lightning Bolt", 310 + k); }   // inert filler
    return s;
}

bool InHandDs(const GameState& s, int number)
{
    for (const Card& c : s.players[0].hand) { if (c.m_number == number) { return true; } }
    return false;
}

bool OnBattlefieldDs(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return true; } }
    return false;
}

// GameEngine::ResolveStack's shape: resolve, then the state-based depletion sweep
// (GameEngine::CheckStateBasedActions -> SacrificeDepletedLands) after EVERY resolution.
void ResolveStackDs(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
        SacrificeDepletedLands(st);
    }
}

GameState RunExecutorDs(const GameState& s, const TurnSolver::Plan& plan)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ResolveStackDs);
    return exec;
}

// The enumerated plan that plays no land and casts exactly Izzet Signet then Preordain.
const TurnSolver::Plan* SignetThenPreordain(const std::vector<TurnSolver::Plan>& plans)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (!p.land_to_play.empty()) { continue; }
        std::vector<std::string> casts;
        bool other = false;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand) { casts.push_back(a.card_name.str()); }
            else { other = true; }
        }
        if (!other && casts == std::vector<std::string>{ "Izzet Signet", "Preordain" }) { return &p; }
    }
    return nullptr;
}

struct ArmDs
{
    ArmDs(heurarm::Slot s, bool on) { heurarm::t_arm[static_cast<int>(s)] = on ? 1 : 0; }
    ~ArmDs() { heurarm::Clear(); }
};

}   // namespace

TEST_CASE("Depletion SBA: the apply sacrifices an emptied depletion land before the next cast resolves (seed 6 T3), both worlds")
{
    const GameState s = Board6();
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = SignetThenPreordain(plans);
    REQUIRE(p != nullptr);

    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *p, true);
    const GameState exec = RunExecutorDs(s, *p);

    // The executor: the Skerry paid Izzet Signet and was sacrificed before Preordain resolved, so the
    // scry saw ONE land and kept Island + Bluffs; the draw took the Island.
    REQUIRE(OnBattlefieldDs(exec, 26));            // Izzet Signet resolved
    REQUIRE_FALSE(OnBattlefieldDs(exec, 50));      // ...paid by the Skerry's last counter: sacrificed
    REQUIRE(InHandDs(exec, 301));                  // Preordain drew the Island

    // The rollout now resolves Preordain against the same board.
    CHECK_FALSE(OnBattlefieldDs(roll, 50));
    CHECK(InHandDs(roll, 301));
    CHECK_FALSE(InHandDs(roll, 303));
    REQUIRE_FALSE(roll.players[0].library.empty());
    CHECK(roll.players[0].library.front().m_number == exec.players[0].library.front().m_number);
    CHECK(roll.players[0].library.front().m_number == 302);   // Cascade Bluffs stayed on top

    // Lever OFF: the end-of-plan sweep -- the rollout's Preordain counts the dead Skerry as a second
    // land, bottoms both and draws Niv-Mizzet (the divergence).
    {
        ArmDs off(heurarm::DEPLETION_SBA, false);
        GameState r0 = s;
        TurnSolver::ApplyPlan(r0, *p, true);
        CHECK(InHandDs(r0, 303));
        CHECK_FALSE(InHandDs(r0, 301));
        CHECK_FALSE(OnBattlefieldDs(r0, 50));      // ...swept at the end of the plan all the same
    }
}
