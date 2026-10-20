// MTG_BP_RESUME_LINE_HOLD (2026-10-20): a breakpoint-node RESUME must run its continuation under the line's
// unpaid-cost hold (g_line_unpaid_cost) AS THE PREFIX LEFT IT. ApplyPlanDirect binds the hold from the whole
// plan's bill at entry and apply_one pays it down cast by cast; a resume skips the prefix casts, so it used to
// pay its continuation under the FULL bill -- a phantom demand for casts already paid, which the payer reads
// (LineFloatTapFirstPlan) and the executor (a full apply) never has. Root case: Hinata s12111487 gi 486
// (MTG_HINATA_GAMBLE_PAST_DORK=1, d8 b0) committed a T5 kill the game could not pay. EngineFlags.h has the story.
#include <doctest/doctest.h>

#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <cstdint>
#include <string>

namespace
{

void EnsureCardsRl()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefRl(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardRl(const std::string& name, int number)
{
    Card c = DefRl(name).card;
    c.m_number = number;
    return c;
}

void PutRl(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card              = CardRl(name, number);
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = false;
    s.battlefield.push_back(p);
}

Action CastRl(const GameState& s, const std::string& name)
{
    const CardDefinition& d = DefRl(name);
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = name;
    a.def       = &d;
    a.cost      = EffectiveSpellCost(d, s, 1);
    a.card_mv   = d.card.m_mana_cost.ManaValue();
    return a;
}

struct RlArm
{
    std::int8_t saved;
    explicit RlArm(bool on) : saved(heurarm::t_arm[heurarm::BP_RESUME_LINE_HOLD])
    { heurarm::t_arm[heurarm::BP_RESUME_LINE_HOLD] = on ? 1 : 0; }
    ~RlArm() { heurarm::t_arm[heurarm::BP_RESUME_LINE_HOLD] = saved; }
};

// Island + Mountain; hand Sol Ring + Preordain; no land drop left. The plan casts both, so at Preordain's
// deferred re-solve (the node's partition point) every cast is paid and the true hold is ZERO.
GameState BoardRl()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    PutRl(s, "Island", 10);
    PutRl(s, "Mountain", 11);
    s.players[0].hand.push_back(CardRl("Sol Ring", 20));
    s.players[0].hand.push_back(CardRl("Preordain", 21));
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(CardRl("Island", 30 + k)); }
    return s;
}

bool SameCost(const ManaCost& a, const ManaCost& b)
{
    return a.generic == b.generic && a.white == b.white && a.blue == b.blue && a.black == b.black
        && a.red == b.red && a.green == b.green && a.colorless == b.colorless;
}

}   // namespace

TEST_CASE("BP_RESUME_LINE_HOLD: a resumed node child pays under the hold the prefix left, not the plan's whole bill")
{
    EnsureCardsRl();
    const GameState s = BoardRl();
    TurnSolver::Plan plan;
    plan.land_decided = true;
    plan.actions = { CastRl(s, "Sol Ring"), CastRl(s, "Preordain") };
    {
        RlArm arm(true);   // the fix (DEFAULT ON)
        ManaCost cap, res;
        REQUIRE(TurnSolver::BpResumeLineHoldForTest(s, plan, /*is_pre_combat=*/true, cap, res));
        CHECK(cap.ManaValue() == 0);      // both casts paid before Preordain's re-solve
        CHECK(SameCost(res, cap));        // the resume runs exactly where the full apply stood
    }
    {
        RlArm arm(false);  // =0: the phantom hold, kept for the A/B
        ManaCost cap, res;
        REQUIRE(TurnSolver::BpResumeLineHoldForTest(s, plan, /*is_pre_combat=*/true, cap, res));
        CHECK(cap.ManaValue() == 0);
        CHECK(res.generic == 1);          // Sol Ring's {1} and Preordain's {U} -- already paid -- held again
        CHECK(res.blue == 1);
        CHECK(res.ManaValue() == 2);
    }
}
