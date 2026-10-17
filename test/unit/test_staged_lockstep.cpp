// Staged cards (CR 406: exiled with "you may play it until ...", modelled as m_is_staged hand cards)
// across the executor/rollout boundary -- pins for docs/design/rollout-executor-lockstep.md #5 and #11.
//
//   #5  (fixed fef4a54b): Gamble's "discard a card at random" indexes the hand AS STORED, so both worlds
//       must store it alike. The executor holds unplayed staged cards OUT of hand between passes and
//       re-appends them at each pass head, so a card drawn in between sits BEFORE them; the rollout keeps
//       them in hand and re-lays them last (TurnSolver::LayStagedCardsLast). Hinata overnight d5 s4004
//       gi107 (2026-08-25, docs/design/hinata-commit-the-line-fd-diverge.md) was this: same victim index,
//       different card -- the search shed an Ornithopter, the game shed a Mountain, kept the Ornithopter,
//       cast it, and stranded Reality Spasm's {U}{U} (committed T5, realised T7).
//   #11 (fixed here): the executor's m2 stranded-kill scan (AIEngine::TrySecondMainStrandedKill) ran
//       after TakeTurn's tail had moved the staged cards out of hand, so a kill that needed one was
//       found by the rollout twin (ApplySecondMainInSearch's kill-scan, staged cards in hand) and not by
//       the executor. Census over every Hinata suite cell: 421 of 636 executor scans held staged cards
//       out; 3 of those had a kill only with them.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{
void EnsureCardsLoadedSl()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card CardSl(const char* name, int num)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Card c = d->card;
    c.m_number = num;
    return c;
}

StagedCard StagedSl(const char* name, int num, int expiry)
{
    StagedCard sc;
    sc.card        = CardSl(name, num);
    sc.expiry_turn = expiry;
    return sc;
}

void PutSl(GameState& s, const char* name, int num)
{
    Permanent p;
    p.card             = CardSl(name, num);
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

// Main 2 of turn 5, land drop spent, one untapped Mountain: exactly {R} for a staged Lightning Bolt.
GameState Main2Sl(int opp_life)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = 20;
    s.players[1].life     = opp_life;
    s.players[0].lands_played_this_turn = 1;
    PutSl(s, "Mountain", 10);
    s.players[0].hand.push_back(CardSl("Island", 11));   // a card that kills nothing
    for (int i = 0; i < 10; ++i) { s.players[0].library.push_back(CardSl("Island", 40 + i)); }
    return s;
}

int CountInHand(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

std::vector<int> HandNumbers(const GameState& s)
{
    std::vector<int> v;
    for (const Card& c : s.players[0].hand) { v.push_back(c.m_number); }
    return v;
}
}   // namespace

TEST_CASE("Lockstep #11: the executor's m2 stranded-kill scan casts a STAGED burn spell for the kill")
{
    EnsureCardsLoadedSl();
    GameState s = Main2Sl(/*opp_life=*/3);
    s.players[0].staged_cards.push_back(StagedSl("Lightning Bolt", 20, /*expiry=*/5));

    // The rollout twin's view (staged cards in hand, laid last) sees the kill -- the premise.
    {
        GameState merged = s;
        MergeStagedIntoHand(merged);
        bool kill = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(merged, /*is_pre_combat=*/false))
        {
            if (p.actions.empty()) { continue; }
            GameState probe = merged;
            TurnSolver::ApplyPlan(probe, p, false);
            if (OpponentHasLost(probe)) { kill = true; break; }
        }
        REQUIRE(kill);
    }

    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    CHECK(eng.TrySecondMainStrandedKill(s));
    CHECK(OpponentHasLost(s));
    CHECK(s.players[0].staged_cards.empty());              // the Bolt was cast out of exile
    CHECK(CountInHand(s, "Lightning Bolt") == 0);
}

TEST_CASE("Lockstep #11: a scan that finds no kill returns every staged card to exile, not to hand")
{
    EnsureCardsLoadedSl();
    GameState s = Main2Sl(/*opp_life=*/20);
    s.players[0].staged_cards.push_back(StagedSl("Lightning Bolt", 20, /*expiry=*/6));

    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    CHECK_FALSE(eng.TrySecondMainStrandedKill(s));
    CHECK(s.players[1].life == 20);
    REQUIRE(s.players[0].staged_cards.size() == 1);
    CHECK(s.players[0].staged_cards[0].card.m_number == 20);
    CHECK(s.players[0].staged_cards[0].expiry_turn == 6);       // the expiry survives the round trip
    CHECK_FALSE(s.players[0].staged_cards[0].card.m_is_staged);
    CHECK(CountInHand(s, "Lightning Bolt") == 0);               // no hand duplicate
    for (const Card& c : s.players[0].hand) { CHECK_FALSE(c.m_is_staged); }
}

TEST_CASE("Lockstep #11: an EXPIRED staged card is not castable by the scan")
{
    EnsureCardsLoadedSl();
    GameState s = Main2Sl(/*opp_life=*/3);
    s.players[0].staged_cards.push_back(StagedSl("Lightning Bolt", 20, /*expiry=*/4));   // gone at T5

    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    CHECK_FALSE(eng.TrySecondMainStrandedKill(s));
    CHECK(s.players[1].life == 3);
    CHECK(CountInHand(s, "Lightning Bolt") == 0);
}

TEST_CASE("Lockstep #5: a card drawn while staged cards are out sits BEFORE them in both worlds")
{
    EnsureCardsLoadedSl();
    // One hand, two storage histories. Executor: the staged Mountain#3 is in staged_cards across the
    // draw, then merged (appended) at the next pass head. Rollout: it stays in hand, flagged, and the
    // draw lands after it; the pass-head re-lay must reproduce the executor's order.
    GameState exec;
    exec.active_player_index = 0;
    exec.turn_number         = 5;
    exec.players[0].hand.push_back(CardSl("Island", 1));
    exec.players[0].hand.push_back(CardSl("Reality Spasm", 2));
    exec.players[0].staged_cards.push_back(StagedSl("Mountain", 3, 5));

    GameState roll = exec;
    roll.players[0].staged_cards.clear();
    {
        Card staged = CardSl("Mountain", 3);
        staged.m_is_staged     = true;
        staged.m_staged_expiry = 5;
        roll.players[0].hand.insert(roll.players[0].hand.begin() + 1, staged);   // [Island, Mountain*, Spasm]
    }

    exec.players[0].hand.push_back(CardSl("Preordain", 4));   // the draw
    roll.players[0].hand.push_back(CardSl("Preordain", 4));
    REQUIRE(HandNumbers(roll) != std::vector<int>({ 1, 2, 4, 3 }));   // the raw rollout layout differs

    MergeStagedIntoHand(exec);
    TurnSolver::LayStagedCardsLast(roll);
    CHECK(HandNumbers(exec) == std::vector<int>({ 1, 2, 4, 3 }));
    CHECK(HandNumbers(roll) == HandNumbers(exec));

    // ...so an index-drawn random discard (Gamble) sheds the SAME physical card in both worlds.
    exec.game_seed = roll.game_seed = 4111;
    for (int i = 0; i < 6; ++i)
    {
        exec.players[0].library.push_back(CardSl("Ponder", 50 + i));
        roll.players[0].library.push_back(CardSl("Ponder", 50 + i));
    }
    const CardParams& gamble = CardDatabase::Instance().Lookup("Gamble")->params;
    PerformTutor(exec, 0, gamble, "Ponder", "Gamble");
    PerformTutor(roll, 0, gamble, "Ponder", "Gamble");
    CHECK(HandNumbers(roll) == HandNumbers(exec));
    REQUIRE(exec.players[0].graveyard.size() == 1);
    REQUIRE(roll.players[0].graveyard.size() == 1);
    CHECK(exec.players[0].graveyard[0].m_number == roll.players[0].graveyard[0].m_number);
}
