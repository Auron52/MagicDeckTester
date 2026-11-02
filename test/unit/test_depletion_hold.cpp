// MTG_PAY_DEPLETION_HOLD -- the USER's depletion-land rule (2026-10-22, NivMizzetCombo seeds 1 and 6):
//   "The smartest rule for depletion lands would hold the depletion land if it can do so and use it
//    otherwise. At least, it should hold it over non-creature source that are not consumed."
//
// Pinned here, every payment through BOTH twins (the executor's AIEngine::TapForCost and the rollout's
// TapForCostDirect -- one shared payer, so they must agree):
//   * seed 1 ({2}{U} Tandem Lookout off a last-counter Sandstone Needle, a 2-counter Saprazzan Skerry and
//     Spirebluff Canal): HOLD keeps the Needle and pays Skerry + Canal; OFF is today's Skerry + Needle;
//   * the seed-6 shape (a {U} cantrip then {R}{R} in the same line off a last-counter Skerry, the Canal and
//     a Mountain): the Skerry IS spent -- holding it would leave the {R}{R} one red short -- and the line
//     pays; with the line's {R}{R} hidden from the payer, the same board holds the Skerry, the proof that
//     the rest of the line is what decides;
//   * colour-aware: same board and first cast, a later {R} instead of {R}{R} -> the Skerry is held;
//   * the only source of a colour is used (an order, never a filter), a land the payment cannot do without
//     keeps today's order, and a filter whose only feeder is the depletion land is not supply;
//   * two depletion lands and a dual: hold the last counter, spend the other land (not the dual AND it);
//   * the whole-turn prepay's PARTIAL rung: {Tandem Lookout, Brainstorm} keeps the Needle where the
//     all-or-nothing ladder spent it;
//   * flag OFF reproduces the historical order on every board above.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsDh()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// The lever for one scope, restored on exit (heurarm overrides the process-wide env default).
struct HoldArm
{
    std::int8_t prev;
    explicit HoldArm(bool on) : prev(heurarm::t_arm[heurarm::PAY_DEPLETION_HOLD])
    { heurarm::t_arm[heurarm::PAY_DEPLETION_HOLD] = on ? 1 : 0; }
    ~HoldArm() { heurarm::t_arm[heurarm::PAY_DEPLETION_HOLD] = prev; }
    HoldArm(const HoldArm&)            = delete;
    HoldArm& operator=(const HoldArm&) = delete;
};

Permanent LandDh(const std::string& name, int depletion = 0)
{
    const CardDefinition* def = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(def != nullptr, "card not in cards.json: ", name);
    Permanent p;
    p.card              = def->card;
    p.card.m_number     = 0;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = false;
    if (depletion > 0)
    {
        Counter c; c.type = Counter::Type::Depletion; c.count = depletion;
        p.counters.Add(c);
    }
    return p;
}

GameState BoardDh(const std::vector<Permanent>& perms)
{
    EnsureCardsDh();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    int num = 40;
    for (Permanent p : perms) { p.card.m_number = num++; s.battlefield.push_back(p); }
    return s;
}

ManaCost CostDh(int generic, int u = 0, int r = 0)
{
    ManaCost c;
    c.generic = generic; c.blue = u; c.red = r;
    return c;
}

std::vector<bool> TappedDh(const GameState& s)
{
    std::vector<bool> t;
    for (const Permanent& p : s.battlefield) { t.push_back(p.tapped); }
    return t;
}

// One payment of `cost` (inside a line whose remaining bill, THIS payment included, is `line`) through
// the executor twin and the rollout twin. Both must succeed or fail together and tap the same sources;
// returns the rollout's end board.
GameState PayBoth(const GameState& board, const ManaCost& cost, bool for_creature, const ManaCost& line,
                  bool* ok)
{
    GameState ex = board;
    GameState ro = board;
    bool ok_ex = false, ok_ro = false;
    {
        LineUnpaidCostScope _luc(line);
        AIEngine eng;
        ManaPool avail = AvailableManaPool(ex);
        ok_ex = MtgTestSeam::TapForCost(eng, ex, cost, avail, for_creature);
    }
    {
        LineUnpaidCostScope _luc(line);
        ok_ro = TapForCostDirect(ro, cost, for_creature);
    }
    CHECK(ok_ex == ok_ro);
    CHECK(TappedDh(ex) == TappedDh(ro));
    *ok = ok_ro;
    return ro;
}

// A two-cast line, paid cast by cast as the apply worlds do when the prepay declines: the line hold
// carries both casts while the first pays, then only the second.
bool PayLineDh(GameState& s, const ManaCost& first, const ManaCost& second)
{
    ManaCost line = first;
    AddManaCost(line, second);
    LineUnpaidCostScope _luc(line);
    if (!TapForCostDirect(s, first, false)) { return false; }
    SubManaCost(g_line_unpaid_cost, first);
    return TapForCostDirect(s, second, false);
}

Action CastDh(const std::string& name, int hand_index)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE(d != nullptr);
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = d->card.m_name;
    a.def            = d;
    a.cost           = d->card.m_mana_cost;
    a.is_noncreature = !d->card.IsCreature();
    a.card_mv        = d->card.m_mana_cost.ManaValue();
    a.hand_index     = hand_index;
    return a;
}

}   // namespace

TEST_CASE("depletion hold: Niv seed 1 -- {2}{U} keeps the last-counter Needle and pays Skerry + Canal")
{
    EnsureCardsDh();
    const GameState board = BoardDh({ LandDh("Sandstone Needle", 1), LandDh("Saprazzan Skerry", 2),
                                      LandDh("Spirebluff Canal") });
    const ManaCost lookout = CostDh(2, /*u=*/1);
    {
        HoldArm arm(true);
        bool ok = false;
        const GameState end = PayBoth(board, lookout, /*for_creature=*/true, lookout, &ok);
        CHECK(ok);
        CHECK(TappedDh(end) == std::vector<bool>{ false, true, true });   // Needle held; Skerry + Canal pay
        CHECK(DepletionCountersOn(end.battlefield[0]) == 1);               // ...and keeps its last counter
    }
    {
        HoldArm arm(false);   // today's order: Skerry {U}{U} for the {U}, the Needle's {R}{R} for a generic
        bool ok = false;
        const GameState end = PayBoth(board, lookout, /*for_creature=*/true, lookout, &ok);
        CHECK(ok);
        CHECK(TappedDh(end) == std::vector<bool>{ true, true, false });
        CHECK(DepletionCountersOn(end.battlefield[0]) == 0);
    }
}

TEST_CASE("depletion hold: seed-6 shape -- the Skerry IS spent when holding it leaves the line's {R}{R} short")
{
    EnsureCardsDh();
    const GameState board = BoardDh({ LandDh("Saprazzan Skerry", 1), LandDh("Spirebluff Canal"),
                                      LandDh("Mountain") });
    for (bool on : { true, false })
    {
        CAPTURE(on);
        HoldArm arm(on);
        GameState s = board;
        // Brainstorm {U}, then a {R}{R} cast in the same line.
        CHECK(PayLineDh(s, CostDh(0, /*u=*/1), CostDh(0, 0, /*r=*/2)));
        CHECK(TappedDh(s) == std::vector<bool>{ true, true, true });
        CHECK(DepletionCountersOn(s.battlefield[0]) == 0);   // the Skerry paid the {U}
    }
    // The same first cast with nothing after it in the line: now the Skerry CAN be held, and is.
    {
        HoldArm arm(true);
        bool ok = false;
        const GameState end = PayBoth(board, CostDh(0, 1), false, CostDh(0, 1), &ok);
        CHECK(ok);
        CHECK(TappedDh(end) == std::vector<bool>{ false, true, false });   // the Canal pays the {U}
    }
    // ...which is exactly what a hold blind to the rest of the line would have done to the {R}{R}: with the
    // later cast hidden from the payer the Canal pays the {U}, and the {R}{R} is then one red short.
    {
        HoldArm arm(true);
        GameState s = board;
        {
            LineUnpaidCostScope _luc(CostDh(0, 1));
            CHECK(TapForCostDirect(s, CostDh(0, 1), false));
        }
        CHECK(TappedDh(s) == std::vector<bool>{ false, true, false });
        CHECK_FALSE(TapForCostDirect(s, CostDh(0, 0, 2), false));
    }
}

TEST_CASE("depletion hold: colour-aware -- a later {R} lets the Skerry be held, a later {R}{R} does not")
{
    EnsureCardsDh();
    const GameState board = BoardDh({ LandDh("Saprazzan Skerry", 1), LandDh("Spirebluff Canal"),
                                      LandDh("Mountain") });
    HoldArm arm(true);
    GameState s = board;
    CHECK(PayLineDh(s, CostDh(0, /*u=*/1), CostDh(0, 0, /*r=*/1)));
    CHECK(TappedDh(s) == std::vector<bool>{ false, true, true });   // Canal {U}, Mountain {R}; Skerry held
    CHECK(DepletionCountersOn(s.battlefield[0]) == 1);
}

TEST_CASE("depletion hold: the only source of a colour is still used (an order, never a filter)")
{
    EnsureCardsDh();
    const GameState board = BoardDh({ LandDh("Saprazzan Skerry", 2), LandDh("Mountain") });
    for (bool on : { true, false })
    {
        CAPTURE(on);
        HoldArm arm(on);
        bool ok = false;
        // {1}{U}: the Skerry is the only blue; its second {U} pays the generic and the Mountain stays up.
        const GameState end = PayBoth(board, CostDh(1, /*u=*/1), false, CostDh(1, 1), &ok);
        CHECK(ok);
        CHECK(TappedDh(end) == std::vector<bool>{ true, false });
    }
    // A land the payment cannot do without keeps today's order exactly: {2} with only an Island beside the
    // Skerry -> the Island (rank 10) then the Skerry (11), with or without the lever -- and a {U} floats away.
    // The per-payment refine (MTG_PAY_REFINE, pinned on here) then gives the Island back: the Skerry's {U}{U}
    // pays the {2} alone. (The Skerry is Niv-Mizzet's, a deck with no same-phase untapper: stamped so. With one,
    // the refine keeps the floated {U} -- GameState::deck_has_mana_untapper.)
    for (int refine : { 0, 1 })
    for (bool on : { true, false })
    {
        CAPTURE(refine);
        CAPTURE(on);
        HoldArm arm(on);
        heurarm::t_arm[heurarm::PAY_REFINE] = static_cast<std::int8_t>(refine);
        GameState b2 = BoardDh({ LandDh("Saprazzan Skerry", 1), LandDh("Island") });
        b2.deck_has_mana_untapper = false;
        bool ok = false;
        const GameState end = PayBoth(b2, CostDh(2), false, CostDh(2), &ok);
        heurarm::t_arm[heurarm::PAY_REFINE] = -1;
        CHECK(ok);
        CHECK(TappedDh(end) == std::vector<bool>{ true, refine == 0 });
    }
}

TEST_CASE("depletion hold: a filter's colour is supply only while something ELSE can feed it")
{
    EnsureCardsDh();
    auto pay = [](const GameState& b, const ManaCost& c, bool on) -> GameState
    {
        HoldArm arm(on);
        bool ok = false;
        const GameState end = PayBoth(b, c, false, c, &ok);
        CHECK(ok);
        return end;
    };
    // {U} off a last-counter Skerry and Cascade Bluffs: the Bluffs' only feeder IS the Skerry, so the Bluffs
    // is no blue without it -- not held, and the payment is exactly today's (whatever the feed reroute does).
    {
        const GameState b = BoardDh({ LandDh("Saprazzan Skerry", 1), LandDh("Cascade Bluffs") });
        const GameState on = pay(b, CostDh(0, 1), true);
        const GameState off = pay(b, CostDh(0, 1), false);
        CHECK(TappedDh(on) == TappedDh(off));
        CHECK(DepletionCountersOn(on.battlefield[0]) == 0);
    }
    // Add a Mountain: it feeds the Bluffs ({R} -> {U}{U}), so the Skerry's last counter is held.
    {
        const GameState b = BoardDh({ LandDh("Saprazzan Skerry", 1), LandDh("Cascade Bluffs"), LandDh("Mountain") });
        const GameState on = pay(b, CostDh(0, 1), true);
        CHECK(TappedDh(on) == std::vector<bool>{ false, true, true });
        CHECK(DepletionCountersOn(on.battlefield[0]) == 1);
        const GameState off = pay(b, CostDh(0, 1), false);
        CHECK(DepletionCountersOn(off.battlefield[0]) == 0);   // today: the Skerry (11) beats the filter (25)
    }
    // A generic pip: the Bluffs' free {C} is supply on its own, so {1} holds the Skerry.
    {
        const GameState b = BoardDh({ LandDh("Saprazzan Skerry", 1), LandDh("Cascade Bluffs") });
        CHECK(TappedDh(pay(b, CostDh(1), true))  == std::vector<bool>{ false, true });
        CHECK(TappedDh(pay(b, CostDh(1), false)) == std::vector<bool>{ true, false });
    }
}

TEST_CASE("depletion hold: two depletion lands, one dual -- hold the last counter, spend the other land first")
{
    EnsureCardsDh();
    // {2}: either land alone pays it; both cannot be held. The set holds the Needle (its last counter) and
    // leaves the Skerry at its old rank, so the Skerry pays exactly and the Canal stays up -- holding BOTH
    // would tap the Canal AND then the Skerry anyway.
    const GameState board = BoardDh({ LandDh("Sandstone Needle", 1), LandDh("Saprazzan Skerry", 2),
                                      LandDh("Spirebluff Canal") });
    HoldArm arm(true);
    bool ok = false;
    const GameState end = PayBoth(board, CostDh(2), false, CostDh(2), &ok);
    CHECK(ok);
    CHECK(TappedDh(end) == std::vector<bool>{ false, true, false });
}

TEST_CASE("depletion hold: the whole-turn prepay's partial rung keeps the Needle")
{
    EnsureCardsDh();
    // {Tandem Lookout {2}{U}, Brainstorm {U}} = {2}{U}{U} off Needle (1 counter), Skerry (2), Canal, Island.
    // Holding BOTH depletion lands leaves two mana for four; the all-or-nothing ladder then released both
    // and the joint solve (battlefield order) tapped the Needle and the Skerry. The partial rung holds the
    // Needle: Skerry {U}{U} + Canal + Island pay the bill.
    auto run = [](bool on) -> GameState
    {
        HoldArm arm(on);
        GameState s = BoardDh({ LandDh("Sandstone Needle", 1), LandDh("Saprazzan Skerry", 2),
                                LandDh("Spirebluff Canal"), LandDh("Island") });
        Card c1 = CardDatabase::Instance().Lookup("Tandem Lookout")->card; c1.m_number = 1;
        Card c2 = CardDatabase::Instance().Lookup("Brainstorm")->card;     c2.m_number = 2;
        s.players[0].hand = { c1, c2 };
        const std::vector<Action> line = { CastDh("Tandem Lookout", 0), CastDh("Brainstorm", 1) };
        CHECK(TurnSolver::BatchPrepayMainCasts(s, line));
        return s;
    };
    const GameState on = run(true);
    CHECK(TappedDh(on) == std::vector<bool>{ false, true, true, true });
    CHECK(DepletionCountersOn(on.battlefield[0]) == 1);
    const GameState off = run(false);
    CHECK(TappedDh(off) == std::vector<bool>{ true, true, false, false });
}
