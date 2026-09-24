// Unit tests for the Fungus list revision (2026-09-24) -- see
// docs/design/fungus-list-revision-2026-09.md.
//
// Every case here guards a failure that is SILENT: the run finishes and prints an ordinary-looking
// number. That is the whole reason they are unit tests rather than something the regression suite
// would have caught -- the regression suite compares each deck to its own committed fingerprint,
// and none of these cards was in a committed deck when the bug existed.
//
//  1. DEPLETION COUNTERS ARE DOUBLED BY DOUBLING SEASON. The land drop used to stamp
//     Counter{Depletion, N} directly into perm.counters, bypassing every doubling chokepoint, so
//     Doubling Season was the one counter-doubler that could not see depletion. Under CR 121.6 /
//     614.1c "enters with N counters" is a replacement effect and the Season applies -- the same
//     rule the engine already relies on for devour's enters-with +1/+1 counters. This mattered
//     one-directionally: the candidate Fungus list pairs 4 Doubling Season with 8 depletion lands
//     and the shipped list has none, so the bug UNDER-rated exactly the arm under test.
//
//  2. ...and it is byte-identical without a doubler, which is what made the fix safe to land on
//     six committed decks that play depletion lands (Angels, BreachingDragonstorm, CritterLifegain,
//     Dragonstorm, Mirrorwing Dragon, treasure_hunt) -- none of which plays a counter-doubler.
//
//  3. CONCORDANT CROSSROADS grants haste through the shared HasHasteFromLords oracle even though
//     the granter is an ENCHANTMENT, not a creature. The scan applies no IsCreature() test, but
//     nothing previously depended on that, so it is asserted rather than assumed.
//
//  4. ...and it lifts BOTH halves of CR 302.6 -- attacking (CanAttackFull) and {T} abilities
//     (CanTapNow) -- because haste from any source lifts one restriction, not two separate ones.
//     This is load-bearing for the candidate list: it is what would let a Saproling created this
//     turn tap for mana if a granted tap-ability ever lands.
//
//  5. ...and it must NOT be mistaken for a P/T lord. IsLordPermanent requires a creature with a
//     nonzero bonus; a keyword-only, non-creature grant that slipped into ComputeLordBonus would
//     silently pump the board.
//
//  6. HICKORY WOODLOT is the green Peat Bog: same enters-tapped, same two depletion counters, same
//     two mana on tap. Asserted as a pair so a future edit to one cannot silently diverge.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"

#include <string>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        const auto path = ResolveHeuristicDefaultsPath("src/cards/data/cards.json");
        CardDatabase::Instance().LoadFromJson(path);
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

int Put(GameState& s, const std::string& name, int controller, int number, bool sick = false)
{
    Permanent p;
    p.card              = Def(name).card;
    p.card.m_number     = number;
    p.controller_index  = controller;
    p.owner_index       = controller;
    p.entered_this_turn = sick;
    p.def_absent        = false;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

// A bare Permanent standing in for the land being dropped. PutDepletionCounters is called before
// the permanent is pushed onto the battlefield (see LandPlay.cpp), which is exactly the shape
// reproduced here -- and it matters, because a land is not itself a doubler, so the count it sees
// must not depend on whether it is already on the battlefield.
Permanent EnteringLand(const std::string& name, int controller)
{
    Permanent p;
    p.card             = Def(name).card;
    p.controller_index = controller;
    p.owner_index      = controller;
    return p;
}

int DepletionOn(const Permanent& p)
{
    for (const Counter& c : p.counters)
    { if (c.type == Counter::Type::Depletion) { return c.count; } }
    return 0;
}

}  // namespace

TEST_CASE("Depletion counters are doubled by Doubling Season (CR 121.6 / 614.1c)")
{
    EnsureCardsLoaded();

    // The deck-level presence stamp is what DoublerShift consults before it walks the battlefield;
    // without it the walk is skipped entirely, which is the per-game optimisation the real runner
    // sets from the decklist.
    GameState s;
    s.deck_has_counter_doubler = true;

    const int bog_n = Def("Peat Bog").params.enters_tapped_with_depletion;
    REQUIRE(bog_n == 2);

    SUBCASE("no doubler on the battlefield -> printed value, unchanged")
    {
        Permanent land = EnteringLand("Peat Bog", 0);
        PutDepletionCounters(s, land, bog_n);
        CHECK(DepletionOn(land) == 2);
    }

    SUBCASE("one Doubling Season -> four, i.e. twice the mana over the land's life")
    {
        Put(s, "Doubling Season", 0, 100);
        Permanent land = EnteringLand("Peat Bog", 0);
        PutDepletionCounters(s, land, bog_n);
        CHECK(DepletionOn(land) == 4);
    }

    SUBCASE("copies MULTIPLY -- two Seasons is x4, not x2 (each is its own replacement)")
    {
        Put(s, "Doubling Season", 0, 100);
        Put(s, "Doubling Season", 0, 101);
        Permanent land = EnteringLand("Hickory Woodlot", 0);
        PutDepletionCounters(s, land, Def("Hickory Woodlot").params.enters_tapped_with_depletion);
        CHECK(DepletionOn(land) == 8);
    }

    SUBCASE("the doubler is per-CONTROLLER: the opponent's Season does not double ours")
    {
        Put(s, "Doubling Season", 1, 100);
        Permanent land = EnteringLand("Peat Bog", 0);
        PutDepletionCounters(s, land, bog_n);
        CHECK(DepletionOn(land) == 2);
    }

    SUBCASE("zero is not a counter event")
    {
        Put(s, "Doubling Season", 0, 100);
        Permanent land = EnteringLand("Peat Bog", 0);
        PutDepletionCounters(s, land, 0);
        CHECK(land.counters.empty());
    }
}

TEST_CASE("Depletion is unchanged for a deck with no counter-doubler (the six shipped decks)")
{
    EnsureCardsLoaded();

    // deck_has_counter_doubler defaults from the decklist and is FALSE for every committed deck
    // that plays a depletion land. This is the assertion that made the chokepoint change safe to
    // land without a rebaseline: smoke reported play-changed=0 across all 93 configs.
    GameState s;
    s.deck_has_counter_doubler = false;
    Put(s, "Doubling Season", 0, 100);   // even present on board, the per-game stamp short-circuits

    for (const char* name : { "Peat Bog", "Sandstone Needle", "Saprazzan Skerry", "Remote Farm" })
    {
        Permanent land = EnteringLand(name, 0);
        const int n = Def(name).params.enters_tapped_with_depletion;
        PutDepletionCounters(s, land, n);
        CHECK_MESSAGE(DepletionOn(land) == n, "depletion changed for ", name);
    }
}

TEST_CASE("Hickory Woodlot is the green Peat Bog -- same shape, different colour")
{
    EnsureCardsLoaded();
    const CardParams& hw  = Def("Hickory Woodlot").params;
    const CardParams& bog = Def("Peat Bog").params;

    CHECK(hw.enters_tapped);
    CHECK(hw.enters_tapped_with_depletion == bog.enters_tapped_with_depletion);
    CHECK(hw.produces_amount == bog.produces_amount);
    CHECK(hw.produces_amount == 2);
    REQUIRE(hw.produces.size() == 1);
    REQUIRE(bog.produces.size() == 1);
    CHECK(hw.produces[0]  == Color::Green);
    CHECK(bog.produces[0] == Color::Black);
}

TEST_CASE("Concordant Crossroads: a NON-CREATURE granter hastes the whole team")
{
    EnsureCardsLoaded();

    GameState s;
    const int sap = Put(s, "Thallid", 0, 1, /*sick=*/true);
    REQUIRE(s.battlefield[sap].entered_this_turn);

    SUBCASE("without it, a creature that entered this turn cannot attack")
    {
        CHECK_FALSE(CanAttackFull(s.battlefield[sap], s.battlefield, 0));
    }

    SUBCASE("with it, the same creature can -- and the granter is an ENCHANTMENT")
    {
        const int cc = Put(s, "Concordant Crossroads", 0, 2);
        REQUIRE_FALSE(s.battlefield[cc].card.IsCreature());
        CHECK(CanAttackFull(s.battlefield[sap], s.battlefield, 0));
    }

    SUBCASE("it lifts BOTH halves of CR 302.6 -- {T} abilities too, not only attacking")
    {
        Put(s, "Concordant Crossroads", 0, 2);
        CHECK(CanTapNow(s.battlefield[sap], s.battlefield));
    }

    SUBCASE("it is NOT a P/T lord -- a keyword grant must not reach ComputeLordBonus")
    {
        const int cc = Put(s, "Concordant Crossroads", 0, 2);
        CHECK_FALSE(IsLordPermanent(Def("Concordant Crossroads")));
        // The Thallid is a printed 1/1 and stays one: no stat bonus leaks out of the grant.
        CHECK(s.battlefield[sap].EffectivePower() == 1);
        CHECK(s.battlefield[cc].card.IsEnchantment());
    }

    SUBCASE("the grant is controller-scoped (the disclosed one-sided collapse)")
    {
        Put(s, "Concordant Crossroads", 1, 2);   // the OPPONENT's copy
        CHECK_FALSE(CanAttackFull(s.battlefield[sap], s.battlefield, 0));
    }
}
