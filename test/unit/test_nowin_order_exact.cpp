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
GameState Board(bool permuted, int opp_life = 40)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.phase               = Phase::PreCombatMain;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = opp_life;
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

// MTG_NOWIN_TWIN_EST: a no-win cut ONLY by twin reuses is no refutation (nothing is stored under the order-exact key),
// but it leaves the twin hint, so the next query of the node is served by the same budget gate instead of re-searched.
// The root's own hint from `first` is dropped, so `second`'s first search can only reuse its CHILDREN's twins; its
// second search is then answered at the root by the hint the first one left. Runs under the default, so
// `MTG_NOWIN_TWIN_EST=0 mtg-test` fails it.
TEST_CASE("Twin estimate hints: a node cut only by twin reuses leaves a gated hint")
{
    EnsureCardsLoaded();
    const TurnSolver::TwinWaveProbe r =
        TurnSolver::TwinWaveProbeForTest(Board(false), Board(true), kDepth, kMaxTurns, false, 2000, 0.0, true);
    CHECK(r.win[0] == kMaxTurns + 1);
    CHECK(r.win[1] == kMaxTurns + 1);
    CHECK(r.win[2] == kMaxTurns + 1);
    CHECK(r.units[1] > 0);           // the root itself was searched (its own hint was dropped)...
    CHECK(r.units[1] < r.units[0]);  // ...but its children were twin reuses
    CHECK(r.est_hints >= 1);
    CHECK(r.units[2] == 0);          // answered by the estimate hint at the root
}

TEST_CASE("Twin estimate hints: control -- pinned OFF, the reuse's ancestor is searched again")
{
    EnsureCardsLoaded();
    const ArmScope off(heurarm::NOWIN_TWIN_EST, 0);
    const TurnSolver::TwinWaveProbe r =
        TurnSolver::TwinWaveProbeForTest(Board(false), Board(true), kDepth, kMaxTurns, false, 2000, 0.0, true);
    CHECK(r.est_hints == 0);
    CHECK(r.units[2] > 0);
}

TEST_CASE("Twin estimate hints: never at an unlimited budget")
{
    EnsureCardsLoaded();
    const TurnSolver::TwinWaveProbe r =
        TurnSolver::TwinWaveProbeForTest(Board(false), Board(true), kDepth, kMaxTurns, false, 0, 0.0, true);
    CHECK(r.est_hints == 0);
    CHECK(r.units[1] > 0);    // no gate at an unlimited budget: the twin is searched in full...
    CHECK(r.units[2] == 0);   // ...so its refutation is a PROOF, and the repeat is an exact transposition
}

// MTG_NOWIN_TWIN_HEQ: a hint stored inside an emulated warm-up pass prices its node in the committing leaf's units
// (units + R x leaves). The budget is sized so that the gate is shut for the raw units and open for the priced ones.
TEST_CASE("Twin hint pricing: a warm-up's hint carries the committing leaf's cost")
{
    EnsureCardsLoaded();
    constexpr int kMt = 9;   // a horizon past the depth-2 frontier, so the search has leaves to price
    const TurnSolver::TwinWaveProbe cal =
        TurnSolver::TwinWaveProbeForTest(Board(false, 400), Board(true, 400), kDepth, kMt, false, 0, 0.0, false);
    REQUIRE(cal.win[0] == kMt + 1);
    REQUIRE(cal.first_leaves > 0);
    constexpr double kR = 1000.0;
    // share 0.0005: the gate opens at the root when hint > budget / 2000. Put that threshold midway.
    const long long budget = 2000 * (cal.units[0] + static_cast<long long>(kR / 2) * cal.first_leaves);
    const TurnSolver::TwinWaveProbe raw =
        TurnSolver::TwinWaveProbeForTest(Board(false, 400), Board(true, 400), kDepth, kMt, false, budget, 0.0, false);
    CHECK(raw.units[1] > 0);   // raw units: below the gate, the twin is searched
    const TurnSolver::TwinWaveProbe priced =
        TurnSolver::TwinWaveProbeForTest(Board(false, 400), Board(true, 400), kDepth, kMt, false, budget, kR, false);
    CHECK(priced.units[1] == 0);   // priced: above it, the twin's refutation is reused
    CHECK(priced.win[1] == kMt + 1);
    const ArmScope off(heurarm::NOWIN_TWIN_HEQ, 0);
    const TurnSolver::TwinWaveProbe pinned =
        TurnSolver::TwinWaveProbeForTest(Board(false, 400), Board(true, 400), kDepth, kMt, false, budget, kR, false);
    CHECK(pinned.units[1] > 0);
}

// MTG_NOWIN_TWIN_EST, the WIN half: a search that finds a win while cut only by twin reuses is no proof (the game line
// cache keeps a win only from a complete search), but it is kept as a game-cache WIN estimate, so the next query of the
// node is answered by its line through the gate instead of re-searched. The opponent's life is set so the kill is a
// turn-7 one, inside the depth-2 horizon: the root has a win and its no-win siblings are twins of `first`'s.
TEST_CASE("Twin estimate hints: a win found under twin reuses is kept as a gated game-cache estimate")
{
    EnsureCardsLoaded();
    // Three DECISIONS of one game (a fresh per-decision cache each; the game line cache persists), opponent at 12.
    const TurnSolver::TwinWaveProbe r = TurnSolver::TwinWaveProbeForTest(
        Board(false, 12), Board(true, 12), kDepth, kMaxTurns, false, 2000, 0.0, true, true);
    REQUIRE(r.win[0] <= kMaxTurns);   // a win inside the horizon
    CHECK(r.win[1] == r.win[0]);
    CHECK(r.units[1] > 0);            // searched, its no-win siblings answered by twin reuses...
    CHECK(r.est_wins >= 1);           // ...so the win it found is an estimate, not a proof
    CHECK(r.win[2] == r.win[1]);
    CHECK(r.units[2] == 0);           // the next decision takes the estimate's line through the gate
}

TEST_CASE("Twin estimate hints: control -- pinned OFF, the next decision searches the win again")
{
    EnsureCardsLoaded();
    const ArmScope off(heurarm::NOWIN_TWIN_EST, 0);
    const TurnSolver::TwinWaveProbe r = TurnSolver::TwinWaveProbeForTest(
        Board(false, 12), Board(true, 12), kDepth, kMaxTurns, false, 2000, 0.0, true, true);
    CHECK(r.est_wins == 0);
    CHECK(r.win[2] == r.win[1]);
    CHECK(r.units[2] > 0);
}

// MTG_NOWIN_TWIN_FREE: a SUB-GATE twin re-search (a covering twin hint exists but costs <= share x Remaining, so the
// gate searches this order instead of reusing the twin) hands the units it spent back to its budget: the order-exact
// answer at the canonical hit's price. The budget is sized so the gate stays SHUT at the root (hint well under
// 0.0005 x budget). The lever is default OFF (a measurement), so these pin it.
TEST_CASE("Twin re-search refund: a sub-gate twin re-search is searched and refunded")
{
    EnsureCardsLoaded();
    const TurnSolver::TwinCacheProbe cal =
        TurnSolver::TwinNoWinCacheForTest(Board(false), Board(true), kDepth, kMaxTurns, false);
    REQUIRE(cal.first_units > 0);
    const long long budget = 4000 * cal.first_units;   // gate threshold = 2 x the twin's cost: shut
    {
        const ArmScope off(heurarm::NOWIN_TWIN_FREE, 0);
        const TurnSolver::TwinCacheProbe r =
            TurnSolver::TwinNoWinCacheForTest(Board(false), Board(true), kDepth, kMaxTurns, false, budget);
        CHECK(r.second_win == kMaxTurns + 1);
        CHECK(r.second_units > 0);    // searched and charged
    }
    {
        const ArmScope on(heurarm::NOWIN_TWIN_FREE, 1);
        const TurnSolver::TwinCacheProbe r =
            TurnSolver::TwinNoWinCacheForTest(Board(false), Board(true), kDepth, kMaxTurns, false, budget);
        CHECK(r.second_win == kMaxTurns + 1);   // the same answer: this order's own search ran...
        CHECK(r.second_root_hit == 0);          // ...it was not a cache hit...
        CHECK(r.second_units == 0);             // ...and the budget got its units back
    }
}

TEST_CASE("Twin re-search refund: inert at an unlimited budget")
{
    EnsureCardsLoaded();
    const ArmScope on(heurarm::NOWIN_TWIN_FREE, 1);
    const TurnSolver::TwinCacheProbe r =
        TurnSolver::TwinNoWinCacheForTest(Board(false), Board(true), kDepth, kMaxTurns, false);
    CHECK(r.second_root_hit == 0);
    CHECK(r.second_units > 0);   // every unit metered: no gate, no refund
}

TEST_CASE("Twin re-search refund: never more than the pool")
{
    EnsureCardsLoaded();
    SearchBudget b(1000);
    b.Consume(400);
    CHECK(b.Refund(30, 0.05) == 30);     // pool = 50
    CHECK(b.Refund(30, 0.05) == 20);     // 20 left in the pool
    CHECK(b.Refund(30, 0.05) == 0);
    CHECK(b.Used() == 350);
    SearchBudget u(0);
    u.Consume(10);
    CHECK(u.Refund(5, 0.05) == 0);       // unlimited: nothing refunded
    CHECK(u.Used() == 10);
}
