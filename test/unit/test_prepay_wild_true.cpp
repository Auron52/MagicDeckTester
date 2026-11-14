// The whole-turn prepay (TurnSolver::BatchPrepayMainCasts) funds the bill's GENERIC requirement out of the solve's
// surplus colours and books it `wild` -- and `wild` pays any colour pip (SpendFloatingTowardCost step 2). A pip
// OUTSIDE the bill could then spend a Forest's {G} as {W}, or Vivid Creek's free {U} as {R} (Gods s30018016 /
// s30018089: Faithless Looting's flashback {R}). CR 106.1b: a coloured pip is paid only with mana of its colour.
// MTG_PREPAY_WILD_TRUE (GameState::prepay_wild_true): a colour-funded unit keeps its TRUE colour unless its source's
// tapped mode could make any colour (Birds of Paradise: committed at spend, USER: no per-colour branching).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsWt()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card CardWt(const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c = d->card;
    c.m_number = number;
    return c;
}

void PutWt(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card              = CardWt(name, number);
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = false;
    s.battlefield.push_back(p);
}

GameState BaseWt(int turn)
{
    static const VialProvider vp;
    EnsureCardsWt();
    GameState s;
    s.m_provider          = &vp;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    return s;
}

Action CastWt(const GameState& s, const std::string& name)
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
    for (int i = 0; i < static_cast<int>(s.players[0].hand.size()); ++i)
    { if (s.players[0].hand[static_cast<std::size_t>(i)].m_name.str() == name) { a.hand_index = i; break; } }
    return a;
}

const ManaCost& CostWt(const char* nm) { return CardDatabase::Instance().Lookup(nm)->card.m_mana_cost; }

ManaPool FloatWt(const GameState& s)
{
    ManaPool f = s.floating_mana;
    f.AddPool(s.floating_creature_mana);
    return f;
}

// The handoff shape: Plains, Plains, `third` + Hatchery Sliver (every Sliver spell has replicate = its cost). The bill
// Plated Sliver {W} + Sinew Sliver {1}{W} taps all three; `third` funds Sinew's {1}.
GameState SliverPrepaid(const char* third)
{
    GameState s = BaseWt(3);
    PutWt(s, "Plains", 40);
    PutWt(s, "Plains", 41);
    PutWt(s, third, 42);
    PutWt(s, "Hatchery Sliver", 43);
    s.players[0].hand.push_back(CardWt("Plated Sliver", 100));
    s.players[0].hand.push_back(CardWt("Sinew Sliver", 101));
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(CardWt("Plains", 900 + k)); }
    const std::vector<Action> line = { CastWt(s, "Plated Sliver"), CastWt(s, "Sinew Sliver") };
    REQUIRE(TurnSolver::BatchPrepayMainCasts(s, line));
    for (const Permanent& p : s.battlefield) { if (p.card.IsLand() || p.card.m_name.str() == third) { CHECK(p.tapped); } }
    REQUIRE(FloatWt(s).Total() == 3);
    return s;
}

// The Gods shape: Mountain, Island, Vivid Creek; the bill Faithless Looting {R} + Magus of the Bazaar {1}{U}.
GameState LootingPrepaid(int vivid_counters)
{
    GameState s = BaseWt(4);
    PutWt(s, "Mountain", 40);
    PutWt(s, "Island", 41);
    PutWt(s, "Vivid Creek", 42);
    s.battlefield.back().charge_counters = vivid_counters;
    s.players[0].hand.push_back(CardWt("Faithless Looting", 100));
    s.players[0].hand.push_back(CardWt("Magus of the Bazaar", 101));
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(CardWt("Island", 900 + k)); }
    const std::vector<Action> line = { CastWt(s, "Faithless Looting"), CastWt(s, "Magus of the Bazaar") };
    REQUIRE(TurnSolver::BatchPrepayMainCasts(s, line));
    for (const Permanent& p : s.battlefield) { CHECK(p.tapped); }
    REQUIRE(FloatWt(s).Total() == 3);
    return s;
}

}   // namespace

TEST_CASE("prepay wild-true: a Forest's {G} funding Sinew's {1} cannot pay a replicate copy's {W}")
{
    GameState s = SliverPrepaid("Forest");
    // Plated {W}, then one replicate copy {W}: the two real whites.
    REQUIRE(TapForCostDirect(s, CostWt("Plated Sliver"), /*for_creature=*/true));
    REQUIRE(TapForCostDirect(s, CostWt("Plated Sliver"), /*for_creature=*/true));
    // A third {W}: only the Forest's {G} is left and nothing is untapped. CR 106.1b -> not payable.
    CHECK_FALSE(TapForCostDirect(s, CostWt("Plated Sliver"), /*for_creature=*/true));
    CHECK(FloatWt(s).Total() == 1);
    // ...but it still pays a generic, or a {G} (it IS green).
    ManaCost g; g.green = 1;
    CHECK(TapForCostDirect(s, g, /*for_creature=*/true));
    CHECK(FloatWt(s).Total() == 0);
}

TEST_CASE("prepay wild-true: the bill's own pinned colours still pay in full")
{
    GameState s = SliverPrepaid("Forest");
    REQUIRE(TapForCostDirect(s, CostWt("Plated Sliver"), /*for_creature=*/true));
    REQUIRE(TapForCostDirect(s, CostWt("Sinew Sliver"), /*for_creature=*/true));
    CHECK(FloatWt(s).Total() == 0);
}

TEST_CASE("prepay wild-true: the bill pays in either order")
{
    GameState s = SliverPrepaid("Forest");
    REQUIRE(TapForCostDirect(s, CostWt("Sinew Sliver"), /*for_creature=*/true));
    REQUIRE(TapForCostDirect(s, CostWt("Plated Sliver"), /*for_creature=*/true));
    CHECK(FloatWt(s).Total() == 0);
}

TEST_CASE("prepay wild-true: an any-colour source's unit (Birds of Paradise) still pays the {W}")
{
    GameState s = SliverPrepaid("Birds of Paradise");
    REQUIRE(TapForCostDirect(s, CostWt("Plated Sliver"), /*for_creature=*/true));
    REQUIRE(TapForCostDirect(s, CostWt("Plated Sliver"), /*for_creature=*/true));
    CHECK(TapForCostDirect(s, CostWt("Plated Sliver"), /*for_creature=*/true));
    CHECK(FloatWt(s).Total() == 0);
}

TEST_CASE("prepay wild-true: Vivid Creek's free {U} funding Magus's {1} cannot pay a flashback's {R}")
{
    GameState s = LootingPrepaid(0);
    REQUIRE(TapForCostDirect(s, CostWt("Faithless Looting"), /*for_creature=*/false));
    // A second {R} (the flashback's): only Island's {U} and Vivid Creek's {U} are left.
    CHECK_FALSE(TapForCostDirect(s, CostWt("Faithless Looting"), /*for_creature=*/false));
    REQUIRE(TapForCostDirect(s, CostWt("Magus of the Bazaar"), /*for_creature=*/true));
    CHECK(FloatWt(s).Total() == 0);
}

TEST_CASE("prepay wild-true: a Vivid Creek that paid its counter made any-colour mana")
{
    GameState s = LootingPrepaid(2);
    const bool counter_spent = s.battlefield[2].charge_counters < 2;
    REQUIRE(TapForCostDirect(s, CostWt("Faithless Looting"), /*for_creature=*/false));
    // The second {R} is payable exactly when the Creek's tap removed a counter (any colour), never off its free {U}.
    CHECK(TapForCostDirect(s, CostWt("Faithless Looting"), /*for_creature=*/false) == counter_spent);
}

TEST_CASE("prepay wild-true: =0 restores the old any-colour `wild` (the violation, A/B only)")
{
    struct ArmOff
    {
        ArmOff()  { heurarm::t_arm[heurarm::PREPAY_WILD_TRUE] = 0; }
        ~ArmOff() { heurarm::Clear(); }
    } _off;
    GameState s = SliverPrepaid("Forest");
    REQUIRE(TapForCostDirect(s, CostWt("Plated Sliver"), /*for_creature=*/true));
    REQUIRE(TapForCostDirect(s, CostWt("Plated Sliver"), /*for_creature=*/true));
    CHECK(TapForCostDirect(s, CostWt("Plated Sliver"), /*for_creature=*/true));
}

TEST_CASE("prepay wild-true: a white-restricted unit pays a hybrid {R/W} pip, never a plain {R}")
{
    EnsureCardsWt();
    const ManaCost& fig = CostWt("Figure of Destiny");   // {R/W}: the flat ints carry it as one colour
    REQUIRE(fig.hybrid_count == 1);
    {
        ManaPool r; r.wild = 1;
        WildTrueRec wt; wt.Push(1u << 0);                // the unit is really {W}
        ManaCost c = fig;
        WildTrueScope _s(&wt, /*enforce=*/true);
        SpendFloatingTowardCost(r, c);
        CHECK(c.ManaValue() == 0);
        CHECK(r.wild == 0);
        CHECK(wt.n == 0);
    }
    {
        ManaPool r; r.wild = 1;
        WildTrueRec wt; wt.Push(1u << 0);
        ManaCost c; c.red = 1;
        WildTrueScope _s(&wt, /*enforce=*/true);
        SpendFloatingTowardCost(r, c);
        CHECK(c.red == 1);                               // refused: {R} needs red mana
        CHECK(r.wild == 1);
    }
}

// A DUAL's unit is mana of either of its colours, committed at spend (USER: no per-colour branching; the approved
// route (a): "a unit from a genuinely multi-colour source stays any-of-its-colours"). Razorverge Thicket {G}/{W}
// funding Sinew's {1}: whichever colour the solve tapped it for, a replicate copy's {W} may take it -- and a {B}
// may not.
TEST_CASE("prepay wild-true: a dual's unit (Razorverge Thicket) pays either of its colours, never a third")
{
    {
        GameState s = SliverPrepaid("Razorverge Thicket");
        REQUIRE(TapForCostDirect(s, CostWt("Plated Sliver"), /*for_creature=*/true));
        REQUIRE(TapForCostDirect(s, CostWt("Plated Sliver"), /*for_creature=*/true));
        CHECK(TapForCostDirect(s, CostWt("Plated Sliver"), /*for_creature=*/true));
        CHECK(FloatWt(s).Total() == 0);
    }
    {
        GameState s = SliverPrepaid("Razorverge Thicket");
        REQUIRE(TapForCostDirect(s, CostWt("Plated Sliver"), /*for_creature=*/true));
        REQUIRE(TapForCostDirect(s, CostWt("Plated Sliver"), /*for_creature=*/true));
        ManaCost b; b.black = 1;
        CHECK_FALSE(TapForCostDirect(s, b, /*for_creature=*/true));
        ManaCost g; g.green = 1;
        CHECK(TapForCostDirect(s, g, /*for_creature=*/true));
    }
}

// The drain MATCHES pips to restricted units: {U,R} + {U,B} pay {U} and {B} (and {U} and {R}) whatever the order the
// pips are read in; {R} and {B} together too. {G} is refused.
TEST_CASE("prepay wild-true: pips are matched to dual units, not taken first-fit")
{
    const unsigned UR = (1u << 1) | (1u << 3), UB = (1u << 1) | (1u << 2);
    auto pay = [&](int u, int b, int r, int g) -> ManaCost
    {
        ManaPool pool; pool.wild = 2;
        WildTrueRec wt; wt.Push(UR); wt.Push(UB);
        ManaCost c; c.blue = u; c.black = b; c.red = r; c.green = g;
        WildTrueScope _s(&wt, /*enforce=*/true);
        SpendFloatingTowardCost(pool, c);
        return c;
    };
    CHECK(pay(1, 1, 0, 0).ManaValue() == 0);
    CHECK(pay(1, 0, 1, 0).ManaValue() == 0);
    CHECK(pay(0, 1, 1, 0).ManaValue() == 0);
    CHECK(pay(0, 0, 0, 1).green == 1);
    // Generic spends the least flexible unit first: one {U} unit and one Birds unit (unrecorded) -> the {U} goes.
    {
        ManaPool pool; pool.wild = 2;
        WildTrueRec wt; wt.Push(1u << 1);
        ManaCost c; c.generic = 1;
        WildTrueScope _s(&wt, /*enforce=*/true);
        SpendFloatingTowardCost(pool, c);
        CHECK(wt.n == 0);
        ManaCost w; w.white = 1;
        SpendFloatingTowardCost(pool, w);
        CHECK(w.white == 0);
    }
}

TEST_CASE("prepay wild-true: the flex label round-trips (mask, colour, count)")
{
    for (int col = 0; col < 5; ++col)
    {
        for (unsigned m = 0; m < 32; ++m)
        {
            if (((m >> col) & 1u) == 0 || PwtPop(m) < 2) { continue; }
            for (int n = 1; n <= 3; ++n)
            {
                int n2 = 0, c2 = 0; unsigned m2 = 0;
                BtFlexUnpack(BtFlexPack(n, col, m), n2, c2, m2);
                CHECK(n2 == n); CHECK(c2 == col); CHECK(m2 == m);
            }
        }
        CHECK(BtFlexPack(1, col, 1u << col) == 0);   // one colour: fixed, no label
    }
}

// ---- ROUTE (a), LITERAL (MTG_PREPAY_WILD_LITERAL; USER 2026-11-13: "Does this wild-mana fix avoid floating wild
// mana? That shouldn't show up in anything.") -- a single-colour source's funding unit floats as ITS colour, never
// `wild`; only a multi-colour source's unit stays `wild` (its colours, committed at spend). ----

TEST_CASE("prepay wild-literal: a Forest's funding floats as {G}, not `wild`")
{
    GameState s = SliverPrepaid("Forest");
    CHECK(s.floating_mana.wild + s.floating_creature_mana.wild == 0);
    CHECK(FloatWt(s).green == 1);
    CHECK(FloatWt(s).white == 2);
}

TEST_CASE("prepay wild-literal: Vivid Creek's free {U} floats as {U}")
{
    GameState s = LootingPrepaid(0);
    CHECK(FloatWt(s).wild == 0);
    CHECK(FloatWt(s).blue == 2);
    CHECK(FloatWt(s).red == 1);
}

TEST_CASE("prepay wild-literal: a dual's funding unit stays `wild` (either of its colours)")
{
    GameState s = SliverPrepaid("Razorverge Thicket");
    CHECK(FloatWt(s).wild == 1);
}

TEST_CASE("prepay wild-literal: =0 restores the tag version's booking (funding `wild` + its colour record)")
{
    struct ArmOff
    {
        ArmOff()  { heurarm::t_arm[heurarm::PREPAY_WILD_LITERAL] = 0; }
        ~ArmOff() { heurarm::Clear(); }
    } _off;
    GameState s = SliverPrepaid("Forest");
    CHECK(FloatWt(s).wild == 1);
    CHECK(FloatWt(s).green == 0);
    // ...and the record still refuses the third {W}.
    REQUIRE(TapForCostDirect(s, CostWt("Plated Sliver"), /*for_creature=*/true));
    REQUIRE(TapForCostDirect(s, CostWt("Plated Sliver"), /*for_creature=*/true));
    CHECK_FALSE(TapForCostDirect(s, CostWt("Plated Sliver"), /*for_creature=*/true));
}

// The bill's PINNED colours are protected by the fund count: a generic pip spends the funding colour first, so the
// colour a later cast pinned survives -- in every order.
TEST_CASE("prepay wild-literal: a generic pip spends the funding colour before a pinned colour")
{
    {
        ManaPool r; r.white = 1; r.green = 1;
        WildTrueRec wt; wt.fund[4] = 1;                  // the {G} is the bill's generic funding
        ManaCost c; c.generic = 1;
        WildTrueScope _s(&wt, /*enforce=*/true);
        SpendFloatingTowardCost(r, c);
        CHECK(c.generic == 0);
        CHECK(r.white == 1);
        CHECK(r.green == 0);
        CHECK(wt.fund[4] == 0);
    }
    {
        ManaPool r; r.white = 1; r.green = 1;            // no record: the W..G order eats the {W}
        ManaCost c; c.generic = 1;
        SpendFloatingTowardCost(r, c);
        CHECK(r.white == 0);
    }
}

// Bill: Plated Sliver {W} + Sinew Sliver {1}{W} off Plains, Plains, Forest -- the Forest's {G} funds the {1} and floats
// as {G}. Every order pays in full.
TEST_CASE("prepay wild-literal: pinned colours still pay in every order")
{
    for (int order = 0; order < 2; ++order)
    {
        GameState s = SliverPrepaid("Forest");
        if (order == 0)
        {
            REQUIRE(TapForCostDirect(s, CostWt("Sinew Sliver"), /*for_creature=*/true));
            REQUIRE(TapForCostDirect(s, CostWt("Plated Sliver"), /*for_creature=*/true));
        }
        else
        {
            REQUIRE(TapForCostDirect(s, CostWt("Plated Sliver"), /*for_creature=*/true));
            REQUIRE(TapForCostDirect(s, CostWt("Sinew Sliver"), /*for_creature=*/true));
        }
        CHECK(FloatWt(s).Total() == 0);
    }
}

// More than 8 units of one dual mask round-trip without release (a slot packs mask + count).
TEST_CASE("prepay wild-literal: the record packs runs of one mask (no overflow release)")
{
    GameState s = BaseWt(3);
    s.prepay_wild_true.stamp = PrepayCStampNow(s);
    s.floating_mana.wild = 12;
    WildTrueRec in; in.lim = WildTrueRec::kCap;
    const unsigned GW = (1u << 0) | (1u << 4);
    for (int k = 0; k < 12; ++k) { in.Push(GW); }
    const long long ov0 = g_pwt_overflow.load();
    PrepayWildTrueStore(s, 0, in);
    CHECK(g_pwt_overflow.load() == ov0);
    WildTrueRec out;
    REQUIRE(PrepayWildTrueLoad(s, 0, out));
    CHECK(out.n == 12);
    CHECK(out.Count(GW) == 12);
}

// A PLANNER'S REPLAY OF THE DRAIN READS THE FUNDING (item A round 5). The shared payer's line-float rung
// (MTG_LINE_FLOAT_TAP_FIRST) replays the drain to see which owed colours this payment's generic would eat. Without
// the record it drained the generic in W..G order -- the {U} a later cast owes -- and held the pip off the float, so
// a Forest was tapped for it. AccomplishedAlchemist regression d0 s2002 gi83 (seed 2085) T6: Living Wish and Weather
// the Storm each tapped an extra Forest, and the kill slipped a turn. The generic is the bill's own: it pays from the
// green funding and the Forest stays up.
TEST_CASE("prepay wild-literal: the line-float rung's replay spends the funding, not an owed colour")
{
    GameState s = BaseWt(6);
    PutWt(s, "Forest", 50);
    // The bill's pre-load: {G} pinned for this cast, {U} for a later one, two {G} funding the bill's generic.
    s.floating_mana.green = 3;
    s.floating_mana.blue  = 1;
    s.prepay_wild_true.gfund[4] = 2;
    s.prepay_wild_true.stamp    = PrepayCStampNow(s);
    struct LineScope
    {
        ManaCost prev;
        LineScope() : prev(g_line_unpaid_cost)
        { ManaCost lu; lu.green = 1; lu.blue = 1; lu.generic = 2; g_line_unpaid_cost = lu; }
        ~LineScope() { g_line_unpaid_cost = prev; }
    } _line;
    ManaCost c; c.green = 1; c.generic = 1;   // Living Wish {1}{G}
    REQUIRE(TapForCostDirect(s, c, /*for_creature=*/false));
    CHECK_FALSE(s.battlefield.front().tapped);
    CHECK(s.floating_mana.green == 1);
    CHECK(s.floating_mana.blue == 1);
}

// ITEM W (MTG_PREPAY_WILD_DEFER): Dragons gi363 T5. Bill {Urza's Incubator, Lathliss} funded by Sol Ring {C}{C} +
// two Mountains' {R} + Gruul Turf's {G}, pins {R}{R}; the second Lathliss ({R}{R} after the second Incubator) is paid
// from what the bill left. The Incubator's {3} took the funding {R}{R}{G}, so the second Lathliss met {C}{C} -- yet
// {C}{C}{G} on the {3} pays everything legally. The {3}'s generic pips keep a credit per colour unit they took, and
// each {R} pip of the second Lathliss takes an {R} back while its generic moves onto a {C} (the payer's {C}-first cap).
TEST_CASE("prepay wild-defer: a generic pip gives its {R} back to a later {R} pip and takes the {C}")
{
    ManaPool r; r.red = 4; r.green = 1; r.colorless = 2;
    WildTrueRec wt; wt.lim = WildTrueRec::kCap; wt.fund[3] = 2; wt.fund[4] = 1;
    WildTrueScope _s(&wt, /*enforce=*/true);
    auto pay = [&](ManaCost c) { GenericCFirstScope _gcf(r.colorless); SpendFloatingTowardCost(r, c); return c; };
    ManaCost inc; inc.generic = 3;
    CHECK(pay(inc).ManaValue() == 0);
    CHECK(wt.HeldTotal() == 3);               // every colour unit the {3} took is re-assignable
    ManaCost l; l.red = 2;
    CHECK(pay(l).ManaValue() == 0);
    CHECK(pay(l).ManaValue() == 0);           // lever off: both {R} refused (the float is {C}{C})
    CHECK(r.Total() == 0);
    CHECK(wt.held[4] == 1);                   // the {G} paid the {3}
}

// The credit never admits an illegal pip: after a generic took the {R} out of {R}{G}{W}, a {G}{G} pays one {G}
// only (there was only one {G} ever), and {R}{W} pays in full (the generic moves to the {G}).
TEST_CASE("prepay wild-defer: a credit pays the colour its unit was, never a colour the float never held")
{
    ManaPool r; r.red = 1; r.green = 1; r.white = 1;
    WildTrueRec wt; wt.lim = WildTrueRec::kCap; wt.fund[3] = 1; wt.fund[4] = 1;
    WildTrueScope _s(&wt, /*enforce=*/true);
    ManaCost g; g.generic = 1;
    SpendFloatingTowardCost(r, g);
    CHECK(g.generic == 0);
    CHECK(r.Total() == 2);
    ManaPool r2 = r; WildTrueRec wt2 = wt;
    {
        WildTrueScope _s2(&wt2, true);
        ManaCost gg; gg.green = 2;
        SpendFloatingTowardCost(r2, gg);
        CHECK(gg.green == 1);
    }
    ManaCost rw; rw.red = 1; rw.white = 1;
    SpendFloatingTowardCost(r, rw);
    CHECK(rw.red == 0);
    CHECK(rw.white == 0);
    CHECK(r.Total() == 0);
}

// The credits ride the record (GameState::PrepayWildTrue::gheld) through the real payer, and hold only while the
// float is exactly what the last recorded drain left: mana added since (it did not exist when the generic was paid)
// drops them.
TEST_CASE("prepay wild-defer: the payer keeps the credits across casts, and a changed float drops them")
{
    GameState s = BaseWt(5);
    s.deck_spends_c_pip = false;
    s.floating_mana.red = 4; s.floating_mana.green = 1; s.floating_mana.colorless = 2;
    s.prepay_wild_true.gfund[3] = 2; s.prepay_wild_true.gfund[4] = 1;
    s.prepay_wild_true.stamp = PrepayCStampNow(s);
    ManaCost inc; inc.generic = 3;
    REQUIRE(TapForCostDirect(s, inc, /*for_creature=*/false));
    ManaCost l; l.red = 2;
    REQUIRE(TapForCostDirect(s, l, false));
    {
        GameState t = s;
        REQUIRE(TapForCostDirect(t, l, false));
        CHECK(t.floating_mana.Total() == 0);
    }
    {
        GameState t = s;
        t.floating_mana.white += 1;          // mana added outside the recorded drains
        WildTrueRec out;
        PrepayWildTrueLoad(t, 0, out);
        CHECK(out.HeldTotal() == 0);
        CHECK_FALSE(TapForCostDirect(t, l, false));
    }
}
