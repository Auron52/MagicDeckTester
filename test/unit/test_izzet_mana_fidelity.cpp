// Izzet mana-colour fidelity (NivMizzetCombo onboarding, 2026-10-17).
//
// Izzet Signet / Ferrous Lake ("{1}, {T}: Add {U}{R}") are RAMP FILTERS: they eat one mana and make one
// {U} AND one {R}. Niv-Mizzet, Parun costs {U}{U}{U}{R}{R}{R}, so the colour split is load-bearing. The
// flat affordability pool credits a fed filter as "+1 wild" (the feeder kept in its own colour), which
//   * UNDER-credits a COLOURLESS feeder (Sol Ring's {C}, a Generator Servant's floated {C}): the real
//     tap turns that {C} into a {U} and an {R}; the flat pool keeps the {C} (no coloured pip) + 1 wild;
//   * OVER-credits a one-colour feeder base (Island + Island + Signet "pays" {U}{U}{U}; the real
//     output is {U}{U}{R}).
// These pin, with the real payer as the oracle:
//   1. the under-credit is RESCUED: the enumerator offers the {U}{U}{U}{R}{R}{R} cast off Sol Ring ->
//      Signet, and off a cracked Servant -> Signet, and both worlds realise it;
//   2. the over-credit is REJECTED at enumeration (BuildColorFeasibility charges the filter's feed --
//      a necessary condition, so no payable subset is lost), and a forced unpayable cast leaves no
//      partial taps behind.
// The two Niv-Mizzets are not in cards.json on this branch (another worktree implements them), so the
// costs are carried by two TEST-ONLY vanilla creatures loaded from a fixture written at run time.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "ai/AIEngine.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "mtg_test_seam.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

constexpr const char* kSix   = "Test Fixture Izzet Six";    // {U}{U}{U}{R}{R}{R} -- Parun's bill
constexpr const char* kFire  = "Test Fixture Izzet Mind";   // {2}{U}{U}{R}{R}   -- Firemind's bill

void EnsureCardsMf()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        const std::filesystem::path fx =
            std::filesystem::temp_directory_path() / "mtg_test_izzet_mana_fidelity_fixture.json";
        {
            std::ofstream o(fx);
            o << R"({"cards": [
 {"name": "Test Fixture Izzet Six", "mana_cost": "{U}{U}{U}{R}{R}{R}", "types": ["Creature"],
  "subtypes": [], "keywords": [], "power": 5, "toughness": 5, "oracle_text": "TEST FIXTURE",
  "template": "vanilla_creature", "parameters": {}},
 {"name": "Test Fixture Izzet Mind", "mana_cost": "{2}{U}{U}{R}{R}", "types": ["Creature"],
  "subtypes": [], "keywords": [], "power": 4, "toughness": 4, "oracle_text": "TEST FIXTURE",
  "template": "vanilla_creature", "parameters": {}}
]})";
        }
        CardDatabase::Instance().LoadFromJson(fx);
        std::error_code ec;
        std::filesystem::remove(fx, ec);
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefMf(const std::string& name)
{
    EnsureCardsMf();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in the DB: ", name);
    return *d;
}

void PutMf(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefMf(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

void HandMf(GameState& s, const std::string& name, int number)
{
    Card c = DefMf(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

GameState FreshMf()
{
    EnsureCardsMf();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.game_seed           = 7373;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;   // no land drop: the board is the whole supply
    for (int k = 0; k < 8; ++k)
    {
        Card c = DefMf("Lightning Bolt").card;   // uncastable filler (no red left after the casts)
        c.m_number = 300 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

const Permanent* FindMf(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

void ResolveStackMf(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

GameState RunExecutorMf(const GameState& s, const TurnSolver::Plan& plan)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ResolveStackMf);
    return exec;
}

int CastsOf(const TurnSolver::Plan& p, const std::string& name)
{
    int n = 0;
    for (const Action& a : p.actions) { if (a.kind == Action::Kind::CastFromHand && a.card_name == name) { ++n; } }
    return n;
}
int AllCasts(const TurnSolver::Plan& p)
{
    int n = 0;
    for (const Action& a : p.actions) { if (a.kind == Action::Kind::CastFromHand) { ++n; } }
    return n;
}

const TurnSolver::Plan* PlanCasting(const std::vector<TurnSolver::Plan>& plans, const std::string& name)
{
    for (const TurnSolver::Plan& p : plans) { if (CastsOf(p, name) == 1) { return &p; } }
    return nullptr;
}
const TurnSolver::Plan* PlanCasting(std::vector<TurnSolver::Plan>&&, const std::string&) = delete;

std::string WorldMf(const GameState* g, const GameState* roll)
{ return std::string(g == roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"); }

}   // namespace

TEST_CASE("Izzet fidelity: Sol Ring feeding Izzet Signet pays {U}{U}{U}{R}{R}{R} -- payer, enumerator, both worlds")
{
    // Real supply: Sol Ring {C}{C} -> one {C} feeds the Signet ({U}{R}); Islands {U}{U}; Mountains
    // {R}{R} => {U}x3 {R}x3 + a spare {C}. The flat pool sees {U}2 {R}2 {C}2 + 1 wild: short.
    GameState s = FreshMf();
    PutMf(s, "Sol Ring", 1);
    PutMf(s, "Izzet Signet", 2);
    PutMf(s, "Island", 3);
    PutMf(s, "Island", 4);
    PutMf(s, "Mountain", 5);
    PutMf(s, "Mountain", 6);
    {
        GameState pay = s;
        ManaCost six = DefMf(kSix).card.m_mana_cost;
        CHECK(TapForCostShared(pay, six, /*for_creature=*/true, nullptr, true));
    }
    HandMf(s, kSix, 20);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = PlanCasting(plans, kSix);
    REQUIRE_MESSAGE(p != nullptr, "the enumerator never offered the {U}{U}{U}{R}{R}{R} cast");
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *p, true);
    GameState exec = RunExecutorMf(s, *p);
    for (const GameState* g : { &roll, &exec })
    {
        INFO("world: " << WorldMf(g, &roll));
        CHECK(FindMf(*g, 20) != nullptr);
    }
}

TEST_CASE("Izzet fidelity: a cracked Generator Servant's {C} feeding the Signet pays Parun's and Firemind's bills")
{
    // Parun's bill: Servant {C}{C} -> one {C} feeds the Signet; Islands {U}{U}; Mountains {R}{R}.
    {
        GameState s = FreshMf();
        PutMf(s, "Generator Servant", 1);
        PutMf(s, "Izzet Signet", 2);
        PutMf(s, "Island", 3);
        PutMf(s, "Island", 4);
        PutMf(s, "Mountain", 5);
        PutMf(s, "Mountain", 6);
        HandMf(s, kSix, 20);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        const TurnSolver::Plan* p = PlanCasting(plans, kSix);
        REQUIRE_MESSAGE(p != nullptr, "the enumerator never offered Servant -> Signet -> {U}{U}{U}{R}{R}{R}");
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, *p, true);
        GameState exec = RunExecutorMf(s, *p);
        for (const GameState* g : { &roll, &exec })
        {
            INFO("world: " << WorldMf(g, &roll));
            const Permanent* six = FindMf(*g, 20);
            REQUIRE(six != nullptr);
            // No {C} can be spent on {U}{U}{U}{R}{R}{R}: the Servant's mana went into the Signet's
            // ability, so the creature gains NO haste.
            CHECK_FALSE(six->temp_haste);
        }
    }
    // Firemind's bill: Servant {C}{C}: one {C} pays a {2} pip (-> HASTE), one feeds the Signet ({U}{R});
    // Island {U}; Mountains {R}{R}.
    {
        GameState s = FreshMf();
        PutMf(s, "Generator Servant", 1);
        PutMf(s, "Izzet Signet", 2);
        PutMf(s, "Island", 3);
        PutMf(s, "Mountain", 5);
        PutMf(s, "Mountain", 6);
        HandMf(s, kFire, 20);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        const TurnSolver::Plan* p = PlanCasting(plans, kFire);
        REQUIRE_MESSAGE(p != nullptr, "the enumerator never offered Servant -> Signet -> {2}{U}{U}{R}{R}");
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, *p, true);
        GameState exec = RunExecutorMf(s, *p);
        for (const GameState* g : { &roll, &exec })
        {
            INFO("world: " << WorldMf(g, &roll));
            const Permanent* m = FindMf(*g, 20);
            REQUIRE(m != nullptr);
            CHECK(m->temp_haste);   // a tagged {C} paid a generic pip of the creature spell
            CHECK(CanTapNow(*m, g->battlefield));
        }
    }
}

TEST_CASE("Izzet fidelity: Island + Island + Signet cannot pay three blue -- not offered, and a forced try leaves no taps")
{
    // Real supply: Island -> Signet = {U}{R}, the other Island {U}: two blue at most.
    GameState s = FreshMf();
    PutMf(s, "Izzet Signet", 2);
    PutMf(s, "Island", 3);
    PutMf(s, "Island", 4);
    HandMf(s, "Ponder", 20);
    HandMf(s, "Preordain", 21);
    HandMf(s, "Brainstorm", 22);
    {
        GameState pay = s;
        ManaCost uuu; uuu.blue = 3;
        CHECK_FALSE(TapForCostShared(pay, uuu, /*for_creature=*/false, nullptr, true));
        // A failed payment is atomic: nothing tapped, nothing floating.
        for (const Permanent& q : pay.battlefield) { CHECK_FALSE(q.tapped); }
        CHECK(pay.floating_mana.Total() == 0);
        ManaCost uur; uur.blue = 2; uur.red = 1;
        CHECK(TapForCostShared(pay, uur, /*for_creature=*/false, nullptr, true));   // the real ceiling
    }
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    int three = 0, two = 0;
    for (const TurnSolver::Plan& p : plans)
    {
        if (!p.land_to_play.empty()) { continue; }
        if (AllCasts(p) == 3) { ++three; }
        if (AllCasts(p) == 2) { ++two; }
    }
    CHECK(three == 0);    // the over-credited three-blue subset is not offered
    CHECK(two > 0);       // ...and every payable pair still is
}

TEST_CASE("Izzet fidelity: the feed-charged colour test never rejects a payable chain (Signet + Ferrous Lake)")
{
    // Island -> Signet ({U}{R}); the Signet's {R} -> Ferrous Lake ({U}{R}): {U}{U}{R} from one Island.
    GameState s = FreshMf();
    PutMf(s, "Izzet Signet", 2);
    PutMf(s, "Ferrous Lake", 3);
    PutMf(s, "Island", 4);
    {
        GameState pay = s;
        ManaCost uur; uur.blue = 2; uur.red = 1;
        CHECK(TapForCostShared(pay, uur, /*for_creature=*/false, nullptr, true));
    }
    HandMf(s, "Ponder", 20);
    HandMf(s, "Preordain", 21);
    HandMf(s, "Goblin Guide", 22);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    bool all_three = false;
    for (const TurnSolver::Plan& p : plans)
    { if (p.land_to_play.empty() && AllCasts(p) == 3) { all_three = true; } }
    CHECK(all_three);     // {U} + {U} + {R}: exactly the chain's output
}
