// Unpredictable Cyclone onboarding, PHASE 2 (2026-10-27): extra turns + the three Eldrazi.
//
//   * EXTRA TURNS, engine-wide (USER ruling 2026-10-26: "extra turns do not count toward the turn
//     count"): GameState::turn_number stays the ROUND, an extra turn advances TurnSeq() only. Pinned in
//     BOTH worlds -- the executor (GameEngine::PlayOutFrom) and the rollout (TurnSolver's
//     SimulateToEnd) -- on the same states: the win turn is counted in rounds, summoning sickness ends
//     in the extra turn, no spawn and no opponent draw before it, two pending extra turns, the TurnSeq
//     cap, and the on-the-play T1 draw skip keyed on turns TAKEN.
//   * SEARCH DEPTH COUNTS ROUNDS: a depth-1 full-line search sees (and searches) a kill in this round's
//     extra turn.
//   * Emrakul's cast trigger in both worlds (executor PushCastTriggers / PushFreeCast, rollout apply_one).
//   * The Ulamogs' targeted cast triggers: own-permanent forced targeting, the <N-targets removal, the
//     identity-folded option list and the searched rank, executor == rollout, destroy vs indestructible.
//   * Annihilator vs the opponent's spawned tokens (and the attacker-index remap).
//   * Emrakul's graveyard shuffle on discard, on death (legend rule), and batched over one event.
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

#include <algorithm>
#include <string>
#include <vector>

namespace
{

constexpr const char* kEmrakul = "Emrakul, the Aeons Torn";
constexpr const char* kHunger  = "Ulamog, the Ceaseless Hunger";
constexpr const char* kGyre    = "Ulamog, the Infinite Gyre";

void EnsureCards2()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def2(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card Card2(const std::string& name, int number)
{
    Card c     = Def2(name).card;
    c.m_number = number;
    return c;
}

Permanent& Put2(GameState& s, const std::string& name, int number, int controller = 0, bool sick = false)
{
    Permanent p;
    p.card              = Card2(name, number);
    p.controller_index  = controller;
    p.owner_index       = controller;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

// An opponent token exactly as GameEngine's spawn site makes it.
void PutSpawn(GameState& s, int power, int toughness)
{
    Card token;
    token.m_name = std::to_string(power) + "/" + std::to_string(toughness) + " Creature";
    token.RehashName();
    token.AddType(CardType::Creature);
    token.m_power     = power;
    token.m_toughness = toughness;
    token.m_number    = s.next_opp_spawn_number++;
    Permanent p;
    p.card             = token;
    p.controller_index = 1;
    p.owner_index      = 1;
    s.battlefield.push_back(p);
}

int OppPermanents(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.controller_index == 1) { ++n; } }
    return n;
}

int CountNamed(const std::vector<Card>& zone, const std::string& name)
{
    int n = 0;
    for (const Card& c : zone) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

bool OnBattlefield(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return true; } }
    return false;
}

const CycloneProvider& Prov2() { static const CycloneProvider cp; return cp; }

// Round `turn`, a fresh main 1 (land drop open), `lands` untapped Islands, a library of Islands, the
// opponent at `opp_life`. The deck stamps an extra-turn deck carries.
GameState Base2(int turn, int lands, int opp_life)
{
    GameState s;
    s.m_provider          = &Prov2();
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.on_the_play         = true;
    s.players[0].life     = 20;
    s.players[1].life     = opp_life;
    s.deck_has_extra_turn = true;
    s.deck_has_gy_shuffle = true;
    s.deck_has_annihilator = true;
    s.deck_has_cast_target_trigger = true;
    for (int k = 0; k < lands; ++k) { Put2(s, "Island", 40 + k); }
    for (int k = 0; k < 30; ++k) { s.players[0].library.push_back(Card2("Island", 900 + k)); }
    return s;
}

int ExecutorWin(GameState s, GameEngine::ResumeAt from, int max_turns, GameState* out = nullptr)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 0, 0);
    GameEngine ge(eng);
    const int w = ge.PlayOutFrom(s, max_turns, from);
    if (out) { *out = s; }
    return w;
}

TurnSolver::Plan CastPlan2(const GameState& s, const std::string& name)
{
    TurnSolver::Plan plan;
    plan.land_decided = true;
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = name;
    a.def       = &Def2(name);
    a.cost      = EffectiveSpellCost(Def2(name), s);
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i)
    { if (s.players[0].hand[i].m_name == name) { a.hand_index = i; break; } }
    plan.actions.push_back(a);
    return plan;
}

// Resolve the executor's stack LIFO, as GameEngine::ResolveStack does.
void ResolveAll(GameState& s)
{
    while (!s.stack.empty())
    {
        StackEntry e = s.stack.back();
        s.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        REQUIRE(d != nullptr);
        EffectHandler::Resolve(s, e, *d);
    }
}

}   // namespace

TEST_CASE("Cyclone phase 2: card data for the three Eldrazi")
{
    EnsureCards2();
    const CardDefinition& e = Def2(kEmrakul);
    CHECK(e.card.m_mana_cost.ManaValue() == 15);
    CHECK(e.card.m_power == 15);
    CHECK(e.card.m_toughness == 15);
    CHECK(e.card.HasSupertype(Supertype::Legendary));
    CHECK(e.card.HasKeyword(Keyword::Flying));
    CHECK(e.params.cant_be_countered);
    CHECK(e.params.cast_trigger_extra_turns == 1);
    CHECK(e.params.annihilator == 6);
    CHECK(e.params.put_into_graveyard_shuffles_graveyard);

    const CardDefinition& h = Def2(kHunger);
    CHECK(h.card.m_mana_cost.ManaValue() == 10);
    CHECK(h.card.m_power == 10);
    CHECK(h.card.HasKeyword(Keyword::Indestructible));
    CHECK(h.params.cast_trigger_exile_target_permanents == 2);
    CHECK(h.params.annihilator == 0);
    CHECK_FALSE(h.params.put_into_graveyard_shuffles_graveyard);

    const CardDefinition& g = Def2(kGyre);
    CHECK(g.card.m_mana_cost.ManaValue() == 11);
    CHECK(g.card.m_power == 10);              // Scryfall: 10/10 (verified 2026-10-27)
    CHECK(g.card.m_toughness == 10);
    CHECK(g.card.HasKeyword(Keyword::Indestructible));
    CHECK(g.params.cast_trigger_destroy_target_permanents == 1);
    CHECK(g.params.annihilator == 4);
    CHECK(g.params.put_into_graveyard_shuffles_graveyard);
}

TEST_CASE("Cyclone phase 2: TurnSeq derives from the round + extra turns; AdvanceTurnCounter")
{
    GameState s;
    s.turn_number = 3;
    CHECK(s.TurnSeq() == 3);
    s.AdvanceTurnCounter();                       // nothing pending: the next round
    CHECK(s.turn_number == 4);
    CHECK_FALSE(s.in_extra_turn);
    CHECK(s.TurnSeq() == 4);
    s.pending_extra_turns = 2;
    s.AdvanceTurnCounter();                       // an extra turn: the round stays
    CHECK(s.turn_number == 4);
    CHECK(s.in_extra_turn);
    CHECK(s.extra_turns_taken == 1);
    CHECK(s.pending_extra_turns == 1);
    CHECK(s.TurnSeq() == 5);
    s.AdvanceTurnCounter();
    CHECK(s.turn_number == 4);
    CHECK(s.TurnSeq() == 6);
    s.AdvanceTurnCounter();
    CHECK(s.turn_number == 5);
    CHECK_FALSE(s.in_extra_turn);
    CHECK(s.TurnSeq() == 7);
    CHECK_FALSE(s.TurnSeqCapped(8));              // 7 <= 32
    s.extra_turns_taken = 40;
    CHECK(s.TurnSeqCapped(8));                    // 45 > 4 x 8
}

TEST_CASE("Cyclone phase 2: Emrakul's cast trigger -- executor hand cast, free cast, and the rollout")
{
    EnsureCards2();
    {   // EffectHandler::PushCastTriggers: one ExtraTurn entry ABOVE the spell; resolving it owes a turn.
        GameState s = Base2(3, 0, 20);
        EffectHandler::PushCastTriggers(s, Def2(kEmrakul), 0);
        REQUIRE(s.stack.size() == 1);
        CHECK(s.stack.back().trigger_kind == StackEntry::TriggerKind::ExtraTurn);
        ResolveAll(s);
        CHECK(s.pending_extra_turns == 1);
    }
    {   // A FREE cast (PushFreeCast -- cascade / Cyclone's route) fires it too; two casts owe two turns.
        GameState s = Base2(3, 0, 20);
        CHECK(EffectHandler::PushFreeCast(s, Card2(kEmrakul, 61), 0));
        CHECK(s.stack.size() == 2);                  // the spell + its trigger above it
        CHECK(s.stack.back().trigger_kind == StackEntry::TriggerKind::ExtraTurn);
        ResolveAll(s);
        CHECK(s.pending_extra_turns == 1);
        CHECK(OnBattlefield(s, 61));
        CHECK(EffectHandler::PushFreeCast(s, Card2(kEmrakul, 62), 0));
        ResolveAll(s);
        CHECK(s.pending_extra_turns == 2);            // the legend rule took one copy; both triggers resolved
    }
    {   // The rollout's apply_one (TurnSolver::ApplyPlan): a 15-land hard cast owes the turn too.
        GameState s = Base2(3, 15, 20);
        s.players[0].hand.push_back(Card2(kEmrakul, 61));
        TurnSolver::ApplyPlan(s, CastPlan2(s, kEmrakul), /*is_pre_combat=*/true);
        CHECK(s.pending_extra_turns == 1);
        CHECK(OnBattlefield(s, 61));
        CHECK(s.turn_number == 3);
    }
}

TEST_CASE("Cyclone phase 2: an extra turn -- win in ROUNDS, sickness ends, no spawn, no opponent draw")
{
    EnsureCards2();
    // Round 3, main 1: a sick 10/10 (as if cast this turn) and one extra turn owed. The opponent is at
    // 20: the extra turn's attack (10) plus round 4's (10) kills -> win reported in ROUND 4 (five turns
    // taken). Spawns: one scheduled for round 3 (already arrived -- the extra turn must not re-fire it)
    // and one for round 4.
    static const std::vector<OpponentSpawn> spawns = { { 3, 1, 1 }, { 4, 2, 2 } };
    GameState s = Base2(3, 2, 20);
    s.players[0].lands_played_this_turn = 1;
    Put2(s, kHunger, 61, 0, /*sick=*/true);
    s.pending_extra_turns = 1;
    s.opponent_spawns     = &spawns;
    s.deck_has_annihilator = false;   // a non-annihilator body: the spawns stay countable
    // An opponent library, so the skipped notional draw is observable.
    s.opponent_library_dealt = true;
    for (int k = 0; k < 5; ++k) { Card c = Card2("Island", 700 + k); s.players[1].library.push_back(c); }

    GameState e;
    const int we = ExecutorWin(s, GameEngine::ResumeAt::Main1, 8, &e);
    CHECK(we == 4);
    CHECK(e.turn_number == 4);
    CHECK(e.extra_turns_taken == 1);
    CHECK(e.TurnSeq() == 5);
    CHECK(OppPermanents(e) == 1);                 // only round 4's 2/2: the extra turn spawned nothing
    // One opponent draw: after the extra turn (its own turn falls between round 3 and 4); none between
    // round 3's regular turn and the extra turn.
    CHECK(e.players[1].library.size() == 4);

    // The rollout plays the same game to the same round.
    const int wr = TurnSolver::SimulateToEndForTest(s, 0, 8, 9, false);
    CHECK(wr == 4);
    const int wr1 = TurnSolver::SimulateToEndForTest(s, 1, 8, 9, false);
    CHECK(wr1 == 4);
}

TEST_CASE("Cyclone phase 2: two pending extra turns -- the kill lands inside the same round")
{
    EnsureCards2();
    GameState s = Base2(3, 2, 20);
    s.players[0].lands_played_this_turn = 1;
    Put2(s, kHunger, 61, 0, /*sick=*/true);
    s.pending_extra_turns = 2;
    GameState e;
    CHECK(ExecutorWin(s, GameEngine::ResumeAt::Main1, 8, &e) == 3);   // two extra-turn attacks, round 3
    CHECK(e.extra_turns_taken == 2);
    CHECK(e.turn_number == 3);
    CHECK(TurnSolver::SimulateToEndForTest(s, 0, 8, 9, false) == 3);
    CHECK(TurnSolver::SimulateToEndForTest(s, 1, 8, 9, false) == 3);
}

TEST_CASE("Cyclone phase 2: an extra turn owed at the last round is still played")
{
    EnsureCards2();
    // Round 8 = max_turns: without the pending guard the game would end; the extra turn kills.
    GameState s = Base2(8, 2, 10);
    s.players[0].lands_played_this_turn = 1;
    Put2(s, kHunger, 61, 0, /*sick=*/true);
    s.pending_extra_turns = 1;
    CHECK(ExecutorWin(s, GameEngine::ResumeAt::Main1, 8) == 8);
    CHECK(TurnSolver::SimulateToEndForTest(s, 0, 8, 9, false) == 8);
    s.pending_extra_turns = 0;
    CHECK(ExecutorWin(s, GameEngine::ResumeAt::Main1, 8) == -1);   // no extra turn: no attack in time
}

TEST_CASE("Cyclone phase 2: the TurnSeq cap ends an extra-turn chain as a no-win in both worlds")
{
    EnsureCards2();
    GameState s = Base2(3, 2, 10);
    s.players[0].lands_played_this_turn = 1;
    Put2(s, kHunger, 61, 0, /*sick=*/false);
    s.extra_turns_taken = 40;   // TurnSeq 43 > 4 x 8
    CHECK(ExecutorWin(s, GameEngine::ResumeAt::NewTurn, 8) == -1);
    CHECK(TurnSolver::SimulateToEndForTest(s, 0, 8, 9, false) == 9);   // max_turns + 1 = no win
}

TEST_CASE("Cyclone phase 2: on the play, the T1 draw skip keys on the first turn TAKEN")
{
    EnsureCards2();
    // Round 1 just ended with an extra turn owed: the extra turn is the second turn taken, so it draws.
    GameState s = Base2(1, 1, 20);
    s.pending_extra_turns = 1;
    const std::size_t lib = s.players[0].library.size();
    GameState e;
    ExecutorWin(s, GameEngine::ResumeAt::Cleanup, 1, &e);   // max_turns 1: only the extra turn is played
    CHECK(e.extra_turns_taken == 1);
    CHECK(e.turn_number == 1);
    CHECK(e.players[0].library.size() == lib - 1);
}

TEST_CASE("Cyclone phase 2: search depth counts ROUNDS -- a depth-1 search finds the extra-turn kill")
{
    EnsureCards2();
    // Round 3 main 1, a sick 10/10, one extra turn owed, the opponent at 10: the kill is in this round's
    // extra turn. Depth 1 covers round 3 only; the extra turn is inside it and SEARCHED (not a leaf).
    GameState s = Base2(3, 2, 10);
    s.players[0].lands_played_this_turn = 1;
    Put2(s, kHunger, 61, 0, /*sick=*/true);
    s.pending_extra_turns = 1;
    int committed = 0;
    const TurnSolver::SearchLine line = TurnSolver::FullSearchLine(s, 1, 8, false, nullptr, nullptr, &committed);
    CHECK(line.win_turn == 3);
    CHECK(line.win_turn <= s.turn_number + committed - 1);   // VERIFIED inside the window
    CHECK(line.phases.size() >= 2);                          // the extra turn's main is part of the line
}

TEST_CASE("Cyclone phase 2: Ulamog CH targets -- opponent first, own permanents forced, <2 removed")
{
    EnsureCards2();
    const CardDefinition& hd = Def2(kHunger);
    {   // Two opponent spawns: both exiled (the provider's front option).
        GameState s = Base2(3, 3, 20);
        PutSpawn(s, 1, 1);
        PutSpawn(s, 4, 4);
        const std::vector<int> nums = ChooseCastTriggerTargets(s, 0, 2, false, kHunger);
        REQUIRE(nums.size() == 2);
        ResolveCastTriggerTargets(s, 0, kHunger, nums, false);
        CHECK(OppPermanents(s) == 0);
        CHECK(s.battlefield.size() == 3);           // our Islands untouched
        CHECK(s.exile.empty());                     // the opponent's tokens cease to exist
    }
    {   // One spawn: it and ONE of our permanents (forced -- "target permanent" includes ours).
        GameState s = Base2(3, 3, 20);
        PutSpawn(s, 2, 2);
        const std::vector<int> nums = ChooseCastTriggerTargets(s, 0, 2, false, kHunger);
        REQUIRE(nums.size() == 2);
        ResolveCastTriggerTargets(s, 0, kHunger, nums, false);
        CHECK(OppPermanents(s) == 0);
        CHECK(s.battlefield.size() == 2);
        CHECK(CountNamed(s.exile, "Island") == 1);
    }
    {   // No opponent permanent: two of ours, and the three identical Islands are ONE option.
        GameState s = Base2(3, 3, 20);
        Put2(s, "Street Wraith", 70);
        std::vector<std::vector<int>> classes;
        CastTriggerTargetClasses(s, 0, false, classes);
        REQUIRE(classes.size() == 2);               // {Island x3}, {Street Wraith}
        CHECK(classes[0].size() == 3);              // lands before creatures (the base order)
        std::vector<int> counts{ 3, 1 };
        CHECK(CastTriggerOptionCount(counts, 2) == 2);   // {Island, Island}, {Island, Wraith}
        const std::vector<int> o0 = CastTriggerTargetOption(classes, 2, 0);
        const std::vector<int> o1 = CastTriggerTargetOption(classes, 2, 1);
        REQUIRE(o0.size() == 2);
        REQUIRE(o1.size() == 2);
        CHECK(s.battlefield[o0[0]].card.m_name.str() == "Island");
        CHECK(s.battlefield[o0[1]].card.m_name.str() == "Island");
        CHECK(s.battlefield[o1[1]].card.m_name.str() == "Street Wraith");
        // The searched rank selects the option (ScriptedCastTriggerTarget, keyed on TurnSeq) ...
        {
            ScriptedCastTriggerTarget pin(1, s.TurnSeq());
            const std::vector<int> nums = ChooseCastTriggerTargets(s, 0, 2, false, kHunger);
            REQUIRE(nums.size() == 2);
            CHECK(std::find(nums.begin(), nums.end(), 70) != nums.end());
        }
        // ... and a pin from another turn (the regular turn of this round, inside its extra turn) is stale.
        {
            ScriptedCastTriggerTarget pin(1, s.TurnSeq() - 1);
            const std::vector<int> nums = ChooseCastTriggerTargets(s, 0, 2, false, kHunger);
            CHECK(std::find(nums.begin(), nums.end(), 70) == nums.end());
        }
        // A rank past the end clamps to the last option.
        {
            ScriptedCastTriggerTarget pin(9, s.TurnSeq());
            const std::vector<int> nums = ChooseCastTriggerTargets(s, 0, 2, false, kHunger);
            CHECK(std::find(nums.begin(), nums.end(), 70) != nums.end());
        }
    }
    {   // Fewer than two legal permanents: the trigger is removed (CR 603.3d) -- nothing pushed.
        GameState s = Base2(3, 1, 20);
        CHECK(ChooseCastTriggerTargets(s, 0, 2, false, kHunger).empty());
        EffectHandler::PushCastTriggers(s, hd, 0);
        CHECK(s.stack.empty());
    }
}

TEST_CASE("Cyclone phase 2: Ulamog CH -- executor push and rollout inline cast agree")
{
    EnsureCards2();
    // 10 Islands + one spawn: the cast trigger exiles the spawn and one Island in both worlds.
    GameState a = Base2(3, 10, 20);
    PutSpawn(a, 3, 3);
    a.players[0].hand.push_back(Card2(kHunger, 63));
    GameState b = a;
    TurnSolver::ApplyPlan(a, CastPlan2(a, kHunger), /*is_pre_combat=*/true);   // rollout / ApplyPlan

    AIEngine eng(MulliganProfile::DefaultProfile(), 0, 0);
    ManaPool avail;
    Card& hc = b.players[0].hand.back();
    MtgTestSeam::CastSpellFromHand(eng, b, hc, avail, 0);
    ResolveAll(b);
    CHECK(OppPermanents(a) == 0);
    CHECK(OppPermanents(b) == 0);
    CHECK(OnBattlefield(a, 63));
    CHECK(OnBattlefield(b, 63));
    CHECK(CountNamed(a.exile, "Island") == 1);
    CHECK(CountNamed(b.exile, "Island") == 1);
    CHECK(a.battlefield.size() == b.battlefield.size());
}

TEST_CASE("Cyclone phase 2: Ulamog IG destroy -- indestructible no-op; our Emrakul dies and reshuffles")
{
    EnsureCards2();
    {   // Destroy on an indestructible permanent: legal, nothing happens.
        GameState s = Base2(3, 0, 20);
        Put2(s, kHunger, 61);
        ResolveCastTriggerTargets(s, 0, kGyre, { 61 }, /*destroy=*/true);
        CHECK(OnBattlefield(s, 61));
    }
    {   // Destroying our own Emrakul: it dies, and its trigger shuffles the WHOLE graveyard in.
        GameState s = Base2(3, 0, 20);
        Put2(s, kEmrakul, 61);
        s.players[0].graveyard.push_back(Card2("Street Wraith", 71));
        const std::size_t lib = s.players[0].library.size();
        ResolveCastTriggerTargets(s, 0, kGyre, { 61 }, /*destroy=*/true);
        CHECK_FALSE(OnBattlefield(s, 61));
        CHECK(s.players[0].graveyard.empty());
        CHECK(s.players[0].library.size() == lib + 2);   // Emrakul + the Wraith
    }
    {   // EXILE is not a graveyard: an exiled Emrakul does not shuffle.
        GameState s = Base2(3, 0, 20);
        Put2(s, kEmrakul, 61);
        s.players[0].graveyard.push_back(Card2("Street Wraith", 71));
        ResolveCastTriggerTargets(s, 0, kHunger, { 61 }, /*destroy=*/false);
        CHECK(CountNamed(s.exile, kEmrakul) == 1);
        CHECK(s.players[0].graveyard.size() == 1);
    }
}

TEST_CASE("Cyclone phase 2: annihilator sacrifices the opponent's spawns and remaps the attackers")
{
    EnsureCards2();
    GameState s = Base2(4, 0, 30);
    PutSpawn(s, 1, 1);
    PutSpawn(s, 1, 1);
    PutSpawn(s, 2, 2);
    Put2(s, kGyre, 62);                       // annihilator 4, at battlefield index 3
    std::vector<int> atk{ 3 };
    ApplyAnnihilator(s, 0, atk);
    CHECK(OppPermanents(s) == 0);             // three permanents < 4: all of them
    REQUIRE(atk.size() == 1);
    CHECK(s.battlefield[atk[0]].card.m_number == 62);   // the attacker index followed the erases
    // An attack in a real game (both worlds share ApplyAnnihilator): the board is cleared by combat.
    GameState g = Base2(4, 2, 30);
    g.players[0].lands_played_this_turn = 1;
    PutSpawn(g, 4, 4);
    PutSpawn(g, 1, 1);
    Put2(g, kEmrakul, 61);
    GameState e;
    CHECK(ExecutorWin(g, GameEngine::ResumeAt::Main1, 8, &e) == 5);   // 15 + 15
    CHECK(OppPermanents(e) == 0);
    CHECK(TurnSolver::SimulateToEndForTest(g, 0, 8, 9, false) == 5);
}

TEST_CASE("Cyclone phase 2: the graveyard shuffle -- discard, legend-rule death, one batched event")
{
    EnsureCards2();
    {   // A discarded Emrakul shuffles the graveyard (it included) into the library.
        GameState s = Base2(3, 0, 20);
        s.players[0].graveyard.push_back(Card2("Brainstorm", 72));
        s.players[0].hand.push_back(Card2(kEmrakul, 61));
        const std::size_t lib = s.players[0].library.size();
        DiscardCardFromHand(s, 0, 0, nullptr, "test");
        CHECK(s.players[0].graveyard.empty());
        CHECK(s.players[0].library.size() == lib + 2);
    }
    {   // The legend rule: the second Emrakul dies, and its trigger reshuffles.
        GameState s = Base2(3, 0, 20);
        Put2(s, kEmrakul, 61);
        Put2(s, kEmrakul, 62, 0, /*sick=*/true);
        const std::size_t lib = s.players[0].library.size();
        EnforceLegendRule(s, 0);
        CHECK(OnBattlefield(s, 61));          // keep the oldest
        CHECK_FALSE(OnBattlefield(s, 62));
        CHECK(s.players[0].graveyard.empty());
        CHECK(s.players[0].library.size() == lib + 1);
    }
    {   // ONE simultaneous event (the cleanup discard): the shuffle waits for the whole event, so a card
        // discarded AFTER Emrakul in the same event goes in too.
        GameState s = Base2(3, 0, 20);
        s.players[0].hand.push_back(Card2(kEmrakul, 61));
        s.players[0].hand.push_back(Card2("Brainstorm", 72));
        const std::size_t lib = s.players[0].library.size();
        {
            GyShuffleBatch batch(s);
            DiscardCardFromHand(s, 0, 0, nullptr, "cleanup");
            DiscardCardFromHand(s, 0, 0, nullptr, "cleanup");
            CHECK(s.players[0].graveyard.size() == 2);   // not yet
        }
        CHECK(s.players[0].graveyard.empty());
        CHECK(s.players[0].library.size() == lib + 2);
    }
    {   // An unstamped deck (deck_has_gy_shuffle false): nothing happens -- every other deck's path.
        GameState s = Base2(3, 0, 20);
        s.deck_has_gy_shuffle = false;
        s.players[0].hand.push_back(Card2(kEmrakul, 61));
        DiscardCardFromHand(s, 0, 0, nullptr, "test");
        CHECK(s.players[0].graveyard.size() == 1);
    }
}

TEST_CASE("Cyclone phase 2: the cast-trigger-target axis -- one variant per option rank")
{
    EnsureCards2();
    // 10 Islands (one class) + a Wraith + one spawn, Ulamog CH in hand: the board's options for TWO
    // targets are {spawn, Island}, {spawn, Wraith}, {Island, Island}, {Island, Wraith} (+ the cast's own
    // headroom), so the enumeration carries cast_trigger_target_choice variants beside the base plan.
    GameState s = Base2(3, 10, 20);
    PutSpawn(s, 2, 2);
    Put2(s, "Street Wraith", 70);
    s.players[0].lands_played_this_turn = 1;
    s.players[0].hand.push_back(Card2(kHunger, 63));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    int base = 0, variants = 0, max_rank = -1;
    for (const TurnSolver::Plan& p : plans)
    {
        bool casts = false;
        for (const Action& a : p.actions) { if (a.card_name.str() == kHunger) { casts = true; } }
        if (!casts) { continue; }
        if (p.cast_trigger_target_choice < 0) { ++base; }
        else { ++variants; max_rank = std::max(max_rank, static_cast<int>(p.cast_trigger_target_choice)); }
    }
    CHECK(base >= 1);
    CHECK(variants >= 3);
    CHECK(max_rank >= 3);
}
