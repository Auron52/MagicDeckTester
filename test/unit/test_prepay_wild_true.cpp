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
        int wt[5] = { 1, 0, 0, 0, 0 };                   // the unit is really {W}
        ManaCost c = fig;
        WildTrueScope _s(wt, /*enforce=*/true);
        SpendFloatingTowardCost(r, c);
        CHECK(c.ManaValue() == 0);
        CHECK(r.wild == 0);
        CHECK(wt[0] == 0);
    }
    {
        ManaPool r; r.wild = 1;
        int wt[5] = { 1, 0, 0, 0, 0 };
        ManaCost c; c.red = 1;
        WildTrueScope _s(wt, /*enforce=*/true);
        SpendFloatingTowardCost(r, c);
        CHECK(c.red == 1);                               // refused: {R} needs red mana
        CHECK(r.wild == 1);
    }
}
