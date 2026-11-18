// Maze's End F1 (2026-11-18): the four structural defects the Stage 5d claude-play sweep (seed 61001) found.
//   gi6 -- a draw trigger resolved INSIDE a search-put ability, before its "then shuffle" (CR 603.3).
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/LandPlay.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameEngine.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/LandSearch.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
void LoadF1Cards()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& FDef(const char* name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    return *d;
}

Card FCard(const char* name, int number)
{
    Card c = FDef(name).card;
    c.m_number = number;
    return c;
}

int FPut(GameState& s, const char* name, int number, bool tapped = false)
{
    Permanent p;
    p.card = FCard(name, number);
    p.controller_index = 0;
    p.owner_index = 0;
    p.tapped = tapped;
    s.battlefield.push_back(p);
    return number;
}

GameState FBase()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    s.game_seed           = 61007;
    return s;
}

const Permanent* FFind(const GameState& s, int num)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == num) { return &p; } }
    return nullptr;
}

bool FInHand(const GameState& s, int num)
{
    for (const Card& c : s.players[0].hand) { if (c.m_number == num) { return true; } }
    return false;
}

// The card a draw would take AFTER `taken` leaves the library and the library is shuffled by the next
// search shuffle -- computed on a copy, through the same ShuffleAfterSearch the resolver uses.
int TopAfterTakeAndShuffle(const GameState& s, int taken)
{
    GameState ref = s;
    Library& lib = ref.players[0].library;
    std::vector<Card> keep;
    while (!lib.empty())
    {
        Card c = lib.DrawTop();
        if (c.m_number != taken) { keep.push_back(c); }
    }
    for (const Card& c : keep) { lib.push_back(c); }
    ShuffleAfterSearch(ref, 0);
    return ref.players[0].library.front().m_number;
}
}   // namespace

TEST_CASE("Maze's End F1 (gi6): Guild Summit's Gate draw waits for Maze's End's \"then shuffle\" (CR 603.3)")
{
    LoadF1Cards();
    GameState s = FBase();
    FPut(s, "Guild Summit", 600);
    FPut(s, "Forest", 603);
    FPut(s, "Forest", 604);
    const int maze = FPut(s, "Maze's End", 605);
    for (int k = 0; k < 14; ++k) { s.players[0].library.push_back(FCard("Forest", 900 + k)); }
    s.players[0].library.push_back(FCard("Boros Guildgate", 606));
    const int pre_shuffle_top = s.players[0].library.front().m_number;
    const int expected = TopAfterTakeAndShuffle(s, 606);
    REQUIRE(expected != pre_shuffle_top);   // the seed must make the two orders distinguishable
    const std::size_t hand0 = s.players[0].hand.size();
    const std::string target = "Boros Guildgate";
    {
        const SacNamedPinScope pin(0, &target);
        REQUIRE(PermAbilitySourceLive(s, 0, maze, PermAbilityMode::TapTutorPut, nullptr));
        ApplyPermAbility(s, 0, maze, PermAbilityMode::TapTutorPut);
    }
    REQUIRE(FFind(s, 606) != nullptr);
    CHECK(s.players[0].hand.size() == hand0 + 2);   // Maze's End back + ONE Summit draw
    CHECK(FInHand(s, expected));                    // drawn from the SHUFFLED library
    CHECK_FALSE(FInHand(s, pre_shuffle_top));       // never the known pre-shuffle top
    CHECK(g_triggered_draw_deferral == nullptr);    // the scope closed
}

TEST_CASE("Maze's End F1 (gi6): a land drop's Guild Summit draw is still immediate (no search, no scope)")
{
    LoadF1Cards();
    GameState s = FBase();
    s.players[0].lands_played_this_turn = 0;
    FPut(s, "Guild Summit", 600);
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(FCard("Forest", 900 + k)); }
    const int top = s.players[0].library.front().m_number;
    s.players[0].hand.push_back(FCard("Azorius Guildgate", 601));
    REQUIRE(PlayLandFromHand(s, 0, FDef("Azorius Guildgate"), LandPlayOptions{}));
    CHECK(FInHand(s, top));
}

TEST_CASE("Maze's End F1 (gi6): nested search-put scopes on one state defer to the OUTERMOST; a clone draws now")
{
    LoadF1Cards();
    GameState s = FBase();
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(FCard("Forest", 900 + k)); }
    GameState clone = s;
    {
        SearchPutTriggerScope outer(s);
        {
            SearchPutTriggerScope inner(s);
            TriggeredDraw(s, 0, 1, false);
        }
        CHECK(s.players[0].hand.empty());          // the inner scope did not flush
        TriggeredDraw(clone, 0, 1, false);          // another state: not deferred
        CHECK(clone.players[0].hand.size() == 1);
    }
    CHECK(s.players[0].hand.size() == 1);           // the outer scope flushed
    CHECK(s.players[0].cards_drawn_this_turn == 1);
}

TEST_CASE("Maze's End F1 (gi8): an off-main Maze's End activation that WINS is taken; one that cannot win is not")
{
    LoadF1Cards();
    const char* gates[] = { "Azorius Guildgate", "Boros Guildgate", "Dimir Guildgate", "Golgari Guildgate",
                            "Gruul Guildgate", "Izzet Guildgate", "Orzhov Guildgate", "Rakdos Guildgate",
                            "Selesnya Guildgate" };
    auto board = [&](bool tenth_in_library) {
        GameState s = FBase();
        s.step = Step::End;
        int num = 700;
        for (const char* g : gates) { FPut(s, g, num++); }               // nine distinct names, untapped
        const int maze = FPut(s, "Maze's End", 720);
        for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(FCard("Forest", 900 + k)); }
        s.players[0].library.push_back(FCard(tenth_in_library ? "Simic Guildgate" : "Azorius Guildgate", 730));
        return std::make_pair(s, maze);
    };
    {
        auto [s, maze] = board(true);
        CHECK(TryOffMainAltWinActivation(s));
        CHECK(s.won_by_effect);
        CHECK(FFind(s, 730) != nullptr);            // the tenth name was fetched
        CHECK(FInHand(s, maze));                    // Maze's End returned as the cost
    }
    {
        auto [s, maze] = board(false);              // only a duplicate name left: no win, no activation
        const std::size_t lib = s.players[0].library.size();
        CHECK_FALSE(TryOffMainAltWinActivation(s));
        CHECK_FALSE(s.won_by_effect);
        REQUIRE(FFind(s, maze) != nullptr);
        CHECK_FALSE(FFind(s, maze)->tapped);
        CHECK(s.players[0].library.size() == lib);
    }
    {
        auto [s, maze] = board(true);               // the {3} cannot be paid: every Gate but one is tapped
        int left = 0;
        for (Permanent& p : s.battlefield) { if (p.card.m_number != maze && left++ > 0) { p.tapped = true; } }
        CHECK_FALSE(TryOffMainAltWinActivation(s));
        CHECK_FALSE(s.won_by_effect);
    }
}

TEST_CASE("Maze's End F1 (gi1): Talon's put credits its own {C}, and a plan holds Maze's End for its activation")
{
    LoadF1Cards();
    GameState s = FBase();
    s.players[0].lands_played_this_turn = 1;
    FPut(s, "Azorius Guildgate", 700);
    FPut(s, "Boros Guildgate", 701);
    FPut(s, "Dimir Guildgate", 702);
    FPut(s, "Golgari Guildgate", 703);
    const int maze = FPut(s, "Maze's End", 704);
    for (int k = 0; k < 4; ++k) { s.players[0].library.push_back(FCard("Gruul Guildgate", 900 + k)); }
    s.players[0].hand.push_back(FCard("Talon Gates of Madara", 710));
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
    const Action* talon = nullptr;
    const Action* act = nullptr;
    for (const Action& a : acts)
    {
        if (a.kind == Action::Kind::Channel && a.card_name.str() == "Talon Gates of Madara") { talon = &a; }
        if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::TapTutorPut
            && a.sac_source_id == maze) { act = &a; }
    }
    REQUIRE(talon != nullptr);
    CHECK(talon->rock_mana.colorless == 1);          // the put Gate taps for {C} this turn
    REQUIRE(act != nullptr);
    const std::vector<int> hold = TurnSolver::ActivationTapReserve(s, { *talon, *act });
    CHECK(std::find(hold.begin(), hold.end(), maze) != hold.end());   // not spent on Talon's {4}
}

TEST_CASE("Maze's End F1 (gi8): the rollout credits an end-step Maze's End win to OUR turn")
{
    LoadF1Cards();
    GameState s = FBase();
    s.phase = Phase::PostCombatMain;
    const char* gates[] = { "Azorius Guildgate", "Boros Guildgate", "Dimir Guildgate", "Golgari Guildgate",
                            "Gruul Guildgate", "Izzet Guildgate", "Orzhov Guildgate", "Rakdos Guildgate",
                            "Selesnya Guildgate" };
    int num = 700;
    for (const char* g : gates) { FPut(s, g, num++); }
    FPut(s, "Maze's End", 720);
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(FCard("Forest", 900 + k)); }
    s.players[0].library.push_back(FCard("Simic Guildgate", 730));
    const int turn = s.turn_number;
    TurnSolver::SimulateEndAndStartNextTurnForTest(s);
    CHECK(s.won_by_effect);
    CHECK(s.turn_number == turn);
}

// ---- F2 (sweep 61002 gi1): the combined "put Talon + activate Maze's End" plan is EXECUTED -------------------------
// The T5 board of the sweep game: Talon's {4} paid the usual way (four plain taps) left Maze's End's {3} one short.
// The owed-aware trailing payment (TurnSolver::TrailingStepOwedHold) pays the {4} with Baldur's Gate's fed mode so
// the activation is paid out of Cliffgate + Golgari Guildgate + the fresh Talon. Both worlds.
namespace
{
GameState F2Gi1Board(int& maze_out)
{
    GameState s = FBase();
    s.players[0].lands_played_this_turn = 1;
    FPut(s, "Amulet of Vigor", 1);
    FPut(s, "Gond Gate", 28);
    FPut(s, "Manor Gate", 41);
    FPut(s, "Cliffgate", 16);
    FPut(s, "Baldur's Gate", 7);
    FPut(s, "Golgari Guildgate", 27);
    maze_out = FPut(s, "Maze's End", 43);
    for (int k = 0; k < 6; ++k) { s.players[0].library.push_back(FCard("Gruul Guildgate", 900 + k)); }
    s.players[0].hand.push_back(FCard("Talon Gates of Madara", 58));
    return s;
}
TurnSolver::Plan F2Gi1Plan(const GameState& s, int maze)
{
    TurnSolver::Plan p;
    p.land_decided = true;
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
    const Action* talon = nullptr;
    const Action* act = nullptr;
    for (const Action& a : acts)
    {
        if (a.kind == Action::Kind::Channel && a.card_name.str() == "Talon Gates of Madara") { talon = &a; }
        if (a.kind == Action::Kind::ActivatePermAbility && a.ability_mode == Action::AbilityMode::TapTutorPut
            && a.sac_source_id == maze && act == nullptr) { act = &a; }
    }
    REQUIRE(talon != nullptr);
    REQUIRE(act != nullptr);
    p.actions = { *talon, *act };
    return p;
}
void F2ResolveAll(GameState& st)
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

TEST_CASE("Maze's End F2 (gi1): Talon's {4} is paid so Maze's End's {3} stays payable (owed-aware trailing payment)")
{
    LoadF1Cards();
    int maze = 0;
    const GameState s = F2Gi1Board(maze);
    const TurnSolver::Plan p = F2Gi1Plan(s, maze);

    // The trailing pass holds Maze's End's own {T} source (ActivationTapReserve) around every step; so here.
    {
        const PlanSourceReserveScope maze_held(TurnSolver::ActivationTapReserveUnion(s, p.actions));
        REQUIRE(std::find(g_plan_reserved_sources.begin(), g_plan_reserved_sources.end(), maze)
                != g_plan_reserved_sources.end());
        // The usual payment strands the activation: after it, {3} with Maze's End set aside is not payable.
        {
            GameState c = s;
            REQUIRE(TurnSolver::ApplyChannelStepDirect(c, p.actions[0]));
            for (Permanent& q : c.battlefield) { if (q.card.m_number == maze) { q.tapped = true; } }
            ManaCost three{}; three.generic = 3;
            CHECK_FALSE(TapForCostDirect(c, three, false));
        }
        // ...and the hold finds a payment that does not.
        const std::vector<int> hold = TurnSolver::TrailingStepOwedHold(
            s, p.actions[0], p.actions,
            [&p](GameState& g) { return TurnSolver::ApplyChannelStepDirect(g, p.actions[0]); });
        CHECK_FALSE(hold.empty());
    }

    // Rollout world: the whole plan applies -- Talon put, Maze's End activated (back in hand, a Gate fetched).
    {
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, p, /*is_pre_combat=*/true);
        CHECK(FFind(roll, 58) != nullptr);
        CHECK(FFind(roll, maze) == nullptr);
        CHECK(FInHand(roll, maze));
    }
    // Executor world (AIEngine::TakeTurn's trailing pass): the same.
    {
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, p, true);
        GameState ex = s;
        eng.TakeTurn(ex, true, F2ResolveAll);
        CHECK(FFind(ex, 58) != nullptr);
        CHECK(FFind(ex, maze) == nullptr);
        CHECK(FInHand(ex, maze));
    }
}

TEST_CASE("Maze's End F2 (gi1): no hold when the usual payment already leaves the owed activation payable")
{
    LoadF1Cards();
    int maze = 0;
    GameState s = F2Gi1Board(maze);
    FPut(s, "Izzet Guildgate", 38);
    FPut(s, "Dimir Guildgate", 18);
    const TurnSolver::Plan p = F2Gi1Plan(s, maze);
    const PlanSourceReserveScope maze_held(TurnSolver::ActivationTapReserveUnion(s, p.actions));
    const std::vector<int> hold = TurnSolver::TrailingStepOwedHold(
        s, p.actions[0], p.actions,
        [&p](GameState& g) { return TurnSolver::ApplyChannelStepDirect(g, p.actions[0]); });
    CHECK(hold.empty());
}

