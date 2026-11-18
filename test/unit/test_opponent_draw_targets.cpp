// The OPPONENT target of FiveColour's two "target player draws" effects (2026-11-17):
//   * Garth One-Eye's conjured Braingeyser -- "Target player draws X cards";
//   * Unite the Coalition's "Target player draws a card" mode, each pick at either player.
//
// USER 2026-11-17: "Opponent targets for draws are only useful when they have a chance of decking them. I
// can't see a way for Garth or Unite to make that happen so we should keep it heuristically pruned. (not
// pruned by the engine)". So the ENGINE enumerates both targets (Action::draw_target: Braingeyser 0 / 1,
// Unite the COUNT of picks aimed at the opponent), and DecisionProvider::OpponentDrawCandidates restricts
// the opponent target to the minimal count that decks them (opponentdeck::ForcedDrawLethalX). These pin:
//   1. the provider's branches (OpponentDrawCandidatesFor): autonomous = lethal only, unpruned = every
//      count, human = always offered, no library dealt = nothing;
//   2. Garth: the engine's menu carries the opponent Braingeyser only when its X decks them, and the
//      rollout's apply and the executor's both resolve it as a ForcedDraw (our hand untouched);
//   3. Unite: the same for the opponent's share of the draw picks, the split's remaining draws staying
//      ours, the executor's cast site stamping the share and EffectHandler resolving it;
//   4. FiveColour's split rule keeps a split that leaves the lethal picks when it goes face.
// Run with MTG_UNPRUNE=oppdraw, the enumeration tests check the opened set (every count) instead; with
// MTG_HUMAN_PLAY=1, the human menu (Braingeyser: one opponent variant; Unite: every share).
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/OpponentDeck.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <set>
#include <string>
#include <vector>

namespace
{

void EnsureCardsOd()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefOd(const std::string& name)
{
    EnsureCardsOd();
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

int g_od_num = 9100;

int PutOd(GameState& s, const std::string& name)
{
    Permanent p;
    p.card             = DefOd(name).card;
    p.card.m_number    = g_od_num++;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    return p.card.m_number;
}

void HandOd(GameState& s, const std::string& name)
{
    Card c = DefOd(name).card; c.m_number = g_od_num++; s.players[0].hand.push_back(c);
}

// The opponent's library: `n` cards, dealt (opponent_library_dealt) the way opponentdeck::Deal leaves it.
void DealOppOd(GameState& s, int n)
{
    s.opponent_library_dealt = true;
    for (int k = 0; k < n; ++k)
    {
        Card c = DefOd("Forest").card; c.m_number = opponentdeck::kNumberBase + k;
        s.players[1].library.push_back(c);
    }
}

const FiveColourProvider& FcProvOd()
{
    static const FiveColourProvider p;
    return p;
}

GameState FreshOd()
{
    EnsureCardsOd();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.game_seed           = 4141;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;   // no land drop: keeps the plan space small
    s.m_provider          = &FcProvOd();
    for (int k = 0; k < 12; ++k)
    {
        Card c = DefOd("Lightning Bolt").card;   // drawn cards are inert for the assertions
        c.m_number = 200 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

std::vector<int> Range(int lo, int hi) { std::vector<int> v; for (int k = lo; k <= hi; ++k) { v.push_back(k); } return v; }

// Garth with eight Islands: Braingeyser's X = 8 - {U}{U} = 6.
GameState GarthBoardOd(int opp_library)
{
    GameState s = FreshOd();
    PutOd(s, "Garth One-Eye");
    for (int k = 0; k < 8; ++k) { PutOd(s, "Island"); }
    DealOppOd(s, opp_library);
    return s;
}

const Action* GarthBrainOd(const TurnSolver::Plan& p)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::GarthActivate && a.tutor_target.str() == "Braingeyser") { return &a; } }
    return nullptr;
}

// Unite the Coalition with seven lands: Plains, Island, Swamp, Mountain, Forest + two Islands.
GameState UniteBoardOd(int opp_library)
{
    GameState s = FreshOd();
    for (const char* n : { "Plains", "Island", "Swamp", "Mountain", "Forest", "Island", "Island" }) { PutOd(s, n); }
    HandOd(s, "Unite the Coalition");
    DealOppOd(s, opp_library);
    return s;
}

const Action* UniteCastOd(const TurnSolver::Plan& p)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Unite the Coalition") { return &a; } }
    return nullptr;
}

void ResolveStackOd(GameState& st)
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

TEST_CASE("OpponentDrawCandidatesFor: lethal-only by default, every count unpruned, always for a human")
{
    const CardDefinition& brain = DefOd("Braingeyser");
    const CardDefinition& unite = DefOd("Unite the Coalition");
    GameState full = FreshOd();
    DealOppOd(full, 40);                       // nothing within reach decks 40 cards
    GameState low = FreshOd();
    DealOppOd(low, 3);                         // lethal = 3 cards
    GameState none = FreshOd();                // no library dealt (a legacy board)

    // Autonomous: the minimal lethal count only, and only when it fits.
    CHECK(OpponentDrawCandidatesFor(full, brain, 6, 1, false, false).empty());
    CHECK(OpponentDrawCandidatesFor(low, brain, 6, 1, false, false) == std::vector<int>{ 3 });
    CHECK(OpponentDrawCandidatesFor(low, brain, 2, 1, false, false).empty());    // X 2 < 3
    CHECK(OpponentDrawCandidatesFor(full, unite, 5, 1, false, false).empty());
    CHECK(OpponentDrawCandidatesFor(low, unite, 5, 1, false, false) == std::vector<int>{ 3 });
    CHECK(OpponentDrawCandidatesFor(low, unite, 2, 1, false, false).empty());    // 2 picks < 3
    CHECK(OpponentDrawCandidatesFor(low, unite, 5, 2, false, false) == std::vector<int>{ 2 });   // ceil(3/2)

    // Unpruned (MTG_UNPRUNE=oppdraw): every count, lethal or not.
    CHECK(OpponentDrawCandidatesFor(full, brain, 6, 1, true, false) == Range(1, 6));
    CHECK(OpponentDrawCandidatesFor(full, unite, 5, 1, true, false) == Range(1, 5));

    // Human play: always offered. Braingeyser's X is auto-maxed on Garth's route, so ONE variant: the
    // kill when payable, else the max; Unite: every share.
    CHECK(OpponentDrawCandidatesFor(full, brain, 6, 1, false, true) == std::vector<int>{ 6 });
    CHECK(OpponentDrawCandidatesFor(low, brain, 6, 1, false, true) == std::vector<int>{ 3 });
    CHECK(OpponentDrawCandidatesFor(full, unite, 4, 1, false, true) == Range(1, 4));

    // No library dealt: a forced draw is inert, so nothing is offered on any branch.
    for (bool unpruned : { false, true })
        for (bool human : { false, true })
        {
            CHECK(OpponentDrawCandidatesFor(none, brain, 6, 1, unpruned, human).empty());
            CHECK(OpponentDrawCandidatesFor(none, unite, 5, 1, unpruned, human).empty());
        }
    // Already decked: no count is lethal (ForcedDrawLethalX = 0).
    GameState decked = low;
    decked.opponent_decked = true;
    CHECK(OpponentDrawCandidatesFor(decked, brain, 6, 1, false, false).empty());
}

TEST_CASE("Garth Braingeyser: the engine enumerates the opponent target; the provider keeps it only when lethal")
{
    const bool human    = HumanPlayActive();
    const bool unpruned = !human && DecisionUnpruned(UnprunedGate::OpponentDraw);
    // A full library: X = 6 cannot deck 40 cards -> pruned (every X under MTG_UNPRUNE=oppdraw).
    {
        GameState s = GarthBoardOd(40);
        std::set<int> self_x, opp_x;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
        {
            const Action* g = GarthBrainOd(p);
            if (g == nullptr) { continue; }
            (g->draw_target != 0 ? opp_x : self_x).insert(g->chosen_x);
        }
        CHECK(self_x == std::set<int>{ 6 });                 // the self copy is unchanged (auto-maxed X)
        if (human)         { CHECK(opp_x == std::set<int>{ 6 }); }   // always offered, at the auto-maxed X
        else if (unpruned) { CHECK(opp_x == std::set<int>{ 1, 2, 3, 4, 5, 6 }); }
        else               { CHECK(opp_x.empty()); }
    }
    // Five cards left: X = 5 decks them -> offered at exactly the minimal lethal X.
    {
        GameState s = GarthBoardOd(5);
        std::set<int> opp_x;
        const TurnSolver::Plan* kill = nullptr;
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        for (const TurnSolver::Plan& p : plans)
        {
            const Action* g = GarthBrainOd(p);
            if (g == nullptr || g->draw_target == 0) { continue; }
            opp_x.insert(g->chosen_x);
            if (g->chosen_x == 5 && kill == nullptr) { kill = &p; }
        }
        if (unpruned) { CHECK(opp_x == std::set<int>{ 1, 2, 3, 4, 5, 6 }); }
        else          { CHECK(opp_x == std::set<int>{ 5 }); }   // human: the kill, too
        REQUIRE(kill != nullptr);
        const Action& g = *GarthBrainOd(*kill);
        CHECK(g.cost.generic == 5);                          // X folded into the reserved cost
        CHECK(g.cost.blue == 2);

        // Rollout: the opponent draws X (decked), our hand is untouched.
        GameState r = s;
        TurnSolver::ApplyPlan(r, *kill, /*is_pre_combat=*/true);
        CHECK(OpponentHasLost(r));
        CHECK(r.opponent_decked);
        CHECK(r.players[1].hand.size() == 5);
        CHECK(r.players[0].hand.empty());
        CHECK(r.players[0].library.size() == 12);

        // Executor: the committed plan through TakeTurn reaches the same deck-out.
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::CommitPhasePlan(eng, *kill, true);
        GameState e = s;
        eng.TakeTurn(e, true, ResolveStackOd);
        CHECK(e.opponent_decked);
        CHECK(e.players[1].hand.size() == 5);
        CHECK(e.players[0].library.size() == 12);
    }
}

TEST_CASE("ApplyGarthActivate: Braingeyser's target -- us (acquisition) or the opponent (ForcedDraw)")
{
    for (int target : { 0, 1 })
    {
        GameState s = GarthBoardOd(10);
        const int garth = s.battlefield.front().card.m_number;
        ApplyGarthActivate(s, 0, garth, "Braingeyser", 4, std::string(), target);
        CHECK(s.spells_cast_this_turn == 1);
        CHECK((s.battlefield.front().garth_chosen_mask & (1u << 1)) != 0);   // Braingeyser chosen either way
        CHECK(s.players[0].hand.size() == (target == 0 ? 4u : 0u));
        CHECK(s.players[1].hand.size() == (target == 0 ? 0u : 4u));
        CHECK(s.players[1].library.size() == (target == 0 ? 10u : 6u));
        CHECK_FALSE(s.opponent_decked);
    }
    // The acquisition predicate: only the self copy (and Regrowth) re-solves what was drawn.
    Action a; a.kind = Action::Kind::GarthActivate; a.tutor_target = "Braingeyser";
    CHECK(TurnSolver::IsGarthAcquisition(a));
    a.draw_target = 1;
    CHECK_FALSE(TurnSolver::IsGarthAcquisition(a));
    a.tutor_target = "Regrowth"; a.draw_target = 0;
    CHECK(TurnSolver::IsGarthAcquisition(a));
    a.tutor_target = "Shivan Dragon";
    CHECK_FALSE(TurnSolver::IsGarthAcquisition(a));
}

TEST_CASE("Unite the Coalition: the opponent's share of the draw picks is enumerated; kept only when lethal")
{
    // Human play and the unpruned hatch both open every share.
    const bool unpruned = HumanPlayActive() || DecisionUnpruned(UnprunedGate::OpponentDraw);
    // A full library: no share decks them -> every variant draws for us (every share when unpruned).
    {
        GameState s = UniteBoardOd(40);
        std::set<std::pair<int, int>> got;   // (S, opponent picks)
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false))
        {
            const Action* u = UniteCastOd(p);
            if (u != nullptr) { got.insert({ u->chosen_x, u->draw_target }); }
        }
        REQUIRE_FALSE(got.empty());
        for (const auto& sd : got)
        {
            if (!unpruned) { CHECK(sd.second == 0); }
            CHECK(sd.second <= 5 - sd.first);
        }
        if (unpruned) { CHECK(got.count({ 0, 5 }) == 1); CHECK(got.count({ 0, 1 }) == 1); }
    }
    // Three cards left: three picks at the opponent deck them -> offered at exactly that share, for
    // every split that leaves three draw picks (FiveColour's draw case keeps S = 0, 1, 2 and the all-in).
    {
        GameState s = UniteBoardOd(3);
        std::set<std::pair<int, int>> opp;
        const TurnSolver::Plan* kill = nullptr;
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, false);
        for (const TurnSolver::Plan& p : plans)
        {
            const Action* u = UniteCastOd(p);
            if (u == nullptr || u->draw_target == 0) { continue; }
            opp.insert({ u->chosen_x, u->draw_target });
            if (u->chosen_x == 1 && u->draw_target == 3 && kill == nullptr) { kill = &p; }
        }
        if (!unpruned) { CHECK(opp == std::set<std::pair<int, int>>{ { 0, 3 }, { 1, 3 }, { 2, 3 } }); }
        else           { CHECK(opp.count({ 0, 1 }) == 1); CHECK(opp.count({ 0, 5 }) == 1); }
        REQUIRE(kill != nullptr);
        // Rollout: S = 1 -> 2 damage; of the four draw picks, three at the opponent (decked), one ours.
        GameState r = s;
        TurnSolver::ApplyPlan(r, *kill, /*is_pre_combat=*/false);
        CHECK(r.opponent_decked);
        CHECK(OpponentHasLost(r));
        CHECK(r.players[1].hand.size() == 3);
        CHECK(r.players[1].life == 18);
        CHECK(r.players[0].library.size() == 11);
    }
}

TEST_CASE("Unite the Coalition: the executor stamps the opponent's share and resolves it like the rollout")
{
    const CardDefinition& def = DefOd("Unite the Coalition");
    for (int share : { 0, 2, 4 })
    {
        // Executor: the real cast site (the CastRiders draw_target rider -> StackEntry::modal_opp_draw_picks),
        // then EffectHandler's modal branch.
        GameState ex = UniteBoardOd(10);
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        ManaPool avail = AvailableManaPool(ex);
        MtgTestSeam::CastWithDrawTarget(eng, ex, ex.players[0].hand.front(), avail, /*S=*/1, share);
        REQUIRE(ex.stack.size() == 1);
        CHECK(ex.stack.back().modal_opp_draw_picks == share);
        ResolveStackOd(ex);
        // Rollout twin: the shared apply of the same cast.
        GameState s = UniteBoardOd(10);
        Action a;
        a.kind        = Action::Kind::CastFromHand;
        a.card_name   = def.card.m_name;
        a.hand_index  = 0;
        a.cost        = def.card.m_mana_cost;
        a.chosen_x    = 1;
        a.draw_target = share;
        TurnSolver::Plan p;
        p.actions.push_back(a);
        GameState ro = s;
        TurnSolver::ApplyPlan(ro, p, /*is_pre_combat=*/false);
        CHECK(ex.players[0].hand.size() == ro.players[0].hand.size());
        CHECK(ex.players[0].hand.size() == static_cast<std::size_t>(4 - share));
        CHECK(ex.players[1].hand.size() == ro.players[1].hand.size());
        CHECK(ex.players[1].hand.size() == static_cast<std::size_t>(share));
        CHECK(ex.players[1].library.size() == ro.players[1].library.size());
        CHECK(ex.players[1].life == ro.players[1].life);
        CHECK(ex.players[1].life == 18);
        CHECK(ex.opponent_decked == ro.opponent_decked);
        CHECK_FALSE(ex.opponent_decked);
    }
}

TEST_CASE("FiveColour split rule: going face still leaves the split whose draw picks deck them")
{
    const CardDefinition& def = DefOd("Unite the Coalition");
    GameState s = FreshOd();
    s.players[1].life = 8;                      // 10 face damage is lethal -> the all-in
    DealOppOd(s, 40);
    std::vector<int> out;
    FcProvOd().ModalSplitCandidates(s, def, out);
    if (!DecisionUnpruned(UnprunedGate::Fetch)) { CHECK(out == std::vector<int>{ 5 }); }
    GameState t = FreshOd();
    t.players[1].life = 8;
    DealOppOd(t, 2);                            // two draw picks deck them
    out.clear();
    FcProvOd().ModalSplitCandidates(t, def, out);
    if (!DecisionUnpruned(UnprunedGate::Fetch)) { CHECK(out == std::vector<int>{ 5, 3 }); }
}
