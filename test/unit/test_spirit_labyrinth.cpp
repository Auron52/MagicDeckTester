// Spirit of the Labyrinth (Spirits, integrator F, 2026-11-13): "Each player can't draw more than one card
// each turn." (CardParams::each_player_draw_cap_per_turn; core/DrawCap.h)
//
// The cap is applied at the one draw choke point (AttemptDrawIntoHand) and at the opponent's raw draws, and
// both players' counts restart at our untap and at the opponent's notional turn. These tests drive the
// choke point directly, then both worlds -- the rollout (TurnSolver::ApplyPlan /
// SimulateEndAndStartNextTurnForTest) and the executor (AIEngine::TakeTurn through the committed-plan seam
// / GameEngine::AdvanceToFirstMainForTest) -- on the same boards and require the same result.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/DrawCap.h"
#include "core/EffectHandler.h"
#include "core/GameEngine.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HandEntry.h"
#include "core/HeuristicDefaults.h"
#include "core/OpponentDeck.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{
const CardDefinition& LbDef(const std::string& name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const auto* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card LbCard(const std::string& name, int number)
{
    Card c = LbDef(name).card;
    c.m_number = number;
    return c;
}

int LbPut(GameState& s, const std::string& name, int number, int controller = 0)
{
    Permanent p;
    p.card = LbDef(name).card;
    p.card.m_number = number;
    p.controller_index = p.owner_index = controller;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

// A cap deck mid-turn (main 1, land drop spent), 20 Plains in the library.
GameState LbState(bool cap_deck = true)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.phase               = Phase::PreCombatMain;
    s.step                = Step::MainPhase;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.deck_has_draw_cap   = cap_deck;
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(LbCard("Plains", 900 + k)); }
    return s;
}

void LbResolveAll(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

GameState LbRunExecutor(const GameState& s, const TurnSolver::Plan& plan)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, LbResolveAll);
    return exec;
}

const Action* LbCast(const std::vector<Action>& acts, const std::string& name)
{
    for (const Action& a : acts)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return &a; } }
    return nullptr;
}

bool LbInHand(const GameState& s, int number)
{
    for (const Card& c : s.players[0].hand) { if (c.m_number == number) { return true; } }
    return false;
}

// The loot board: Careful Study ({U}: draw two, then discard two) in hand with two Plains to discard, an
// untapped Island, and the given count of cards already drawn this turn.
GameState LootBoard(int drawn_this_turn, bool labyrinth_on_battlefield)
{
    GameState s = LbState();
    if (labyrinth_on_battlefield) { LbPut(s, "Spirit of the Labyrinth", 1); }
    LbPut(s, "Island", 101);
    s.players[0].hand.push_back(LbCard("Careful Study", 30));
    s.players[0].hand.push_back(LbCard("Plains", 31));
    s.players[0].hand.push_back(LbCard("Plains", 32));
    s.players[0].cards_drawn_this_turn = drawn_this_turn;
    return s;
}
}   // namespace

// ---- the choke point --------------------------------------------------------------------------------

TEST_CASE("Spirit of the Labyrinth: the card carries the cap; the deck stamp arms it")
{
    CHECK(LbDef("Spirit of the Labyrinth").params.each_player_draw_cap_per_turn == 1);
    GameState s = LbState(/*cap_deck=*/false);
    LbPut(s, "Spirit of the Labyrinth", 1);
    CHECK(drawcap::CapPerTurn(s) == 0);      // an unstamped deck: no cap (every other deck's exact path)
    s.deck_has_draw_cap = true;
    CHECK(drawcap::CapPerTurn(s) == 1);
    s.battlefield.clear();
    CHECK(drawcap::CapPerTurn(s) == 0);      // the cap lives on the battlefield, not in the deck
}

TEST_CASE("Spirit of the Labyrinth: a second draw this turn is ignored -- no card, no count, no deck-out")
{
    GameState s = LbState();
    LbPut(s, "Spirit of the Labyrinth", 1);
    // The turn's first draw happens.
    DrawAttemptResult first = AttemptDrawIntoHand(s, 0, HandEntryReason::DrawStep);
    CHECK(first.satisfied);
    CHECK(first.drew);
    ++s.players[0].cards_drawn_this_turn;
    REQUIRE(s.players[0].hand.size() == 1);
    // The second is ignored: satisfied (so no loss), not drawn, hand and library untouched.
    const std::size_t lib = s.players[0].library.size();
    DrawAttemptResult second = AttemptDrawIntoHand(s, 0, HandEntryReason::Draw);
    CHECK(second.satisfied);
    CHECK_FALSE(second.drew);
    CHECK(s.players[0].hand.size() == 1);
    CHECK(s.players[0].library.size() == lib);
    // A multi-draw: nothing drawn, every attempt satisfied (no deck-out).
    const DrawBatchResult batch = DrawIntoHandDetailed(s, 0, 3, HandEntryReason::Draw);
    CHECK(batch.drawn == 0);
    CHECK(batch.satisfied == 3);
    // ...even from an EMPTY library: an ignored draw is not an attempt to draw.
    while (!s.players[0].library.empty()) { s.players[0].library.DrawTop(); }
    CHECK(DrawIntoHandDetailed(s, 0, 1, HandEntryReason::Draw).satisfied == 1);
    // Putting a card into hand WITHOUT drawing (tutor, bounce, reveal) is untouched.
    EnterHand(s, 0, LbCard("Plains", 60), HandEntryReason::Tutor);
    EnterHand(s, 0, LbCard("Plains", 61), HandEntryReason::Bounce);
    EnterHand(s, 0, LbCard("Plains", 62), HandEntryReason::Reveal);
    CHECK(s.players[0].hand.size() == 4);
}

TEST_CASE("Spirit of the Labyrinth: a multi-draw from zero draws exactly one (the batch's own draws count)")
{
    GameState s = LbState();
    LbPut(s, "Spirit of the Labyrinth", 1);
    const DrawBatchResult batch = DrawIntoHandDetailed(s, 0, 3, HandEntryReason::Draw);
    CHECK(batch.drawn == 1);
    CHECK(batch.satisfied == 3);
    CHECK(s.players[0].hand.size() == 1);
    // Without the Spirit (or in a deck without a cap card) all three are drawn.
    GameState t = LbState();
    CHECK(DrawIntoHandDetailed(t, 0, 3, HandEntryReason::Draw).drawn == 3);
    GameState u = LbState(/*cap_deck=*/false);
    LbPut(u, "Spirit of the Labyrinth", 1);
    CHECK(DrawIntoHandDetailed(u, 0, 3, HandEntryReason::Draw).drawn == 3);
}

TEST_CASE("Spirit of the Labyrinth: the opponent is capped -- forced draws clamp, lethal-X reads the clamp")
{
    GameState s = LbState();
    LbPut(s, "Spirit of the Labyrinth", 1);
    s.opponent_library_dealt = true;
    for (int k = 0; k < 5; ++k) { s.players[1].library.push_back(LbCard("Plains", 700 + k)); }
    CHECK(opponentdeck::ForcedDrawLethalX(s) == 0);          // 5 cards, one draw a turn: no X decks them
    CHECK(opponentdeck::ForcedDraw(s, 4) == 1);               // draws one; the other three are ignored
    CHECK(s.players[1].hand.size() == 1);
    CHECK_FALSE(s.opponent_decked);
    CHECK(opponentdeck::ForcedDraw(s, 4) == 0);               // already drew this turn
    CHECK(s.players[1].library.size() == 4);
    // One card left and no draw yet this turn: X = 1 decks them, under the cap as without it.
    GameState t = LbState();
    LbPut(t, "Spirit of the Labyrinth", 1);
    t.opponent_library_dealt = true;
    t.players[1].library.push_back(LbCard("Plains", 800));
    CHECK(opponentdeck::ForcedDrawLethalX(t) == 1);
    // Uncapped: the old model (minimal lethal X = the library size).
    GameState u = LbState();
    u.opponent_library_dealt = true;
    for (int k = 0; k < 5; ++k) { u.players[1].library.push_back(LbCard("Plains", 700 + k)); }
    CHECK(opponentdeck::ForcedDrawLethalX(u) == 5);
    CHECK(opponentdeck::ForcedDraw(u, 4) == 4);
}

TEST_CASE("Spirit of the Labyrinth: a cap deck's state key sees the draw count (it decides the next draw)")
{
    GameState a = LbState();
    LbPut(a, "Spirit of the Labyrinth", 1);
    a.players[0].cards_drawn_this_turn = 1;
    GameState b = a;
    b.players[0].cards_drawn_this_turn = 2;
    CHECK(TurnSolver::MidTurnStateKeyOf(a) != TurnSolver::MidTurnStateKeyOf(b));
    // A deck with no cap card (and no drawn-count reader) keeps its exact prior key.
    a.deck_has_draw_cap = b.deck_has_draw_cap = false;
    CHECK(TurnSolver::MidTurnStateKeyOf(a) == TurnSolver::MidTurnStateKeyOf(b));
}

// ---- both worlds: an effect draw (a loot) ---------------------------------------------------------

TEST_CASE("Spirit of the Labyrinth: a loot after the turn's draw draws NOTHING but still discards, both worlds")
{
    for (const bool labyrinth : { true, false })
    {
        const GameState s = LootBoard(/*drawn_this_turn=*/1, labyrinth);
        const Action* cast = LbCast(TurnSolver::CollectActionsForTest(s, true), "Careful Study");
        REQUIRE(cast != nullptr);
        TurnSolver::Plan plan;
        plan.actions = { *cast };
        GameState roll = s;
        TurnSolver::ApplyPlan(roll, plan, true);
        const GameState exec = LbRunExecutor(s, plan);
        for (const GameState* g : { static_cast<const GameState*>(&roll), &exec })
        {
            INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)")
                 << ", Labyrinth " << (labyrinth ? "on" : "off"));
            const int drew = labyrinth ? 0 : 2;
            CHECK(g->players[0].library.size() == s.players[0].library.size() - drew);
            CHECK(g->players[0].cards_drawn_this_turn == 1 + drew);
            // Study resolved and two cards were discarded either way: hand = 3 - 1 + drew - 2.
            CHECK(static_cast<int>(g->players[0].hand.size()) == drew);
            CHECK_FALSE(LbInHand(*g, 30));
            CHECK(g->players[0].graveyard.size() == 3);
        }
    }
}

TEST_CASE("Spirit of the Labyrinth: the turn's first effect draw happens (one of two), both worlds")
{
    const GameState s = LootBoard(/*drawn_this_turn=*/0, /*labyrinth=*/true);
    const Action* cast = LbCast(TurnSolver::CollectActionsForTest(s, true), "Careful Study");
    REQUIRE(cast != nullptr);
    TurnSolver::Plan plan;
    plan.actions = { *cast };
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, plan, true);
    const GameState exec = LbRunExecutor(s, plan);
    for (const GameState* g : { static_cast<const GameState*>(&roll), &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        CHECK(g->players[0].library.size() == s.players[0].library.size() - 1);
        CHECK(g->players[0].cards_drawn_this_turn == 1);
        CHECK(g->players[0].hand.size() == 1);    // 3 - Study + 1 drawn - 2 discarded
        CHECK(g->players[0].graveyard.size() == 3);
    }
}

TEST_CASE("Spirit of the Labyrinth: a draw made BEFORE it entered counts -- cast it, then loot, both worlds")
{
    GameState s = LootBoard(/*drawn_this_turn=*/1, /*labyrinth=*/false);
    s.players[0].hand.push_back(LbCard("Spirit of the Labyrinth", 1));
    LbPut(s, "Plains", 102);
    LbPut(s, "Plains", 103);
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
    const Action* lab = LbCast(acts, "Spirit of the Labyrinth");
    const Action* study = LbCast(acts, "Careful Study");
    REQUIRE(lab != nullptr);
    REQUIRE(study != nullptr);
    TurnSolver::Plan plan;
    plan.actions = { *lab, *study };
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, plan, true);
    const GameState exec = LbRunExecutor(s, plan);
    for (const GameState* g : { static_cast<const GameState*>(&roll), &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        bool lab_out = false;
        for (const Permanent& p : g->battlefield) { if (p.card.m_number == 1) { lab_out = true; } }
        REQUIRE(lab_out);
        CHECK(g->players[0].library.size() == s.players[0].library.size());   // drew nothing
        CHECK(g->players[0].cards_drawn_this_turn == 1);
        CHECK(g->players[0].hand.empty());                                    // two Plains discarded
    }
}

// ---- both worlds: the turn boundary (the draw step, Howling Mine, the opponent's turn) ------------

TEST_CASE("Spirit of the Labyrinth: the cap resets next turn -- draw step yes, Howling Mine no, both worlds")
{
    for (const bool labyrinth : { true, false })
    {
        GameState s = LbState();
        if (labyrinth) { LbPut(s, "Spirit of the Labyrinth", 1); }
        LbPut(s, "Howling Mine", 2);
        s.players[0].cards_drawn_this_turn = 1;            // we drew this turn already
        // The opponent was dealt a library: their notional draw + their Howling Mine draw.
        s.opponent_library_dealt = true;
        for (int k = 0; k < 10; ++k) { s.players[1].library.push_back(LbCard("Plains", 700 + k)); }
        s.phase = Phase::PostCombatMain;

        GameState roll = s;
        REQUIRE(TurnSolver::SimulateEndAndStartNextTurnForTest(roll));
        GameState exec = s;
        AIEngine eng(MulliganProfile::DefaultProfile(), 0, 0);
        GameEngine ge(eng);
        REQUIRE(ge.AdvanceToFirstMainForTest(exec));
        for (const GameState* g : { static_cast<const GameState*>(&roll), static_cast<const GameState*>(&exec) })
        {
            INFO("world: " << std::string(g == &roll ? "rollout (SimulateEndAndStartNextTurn)"
                                                     : "executor (GameEngine)")
                 << ", Labyrinth " << (labyrinth ? "on" : "off"));
            const int ours = labyrinth ? 1 : 2;             // the draw step, + Howling Mine uncapped
            CHECK(g->players[0].hand.size() == static_cast<std::size_t>(ours));
            CHECK(g->players[0].library.size() == s.players[0].library.size() - ours);
            CHECK(g->players[0].cards_drawn_this_turn == ours);
            const int theirs = labyrinth ? 1 : 2;           // their notional draw, + Howling Mine uncapped
            CHECK(g->players[1].hand.size() == static_cast<std::size_t>(theirs));
            CHECK_FALSE(g->opponent_decked);
        }
    }
}
