// Maze's End F5 (2026-11-18; docs/design/analysis-Maze's End.md "F5"): a committed plan's TRAILING activation came up
// unpaid although one joint payment pays the whole line (MTG_LINE_PAY_AUDIT's joint=YES class). The plan's casts are
// paid first -- the whole-turn prepay, or the per-cast greedy on a single-cast turn -- and neither sees the
// activations' GENERIC mana. MTG_OWED_ACTS_JOINT_PAY (TurnSolver::OwedActsJointPrepay, DEFAULT ON) pays the line
// jointly when, and only when, the usual payment strands the owed activations and the joint solve pays. Both worlds.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameEngine.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{
void LoadF5Cards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& F5Def(const char* name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    return *d;
}

Card F5Card(const char* name, int number)
{
    Card c = F5Def(name).card;
    c.m_number = number;
    return c;
}

int F5Put(GameState& s, const char* name, int number, bool tapped = false)
{
    Permanent p;
    p.card = F5Card(name, number);
    p.controller_index = 0;
    p.owner_index = 0;
    p.tapped = tapped;
    s.battlefield.push_back(p);
    return number;
}

const Permanent* F5Find(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return &p; } }
    return nullptr;
}

bool F5InHand(const GameState& s, int num)
{
    for (const Card& c : s.players[0].hand) { if (c.m_number == num) { return true; } }
    return false;
}

struct F5Arm
{
    int slot;
    F5Arm(int sl, int v) : slot(sl) { heurarm::t_arm[sl] = static_cast<std::int8_t>(v); }
    ~F5Arm() { heurarm::t_arm[slot] = -1; }
};

GameState F5Base()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    s.game_seed           = 173041;
    return s;
}

void F5ResolveAll(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

// The d0 s173041 gi40 T5 board (the audit's worked joint=YES case): five Gates and Maze's End, Guild Summit in
// hand. {2}{U} + {3} = six mana from five sources -- only Baldur's Gate's fed mode ({2},{T}: X = 4 other Gates)
// makes it, and the Summit's usual payment taps Baldur's Gate for its plain {C}.
GameState Gi40Board(int& maze, int& summit)
{
    GameState s = F5Base();
    F5Put(s, "Simic Guildgate", 1);
    F5Put(s, "Cliffgate", 2);
    F5Put(s, "Citadel Gate", 3);
    F5Put(s, "Orzhov Guildgate", 4);
    F5Put(s, "Baldur's Gate", 5);
    maze = F5Put(s, "Maze's End", 6);
    summit = 10;
    s.players[0].hand.push_back(F5Card("Guild Summit", summit));
    s.players[0].library.push_back(F5Card("Azorius Guildgate", 20));
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(F5Card("Forest", 30 + k)); }
    s.players[0].library.push_back(F5Card("Azorius Guildgate", 21));
    return s;
}

// [Guild Summit (ETB count pinned to N = 0), activate Maze's End for Azorius Guildgate].
TurnSolver::Plan Gi40Plan(const GameState& s, int maze, int summit)
{
    TurnSolver::Plan p;
    p.land_decided = true;
    const CardDefinition& sd = F5Def("Guild Summit");
    Action cast;
    cast.kind       = Action::Kind::CastFromHand;
    cast.card_name  = sd.card.m_name;
    cast.def        = &sd;
    cast.cost       = sd.card.m_mana_cost;
    cast.card_mv    = sd.card.m_mana_cost.ManaValue();
    cast.chosen_x   = 1;   // the ETB choice axis: N + 1 -> tap no Gate
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i)
    { if (s.players[0].hand[static_cast<std::size_t>(i)].m_number == summit) { cast.hand_index = i; } }
    const Action* act = nullptr;
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
    for (const Action& a : acts)
    {
        if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::TapTutorPut
            && a.sac_source_id == maze && a.tutor_target.str() == "Azorius Guildgate") { act = &a; break; }
    }
    REQUIRE(act != nullptr);
    p.actions = { cast, *act };
    return p;
}

// OwedActsJointPrepay as both applies call it: under the plan's traits (ActLineHoldMask holds the activations' {T}
// sources) and its line hold.
bool JointPrepayInApplyScope(GameState& t, const std::vector<Action>& acts)
{
    PlanTraits pt;
    if (PlanTraitsWanted()) { pt = TurnSolver::ComputePlanTraits(t, acts); }
    PlanTraitsScope sc(PlanTraitsWanted() ? &pt : nullptr);
    LineUnpaidCostScope luc(LineCastCostTotal(acts));
    return TurnSolver::OwedActsJointPrepay(t, acts);
}

bool ActivatedMaze(const GameState& g, int maze) { return F5Find(g, maze) == nullptr && F5InHand(g, maze); }
}   // namespace

TEST_CASE("Maze's End F5 (gi40): the usual payment strands Maze's End's {3}; the joint payment pays the line")
{
    LoadF5Cards();
    int maze = 0, summit = 0;
    const GameState s = Gi40Board(maze, summit);
    const TurnSolver::Plan p = Gi40Plan(s, maze, summit);

    // Lever OFF: the Summit's usual payment leaves two sources for the {3} -- the activation is dropped, both worlds.
    {
        const F5Arm off(heurarm::OWED_ACTS_JOINT_PAY, 0);
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, p, /*is_pre_combat=*/true);
        CHECK(F5Find(roll, summit) != nullptr);
        CHECK_FALSE(ActivatedMaze(roll, maze));
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, p, true);
        GameState ex = s;
        eng.TakeTurn(ex, true, F5ResolveAll);
        CHECK(F5Find(ex, summit) != nullptr);
        CHECK_FALSE(ActivatedMaze(ex, maze));
    }
    // Lever ON (the default): the Summit is cast AND Maze's End is activated (back in hand, Azorius fetched).
    {
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, p, /*is_pre_combat=*/true);
        CHECK(F5Find(roll, summit) != nullptr);
        CHECK(ActivatedMaze(roll, maze));
        CHECK((F5Find(roll, 20) != nullptr || F5Find(roll, 21) != nullptr));
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, p, true);
        GameState ex = s;
        eng.TakeTurn(ex, true, F5ResolveAll);
        CHECK(F5Find(ex, summit) != nullptr);
        CHECK(ActivatedMaze(ex, maze));
        CHECK((F5Find(ex, 20) != nullptr || F5Find(ex, 21) != nullptr));
    }
}

// [Exploration, activate Maze's End] on Simic + Orzhov + Baldur's Gate up and three Guildgates already tapped (Baldur's
// Gate's X counts them). Exploration's {G} paid off Simic leaves two sources for the {3}; feeding Baldur's Gate with
// Simic + Orzhov (X = 5) pays both. With two more Guildgates up the usual payment strands nothing.
GameState ExplBoard(int& maze, bool spare)
{
    GameState s = F5Base();
    F5Put(s, "Simic Guildgate", 1);
    F5Put(s, "Orzhov Guildgate", 2);
    F5Put(s, "Baldur's Gate", 3);
    F5Put(s, "Golgari Guildgate", 4, true);
    F5Put(s, "Rakdos Guildgate", 5, true);
    F5Put(s, "Boros Guildgate", 7, true);
    maze = F5Put(s, "Maze's End", 6);
    if (spare) { F5Put(s, "Izzet Guildgate", 8); F5Put(s, "Dimir Guildgate", 9); }
    s.players[0].hand.push_back(F5Card("Exploration", 10));
    s.players[0].library.push_back(F5Card("Azorius Guildgate", 20));
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(F5Card("Forest", 30 + k)); }
    return s;
}
TurnSolver::Plan ExplPlan(const GameState& s, int maze)
{
    TurnSolver::Plan p;
    p.land_decided = true;
    const CardDefinition& ed = F5Def("Exploration");
    Action cast;
    cast.kind = Action::Kind::CastFromHand; cast.card_name = ed.card.m_name; cast.def = &ed;
    cast.cost = ed.card.m_mana_cost; cast.card_mv = 1; cast.hand_index = 0;
    const Action* act = nullptr;
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
    for (const Action& a : acts)
    {
        if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::TapTutorPut
            && a.sac_source_id == maze && a.tutor_target.str() == "Azorius Guildgate") { act = &a; break; }
    }
    REQUIRE(act != nullptr);
    p.actions = { cast, *act };
    return p;
}

TEST_CASE("Maze's End F5: a single cheap cast strands the activation; joint pays; no joint when nothing strands")
{
    LoadF5Cards();
    int maze = 0;
    {
        const GameState s = ExplBoard(maze, /*spare=*/false);
        const TurnSolver::Plan p = ExplPlan(s, maze);
        {
            const F5Arm off(heurarm::OWED_ACTS_JOINT_PAY, 0);
            GameState roll = s;
            TurnSolver::ApplyPlan(roll, p, /*is_pre_combat=*/true);
            CHECK_FALSE(ActivatedMaze(roll, maze));
        }
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, p, /*is_pre_combat=*/true);
        CHECK(ActivatedMaze(roll, maze));
        CHECK(F5Find(roll, 10) != nullptr);   // Exploration resolved too
    }
    {
        const GameState s = ExplBoard(maze, /*spare=*/true);
        const TurnSolver::Plan p = ExplPlan(s, maze);
        GameState t = s;
        CHECK_FALSE(JointPrepayInApplyScope(t, p.actions));
        CHECK(t.floating_mana.Total() == 0);
        for (std::size_t i = 0; i < s.battlefield.size(); ++i) { CHECK(t.battlefield[i].tapped == s.battlefield[i].tapped); }
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, p, /*is_pre_combat=*/true);
        CHECK(ActivatedMaze(roll, maze));
    }
}

TEST_CASE("Maze's End F5: no joint payment when the board cannot pay the whole line (nothing to rescue)")
{
    LoadF5Cards();
    int maze = 0, summit = 0;
    GameState s = Gi40Board(maze, summit);
    // Baldur's Gate tapped: five mana for six -- no payment of the line exists.
    for (Permanent& q : s.battlefield) { if (q.card.m_number == 5) { q.tapped = true; } }
    const TurnSolver::Plan p = Gi40Plan(s, maze, summit);
    GameState t = s;
    CHECK_FALSE(JointPrepayInApplyScope(t, p.actions));
    CHECK(t.floating_mana.Total() == 0);
}

TEST_CASE("Maze's End F5: Heap Gate's 'tap an untapped Gate' cost is set aside, so an unpayable line is not 'joint'")
{
    // d5 s171002 gi1 T3: Heap Gate, Gond Gate and Thran Portal-like Gates; [Burgeoning, Heap Gate's Treasure]. Heap
    // Gate's {T}, the other Gate's tap and {G} + {1} need four sources; three are there. The pre-F5 audit oracle did
    // not set the other Gate aside and called this joint-payable.
    LoadF5Cards();
    GameState s = F5Base();
    s.turn_number = 3;
    const int heap = F5Put(s, "Heap Gate", 1);
    F5Put(s, "Gond Gate", 2);
    F5Put(s, "Selesnya Guildgate", 3);
    s.players[0].hand.push_back(F5Card("Burgeoning", 10));
    const CardDefinition& bd = F5Def("Burgeoning");
    Action cast;
    cast.kind      = Action::Kind::CastFromHand;
    cast.card_name = bd.card.m_name;
    cast.def       = &bd;
    cast.cost      = bd.card.m_mana_cost;
    cast.card_mv   = 1;
    cast.hand_index = 0;
    const Action* act = nullptr;
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
    for (const Action& a : acts)
    {
        if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::TapTreasure
            && a.sac_source_id == heap) { act = &a; break; }
    }
    if (act == nullptr) { return; }   // the enumerator does not offer it here: nothing to pin
    GameState t = s;
    CHECK_FALSE(JointPrepayInApplyScope(t, { cast, *act }));
    CHECK(t.floating_mana.Total() == 0);
}

TEST_CASE("Maze's End F5 (d0 s173192 gi191): a TRAILING step strands a later one -- Heap Gate's {1} then Maze's End's {3}")
{
    // No cast to blame: the trailing pass pays Heap Gate's Treasure ({1}, {T}, tap another Gate) with a plain tap,
    // and Maze's End's {3} is one short. Feeding Baldur's Gate (X = 4 other Gates) pays both. Both worlds.
    LoadF5Cards();
    GameState s = F5Base();
    s.turn_number = 4;
    const int heap = F5Put(s, "Heap Gate", 1);
    F5Put(s, "Manor Gate", 2);
    F5Put(s, "Manor Gate", 3);
    F5Put(s, "Azorius Guildgate", 4);
    F5Put(s, "Baldur's Gate", 5);
    const int maze = F5Put(s, "Maze's End", 6);
    s.players[0].library.push_back(F5Card("Simic Guildgate", 20));
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(F5Card("Forest", 30 + k)); }
    s.players[0].library.push_back(F5Card("Simic Guildgate", 21));
    const Action* h = nullptr;
    const Action* m = nullptr;
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
    for (const Action& a : acts)
    {
        if (a.kind != Action::Kind::ActivatePermAbility) { continue; }
        if (a.ability_mode == Action::AbilityMode::TapTreasure && a.sac_source_id == heap && h == nullptr) { h = &a; }
        if (a.ability_mode == Action::AbilityMode::TapTutorPut && a.sac_source_id == maze
            && a.tutor_target.str() == "Simic Guildgate" && m == nullptr) { m = &a; }
    }
    REQUIRE(h != nullptr);
    REQUIRE(m != nullptr);
    TurnSolver::Plan p;
    p.land_decided = true;
    p.actions = { *h, *m };
    {
        const F5Arm off(heurarm::OWED_ACTS_JOINT_PAY, 0);
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, p, /*is_pre_combat=*/true);
        CHECK_FALSE(ActivatedMaze(roll, maze));
    }
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, p, /*is_pre_combat=*/true);
    CHECK(ActivatedMaze(roll, maze));
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, p, true);
    GameState ex = s;
    eng.TakeTurn(ex, true, F5ResolveAll);
    CHECK(ActivatedMaze(ex, maze));
}

// ---- The audit's oracle (MTG_LINE_PAY_AUDIT, LineJointPayOracle) -- the false-positive classes F5 found ---------
TEST_CASE("Maze's End F5 audit oracle: the gi40 line IS jointly payable (the real DEFECT class)")
{
    LoadF5Cards();
    int maze = 0, summit = 0;
    const GameState s = Gi40Board(maze, summit);
    const TurnSolver::Plan p = Gi40Plan(s, maze, summit);
    CHECK(LineJointPayOracleForTest(s, p.actions, nullptr) == 1);
}

TEST_CASE("Maze's End F5 audit oracle: Gates the line taps for Guild Summit's draws are not supply")
{
    // The same board with Guild Summit's ETB count tapping Cliffgate and Citadel Gate (the frame's
    // nonmana_tap_nums): {2}{U} + {3} from Simic, Orzhov and Baldur's Gate cannot be paid -- joint=no, where the
    // pre-F5 oracle (which ignored the ETB taps) said YES.
    LoadF5Cards();
    int maze = 0, summit = 0;
    const GameState s = Gi40Board(maze, summit);
    const TurnSolver::Plan p = Gi40Plan(s, maze, summit);
    const std::vector<int> etb_tapped = { 2, 3 };
    CHECK(LineJointPayOracleForTest(s, p.actions, &etb_tapped) == 0);
}

TEST_CASE("Maze's End F5 audit oracle: Heap Gate's other-Gate tap is set aside")
{
    // d5 s171002 gi1 T3 (old oracle: joint=YES): Heap Gate, Gond Gate, Selesnya Guildgate; [Burgeoning, Heap Gate's
    // Treasure]. With one Gate tapped for the Treasure's cost only one source is left for {G} + {1}.
    LoadF5Cards();
    GameState s = F5Base();
    s.turn_number = 3;
    const int heap = F5Put(s, "Heap Gate", 1);
    F5Put(s, "Gond Gate", 2);
    F5Put(s, "Selesnya Guildgate", 3);
    s.players[0].hand.push_back(F5Card("Burgeoning", 10));
    const CardDefinition& bd = F5Def("Burgeoning");
    Action cast;
    cast.kind = Action::Kind::CastFromHand; cast.card_name = bd.card.m_name; cast.def = &bd;
    cast.cost = bd.card.m_mana_cost; cast.card_mv = 1; cast.hand_index = 0;
    const CardDefinition& hd = F5Def("Heap Gate");
    Action act;
    act.kind          = Action::Kind::ActivatePermAbility;
    act.ability_mode  = Action::AbilityMode::TapTreasure;
    act.card_name     = hd.card.m_name;
    act.def           = &hd;
    act.sac_source_id = heap;
    act.cost.generic  = 1;
    CHECK(LineJointPayOracleForTest(s, { cast, act }, nullptr) == 0);
    // ...and with a fourth source it is payable (the other Gate set aside, two left for {G} + {1}).
    F5Put(s, "Gruul Guildgate", 4);
    CHECK(LineJointPayOracleForTest(s, { cast, act }, nullptr) == 1);
}
