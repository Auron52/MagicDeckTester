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
#include "ai/HeuristicArm.h"  // heurarm::t_arm -- per-test lever control (env reads are static)
#include "ai/ManaPayment.h"   // AvailableManaPool: the unified accounting pool (C1 unit 4)
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/EnvFlags.h"            // EnvPut -- portable setenv (MSVC has only _putenv_s)
#include "core/SpellEffects.h"        // CreatureAbilityPayScope (D12 payment context)
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
    EnvPut("MTG_COLOR_EXACT", "1", /*overwrite=*/true);
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
    EnvPut("MTG_COLOR_EXACT", "1", /*overwrite=*/true);
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
    EnvPut("MTG_COLOR_EXACT", "1", /*overwrite=*/true);
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
    EnvPut("MTG_COLOR_EXACT", "1", /*overwrite=*/true);
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

TEST_CASE("ramp filter: FLOATING mana feeds it, so the pool credits what the payer can really make")
{
    EnsureCards();
    // Ferrous Lake is "{1},{T}: Add {U}{R}" -- a filter that needs a mana INPUT. The payment path
    // has always spent floating mana on that {1} ("use floating if any, else feed one mana from a
    // non-ramp source"), but AddSourceToPool only looked for another untapped PERMANENT, so on a
    // board whose sole feeder was the floating reserve the pool credited the Lake ZERO and every
    // cast needing it was pruned before enumeration. Found from a saved viewer artifact
    // (treasure_hunt s3/gi2 T3, verdict legal_not_enumerated): floating {R} + an untapped Ferrous
    // Lake casts Treasure Hunt {1}{U}, and none of the 24 enumerated plans contained it.
    GameState fed = MakeBoard({"Ferrous Lake"});
    fed.floating_mana.Add(Color::Red, 1);
    // NET accounting, so this cannot double-count: the floating {R} is added once by the caller and
    // the Lake contributes only its +1 net. Two total is exactly what the board makes -- spend the
    // {R}, receive {U}{R}.
    CHECK(AvailableManaPool(fed).Total() == 2);

    // And the pool is not merely optimistic here: both payment twins really can cast {1}{U} off it.
    CheckTwinsAgree(fed, Cost(1, 0, /*u=*/1), false, true);

    // With NOTHING to feed it the Lake still credits zero -- it has no free mode, so this stays a
    // credit for a real conversion rather than a phantom mana.
    const GameState starved = MakeBoard({"Ferrous Lake"});
    CHECK(AvailableManaPool(starved).Total() == 0);
    CheckTwinsAgree(starved, Cost(1, 0, /*u=*/1), false, false);

    // A PERMANENT feeder is the case that always worked; pin it beside the new one so a future
    // refactor cannot fix one by breaking the other.
    const GameState land_fed = MakeBoard({"Ferrous Lake", "Island"});
    CHECK(AvailableManaPool(land_fed).Total() == 2);
    CheckTwinsAgree(land_fed, Cost(1, 0, /*u=*/1), false, true);
}

TEST_CASE("depletion tap order: plain land first, then the depletion land with MORE counters")
{
    EnsureCards();
    // A depletion land's tap SPENDS a counter (finite; the last one sacrifices the land), so
    // (1) it ranks one slot past its plain-tier peers and (2) among depletion lands the fresher
    // copy taps first, preserving per-turn burst (see DepletionTapOrderEnabled). Motivating board:
    // treasure_hunt seed 8 T5, where Fiery Islet's sac-to-draw {1} killed a last-counter Saprazzan
    // Skerry over a fresh Island on a battlefield-order tie. Under MTG_NO_DEPLETION_TAP_ORDER=1
    // (whole-binary A/B hatch; the readers cache, so it cannot be toggled in-process) the tapped
    // patterns below revert to first-in-battlefield-order.
    auto with_dep = [](Permanent p, int n)
    {
        Counter c; c.type = Counter::Type::Depletion; c.count = n;
        p.counters.push_back(c);
        return p;
    };

    // (1) TIER: {1} on [Skerry(1 counter), Island] taps the ISLAND -- exact, keeps the Skerry's
    // last counter -- even though the Skerry sits earlier on the battlefield.
    {
        GameState s;
        s.active_player_index = 0;
        s.turn_number         = 3;
        s.battlefield.push_back(with_dep(MakeLand("Saprazzan Skerry"), 1));
        s.battlefield.push_back(MakeLand("Island"));
        const PayEnd ex = RunExecutor(s, Cost(1), false);
        const PayEnd ro = RunRollout(s, Cost(1), false);
        CHECK(ex.ok); CHECK(ro.ok); CHECK(ex == ro);
        CHECK(ex.tapped == std::vector<bool>{false, true});   // Island pays; Skerry spared
        CHECK(ex.floating_total == 0);                         // and nothing over-produced
    }

    // (2) TIEBREAK: two Skerries, the battlefield-earlier one on its LAST counter. {1}{U} taps the
    // FRESHER copy (2 counters): both lands stay alive = two taps available next turn, where the
    // old first-in-order winner died for the same total mana.
    {
        GameState s;
        s.active_player_index = 0;
        s.turn_number         = 3;
        s.battlefield.push_back(with_dep(MakeLand("Saprazzan Skerry"), 1));
        s.battlefield.push_back(with_dep(MakeLand("Saprazzan Skerry"), 2));
        const PayEnd ex = RunExecutor(s, Cost(1, 0, /*u=*/1), false);
        const PayEnd ro = RunRollout(s, Cost(1, 0, /*u=*/1), false);
        CHECK(ex.ok); CHECK(ro.ok); CHECK(ex == ro);
        CHECK(ex.tapped == std::vector<bool>{false, true});   // the 2-counter copy pays
    }

    // (3) The nudge is ORDERING, not exclusion: a cost only the depletion land can pay still taps it.
    {
        GameState s;
        s.active_player_index = 0;
        s.turn_number         = 3;
        s.battlefield.push_back(with_dep(MakeLand("Saprazzan Skerry"), 2));
        s.battlefield.push_back(MakeLand("Mountain"));
        const PayEnd ex = RunExecutor(s, Cost(0, 0, /*u=*/1), false);
        const PayEnd ro = RunRollout(s, Cost(0, 0, /*u=*/1), false);
        CHECK(ex.ok); CHECK(ro.ok); CHECK(ex == ro);
        CHECK(ex.tapped == std::vector<bool>{true, false});   // only the Skerry makes {U}
    }
}

TEST_CASE("filter {C} mode pays a generic pip first in HUMAN play; the search keeps its order")
{
    EnsureCards();
    // USER 2026-08-27 (treasure_hunt s11 T6): paying a cycling cost tapped the real dual and left
    // Cascade Bluffs "up on its own" -- a feeder-less filter whose only mana is {C}. In HUMAN play
    // the filter's plain "{T}: Add {C}" now pays a generic pip first (FilterCFirstEnabled +
    // HumanPlayActive). AUTONOMOUS play keeps the historical dual-first order: the unconditional
    // tier measured WORSE on the suite everywhere it moved (regression th/hinata, zero cells
    // faster) -- the dual-first spend preserves the filter's conversion for the turn's later
    // casts. This test runs WITHOUT MTG_HUMAN_PLAY, so it pins the autonomous contract; the
    // human-play behaviour is exercised end-to-end by the viewer protocol sweep.
    {
        GameState s = MakeBoard({"Cascade Bluffs", "Island"});
        const PayEnd ex = RunExecutor(s, Cost(1), false);
        const PayEnd ro = RunRollout(s, Cost(1), false);
        CHECK(ex.ok); CHECK(ro.ok); CHECK(ex == ro);
        CHECK(ex.tapped == std::vector<bool>{false, true});   // autonomous: the mono land pays
    }
    // A COLOURED pip: the Island is the Bluffs' LAST feeder, so the adopted feed-aware reroute
    // (MTG_FEED_FILTER_FIRST, 2026-09-01) routes it THROUGH the filter -- both tap, and the
    // conversion's second unit floats for the turn's later casts instead of the filter
    // stranding as a {C}-only source. (Pre-adoption: Island paid directly, Bluffs stayed up.)
    {
        GameState s = MakeBoard({"Cascade Bluffs", "Island"});
        const PayEnd ex = RunExecutor(s, Cost(0, 0, /*u=*/1), false);
        const PayEnd ro = RunRollout(s, Cost(0, 0, /*u=*/1), false);
        CHECK(ex.ok); CHECK(ro.ok); CHECK(ex == ro);
        CHECK(ex.tapped == std::vector<bool>{true, true});    // Island feeds the Bluffs
        CHECK(ex.floating_total == 1);                        // the banked leftover {U}
    }
}

TEST_CASE("D12: Secluded Courtyard's coloured mana pays a creature-source ABILITY, cast-only lands do not")
{
    EnsureCards();
    // Burning-Fist's "{1}{R}, Discard a card" / Sethron's "{2}{B/R}" are activated abilities of
    // creature sources of the chosen type -- Secluded Courtyard's oracle text allows its restricted
    // coloured mana there; Unclaimed Territory's clause covers CASTS only. The payment context is
    // the CreatureAbilityPayScope guard the ActivatePump sites install.
    const GameState courtyard = MakeBoard({"Secluded Courtyard"});
    const GameState territory = MakeBoard({"Unclaimed Territory"});
    const ManaCost  red       = Cost(0, 0, 0, 0, /*r=*/1);

    // Outside the scope (a plain non-creature payment): both lands are {C}-only for the pip.
    CheckTwinsAgree(courtyard, red, /*for_creature=*/false, /*expect_ok=*/false);
    CheckTwinsAgree(territory, red, /*for_creature=*/false, /*expect_ok=*/false);

    {   // Inside the scope: Courtyard's colours become payment-legal...
        CreatureAbilityPayScope scope;
        CheckTwinsAgree(courtyard, red, /*for_creature=*/false, /*expect_ok=*/true);
        // ...but a cast-only land is unchanged (the clause is per-card, not per-context).
        CheckTwinsAgree(territory, red, /*for_creature=*/false, /*expect_ok=*/false);
    }

    // Scope is RAII: legality reverts once the activation payment ends.
    CheckTwinsAgree(courtyard, red, /*for_creature=*/false, /*expect_ok=*/false);
}

TEST_CASE("strict filter feed: on-colour feeds work, off-colour laundering is refused")
{
    EnsureCards();
    // Force the lever through its heurarm slot (the env read is a process-lifetime static, so a
    // test cannot toggle it via EnvPut once anything has read it). Cleared at each block's end.
    struct ArmScope
    {
        ArmScope(bool on) { heurarm::t_arm[heurarm::FILTER_FEED_STRICT] = on ? 1 : 0; }
        ~ArmScope() { heurarm::Clear(); }
    };

    const GameState island_board = MakeBoard({"Cascade Bluffs", "Island"});
    const GameState ring_board   = MakeBoard({"Cascade Bluffs", "Sol Ring"});
    const ManaCost  rr = Cost(0, 0, 0, 0, /*r=*/2);
    const ManaCost  ur = Cost(0, 0, /*u=*/1, 0, /*r=*/1);

    {   // STRICT: the Island's {U} is a legal {U/R} feed -- {R}{R} and {U}{R} must both stay payable.
        ArmScope strict(true);
        CheckTwinsAgree(island_board, rr, /*for_creature=*/false, /*expect_ok=*/true);
        CheckTwinsAgree(island_board, ur, /*for_creature=*/false, /*expect_ok=*/true);
        // Sol Ring makes only {C}, which cannot pay the {U/R} feed: without the launder the board's
        // only red is unreachable, so {R}{R} must be refused.
        CheckTwinsAgree(ring_board, rr, /*for_creature=*/false, /*expect_ok=*/false);
    }

    {   // LENIENT (the pre-fix model, still the default until adoption): the same {R}{R} is paid by
        // feeding the Bluffs with Sol Ring's {C} -- the exact laundering the strict mode removes.
        ArmScope lenient(false);
        CheckTwinsAgree(ring_board, rr, /*for_creature=*/false, /*expect_ok=*/true);
    }
}

TEST_CASE("strict filter feed: a DORK's mana feeds the filter inside a joint bill (gi68 T2 shape)")
{
    EnsureCards();
    struct ArmScope
    {
        ArmScope() { heurarm::t_arm[heurarm::FILTER_FEED_STRICT] = 1; }
        ~ArmScope() { heurarm::Clear(); }
    } strict;
    // gi68's control T2: Cascade Bluffs + Sol Ring + Ornithopter of Paradise paying the joint bill
    // of Preordain {U} + Ornithopter {2} + Ponder {U}. Strictly legal: the dork makes {U}, feeds
    // the Bluffs -> {U}{U}; Sol Ring's {C}{C} covers the generics.
    const GameState board = MakeBoard({"Cascade Bluffs", "Sol Ring", "Ornithopter of Paradise"});
    CheckTwinsAgree(board, Cost(2, 0, /*u=*/2), /*for_creature=*/false, /*expect_ok=*/true);
    // A third U is genuinely out of reach (the feed spends the dork's unit).
    CheckTwinsAgree(board, Cost(2, 0, /*u=*/3), /*for_creature=*/false, /*expect_ok=*/false);
}

TEST_CASE("strict filter feed: a DEPLETION land's burst feeds the filter (gi448 T2 shape)")
{
    EnsureCards();
    struct ArmScope
    {
        ArmScope() { heurarm::t_arm[heurarm::FILTER_FEED_STRICT] = 1; }
        ~ArmScope() { heurarm::Clear(); }
    } strict;
    auto with_dep = [](Permanent p, int n)
    {
        Counter c; c.type = Counter::Type::Depletion; c.count = n;
        p.counters.push_back(c);
        return p;
    };
    // gi448's control T2: Saprazzan Skerry ({T}, remove a counter: add {U}{U}) + Cascade Bluffs
    // paying Land's Edge {1}{R}{R}. Strictly legal: one Skerry {U} feeds the Bluffs -> {R}{R},
    // the other pays the generic.
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.battlefield.push_back(with_dep(MakeLand("Saprazzan Skerry"), 2));
    s.battlefield.push_back(MakeLand("Cascade Bluffs"));
    CheckTwinsAgree(s, Cost(1, 0, 0, 0, /*r=*/2), /*for_creature=*/false, /*expect_ok=*/true);
    // ...and {1}{R}{R}{R} is genuinely out of reach (only two Bluffs outputs exist).
    CheckTwinsAgree(s, Cost(1, 0, 0, 0, /*r=*/3), /*for_creature=*/false, /*expect_ok=*/false);
}

TEST_CASE("feed-filter-first: the last feeder routes THROUGH the filter so a later cast keeps its feed (th gi448 T3 shape)")
{
    EnsureCards();
    // th gi448's control T3: Saprazzan Skerry (1 counter) + Cascade Bluffs + Reliquary Tower paying
    // Treasure Hunt {1}{U} TWICE. The per-cast greedy pays TH#1 from the Skerry's {U}{U} alone --
    // which SUCCEEDS, so the complete backtracker is never consulted -- and strands the Bluffs
    // (Tower is {C}-only, no strict feed left): TH#2 unpayable, a whole-turn stranding no per-cast
    // fallback can see. MTG_FEED_FILTER_FIRST reroutes the Skerry (the Bluffs' LAST feeder)
    // THROUGH the filter: same two taps float {U}{U}{U}, the leftover {U} carries (CR 500.4
    // float retention) and TH#2 pays off it plus the Tower.
    struct ArmScope
    {
        ArmScope(bool fff)
        {
            heurarm::t_arm[heurarm::FILTER_FEED_STRICT] = 1;
            heurarm::t_arm[heurarm::FEED_FILTER_FIRST]  = fff ? 1 : 0;
        }
        ~ArmScope() { heurarm::Clear(); }
    };
    auto board = []
    {
        GameState s;
        s.active_player_index = 0;
        s.turn_number         = 3;
        Permanent skerry = MakeLand("Saprazzan Skerry");
        Counter c; c.type = Counter::Type::Depletion; c.count = 1;
        skerry.counters.push_back(c);
        s.battlefield.push_back(skerry);
        s.battlefield.push_back(MakeLand("Cascade Bluffs"));
        s.battlefield.push_back(MakeLand("Reliquary Tower"));
        return s;
    };
    {   // ON: both casts pay, and the twins agree on the end state.
        ArmScope on(true);
        GameState ex = board();
        AIEngine  eng;
        ManaPool  avail = AvailableManaPool(ex);
        CHECK(MtgTestSeam::TapForCost(eng, ex, Cost(1, 0, /*u=*/1), avail, false));
        CHECK(MtgTestSeam::TapForCost(eng, ex, Cost(1, 0, /*u=*/1), avail, false));
        GameState ro = board();
        CHECK(TapForCostDirect(ro, Cost(1, 0, /*u=*/1), false));
        CHECK(TapForCostDirect(ro, Cost(1, 0, /*u=*/1), false));
        CHECK(Snapshot(ex, true) == Snapshot(ro, true));
    }
    {   // OFF (the gap this lever exists for): TH#1 pays, TH#2 is strictly stranded.
        ArmScope off(false);
        GameState ro = board();
        CHECK(TapForCostDirect(ro, Cost(1, 0, /*u=*/1), false));
        CHECK_FALSE(TapForCostDirect(ro, Cost(1, 0, /*u=*/1), false));
    }
}

// USER, viewer seed 10 T3 (2026-09-05): "I want to leave black untapped and cycle until I get
// unearth and stinger in the graveyard, but the tap order is really bad and taps both black lands."
//
// Board: Canyon Slough (B/R), Fetid Pools (U/B), Capital City -- the exact Fluctuator T3 board --
// paying Fluctuator's {2}. One land must survive, and it has to be one that makes BLACK, because
// Unearth {B} is the rest of the turn. Capital City cannot be that land: its "{1},{T}: Add one
// mana of any color" needs a FEEDER, so alone it makes {C} and nothing else.
//
// The regression this pins: Capital City became an any_color_filter (0cd008cf), and
// ManaSourceRank sends every conversion source to 25 -- BEHIND a dual at 20. So the generic pips
// spent both duals and kept the one land that cannot make a colour by itself. As a plain [C] land
// it ranked 5 and spent first, which is the correct scarcity order and what this asserts.
TEST_CASE("any-colour filter spends its free {C} FIRST, sparing a real colour source (viewer s10)")
{
    EnsureCards();
    auto board = []
    {
        GameState s = MakeBoard({ "Canyon Slough", "Fetid Pools", "Capital City" });
        return s;
    };
    auto black_still_available = [](const GameState& s)
    {
        for (const Permanent& p : s.battlefield)
        {
            if (p.tapped) { continue; }
            const CardDefinition* d = CardDatabase::Instance().Lookup(p.card.m_name.str());
            if (d == nullptr || d->params.any_color_filter) { continue; }   // needs a feeder: not a source
            for (Color c : d->params.produces) { if (c == Color::Black) { return true; } }
        }
        return false;
    };
    {   // EXECUTOR twin
        GameState s = board();
        AIEngine  eng;
        ManaPool  avail = AvailableManaPool(s);
        CHECK(MtgTestSeam::TapForCost(eng, s, Cost(2), avail, false));
        CHECK_MESSAGE(black_still_available(s),
                      "Fluctuator's {2} tapped both black lands and kept the feeder-less filter");
    }
    {   // ROLLOUT twin -- lockstep, or the search plans a line the executor cannot play
        GameState s = board();
        CHECK(TapForCostDirect(s, Cost(2), false));
        CHECK_MESSAGE(black_still_available(s),
                      "rollout twin tapped both black lands");
    }
}

// PAINLAND ({T}: Add {C}. / {T}: Add {G} or {W}. This land deals 1 damage to you.) -- the two are
// SEPARATE abilities and only the coloured one hurts. A GENERIC pip must take the painless {C}
// mode (DripLandAnyPipColor's painland branch on the greedy; the {C}-first colour order in the
// backtracker); a COLOURED pip still taps the coloured ability and takes the damage.
// USER-found, EDF seed 5 (2026-09-07): the blink loop's generic pips re-tapped the painlands
// coloured every iteration, bleeding life no pip required.
TEST_CASE("painland: a generic pip taps the separate painless {C} ability")
{
    EnsureCards();
    const GameState board = MakeBoard({"Brushland"});

    const PayEnd ex = RunExecutor(board, Cost(1), false);
    const PayEnd ro = RunRollout(board, Cost(1), false);
    CHECK(ex.ok);
    CHECK(ro.ok);
    CHECK(ex.life0 == 20);   // painless (was 19: the tap paid the pip as prod[0] = {G})
    CHECK(ex == ro);

    // The coloured ability is unchanged: a {G} pip taps it and takes the printed damage.
    const PayEnd g = RunRollout(board, Cost(0, 0, 0, 0, 0, /*g=*/1), false);
    CHECK(g.ok);
    CHECK(g.life0 == 19);
}

// MANA-CACHE KEY vs LAND AURAS (USER-found twice on EDF, 2026-09-07). The payment DFS credits an
// enchanted land's bonus when the host taps, but the cache key identified a source only by
// (index, def, tapped) -- so "unpayable" cached on the bare board replayed onto the SAME board
// after an aura attached (s3 gi2: Trace of Abundance on Adarkar Wastes, '{1}{G} for Living Wish'
// falsely illegal; s6 T3: a committed Overgrowth cast silently dropped against turn 2's cached
// negative). The fixture makes the aura the ONLY route to the pip: nothing on the bare board
// produces green, so both solves reach the backtracker and the second is answered by the cache
// unless the key splits on the attach.
TEST_CASE("mana cache: attaching a land aura splits the key (no stale unpayable replay)")
{
    EnsureCards();
    GameState s = MakeBoard({"Adarkar Wastes"});
    s.battlefield[0].card.m_number = 4242;         // per-copy ID the aura attaches to
    const ManaCost cost = Cost(1, 0, 0, 0, 0, /*g=*/1);   // {1}{G}

    {   // bare board: W/U/C only -> unpayable, and the negative is cached
        GameState bare = s;
        ManaPool  leftover;
        CHECK_FALSE(TapForCostBacktrack(bare, cost, /*for_creature=*/false, ManaPool{},
                                        nullptr, nullptr, &leftover));
    }

    const CardDefinition* trace = CardDatabase::Instance().Lookup("Trace of Abundance");
    REQUIRE(trace != nullptr);
    Permanent aura;
    aura.card             = trace->card;
    aura.controller_index = 0;
    aura.owner_index      = 0;
    aura.aura_attached_to = 4242;                  // "adds an additional one mana of any color"
    s.battlefield.push_back(aura);

    ManaPool leftover;
    CHECK(TapForCostBacktrack(s, cost, /*for_creature=*/false, ManaPool{},
                              nullptr, nullptr, &leftover));
}
