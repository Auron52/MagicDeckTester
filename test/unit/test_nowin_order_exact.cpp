// MTG_NOWIN_ORDER_EXACT (docs/design/canonical-nowin-order-dependence.md). Under MTG_CANON_SIMKEY both no-win
// caches -- the per-decision FSLineCache and the game line cache -- used to store a NO-WIN under the CANONICAL key
// (hand and battlefield folded as multisets), so a zone-permuted twin's refutation answered this position. The
// search that produced it reads zone order (enumeration order, vector-position tie-breaks, breakpoint
// continuation ranks, the greedy horizon rollout), so that refutation could hide a win this order finds at every
// budget. The lever keys every searched no-win order-exactly.
//
// These run under the lever's DEFAULT (no slot pinned), so `MTG_NOWIN_ORDER_EXACT=0 mtg-test` fails them:
//   * a twin (same multiset, different hand + battlefield order) must be SEARCHED, not answered by the first
//     order's refutation (TurnSolver::TwinNoWinCacheForTest searches `first`, then `second` with the SAME caches,
//     both as non-root nodes so the game line cache is in play too);
// and two controls pin the slot:
//   * an exact transposition (the same order twice) is still answered from the cache with the lever on, so the
//     fix keeps every sound share;
//   * with the slot pinned OFF the twin IS answered by the first order's refutation -- the old behaviour, so
//     the probe demonstrably sees a cross-order hit when one happens.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "deck/DeckLoader.h"

#include <algorithm>
#include <string>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(
            ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void Put(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = Def(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

void Hand(GameState& s, const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

struct ArmScope   // pin one heurarm slot for the scope
{
    heurarm::Slot s;
    explicit ArmScope(heurarm::Slot slot, int v) : s(slot) { heurarm::t_arm[slot] = static_cast<std::int8_t>(v); }
    ~ArmScope() { heurarm::t_arm[s] = -1; }
};

constexpr int kMaxTurns = 7;
constexpr int kDepth    = 2;

// Turn 6, Vampires, our pre-combat main with the land drop spent, a library of Swamps: several castable plans and
// no kill of a 40-life opponent inside the 2-turn horizon, so every search here is a completed NO-WIN.
// `permuted` builds the same multiset with the hand reversed and the battlefield rotated: a canonical-key twin.
GameState Board(bool permuted)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = 40;
    s.players[0].lands_played_this_turn = 1;
    s.m_provider = &SelectDecisionProvider(DeckLoader::LoadFromFile(
        ResolveHeuristicDefaultsPath("decks/Vampires/Vampire.cod")));
    for (int k = 0; k < 20; ++k)
    {
        Card c = Def("Swamp").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    for (int k = 0; k < 4; ++k) { Put(s, "Swamp", 50 + k); }
    Put(s, "Blackcleave Cliffs", 60);
    Put(s, "Bloodghast", 1);
    Hand(s, "Falkenrath Gorger", 10);
    Hand(s, "Legion Lieutenant", 11);
    Hand(s, "Markov Baron", 12);
    Hand(s, "Stromkirk Condemned", 13);
    if (permuted)
    {
        std::reverse(s.players[0].hand.begin(), s.players[0].hand.end());
        std::rotate(s.battlefield.begin(), s.battlefield.begin() + 2, s.battlefield.end());
    }
    return s;
}

}   // namespace

TEST_CASE("Order-exact no-win: a zone-permuted twin is searched, not answered by the other order's refutation")
{
    EnsureCardsLoaded();
    const GameState a = Board(false);
    const GameState b = Board(true);
    REQUIRE(a.players[0].hand.front().m_name != b.players[0].hand.front().m_name);   // really a different order

    const TurnSolver::TwinCacheProbe r = TurnSolver::TwinNoWinCacheForTest(a, b, kDepth, kMaxTurns, false);
    CHECK(r.first_win == kMaxTurns + 1);    // a completed no-win: what the cache stores
    CHECK(r.second_win == kMaxTurns + 1);
    // The twin ran its own search instead of returning the first order's refutation.
    CHECK(r.first_units > 0);
    CHECK(r.second_units > 0);
    CHECK(r.second_root_hit == 0);
}

TEST_CASE("Order-exact no-win: an exact transposition is still answered from the cache")
{
    EnsureCardsLoaded();
    const ArmScope on(heurarm::NOWIN_ORDER_EXACT, 1);
    const GameState a = Board(false);
    const TurnSolver::TwinCacheProbe r = TurnSolver::TwinNoWinCacheForTest(a, a, kDepth, kMaxTurns, false);
    CHECK(r.first_win == kMaxTurns + 1);
    CHECK(r.second_win == kMaxTurns + 1);
    CHECK(r.second_root_hit == 1);
    CHECK(r.second_units == 0);
}

TEST_CASE("Order-exact no-win: control -- pinned OFF, the twin IS answered by the other order's refutation")
{
    EnsureCardsLoaded();
    const ArmScope off(heurarm::NOWIN_ORDER_EXACT, 0);
    const TurnSolver::TwinCacheProbe r =
        TurnSolver::TwinNoWinCacheForTest(Board(false), Board(true), kDepth, kMaxTurns, false);
    CHECK(r.first_win == kMaxTurns + 1);
    CHECK(r.second_root_hit == 1);
    CHECK(r.second_units == 0);
}

// MTG_NOWIN_TWIN_WAVE (the budget-starvation fallback on top of the order-exact key; see NowinTwinWaveOn): a twin's
// refutation may answer this order ONLY when re-searching would cost a serious share of the budget still left. The
// first case runs under the levers' defaults, so `MTG_NOWIN_TWIN_WAVE=0 mtg-test` fails it; the unlimited-budget case
// is the soundness half and holds with the wave on or off (the gate never opens without a finite budget).
TEST_CASE("Twin refutation wave: under a starved budget the twin's refutation is reused")
{
    EnsureCardsLoaded();
    // 2,000 units: the twin's search cost far more than 0.0005 of that, so the gate opens at the root.
    const TurnSolver::TwinCacheProbe r =
        TurnSolver::TwinNoWinCacheForTest(Board(false), Board(true), kDepth, kMaxTurns, false, 2000);
    CHECK(r.first_win == kMaxTurns + 1);
    CHECK(r.second_win == kMaxTurns + 1);
    CHECK(r.second_root_hit == 1);
    CHECK(r.second_units == 0);
}

TEST_CASE("Twin refutation wave: at an unlimited budget the twin is still searched")
{
    EnsureCardsLoaded();
    const ArmScope wave(heurarm::NOWIN_TWIN_WAVE, 1);
    const ArmScope exact(heurarm::NOWIN_ORDER_EXACT, 1);
    const TurnSolver::TwinCacheProbe r =
        TurnSolver::TwinNoWinCacheForTest(Board(false), Board(true), kDepth, kMaxTurns, false);
    CHECK(r.second_root_hit == 0);
    CHECK(r.second_units > 0);
}
