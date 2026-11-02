// RESOLVED-SPELL COPY ID (MTG_SPELL_COPY_ID, Cyclone ledger Q53): the apply path files a resolved instant / sorcery
// into the graveyard as its CAST COPY (its m_number), as the executor does -- never the definition's template
// (m_number 0), which an Eldrazi's graveyard shuffle dealt back as tied, duplicate-number cards.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{
void EnsureCardsSc()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}
Card CardSc(const std::string& name, int number)
{
    EnsureCardsSc();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Card c = d->card;
    c.m_number = number;
    return c;
}
GameState BoardSc()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.game_seed           = 31;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    Permanent p;
    p.card = CardSc("Island", 1); p.controller_index = 0; p.owner_index = 0;
    s.battlefield.push_back(p);
    s.players[0].hand.push_back(CardSc("Ponder", 20));
    s.players[0].hand.push_back(CardSc("Lightning Bolt", 21));
    for (int k = 0; k < 8; ++k) { s.players[0].library.push_back(CardSc("Island", 100 + k)); }
    return s;
}
void ResolveStackSc(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}
const TurnSolver::Plan* PonderOnly(const std::vector<TurnSolver::Plan>& plans)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() == 1 && p.actions[0].card_name == "Ponder" && p.land_to_play.empty()
            && p.ponder_choice < 0) { return &p; }
    }
    return nullptr;
}
std::vector<int> GyNums(const GameState& s)
{
    std::vector<int> v;
    for (const Card& c : s.players[0].graveyard) { v.push_back(c.m_number); }
    return v;
}
}   // namespace

TEST_CASE("Spell copy id (Q53): the apply files the CAST COPY into the graveyard, lockstep with the executor")
{
    const GameState s = BoardSc();
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = PonderOnly(plans);
    REQUIRE(p != nullptr);
    // The executor (EffectHandler::MoveToGraveyard) files the real copy, #20.
    GameState exec = s;
    {
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, *p, true);
        eng.TakeTurn(exec, true, ResolveStackSc);
    }
    CHECK(GyNums(exec) == std::vector<int>{ 20 });
    // The apply, the lever ON (default): the same copy.
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *p, true);
    CHECK(GyNums(roll) == std::vector<int>{ 20 });
    // The lever OFF: the definition's template, number 0 -- the defect.
    {
        const heurarm::Arm saved = heurarm::t_arm;
        heurarm::t_arm[heurarm::SPELL_COPY_ID] = 0;
        GameState old = s;
        TurnSolver::ApplyPlan(old, *p, true);
        heurarm::t_arm = saved;
        CHECK(GyNums(old) == std::vector<int>{ 0 });
    }
}
