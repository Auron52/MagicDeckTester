// Unit seed for the Tier C executor/rollout unification (backlog C2).
//
// The engine's rules exist twice: the executor (AIEngine) and the search rollout (TurnSolver).
// They must agree exactly, and historically each divergence was a real bug (see
// docs/design/rollout-executor-lockstep.md). These tests pin the CURRENT behaviour of the
// most-duplicated leaf -- mana payment -- as golden values, and assert the two twins produce
// IDENTICAL end-states on fixed boards. That gives the unification a seconds-long faithfulness
// check before it spends 15+ minutes on a smoke digest.
//
// Scope is deliberately tiny (backlog C2): twin units only, not a general suite.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/ManaPayment.h"   // AvailableManaPool: the unified accounting pool (C1 unit 4)
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"   // ResolveHeuristicDefaultsPath: exe-relative path walk

#include <string>
#include <vector>

// Friend of AIEngine (see AIEngine.h): the only sanctioned way for tests to reach the
// executor's private payment path.
struct MtgTestSeam
{
    static bool TapForCost(AIEngine& e, GameState& s, const ManaCost& c, ManaPool& avail,
                           bool for_creature)
    { return e.TapForCost(s, c, avail, for_creature); }
};

namespace
{

// Load the real card database once (same data both twins see in production).
void EnsureCards()
{
    static const bool loaded = []
    {
        const auto path = ResolveHeuristicDefaultsPath("src/cards/data/cards.json");
        CardDatabase::Instance().LoadFromJson(path);
        return true;
    }();
    REQUIRE(loaded);
}

Permanent MakeLand(const std::string& name, bool tapped = false)
{
    const CardDefinition* def = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(def != nullptr, "card not in cards.json: ", name);
    Permanent p;
    p.card              = def->card;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.tapped            = tapped;
    p.entered_this_turn = false;
    return p;
}

GameState MakeBoard(const std::vector<std::string>& lands)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    for (const auto& n : lands) { s.battlefield.push_back(MakeLand(n)); }
    return s;
}

ManaCost Cost(int generic, int w = 0, int u = 0, int b = 0, int r = 0, int g = 0)
{
    ManaCost c;
    c.generic = generic; c.white = w; c.blue = u; c.black = b; c.red = r; c.green = g;
    return c;
}

// Everything a payment may touch, flattened for equality checks.
struct PayEnd
{
    bool              ok = false;
    std::vector<bool> tapped;
    int               floating_total = 0;
    int               life0 = 0, life1 = 0;
    bool              opp_lost = false;

    bool operator==(const PayEnd& o) const
    {
        return ok == o.ok && tapped == o.tapped && floating_total == o.floating_total
            && life0 == o.life0 && life1 == o.life1 && opp_lost == o.opp_lost;
    }
};

PayEnd Snapshot(const GameState& s, bool ok)
{
    PayEnd e;
    e.ok = ok;
    for (const Permanent& p : s.battlefield) { e.tapped.push_back(p.tapped); }
    e.floating_total = s.floating_mana.Total();
    e.life0 = s.players[0].life;
    e.life1 = s.players[1].life;
    e.opp_lost = s.opponent_lost_life_this_turn;
    return e;
}

// Run the EXECUTOR twin (AIEngine::TapForCost) on a copy of the board.
PayEnd RunExecutor(const GameState& board, const ManaCost& cost, bool for_creature)
{
    GameState s = board;
    AIEngine  eng;
    ManaPool  avail = AvailableManaPool(s);
    const bool ok = MtgTestSeam::TapForCost(eng, s, cost, avail, for_creature);
    return Snapshot(s, ok);
}

// Run the ROLLOUT twin (TapForCostDirect, via the C2 test seam) on a copy of the board.
PayEnd RunRollout(const GameState& board, const ManaCost& cost, bool for_creature)
{
    GameState s = board;
    const bool ok = TapForCostDirect(s, cost, for_creature);
    return Snapshot(s, ok);
}

// One cast of `name`, carrying the card's real cost (hybrid metadata included -- ColorFeasibility
// un-bakes those, so Deathrite Shaman must arrive as a {B/G} pip and not as flat black).
Action CastOf(const std::string& name, const GameState& s)
{
    const CardDefinition* def = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(def != nullptr, "card not in cards.json: ", name);
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.cost           = EffectiveSpellCost(*def, s);
    a.is_noncreature = !def->card.IsCreature();
    return a;
}

// "Would the colour-exact gate admit this set of casts off this board?"
bool ColorExactAdmits(const GameState& board, const std::vector<std::string>& casts)
{
    const ColorFeasibility feas = BuildColorFeasibility(board);
    REQUIRE(feas.usable);            // the fixtures below all hold a multi-colour source
    std::vector<Action> cands;
    std::vector<int>    sel;
    for (const std::string& n : casts)
    { sel.push_back(static_cast<int>(cands.size())); cands.push_back(CastOf(n, board)); }
    return feas.Payable(cands, sel, ManaPool{});
}

// The same question for a hand-built cost, against either the full pool or the NON-CREATURE one.
bool ColorExactAdmitsCost(const GameState& board, const ManaCost& cost, bool noncreature)
{
    const ColorFeasibility feas = BuildColorFeasibility(board, noncreature);
    REQUIRE(feas.usable);
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.cost           = cost;
    a.is_noncreature = noncreature;
    const std::vector<Action> cands{a};
    const std::vector<int>    sel{0};
    return feas.Payable(cands, sel, ManaPool{}, noncreature);
}

// The combined cost of those same casts, as the enumerator sums it.
ManaCost CombinedCost(const GameState& board, const std::vector<std::string>& casts)
{
    ManaCost c;
    for (const std::string& n : casts)
    {
        const ManaCost one = CastOf(n, board).cost;
        c.generic += one.generic; c.white += one.white; c.blue  += one.blue;
        c.black   += one.black;   c.red   += one.red;   c.green += one.green;
        c.colorless += one.colorless;
    }
    return c;
}

void CheckTwinsAgree(const GameState& board, const ManaCost& cost, bool for_creature,
                     bool expect_ok)
{
    const PayEnd ex = RunExecutor(board, cost, for_creature);
    const PayEnd ro = RunRollout(board, cost, for_creature);
    CAPTURE(cost.ToString());
    CHECK(ex.ok == expect_ok);
    CHECK(ro.ok == expect_ok);
    CHECK(ex == ro);
}

} // namespace

TEST_CASE("unpayable cost: both twins fail and leave the board untouched")
{
    EnsureCards();
    const GameState board = MakeBoard({"Mountain"});
    const PayEnd before = Snapshot(board, false);

    const PayEnd ex = RunExecutor(board, Cost(2, 0, 0, 0, 1), false);
    const PayEnd ro = RunRollout(board, Cost(2, 0, 0, 0, 1), false);
    CHECK(ex == before);   // failed payment must not tap anything
    CHECK(ro == before);
}

TEST_CASE("basic payment: {1}{R} from three Mountains")
{
    EnsureCards();
    const GameState board = MakeBoard({"Mountain", "Mountain", "Mountain"});
    CheckTwinsAgree(board, Cost(1, 0, 0, 0, 1), false, true);

    // Exactly two lands tapped, none left floating.
    const PayEnd ex = RunExecutor(board, Cost(1, 0, 0, 0, 1), false);
    int tapped = 0;
    for (bool t : ex.tapped) { tapped += t ? 1 : 0; }
    CHECK(tapped == 2);
    CHECK(ex.floating_total == 0);
}

TEST_CASE("colour selection: {R}{W} from Mountain + 2 Plains")
{
    EnsureCards();
    const GameState board = MakeBoard({"Mountain", "Plains", "Plains"});
    CheckTwinsAgree(board, Cost(0, /*w=*/1, 0, 0, /*r=*/1), false, true);
    CheckTwinsAgree(board, Cost(0, /*w=*/2, 0, 0, /*r=*/1), false, true);
    CheckTwinsAgree(board, Cost(0, /*w=*/0, 0, 0, /*r=*/2), false, false);  // only one red source
}

TEST_CASE("creature-only source: Ancient Ziggurat pays creatures, never non-creatures")
{
    EnsureCards();
    const GameState board = MakeBoard({"Ancient Ziggurat"});
    CheckTwinsAgree(board, Cost(1), /*for_creature=*/true,  true);
    CheckTwinsAgree(board, Cost(1), /*for_creature=*/false, false);
}

TEST_CASE("turn-scoped reserve (floating ritual mana) drains before tapping")
{
    EnsureCards();
    GameState board = MakeBoard({"Mountain"});
    board.floating_mana.red = 2;   // as if a ritual resolved earlier this main
    CheckTwinsAgree(board, Cost(0, 0, 0, 0, /*r=*/3), false, true);   // 2 floating + 1 tap
    CheckTwinsAgree(board, Cost(0, 0, 0, 0, /*r=*/4), false, false);  // 1 short

    const PayEnd ex = RunExecutor(board, Cost(0, 0, 0, 0, 3), false);
    CHECK(ex.floating_total == 0);       // reserve consumed
    CHECK(ex.tapped == std::vector<bool>{true});
}

// ---- Colour-exact subset affordability (MTG_COLOR_EXACT) --------------------------------------
// The flat accounting pool books every multi-colour source as one `wild` that pays ANY pip, so it
// admits subsets the board cannot pay and the executor silently drops a cast. These pin the exact
// board that surfaced it (Anti-Lifegain gi=697 turn 5) and the two ways the gate must NOT overreach.

TEST_CASE("colour-exact: two white pips off one white source is rejected, one is not")
{
    EnsureCards();
    // The lever is read once into a function-local static; set it before the first call.
    setenv("MTG_COLOR_EXACT", "1", 1);
    const GameState board = MakeBoard({"Godless Shrine", "Grove of the Burnwillows",
                                       "Ignoble Hierarch", "Forest"});

    // What the flat pool sees: green 1 + wild 3. It says the pair is affordable...
    const ManaPool pool = AvailableManaPool(board);
    CHECK(pool.wild == 3);
    CHECK(pool.CanPay(CombinedCost(board, {"Fiery Justice", "Enlightened Tutor"})));

    // ... but only Godless Shrine makes white, and the pair needs two white pips.
    CHECK_FALSE(ColorExactAdmits(board, {"Fiery Justice", "Enlightened Tutor"}));

    // Each on its own is genuinely payable and must survive.
    CHECK(ColorExactAdmits(board, {"Fiery Justice"}));
    CHECK(ColorExactAdmits(board, {"Enlightened Tutor"}));
}

TEST_CASE("colour-exact: hybrid pips are un-baked, not demanded in their first colour")
{
    EnsureCards();
    setenv("MTG_COLOR_EXACT", "1", 1);
    // {B/G}{B/G} with no black source at all: payable green, and the gate must say so. Reading the
    // flat pips alone would demand BLACK twice and reject it (the pip is stored in its first colour).
    const GameState board = MakeBoard({"Breeding Pool", "Forest"});
    CHECK(ColorExactAdmits(board, {"Deathrite Shaman", "Deathrite Shaman"}));
    // Three copies off two sources is short on COUNT, whichever colour pays.
    CHECK_FALSE(ColorExactAdmits(board, {"Deathrite Shaman", "Deathrite Shaman",
                                         "Deathrite Shaman"}));
}

TEST_CASE("colour-exact: a filter land is credited its GROSS yield, in its own colours only")
{
    EnsureCards();
    setenv("MTG_COLOR_EXACT", "1", 1);
    // Cascade Bluffs turns one {U} into {R}{R}. The flat pool books that as one net wild, which loses
    // the fact that it can make two red -- and also the fact that it can never make WHITE. Credit the
    // gross in its own colours: permissive on count (so a real filter chain is never rejected), exact
    // on colour (so the white shortfall is still caught).
    const GameState board = MakeBoard({"Cascade Bluffs", "Island"});
    CHECK(BuildColorFeasibility(board).usable);
    // Feed the Island's {U} into the Bluffs for {R}{R}: genuinely payable, must not be rejected.
    CHECK(ColorExactAdmitsCost(board, Cost(0, 0, 0, 0, /*r=*/2), false));

    // ... but the Bluffs makes no white, so a second white pip has only one source to come from.
    const GameState wboard = MakeBoard({"Cascade Bluffs", "Island", "Godless Shrine"});
    CHECK(ColorExactAdmitsCost(wboard, Cost(0, /*w=*/1), false));
    CHECK_FALSE(ColorExactAdmitsCost(wboard, Cost(0, /*w=*/2), false));
}

TEST_CASE("colour-exact: the NON-CREATURE pool is gated too (creature-only sources dropped)")
{
    EnsureCards();
    setenv("MTG_COLOR_EXACT", "1", 1);
    // Ancient Ziggurat makes any colour but only for creature spells. Two white pips on a NONCREATURE
    // spell may therefore draw on Godless Shrine alone, even though the full pool holds two
    // white-capable sources -- the phantom the flat eff_nc.CanPay admits (wild 2 covers deficit 2).
    const GameState board = MakeBoard({"Ancient Ziggurat", "Godless Shrine", "Breeding Pool"});
    ManaPool flat_nc; flat_nc.wild = 2;                     // Shrine + Breeding Pool, Ziggurat dropped
    CHECK(flat_nc.CanPay(Cost(0, /*w=*/2)));                // the flat pool says {W}{W} is affordable

    CHECK(ColorExactAdmitsCost(board, Cost(0, /*w=*/2), /*noncreature=*/true)  == false);
    // A creature spell may use the Ziggurat, so the same cost is fine against the full pool.
    CHECK(ColorExactAdmitsCost(board, Cost(0, /*w=*/2), /*noncreature=*/false) == true);
}

TEST_CASE("Karoo colour identity: Azorius Chancery makes ONE blue, not two")
{
    EnsureCards();
    // "{T}: Add {W}{U}" is a fixed bundle -- one white and one blue -- not two mana of either. A
    // board whose only blue source is a Chancery therefore cannot pay {1}{U}{U}.
    const GameState board = MakeBoard({"Azorius Chancery", "Forest", "Stomping Ground"});
    CheckTwinsAgree(board, Cost(/*generic=*/1, 0, /*u=*/2), false, /*expect_ok=*/false);
    // The bundle's own colours are of course payable, and so is one blue plus generic.
    CheckTwinsAgree(board, Cost(0, /*w=*/1, /*u=*/1), false, true);
    CheckTwinsAgree(board, Cost(/*generic=*/2, 0, /*u=*/1), false, true);

    // The live case this was found on: mirrorwing_smoke_d3_s1001 gi10 turn 5. Board is four Forests
    // and one Gruul Turf ({R}{G}), i.e. exactly ONE red -- and the engine cast Mirrorwing Dragon
    // {3}{R}{R} off it and won a turn earlier for the trouble. The GT it beat was inflated by an
    // illegal payment, which is why fixing this makes the suite metric WORSE on Karoo decks.
    const GameState mw = MakeBoard({"Gruul Turf", "Forest", "Forest", "Forest", "Forest"});
    CheckTwinsAgree(mw, Cost(/*generic=*/3, 0, 0, 0, /*r=*/2), false, /*expect_ok=*/false);
    CheckTwinsAgree(mw, Cost(/*generic=*/3, 0, 0, 0, /*r=*/1), false, true);   // one red is fine
}

TEST_CASE("twin equivalence matrix: mixed board, sweep of costs")
{
    EnsureCards();
    const GameState board =
        MakeBoard({"Mountain", "Mountain", "Plains", "Plains", "Ancient Ziggurat"});
    for (int generic = 0; generic <= 5; ++generic)
    for (int r = 0; r <= 2; ++r)
    for (int w = 0; w <= 2; ++w)
    for (int fc = 0; fc <= 1; ++fc)
    {
        const ManaCost cost = Cost(generic, w, 0, 0, r);
        if (cost.ManaValue() == 0) { continue; }
        const PayEnd ex = RunExecutor(board, cost, fc != 0);
        const PayEnd ro = RunRollout(board, cost, fc != 0);
        CAPTURE(cost.ToString()); CAPTURE(fc);
        CHECK(ex == ro);
    }
}
