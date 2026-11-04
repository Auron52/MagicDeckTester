// The whole-turn prepay (TurnSolver::BatchPrepayMainCasts) must never let a physical
// {C} pay a coloured pip.
//
// Slivers 2HG gi191 T3 (seed 61000191): Mutavault + Ancient Ziggurat + Sliver Hive, Hatchery Sliver on the
// battlefield (every Sliver spell has replicate = its mana cost), the plan casts Plated Sliver {W} and Sinew
// Sliver {1}{W}. The joint bill {1}{W}{W} taps all three lands: Ziggurat and Hive for {W}, Mutavault for {C}.
// The pre-load booked the generic's funding -- Mutavault's {C} -- as `wild`, and `wild` pays any COLOUR pip
// (SpendFloatingTowardCost step 2). Hatchery's replicate copy of Plated ({W}, outside the bill) then drained
// W, W, and finally the wild: Mutavault's {C} paying {W}. CR 106.1b / 107.4: a {W} pip is paid only with
// white mana.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsCw()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card CardCw(const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c = d->card;
    c.m_number = number;
    return c;
}

void PutCw(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card              = CardCw(name, number);
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = false;
    s.battlefield.push_back(p);
}

GameState BoardCw(const char* c_land = "Mutavault")
{
    static const VialProvider vp;
    EnsureCardsCw();
    GameState s;
    s.m_provider          = &vp;
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    PutCw(s, c_land, 40);
    PutCw(s, "Ancient Ziggurat", 41);
    PutCw(s, "Sliver Hive", 42);
    PutCw(s, "Hatchery Sliver", 43);
    s.players[0].hand.push_back(CardCw("Plated Sliver", 100));
    s.players[0].hand.push_back(CardCw("Sinew Sliver", 101));
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(CardCw("Sliver Hive", 900 + k)); }
    return s;
}

Action CastCw(const GameState& s, const std::string& name)
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

const ManaCost& CostCw(const char* nm) { return CardDatabase::Instance().Lookup(nm)->card.m_mana_cost; }

// Both floats the prepay can book into (an all-creature batch that tapped Ziggurat books creature-only).
ManaPool FloatCw(const GameState& s)
{
    ManaPool f = s.floating_mana;
    f.AddPool(s.floating_creature_mana);
    return f;
}

GameState PrepaidCw(const char* c_land = "Mutavault")
{
    GameState s = BoardCw(c_land);
    const std::vector<Action> line = { CastCw(s, "Plated Sliver"), CastCw(s, "Sinew Sliver") };
    REQUIRE(TurnSolver::BatchPrepayMainCasts(s, line));
    for (const Permanent& p : s.battlefield) { if (p.card.IsLand()) { CHECK(p.tapped); } }
    return s;
}

}   // namespace

TEST_CASE("prepay: Mutavault's {C} funding the bill's generic stays {C}, never `wild`")
{
    GameState s = PrepaidCw();
    const ManaPool f = FloatCw(s);
    CHECK(f.Total() == 3);
    CHECK(f.white == 2);
    CHECK(f.colorless == 1);
    CHECK(f.wild == 0);
}

TEST_CASE("prepay: a {W} outside the bill (a replicate copy) cannot be paid with the prepaid {C}")
{
    GameState s = PrepaidCw();
    // Plated Sliver {W}, then one replicate copy {W}: the two real whites.
    REQUIRE(TapForCostDirect(s, CostCw("Plated Sliver"), /*for_creature=*/true));
    REQUIRE(TapForCostDirect(s, CostCw("Plated Sliver"), /*for_creature=*/true));
    // A second copy is a third {W}. Only Mutavault's {C} is left, and no untapped source: not payable.
    CHECK_FALSE(AvailableManaPool(s, nullptr).CanPay(CostCw("Plated Sliver")));
    CHECK_FALSE(TapForCostDirect(s, CostCw("Plated Sliver"), /*for_creature=*/true));
    CHECK(FloatCw(s).colorless == 1);
}

TEST_CASE("prepay: the bill itself still pays in full off the kept {C}")
{
    GameState s = PrepaidCw();
    REQUIRE(TapForCostDirect(s, CostCw("Plated Sliver"), /*for_creature=*/true));
    REQUIRE(TapForCostDirect(s, CostCw("Sinew Sliver"), /*for_creature=*/true));
    CHECK(FloatCw(s).Total() == 0);
}

// The other half of the rule: a {C} out of a source that could have made the colour instead (Sliver Hive:
// {C} or any colour for a Sliver spell) is any-colour mana, committed at spend. Whatever mode the joint
// solve tapped it for, the third {W} (a second replicate copy) IS payable -- the fix must not lose it
// (Slivers 87301106 T4: the replicate copy's {G} came from a Secluded Courtyard tap).
TEST_CASE("prepay: a {C} from a source that could make the colour still pays the colour")
{
    GameState s = PrepaidCw("Sliver Hive");
    CHECK(FloatCw(s).Total() == 3);
    CHECK(FloatCw(s).colorless == 0);
    REQUIRE(TapForCostDirect(s, CostCw("Plated Sliver"), /*for_creature=*/true));
    REQUIRE(TapForCostDirect(s, CostCw("Plated Sliver"), /*for_creature=*/true));
    CHECK(TapForCostDirect(s, CostCw("Plated Sliver"), /*for_creature=*/true));
}


// GENERIC PAYS {C} BEFORE `wild` (GenericCFirstScope; Slivers smoke d0 gi190). With the physical {C} now
// booked `colorless`, the old wild-first generic order spent the FLEXIBLE unit on a generic and stranded the
// {C}, refusing a later colour pip (Predatory Sliver's replicate {G}) the board pays. In a deck that cannot
// spend {C} on a {C} pip, generic takes the untagged {C} first; EDF (deck_spends_c_pip) and Generator
// Servant's haste-tagged {C} keep the old order.
namespace
{
GameState FloatOnlyCw(bool deck_spends_c_pip)
{
    static const VialProvider vp;
    EnsureCardsCw();
    GameState s;
    s.m_provider          = &vp;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.deck_spends_c_pip   = deck_spends_c_pip;
    s.floating_mana.colorless = 1;
    s.floating_mana.wild      = 1;
    return s;
}
ManaCost GenericCw(int n) { ManaCost c; c.generic = n; return c; }
ManaCost GreenCw()        { ManaCost c; c.green = 1;  return c; }
}   // namespace

TEST_CASE("payer: a generic pip spends the {C} before the wild, so a later {G} is still payable")
{
    GameState s = FloatOnlyCw(/*deck_spends_c_pip=*/false);
    REQUIRE(TapForCostDirect(s, GenericCw(1), /*for_creature=*/true));
    CHECK(s.floating_mana.colorless == 0);
    CHECK(s.floating_mana.wild == 1);
    CHECK(TapForCostDirect(s, GreenCw(), /*for_creature=*/true));
}

TEST_CASE("payer: a deck that spends {C} on a {C} pip (EDF) keeps the wild-first generic order")
{
    GameState s = FloatOnlyCw(/*deck_spends_c_pip=*/true);
    REQUIRE(TapForCostDirect(s, GenericCw(1), /*for_creature=*/false));
    CHECK(s.floating_mana.colorless == 1);
    CHECK(s.floating_mana.wild == 0);
}

TEST_CASE("payer: Generator Servant's haste-tagged {C} is not spent ahead of the wild")
{
    GameState s = FloatOnlyCw(/*deck_spends_c_pip=*/false);
    s.floating_haste_c = 1;
    REQUIRE(TapForCostDirect(s, GenericCw(1), /*for_creature=*/false));
    CHECK(s.floating_mana.colorless == 1);
    CHECK(s.floating_haste_c == 1);
    CHECK(s.floating_mana.wild == 0);
}

// A {C} from a source with only SOME colours (Caves of Koilos: {W}/{B}/{C}) is not any-colour mana: booked
// `wild` it could pay a {G} the land cannot make (Melira Pod smoke d0 gi738: Birds of Paradise's {G}). It
// stays `colorless` like Mutavault's.
TEST_CASE("prepay: a {C} from a two-colour land (Caves of Koilos) never becomes `wild`")
{
    GameState s = PrepaidCw("Caves of Koilos");
    const ManaPool f = FloatCw(s);
    CHECK(f.Total() == 3);
    CHECK(f.wild == 0);
}

TEST_CASE("payer: in a {C}-pip deck the prepay's kept {C} still drains before the wild")
{
    GameState s = FloatOnlyCw(/*deck_spends_c_pip=*/true);
    s.phase = Phase::PreCombatMain;
    SetPrepayKeptC(s, 1);
    REQUIRE(PrepayKeptC(s) == 1);
    REQUIRE(TapForCostDirect(s, GenericCw(1), /*for_creature=*/false));
    CHECK(s.floating_mana.colorless == 0);
    CHECK(s.floating_mana.wild == 1);
    CHECK(PrepayKeptC(s) == 0);
}
