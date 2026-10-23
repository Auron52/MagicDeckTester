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
    // Skerry -> the Island (rank 10) then the Skerry (11), with or without the lever.
    for (bool on : { true, false })
    {
        CAPTURE(on);
        HoldArm arm(on);
        const GameState b2 = BoardDh({ LandDh("Saprazzan Skerry", 1), LandDh("Island") });
        bool ok = false;
        const GameState end = PayBoth(b2, CostDh(2), false, CostDh(2), &ok);
        CHECK(ok);
        CHECK(TappedDh(end) == std::vector<bool>{ true, true });
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

// MTG_PAY_DEPLETION_HOLD_UNSEEN -- the amendment (Mirrorwing, cross-deck A/B 2026-10-22: 27 of its 29 slower
// games). While the line casts a breakpoint opener the line hold is a LOWER bound, so the hold may not
// spend a colour the depletion land cannot make on a pip the land would have paid. Board of play gi=16230
// T4 (lands only): Fists of Flame {1}{R} -- a cantrip, the line names nothing after it -- off Game Trail,
// a last-counter Sandstone Needle and two Forests. The drawn Ignoble Hierarch + Gold Rush then want
// {G} + {1}{G}: holding the Needle spends Game Trail + a Forest and leaves {R}{R} + one Forest.
namespace
{
struct UnseenArm
{
    std::int8_t prev;
    explicit UnseenArm(bool on) : prev(heurarm::t_arm[heurarm::PAY_DEPLETION_HOLD_UNSEEN])
    { heurarm::t_arm[heurarm::PAY_DEPLETION_HOLD_UNSEEN] = on ? 1 : 0; }
    ~UnseenArm() { heurarm::t_arm[heurarm::PAY_DEPLETION_HOLD_UNSEEN] = prev; }
    UnseenArm(const UnseenArm&)            = delete;
    UnseenArm& operator=(const UnseenArm&) = delete;
};
}   // namespace

TEST_CASE("depletion hold, unseen amendment: a cantrip's {1}{R} spends the Needle, not Game Trail + a Forest")
{
    EnsureCardsDh();
    const GameState board = BoardDh({ LandDh("Game Trail"), LandDh("Sandstone Needle", 1), LandDh("Forest"),
                                      LandDh("Forest") });
    const ManaCost fists = CostDh(1, 0, /*r=*/1);
    HoldArm hold(true);
    for (int arm = 0; arm < 3; ++arm)
    {
        // 0: amendment off, line opens unseen   1: amendment on, line opens unseen   2: on, no opener
        CAPTURE(arm);
        UnseenArm un(arm != 0);
        LineOpensUnseenScope _lou(arm != 2);
        bool ok = false;
        const GameState end = PayBoth(board, fists, /*for_creature=*/false, fists, &ok);
        CHECK(ok);
        const bool needle_spent = end.battlefield[1].tapped;
        const int  green_spent  = (end.battlefield[0].tapped ? 1 : 0) + (end.battlefield[2].tapped ? 1 : 0)
                                + (end.battlefield[3].tapped ? 1 : 0);
        if (arm == 1)
        {   // the Needle's {R}{R} pays the {1}{R}; every green source stays up for the drawn cards
            CHECK(needle_spent);
            CHECK(green_spent == 0);
            CHECK(DepletionCountersOn(end.battlefield[1]) == 0);
        }
        else
        {   // the USER's rule as written: the Needle is held, two green-capable lands pay
            CHECK_FALSE(needle_spent);
            CHECK(green_spent == 2);
            CHECK(DepletionCountersOn(end.battlefield[1]) == 1);
        }
    }
}

TEST_CASE("depletion hold, unseen amendment: a colour-NEUTRAL hold (two Mountains for a Needle) still holds")
{
    EnsureCardsDh();
    const GameState board = BoardDh({ LandDh("Mountain"), LandDh("Sandstone Needle", 1), LandDh("Mountain"),
                                      LandDh("Forest") });
    const ManaCost fists = CostDh(1, 0, /*r=*/1);
    HoldArm hold(true);
    UnseenArm un(true);
    LineOpensUnseenScope _lou(true);
    bool ok = false;
    const GameState end = PayBoth(board, fists, /*for_creature=*/false, fists, &ok);
    CHECK(ok);
    CHECK_FALSE(end.battlefield[1].tapped);                     // the Needle is held...
    CHECK(DepletionCountersOn(end.battlefield[1]) == 1);
    CHECK(end.battlefield[0].tapped);                           // ...the two Mountains pay...
    CHECK(end.battlefield[2].tapped);
    CHECK_FALSE(end.battlefield[3].tapped);                     // ...and the Forest's green stays up
}

// MTG_PAY_DEPLETION_FULL_YIELD -- the USER's refinement (2026-10-22): "I would also spend the depletion first if
// you need both of its mana to avoid using both unnecessarily" -- "primarily for Treasures or other expendable
// resources". {1}{R} off a last-counter Sandstone Needle, a Treasure and a Forest: the hold as written cracks
// the Treasure for the {R} (and taps the Forest) to keep the Needle; with the refinement the Needle's {R}{R}
// pays the whole cost and the Treasure survives. A {R} alone would use only half the Needle: Treasure first.
namespace
{
struct FullYieldArm
{
    std::int8_t prev;
    explicit FullYieldArm(bool on) : prev(heurarm::t_arm[heurarm::PAY_DEPLETION_FULL_YIELD])
    { heurarm::t_arm[heurarm::PAY_DEPLETION_FULL_YIELD] = on ? 1 : 0; }
    ~FullYieldArm() { heurarm::t_arm[heurarm::PAY_DEPLETION_FULL_YIELD] = prev; }
    FullYieldArm(const FullYieldArm&)            = delete;
    FullYieldArm& operator=(const FullYieldArm&) = delete;
};
int CountUntappedDh(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (!p.tapped && p.card.m_name.str() == name) { ++n; } }
    return n;
}
}   // namespace

TEST_CASE("depletion hold, full yield: the Needle's {R}{R} pays a {1}{R} ahead of a Treasure; a {R} does not")
{
    EnsureCardsDh();
    const GameState board = BoardDh({ LandDh("Sandstone Needle", 1), LandDh("Treasure Token"), LandDh("Forest") });
    HoldArm hold(true);
    for (bool fy : { false, true })
    {
        CAPTURE(fy);
        FullYieldArm arm(fy);
        bool ok = false;
        const GameState end = PayBoth(board, CostDh(1, 0, /*r=*/1), false, CostDh(1, 0, 1), &ok);
        CHECK(ok);
        if (fy)
        {
            CHECK(CountUntappedDh(end, "Treasure Token") == 1);    // the expendable source survives
            CHECK(CountUntappedDh(end, "Sandstone Needle") == 0);  // both of the Needle's units used
        }
        else
        {
            CHECK(CountUntappedDh(end, "Treasure Token") == 0);    // the hold as written: Treasure cracked
            CHECK(CountUntappedDh(end, "Sandstone Needle") == 1);
        }
    }
    {   // half the Needle: the Treasure still pays first
        FullYieldArm arm(true);
        bool ok = false;
        const GameState end = PayBoth(board, CostDh(0, 0, /*r=*/1), false, CostDh(0, 0, 1), &ok);
        CHECK(ok);
        CHECK(CountUntappedDh(end, "Sandstone Needle") == 1);
        CHECK(CountUntappedDh(end, "Treasure Token") == 0);
    }
}

// MTG_PAY_DEPLETION_HOLD_RETRY's acceptance test -- the USER's strict dominance: "if it saves tapping a land
// that is strictly better", "Even a mountain or a dual land that taps for red would be better than floating
// red", "Otherwise I would probably keep it as-is, since we might need the colour later."
TEST_CASE("depletion hold retry: taken only when it spends the held land and a strict subset (a Mountain saved)")
{
    const int needle = 54, mountain = 47, forest = 13, trail = 29;
    // first payment: Mountain + the Needle (its second {R} floating); re-payment: the Needle alone -> taken
    CHECK(DepletionRetryDominates({ mountain, needle }, { needle }, needle));
    // a dual saved likewise
    CHECK(DepletionRetryDominates({ trail, needle }, { needle }, needle));
    // the re-payment spends something the first did not (a Forest's green) -> keep the first
    CHECK_FALSE(DepletionRetryDominates({ mountain, needle }, { forest, needle }, needle));
    // the same number of sources -> no gain -> keep the first ("we might need the colour later")
    CHECK_FALSE(DepletionRetryDominates({ mountain, needle }, { mountain, needle }, needle));
    // the re-payment does not even spend the land -> not this retry's shape
    CHECK_FALSE(DepletionRetryDominates({ forest, mountain, needle }, { mountain }, needle));
}

// MTG_PAY_ROLLBACK_DEPLETION -- the USER's roll-back for a held depletion land (2026-10-22): "you might keep up a
// depletion land and then realize after the next breakpoint that you need different colours. In that case you
// roll back to the previous one to spend the depletion land instead." Mirrorwing play gi=16230's shape: the
// hold paid Fists of Flame {1}{R} with Game Trail + Forest and kept the last-counter Sandstone Needle; the
// drawn Gold Rush {1}{G} then finds only the Needle's {R}{R}. Two-pip: both lands come back, the Needle pays.
// One-pip: its second unit floats only in the phase of the payment it replaces.
namespace
{
struct RbArm
{
    std::int8_t prev_rb, prev_dep;
    explicit RbArm(bool dep)
        : prev_rb(heurarm::t_arm[heurarm::PAY_ROLLBACK]), prev_dep(heurarm::t_arm[heurarm::PAY_ROLLBACK_DEPLETION])
    { heurarm::t_arm[heurarm::PAY_ROLLBACK] = 1; heurarm::t_arm[heurarm::PAY_ROLLBACK_DEPLETION] = dep ? 1 : 0; }
    ~RbArm() { heurarm::t_arm[heurarm::PAY_ROLLBACK] = prev_rb; heurarm::t_arm[heurarm::PAY_ROLLBACK_DEPLETION] = prev_dep; }
    RbArm(const RbArm&)            = delete;
    RbArm& operator=(const RbArm&) = delete;
};
ManaCost CostG(int generic, int g)
{
    ManaCost c;
    c.generic = generic; c.green = g;
    return c;
}
}   // namespace

TEST_CASE("depletion rollback: two-pip -- the held Needle takes over Fists' {1}{R}, Game Trail + Forest pay Gold Rush")
{
    EnsureCardsDh();
    for (bool dep : { false, true })
    {
        CAPTURE(dep);
        HoldArm hold(true);
        RbArm rb(dep);
        GameState s = BoardDh({ LandDh("Game Trail"), LandDh("Sandstone Needle", 1), LandDh("Forest") });
        s.phase = Phase::PreCombatMain;
        {
            LineUnpaidCostScope _luc(CostDh(1, 0, /*r=*/1));
            LineOpensUnseenScope _lou(true);
            REQUIRE(TapForCostDirect(s, CostDh(1, 0, 1), false));       // Fists: the hold spends GT + Forest
        }
        CHECK_FALSE(s.battlefield[1].tapped);
        ++s.players[0].cards_drawn_this_turn;                           // Fists draws: the breakpoint
        const bool ok = TapForCostDirect(s, CostG(1, 1), false);        // the drawn Gold Rush
        if (dep)
        {
            CHECK(ok);
            CHECK(DepletionCountersOn(s.battlefield[1]) == 0);          // the Needle paid after all
            CHECK(s.floating_mana.Total() == 0);                        // both of its units used
        }
        else
        {
            CHECK_FALSE(ok);                                            // {R}{R} cannot pay {1}{G}
            CHECK_FALSE(s.battlefield[1].tapped);                       // a failed payment changes nothing
        }
    }
}

TEST_CASE("depletion rollback: one-pip -- the Needle's second unit floats in the same phase, is lost across one")
{
    EnsureCardsDh();
    for (bool cross : { false, true })
    {
        CAPTURE(cross);
        HoldArm hold(true);
        RbArm rb(true);
        GameState s = BoardDh({ LandDh("Game Trail"), LandDh("Sandstone Needle", 1) });
        s.phase = Phase::PreCombatMain;
        REQUIRE(TapForCostDirect(s, CostDh(0, 0, /*r=*/1), false));     // Anger {R}: Game Trail, Needle held
        CHECK_FALSE(s.battlefield[1].tapped);
        ++s.players[0].cards_drawn_this_turn;                             // Anger draws: the breakpoint
        if (cross) { s.phase = Phase::PostCombatMain; s.floating_mana = ManaPool{}; }
        CHECK(TapForCostDirect(s, CostG(0, 1), false));                  // a {G}: Game Trail is freed for it
        CHECK(DepletionCountersOn(s.battlefield[1]) == 0);
        CHECK(s.floating_mana.red == (cross ? 0 : 1));
    }
}

// MTG_PAY_ROLLBACK_FLOATBACK -- mana left floating pays an earlier same-phase pip back (strictly dominant: the
// freed source makes that colour). Mirrorwing d3 gi=14488's shape: Ancestral Anger {R} off Game Trail with both
// Needles held, then the two Needles pay Frontline Heroism {2}{R} and float an {R}; it re-pays the Anger, and
// Game Trail is up for the drawn Fortifying Draught's {G}.
TEST_CASE("depletion rollback: float-back -- a Needle's floating {R} re-pays the Anger, Game Trail comes back")
{
    EnsureCardsDh();
    for (bool fb : { false, true })
    {
        CAPTURE(fb);
        HoldArm hold(true);
        RbArm rb(false);
        const std::int8_t prev_fb = heurarm::t_arm[heurarm::PAY_ROLLBACK_FLOATBACK];
        heurarm::t_arm[heurarm::PAY_ROLLBACK_FLOATBACK] = fb ? 1 : 0;
        GameState s = BoardDh({ LandDh("Game Trail"), LandDh("Sandstone Needle", 1), LandDh("Sandstone Needle", 1) });
        s.phase = Phase::PreCombatMain;
        REQUIRE(TapForCostDirect(s, CostDh(0, 0, /*r=*/1), false));       // Anger: Game Trail, both Needles held
        CHECK(s.battlefield[0].tapped);
        REQUIRE(TapForCostDirect(s, CostDh(2, 0, /*r=*/1), false));       // Heroism: Needle + Needle, {R} over
        CHECK(s.battlefield[0].tapped == !fb);                            // float-back frees Game Trail
        CHECK(s.floating_mana.Total() == (fb ? 0 : 1));
        CHECK(TapForCostDirect(s, CostG(0, 1), false) == fb);             // ...for the drawn Draught's {G}
        heurarm::t_arm[heurarm::PAY_ROLLBACK_FLOATBACK] = prev_fb;
    }
}

// ...and only AFTER a breakpoint (USER: "realize after the next breakpoint that you need different colours").
// Niv seed 1's shape: Tandem Lookout {2}{U} off Skerry + Canal with the last-counter Needle held, then a {U}
// in the same phase with no draw between -- demand the hold already weighed; the Needle stays.
TEST_CASE("depletion rollback: no draw since the earlier pip -> no rescue (the hold's own decision stands)")
{
    EnsureCardsDh();
    HoldArm hold(true);
    RbArm rb(true);
    GameState s = BoardDh({ LandDh("Sandstone Needle", 1), LandDh("Saprazzan Skerry", 2), LandDh("Spirebluff Canal") });
    s.phase = Phase::PreCombatMain;
    s.players[0].cards_drawn_this_turn = 1;                            // the draw step, before both payments
    REQUIRE(TapForCostDirect(s, CostDh(2, /*u=*/1), true));            // Lookout: Skerry + Canal, Needle held
    CHECK_FALSE(s.battlefield[0].tapped);
    CHECK_FALSE(TapForCostDirect(s, CostDh(0, /*u=*/1), false));       // a {U}: no rescue without a breakpoint
    CHECK(DepletionCountersOn(s.battlefield[0]) == 1);
}
