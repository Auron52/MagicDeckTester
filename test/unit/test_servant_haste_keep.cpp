// Generator Servant's haste goes to the line's creature (MTG_SERVANT_HASTE_KEEP, DEFAULT ON).
//
// "{T}, Sacrifice this creature: Add {C}{C}. If any of that mana is spent on a creature spell, it gains haste."
// The payer drains floating mana before it taps, so a NONCREATURE cast ahead of the creature in the same line
// spent the tagged {C}{C}: four Islands, Spirebluff Canal and an unsick Servant, hand Izzet Signet + Niv-Mizzet,
// the Firemind -- "crack the Servant, Signet off two Islands, hasty Firemind" -- cast the Signet off the {C}{C}
// and the Firemind arrived without haste, in the search, the executor and the viewer alike (NivMizzetCombo
// cast-order draft, probe p5; the USER hit it saving references). With the lever on, the Signet's payment keeps
// the tagged units for the Firemind when the rest of the line still pays. These pin: haste in BOTH worlds with
// the lever on, none with it off, and the plain payment kept when there is no later creature.
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

void EnsureCardsHk()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefHk(const std::string& name)
{
    EnsureCardsHk();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void PutHk(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = DefHk(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

void HandHk(GameState& s, const std::string& name, int number)
{
    Card c = DefHk(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

// The probe-p5 board. `salt` keeps the arms' boards distinct for every cache.
GameState P5Board(int salt, bool firemind)
{
    EnsureCardsHk();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5 + salt;
    s.game_seed           = 5151 + salt;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 8; ++k)
    {
        Card c = DefHk("Lightning Bolt").card;
        c.m_number = 200 + k;
        s.players[0].library.push_back(c);
    }
    for (int k = 0; k < 4; ++k) { PutHk(s, "Island", 1 + k); }
    PutHk(s, "Spirebluff Canal", 5);
    PutHk(s, "Generator Servant", 10);
    HandHk(s, "Izzet Signet", 20);
    if (firemind) { HandHk(s, "Niv-Mizzet, the Firemind", 30); }
    return s;
}

void ResolveStackHk(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

GameState RunExecutorHk(const GameState& s, const TurnSolver::Plan& plan)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ResolveStackHk);
    return exec;
}

const Permanent* FindHk(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

// The plan that cracks the Servant and casts exactly `casts`, no land.
const TurnSolver::Plan* CrackPlan(const std::vector<TurnSolver::Plan>& plans, const std::vector<std::string>& casts)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (!p.land_to_play.empty()) { continue; }
        bool crack = false;
        int n = 0;
        std::vector<std::string> names;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::SacForMana && a.sac_source_id == 10) { crack = true; }
            if (a.kind == Action::Kind::CastFromHand) { ++n; names.push_back(a.card_name.str()); }
        }
        if (!crack || n != static_cast<int>(casts.size())) { continue; }
        bool all = true;
        for (const std::string& c : casts)
        {
            bool f = false;
            for (const std::string& m : names) { if (m == c) { f = true; } }
            if (!f) { all = false; }
        }
        if (all) { return &p; }
    }
    return nullptr;
}

struct ArmHk
{
    heurarm::Slot slot;
    ArmHk(heurarm::Slot s, bool on) : slot(s) { heurarm::t_arm[s] = on ? 1 : 0; }
    ~ArmHk() { heurarm::t_arm[slot] = -1; }
};

}   // namespace

TEST_CASE("Servant haste keep: the Signet leaves the tagged {C}{C} for the Firemind, both worlds")
{
    for (const bool on : { false, true })
    {
        ArmHk arm(heurarm::SERVANT_HASTE_KEEP, on);
        INFO("MTG_SERVANT_HASTE_KEEP=" << on);
        const GameState s = P5Board(on ? 1 : 0, /*firemind=*/true);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        const TurnSolver::Plan* p = CrackPlan(plans, { "Izzet Signet", "Niv-Mizzet, the Firemind" });
        REQUIRE(p != nullptr);
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, *p, true);
        GameState exec = RunExecutorHk(s, *p);
        for (const GameState* g : { &roll, &exec })
        {
            INFO("world: " << (g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
            REQUIRE(FindHk(*g, 20) != nullptr);   // the Signet resolved
            const Permanent* niv = FindHk(*g, 30);
            REQUIRE(niv != nullptr);              // so did the Firemind
            CHECK(niv->temp_haste == on);         // lever off: the Signet spent the haste
        }
    }
}

TEST_CASE("Servant haste keep: with no creature later in the line the Signet still takes the {C}{C}")
{
    ArmHk arm(heurarm::SERVANT_HASTE_KEEP, true);
    const GameState s = P5Board(2, /*firemind=*/false);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = CrackPlan(plans, { "Izzet Signet" });
    REQUIRE(p != nullptr);
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *p, true);
    REQUIRE(FindHk(roll, 20) != nullptr);
    CHECK(roll.floating_haste_c == 0);   // nothing to keep it for: the drain-first payment stands
    int untapped_islands = 0;
    for (int k = 1; k <= 4; ++k) { if (!FindHk(roll, k)->tapped) { ++untapped_islands; } }
    CHECK(untapped_islands == 4);
}
