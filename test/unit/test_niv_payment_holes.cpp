// Mana-payment completeness holes found by the NivMizzetCombo claude-play sweep (2026-10-18) and
// reproduced by adversarial verifiers. Each made a LEGAL line unreachable at any budget:
//
//   1. MTG_PREPAY_FLOAT_POOL -- a floated sac-for-mana disabled the whole-turn prepay. Both apply worlds
//      crack a plan's SacForMana in their pre-pass, BEFORE BatchPrepayMainCasts, and the prepay declined
//      on any float (PP_FLOAT_NZ), so the casts fell to the per-cast greedy. s71009 T4: with Generator
//      Servant's {C}{C} floating, Niv-Mizzet, the Firemind took Spirebluff Canal's {R} and the Bluffs'
//      {R}{R}, and Curiosity's {U} had no source. The joint solve now starts from the float.
//
// Every case runs BOTH worlds -- the rollout (TurnSolver::ApplyPlan) and the executor (AIEngine::TakeTurn
// through the committed-plan seam) -- and pins the lever-OFF arm to the hole it closes (a heurarm override,
// so the test never depends on the environment).
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "ai/AIEngine.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsNp()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefNp(const std::string& name)
{
    EnsureCardsNp();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Permanent& PutNp(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card              = DefNp(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = false;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

void HandNp(GameState& s, const std::string& name, int number)
{
    Card c = DefNp(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

GameState FreshNp(int turn)
{
    EnsureCardsNp();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.game_seed           = 7100;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;     // the turn's land is already down (on the board below)
    for (int k = 0; k < 8; ++k)
    {
        Card c = DefNp("Lightning Bolt").card;   // inert filler: nothing drawn mid-test is castable here
        c.m_number = 200 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

const Permanent* FindNp(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

bool InHandNp(const GameState& s, int number)
{
    for (const Card& c : s.players[0].hand) { if (c.m_number == number) { return true; } }
    return false;
}

void ResolveStackNp(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

GameState RunExecutorNp(const GameState& s, const TurnSolver::Plan& plan)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ResolveStackNp);
    return exec;
}

int CastCountNp(const TurnSolver::Plan& p)
{
    int n = 0;
    for (const Action& a : p.actions) { if (a.kind == Action::Kind::CastFromHand) { ++n; } }
    return n;
}

// The first enumerated plan that plays no land, cracks `sac_src` (0: cracks nothing) and casts exactly
// `casts`, in that VECTOR order when `ordered`. Points INTO `plans`.
const TurnSolver::Plan* FindPlanNp(const std::vector<TurnSolver::Plan>& plans, int sac_src,
                                   const std::vector<std::string>& casts, bool ordered)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (!p.land_to_play.empty()) { continue; }
        bool sac = false, other_sac = false;
        for (const Action& a : p.actions)
        {
            if (a.kind != Action::Kind::SacForMana) { continue; }
            (a.sac_source_id == sac_src ? sac : other_sac) = true;
        }
        if (other_sac || sac != (sac_src != 0)) { continue; }
        if (CastCountNp(p) != static_cast<int>(casts.size())) { continue; }
        std::vector<std::string> seen;
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::CastFromHand) { seen.push_back(a.card_name.str()); } }
        bool match = true;
        if (ordered) { match = seen == casts; }
        else
        {
            for (const std::string& n : casts)
            {
                bool f = false;
                for (const std::string& x : seen) { if (x == n) { f = true; break; } }
                if (!f) { match = false; break; }
            }
        }
        if (match) { return &p; }
    }
    return nullptr;
}
const TurnSolver::Plan* FindPlanNp(std::vector<TurnSolver::Plan>&&, int, const std::vector<std::string>&, bool) = delete;

std::string WorldNp(const GameState* g, const GameState* roll)
{ return std::string(g == roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"); }

// Force one heurarm lever for the scope (the batch runner's per-job override), restoring the default.
struct ArmNp
{
    ArmNp(heurarm::Slot s, bool on) { heurarm::t_arm[static_cast<int>(s)] = on ? 1 : 0; }
    ~ArmNp() { heurarm::Clear(); }
};

// ---- hole 1: s71009 T4 ----------------------------------------------------------------------------------
// Spirebluff Canal (U/R), Island, Saprazzan Skerry (two counters, {U}{U}), Sol Ring, this turn's Cascade
// Bluffs, and an untapped Generator Servant; hand Niv-Mizzet, the Firemind ({2}{U}{U}{R}{R}) and Curiosity
// ({U}). Legal: the Servant's {C}{C} pay Niv's {2} (one tagged -> haste), Skerry {U}{U}, the Bluffs fed by
// the Island {R}{R}; Curiosity takes the Canal's {U}. Sol Ring stays up.
GameState Board71009()
{
    GameState s = FreshNp(4);
    PutNp(s, "Spirebluff Canal", 54);
    PutNp(s, "Island", 18);
    Permanent& sk = PutNp(s, "Saprazzan Skerry", 49);
    sk.counters.Add(Counter::Type::Depletion, 2);
    PutNp(s, "Sol Ring", 51);
    PutNp(s, "Cascade Bluffs", 5);
    PutNp(s, "Generator Servant", 12);
    HandNp(s, "Niv-Mizzet, the Firemind", 33);
    HandNp(s, "Curiosity", 8);
    return s;
}

}   // namespace

TEST_CASE("Niv payment hole 1: a floated Generator Servant no longer disables the joint prepay (s71009 T4), both worlds")
{
    const GameState s = Board71009();
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = FindPlanNp(plans, 12, { "Niv-Mizzet, the Firemind", "Curiosity" }, false);
    REQUIRE(p != nullptr);

    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *p, true);
    const GameState exec = RunExecutorNp(s, *p);
    for (const GameState* g : { static_cast<const GameState*>(&roll), &exec })
    {
        INFO("world: " << WorldNp(g, &roll));
        const Permanent* niv = FindNp(*g, 33);
        REQUIRE(niv != nullptr);                     // Niv resolved...
        CHECK(niv->temp_haste);                      // ...hasty: a tagged {C} paid its {2}
        const Permanent* cur = FindNp(*g, 8);
        REQUIRE(cur != nullptr);                     // ...and Curiosity resolved -- the line the decline dropped
        CHECK(cur->aura_attached_to == 33);
        CHECK_FALSE(InHandNp(*g, 8));
        CHECK(FindNp(*g, 12) == nullptr);            // the Servant was sacrificed for the {C}{C}
    }

    // Lever OFF: the PP_FLOAT_NZ decline, and the per-cast greedy strands Curiosity's {U} (the hole).
    {
        ArmNp off(heurarm::PREPAY_FLOAT_POOL, false);
        GameState r0 = s;
        TurnSolver::ApplyPlan(r0, *p, true);
        const GameState e0 = RunExecutorNp(s, *p);
        for (const GameState* g : { static_cast<const GameState*>(&r0), &e0 })
        {
            INFO("lever OFF, world: " << WorldNp(g, &r0));
            CHECK(FindNp(*g, 33) != nullptr);
            CHECK(FindNp(*g, 8) == nullptr);
            CHECK(InHandNp(*g, 8));
        }
    }
}

TEST_CASE("Niv payment hole 1: the counted float keeps Generator Servant's haste tag as {C} in the pre-load")
{
    // Prepay directly: the pre-load must leave the tagged {C} AS {C} (not laundered into the generic
    // `wild`), so TakeHasteFloatForCreatureSpell still finds a tagged unit when Niv is cast.
    GameState s = Board71009();
    ApplySacForMana(s, 0, 12, "C", 2);
    REQUIRE(HasteFloatUnits(s) == 2);
    std::vector<Action> acts;
    for (const char* nm : { "Niv-Mizzet, the Firemind", "Curiosity" })
    {
        Action a;
        a.kind      = Action::Kind::CastFromHand;
        a.card_name = nm;
        a.def       = &DefNp(nm);
        a.cost      = DefNp(nm).card.m_mana_cost;
        acts.push_back(a);
    }
    REQUIRE(TurnSolver::BatchPrepayMainCasts(s, acts));
    CHECK(s.floating_mana.Total() >= 7);             // {2}{U}{U}{R}{R} + {U}, nothing lost
    CHECK(s.floating_mana.blue >= 3);
    CHECK(s.floating_mana.red  >= 2);
    CHECK(HasteFloatUnits(s) >= 1);                  // the creature spell can still take one
    ManaCost niv = DefNp("Niv-Mizzet, the Firemind").card.m_mana_cost;
    CHECK(TakeHasteFloatForCreatureSpell(s, niv));
}
