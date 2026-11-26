// Enchanted Evening, USER ruling 22 (2026-11-26): THE LOCK PLAYOUT.
//   *"Once we have the full lock we should just go off and win playing out via heuristic so that it is faster."*
//   *"(just like with out Combo Out situations)"*
// EnchantedEveningProvider::LockPlayoutNow is true while the free-turn lock is CREDITED (GameState::in_free_turn) with
// Enchanted Evening AND Opalescence on our battlefield. Then the search ends its window (FSLineWin returns the depth-0
// playout, the rollout plays the turn at depth 0, no free-turn give-back) and the executor plays the turn with the
// depth-0 runner under GreedySite::LockPlayout. While it holds, the provider keeps the greedy finish lock-safe.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/SearchBudget.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
const CardDefinition& L22Def(const std::string& name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}
Card L22Card(const std::string& name, int number)
{
    Card c = L22Def(name).card;
    c.m_number = number;
    return c;
}
int L22Put(GameState& s, const std::string& name, int number, int controller = 0)
{
    Permanent p;
    p.card = L22Card(name, number);
    p.controller_index = p.owner_index = controller;
    if (L22Def(name).params.fading_counters > 0) { p.fade_counters = L22Def(name).params.fading_counters; }
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}
int L22Index(const GameState& s, int number)
{
    for (std::size_t i = 0; i < s.battlefield.size(); ++i)
    { if (s.battlefield[i].card.m_number == number) { return static_cast<int>(i); } }
    FAIL("no permanent ", number);
    return -1;
}
const EnchantedEveningProvider& L22Prov()
{
    static const EnchantedEveningProvider prov;
    return prov;
}
// A per-job arm scope (heurarm, the pooled A/B's mechanism): EE_LOCK_PLAYOUT forced to `on`.
struct L22Arm
{
    heurarm::Arm saved = heurarm::t_arm;
    explicit L22Arm(bool on) { heurarm::t_arm[heurarm::EE_LOCK_PLAYOUT] = on ? 1 : 0; }
    ~L22Arm() { heurarm::t_arm = saved; }
};
// A credited full lock at our main 1 of round 4: Enchanted Evening, Opalescence and a Parallax Wave ours; the opponent
// at 20 life with no permanent, a hand of nothing and a library of lands (each one dies as a 0/0 the turn it is played,
// so every opponent turn stays locked and free). EE 5/5 + Wave 4/4 swing 9 a turn: the kill takes three of our turns,
// all in this round.
GameState L22FullLock()
{
    L22Def("Forest");
    Decklist deck;
    for (int i = 0; i < 40; ++i) { deck.mainboard.push_back(L22Def("Forest").card); }
    GameState s = GoldFishRunner::SetupGame(deck, 8, 0);
    REQUIRE(s.opponent_plays_lands);
    s.players[1].hand.clear();
    s.opponent2_hand.clear();
    s.battlefield.clear();
    s.opponent_spawns = nullptr;
    while (!s.players[1].library.empty()) { s.players[1].library.erase(s.players[1].library.begin()); }
    for (int k = 0; k < 40; ++k) { s.players[1].library.push_back(L22Card("Plains", 100500 + k)); }
    s.players[0].hand.clear();
    s.players[0].life = 20;
    s.players[1].life = 20;
    s.active_player_index = 0;
    s.turn_number = 4;
    s.phase = Phase::PreCombatMain;
    s.step  = Step::MainPhase;
    s.players[0].lands_played_this_turn = 0;
    s.deck_has_free_turn = true;
    s.deck_float_wild_exact = true;
    s.deck_has_type_statics = true;
    s.deck_has_eot_fov_pitch = true;
    s.m_provider = &L22Prov();
    s.in_free_turn = true;   // the opponent's last turn ended locked: this turn is credited
    s.free_turns_taken = 1;
    L22Put(s, "Enchanted Evening", 51);
    L22Put(s, "Opalescence", 52);
    L22Put(s, "Parallax Wave", 53);
    RefreshTypeStatics(s);
    return s;
}
} // namespace

TEST_CASE("EE ruling 22: LockPlayoutNow -- the credited FULL lock only (EE + Opalescence ours, in_free_turn)")
{
    const L22Arm arm(true);
    GameState s = L22FullLock();
    CHECK(L22Prov().LockPlayoutNow(s));
    GameState n = s;
    n.in_free_turn = false;            // not credited (the lock formed this turn; the opponent's turn decides)
    CHECK_FALSE(L22Prov().LockPlayoutNow(n));
    GameState v = s;                   // Calming Verse + Enchanted Evening: ruling 5's temporary lock, not the full one
    v.battlefield.erase(v.battlefield.begin() + L22Index(v, 52));
    RefreshTypeStatics(v);
    CHECK_FALSE(L22Prov().LockPlayoutNow(v));
    GameState o = s;                   // an OPPONENT's Opalescence does not make our lock
    o.battlefield[static_cast<std::size_t>(L22Index(o, 52))].controller_index = 1;
    CHECK_FALSE(L22Prov().LockPlayoutNow(o));
    const GenericProvider base;        // every other deck: never
    CHECK_FALSE(base.LockPlayoutNow(s));
    const L22Arm off(false);           // the A/B arm
    CHECK_FALSE(L22Prov().LockPlayoutNow(s));
}

TEST_CASE("EE ruling 22: the lock playout's restrictions keep the greedy finish lock-safe")
{
    const L22Arm arm(true);
    GameState s = L22FullLock();
    L22Put(s, "Exploration", 54);
    L22Put(s, "Sterling Grove", 55);
    L22Put(s, "Plains", 100400, 1);   // an opponent permanent (a 0/0 that the SBA has not yet removed)
    RefreshTypeStatics(s);
    REQUIRE(L22Prov().LockPlayoutNow(s));
    const Permanent& wave = s.battlefield[static_cast<std::size_t>(L22Index(s, 53))];
    std::vector<int> legal;
    for (int n : { 51, 52, 53, 54, 100400 }) { legal.push_back(L22Index(s, n)); }
    // Parallax Wave: opponent targets only (the d0 probe Waved our own Enchanted Evening on the credited turn).
    const std::vector<int> wt = L22Prov().FadeExileCreatureTargets(s, 0, wave, legal);
    CHECK(wt == std::vector<int>{ L22Index(s, 100400) });
    // ...while the search outside the playout keeps the full menu (only the proven Exploration fold applies there).
    GameState n = s;
    n.in_free_turn = false;
    const std::vector<int> wn = L22Prov().FadeExileCreatureTargets(n, 0, wave, legal);
    CHECK(std::find(wn.begin(), wn.end(), L22Index(n, 51)) != wn.end());
    // Force of Vigor: no option holding one of our permanents.
    std::vector<std::vector<int>> opts{ {}, { L22Index(s, 51) }, { L22Index(s, 100400) },
                                        { L22Index(s, 100400), L22Index(s, 55) } };
    L22Prov().DestroyUpToArtEnchTargetOptions(s, 0, opts);
    CHECK(opts == std::vector<std::vector<int>>{ { L22Index(s, 100400) } });
    // Calming Verse: no SWEEP-SELF.
    bool keep = true, sweep = true;
    L22Prov().CalmingVerseVariants(s, 0, keep, sweep);
    CHECK_FALSE(sweep);
    // Oblivion Ring's forced own target: a non-lock permanent before a lock piece.
    std::vector<int> ol{ L22Index(s, 51), L22Index(s, 52), L22Index(s, 54) };
    const int k = L22Prov().EtbExileNonlandTargetPick(s, 0, ol);
    REQUIRE(k >= 0);
    CHECK(ol[static_cast<std::size_t>(k)] == L22Index(s, 54));
}

TEST_CASE("EE ruling 22: a rollout entering a credited full lock plays it out and wins in THIS round")
{
    const L22Arm arm(true);
    const GameState s = L22FullLock();
    TurnSolver::ClearPerGameCaches();   // the rollout game cache would answer the second call for free
    TurnSolver::KillOnlyTally on;
    const int w = TurnSolver::SimulateToEndForTest(s, /*depth=*/1, /*max_turns=*/8, /*cutoff=*/9,
                                                   /*second_main=*/true, &on, /*budget_units=*/1LL << 40);
    CHECK(w == 4);   // three swings of 9, every opponent turn locked: the round never advances
    TurnSolver::KillOnlyTally off;
    {
        const L22Arm searched(false);
        TurnSolver::ClearPerGameCaches();
        const int w0 = TurnSolver::SimulateToEndForTest(s, 1, 8, 9, true, &off, 1LL << 40);
        CHECK(w0 == 4);   // the same answer, searched
    }
    CHECK(on.units <= off.units);
}

TEST_CASE("EE ruling 22: GreedySite::LockPlayout is valid inside the executor's lock-playout extent")
{
    REQUIRE_FALSE(TurnSolver::LockPlayoutLive());
    TurnSolver::SetLockPlayoutLive(true);
    {
        const TurnSolver::GreedyPermit permit(TurnSolver::GreedySite::LockPlayout, 0);
        CHECK(TurnSolver::InD0Runner());   // the playout IS the depth-0 runner for the providers' sub-searches
    }
    TurnSolver::SetLockPlayoutLive(false);
    CHECK_FALSE(TurnSolver::LockPlayoutLive());
}
