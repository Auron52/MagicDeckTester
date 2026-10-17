// NivMizzetCombo onboarding (2026-10-17): the two data-only cards.
//
//   Sulfur Falls -- "This land enters tapped unless you control an Island or a Mountain. {T}: Add {U}
//   or {R}." A check land: checkland_subtypes ["Island","Mountain"] read by the ONE shared predicate
//   LandWouldEnterTapped (the enumerator prices the drop off it, both apply worlds realise it). Only a
//   land carrying the TYPE counts -- the deck's Spirebluff Canal / Cascade Bluffs / Ferrous Lake have
//   none, so on the deck's real boards only an Island enables it.
//
//   Gitaxian Probe -- "{U/P}: Look at target player's hand. Draw a card." The Birthing Pod phyrexian
//   machinery, unchanged: a pay-2-life twin with a {0} cost, life-gated so it never pays to 0, and the
//   weak-dominance prune that drops the life twin when the mana bill is payable anyway.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "ai/AIEngine.h"
#include "ai/TurnSolver.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsSf()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefSf(const std::string& name)
{
    EnsureCardsSf();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void PutSf(GameState& s, const std::string& name, int number, bool tapped = false)
{
    Permanent p;
    p.card             = DefSf(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    p.tapped           = tapped;
    s.battlefield.push_back(p);
}

void HandSf(GameState& s, const std::string& name, int number)
{
    Card c = DefSf(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

GameState FreshSf(int life = 20)
{
    EnsureCardsSf();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.game_seed           = 4242;
    s.players[0].life     = life;
    s.players[1].life     = 20;
    for (int k = 0; k < 10; ++k)
    {
        // Ponders only: no blue source survives the casts under test, so a drawn card can never be
        // played on by the executor's draw-breakpoint re-solve (a drawn LAND would be dropped).
        Card c = DefSf("Ponder").card;
        c.m_number = 100 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

const Permanent* FindSf(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

void ResolveStackSf(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

GameState RunExecutorSf(const GameState& s, const TurnSolver::Plan& plan)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ResolveStackSf);
    return exec;
}

bool IsProbeCast(const Action& a)
{ return a.kind == Action::Kind::CastFromHand && a.card_name == "Gitaxian Probe"; }

// Plans whose only action is ONE Probe cast with the given life payment, and no land drop.
std::vector<const TurnSolver::Plan*> SoleProbePlans(const std::vector<TurnSolver::Plan>& plans, int life_paid)
{
    std::vector<const TurnSolver::Plan*> out;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() == 1 && IsProbeCast(p.actions[0]) && p.actions[0].phyrexian_life == life_paid
            && p.land_to_play.empty())
        { out.push_back(&p); }
    }
    return out;
}
std::vector<const TurnSolver::Plan*> SoleProbePlans(std::vector<TurnSolver::Plan>&&, int) = delete;

bool InGy(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].graveyard) { if (c.m_name.str() == name) { return true; } }
    return false;
}

}   // namespace

TEST_CASE("Sulfur Falls: enters untapped iff we control an Island or a Mountain (shared predicate)")
{
    const CardDefinition& sf = DefSf("Sulfur Falls");
    {
        GameState s = FreshSf();
        CHECK(LandWouldEnterTapped(s, sf));                      // no lands at all
    }
    {
        GameState s = FreshSf();
        PutSf(s, "Spirebluff Canal", 1);
        PutSf(s, "Cascade Bluffs", 2);
        PutSf(s, "Ferrous Lake", 3);
        CHECK(LandWouldEnterTapped(s, sf));                      // U/R duals carry no land type
    }
    {
        GameState s = FreshSf();
        PutSf(s, "Island", 1, /*tapped=*/true);
        CHECK_FALSE(LandWouldEnterTapped(s, sf));                // a TAPPED Island still counts
    }
    {
        GameState s = FreshSf();
        PutSf(s, "Mountain", 1);
        CHECK_FALSE(LandWouldEnterTapped(s, sf));
    }
    {
        // An opponent's Island does not count ("you control").
        GameState s = FreshSf();
        PutSf(s, "Island", 1);
        s.battlefield.back().controller_index = 1;
        s.battlefield.back().owner_index      = 1;
        CHECK(LandWouldEnterTapped(s, sf));
    }
    // Produces U or R.
    REQUIRE(sf.params.produces.size() == 2);
    CHECK(sf.params.produces[0] == Color::Blue);
    CHECK(sf.params.produces[1] == Color::Red);
}

TEST_CASE("Sulfur Falls: the real land drop (rollout and executor) realises the predicate")
{
    for (const bool with_island : { false, true })
    {
        GameState s = FreshSf();
        if (with_island) { PutSf(s, "Island", 1); }
        HandSf(s, "Sulfur Falls", 20);
        TurnSolver::Plan p;
        p.land_decided = true;
        p.land_to_play = "Sulfur Falls";
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, p, true);
        GameState exec = RunExecutorSf(s, p);
        for (const GameState* g : { &roll, &exec })
        {
            INFO("island=" << with_island << " world=" << std::string(g == &roll ? "rollout" : "executor"));
            const Permanent* f = FindSf(*g, 20);
            REQUIRE(f != nullptr);
            CHECK(f->tapped == !with_island);
        }
    }
}

TEST_CASE("Gitaxian Probe: {U/P} parses as one blue phyrexian pip (MV 1), a draw-1 sorcery")
{
    const CardDefinition& gp = DefSf("Gitaxian Probe");
    const ManaCost& mc = gp.card.m_mana_cost;
    CHECK(mc.ManaValue() == 1);
    CHECK(mc.blue == 1);
    CHECK(mc.phyrexian_count == 1);
    CHECK(static_cast<Color>(mc.phyrexian_color[0]) == Color::Blue);
    CHECK(gp.tmpl == CardTemplate::DrawSpell);
    CHECK(gp.params.draw == 1);
}

TEST_CASE("Gitaxian Probe: with no blue source the pay-2-life twin is the cast; both worlds draw one, lose 2")
{
    GameState s = FreshSf(20);
    PutSf(s, "Sandstone Needle", 1);   // no blue mana anywhere: only the life twin can cast it
    HandSf(s, "Gitaxian Probe", 20);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    CHECK(SoleProbePlans(plans, 0).empty());
    const std::vector<const TurnSolver::Plan*> life = SoleProbePlans(plans, 2);
    REQUIRE(life.size() == 1);
    CHECK(life[0]->actions[0].cost.ManaValue() == 0);   // the twin's bill is {0}
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *life[0], true);
    GameState exec = RunExecutorSf(s, *life[0]);
    for (const GameState* g : { &roll, &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout" : "executor"));
        CHECK(g->players[0].life == 18);
        CHECK(g->players[0].hand.size() == 1);                 // Probe gone, one card drawn
        CHECK(g->players[0].hand[0].m_number == 100);          // ...the top card
        CHECK(g->players[0].library.size() == 9);
        CHECK(InGy(*g, "Gitaxian Probe"));
        CHECK_FALSE(FindSf(*g, 1)->tapped);                    // nothing tapped for a {0} bill
    }
}

TEST_CASE("Gitaxian Probe: never pays life to 0 -- the twin needs life > 2")
{
    for (const int life : { 1, 2, 3 })
    {
        GameState s = FreshSf(life);
        HandSf(s, "Gitaxian Probe", 20);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        INFO("life=" << life);
        CHECK(SoleProbePlans(plans, 2).size() == (life > 2 ? 1u : 0u));
        if (life > 2)
        {
            GameState roll = s;
            TurnSolver::ApplyPlan(roll, *SoleProbePlans(plans, 2)[0], true);
            CHECK(roll.players[0].life == life - 2);
            CHECK(roll.players[0].life >= 1);
        }
    }
    // A committed life plan whose life moved under it is DROPPED, not paid to 0 (the apply re-gate).
    GameState s = FreshSf(3);
    HandSf(s, "Gitaxian Probe", 20);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    REQUIRE(SoleProbePlans(plans, 2).size() == 1);
    GameState low = s;
    low.players[0].life = 2;
    TurnSolver::ApplyPlan(low, *SoleProbePlans(plans, 2)[0], true);
    CHECK(low.players[0].life == 2);
    CHECK(low.players[0].hand.size() == 1);   // still holding the Probe
    CHECK_FALSE(InGy(low, "Gitaxian Probe"));
}

TEST_CASE("Gitaxian Probe: with an Island the mana cast is offered; the dominated life twin is pruned")
{
    GameState s = FreshSf(20);
    PutSf(s, "Island", 1);
    HandSf(s, "Gitaxian Probe", 20);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const std::vector<const TurnSolver::Plan*> mana = SoleProbePlans(plans, 0);
    REQUIRE(mana.size() == 1);
    // SubsetPhyrexianDominated: the Island pays {U} with nothing else to spend it on, so paying life
    // instead is weakly dominated (same cast, less life, a source left over that nothing uses).
    CHECK(SoleProbePlans(plans, 2).empty());
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *mana[0], true);
    GameState exec = RunExecutorSf(s, *mana[0]);
    for (const GameState* g : { &roll, &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout" : "executor"));
        CHECK(g->players[0].life == 20);
        CHECK(FindSf(*g, 1)->tapped);
        CHECK(g->players[0].hand.size() == 1);
        CHECK(InGy(*g, "Gitaxian Probe"));
    }
}
