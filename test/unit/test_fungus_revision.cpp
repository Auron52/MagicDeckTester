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
#include "ai/Combat.h"

#include <string>
#include <vector>

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

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

// A 1/1 green Saproling TOKEN -- no CardDefinition, def_absent set, subtype written onto the Card
// exactly as CreateTokenOnce does. This is the population Shroofus actually watches, and the whole
// reason the trigger matches on Card::m_subtypes rather than on a definition.
int PutSaprolingToken(GameState& s, int number, bool sick = false)
{
    Permanent p;
    p.card.m_name       = "1/1 Saproling Token";
    p.card.RehashName();
    p.card.AddColor(Color::Green);
    p.card.AddType(CardType::Creature);
    p.card.m_subtypes   = { "Saproling" };
    p.card.m_power      = 1;
    p.card.m_toughness  = 1;
    p.card.m_number     = number;
    p.is_token          = true;
    p.def_absent        = true;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

const Permanent& ByNumber(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return p; } }
    static Permanent none;
    REQUIRE_MESSAGE(false, "permanent not found: ", number);
    return none;
}

int CountSaprolings(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    { if (CardHasSubtype(p.card, "Saproling")) { ++n; } }
    return n;
}

// Attack with every creature currently on our side.
std::vector<int> AllOurCreatures(const GameState& s)
{
    std::vector<int> v;
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    {
        const Permanent& p = s.battlefield[i];
        if (p.controller_index == 0 && p.card.IsCreature()) { v.push_back(i); }
    }
    return v;
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

TEST_CASE("Shroofus Sproutsire: 'that many' is the DAMAGE DEALT, not the printed power")
{
    EnsureCardsLoaded();

    SUBCASE("a lone Shroofus connects for 1 and makes exactly one Saproling")
    {
        GameState s = Fresh();
        Put(s, "Shroofus Sproutsire", 0, 1);
        const auto atk = AllOurCreatures(s);
        const CombatDamageResult r = ResolveCombatDamage(s, atk, 0, false);
        CHECK(r.total_damage == 1);
        // Shroofus is ITSELF a Saproling, so it watches its own combat damage (the Utvara shape).
        CHECK(CountSaprolings(s) == 2);   // Shroofus + the one token it made
    }

    SUBCASE("IT FIRES FOR TOKENS -- the population the card exists to watch")
    {
        // Three definition-less 1/1 Saproling tokens attacking alongside Shroofus: 3 tokens x 1
        // damage + Shroofus x 1 = 4 damage, hence 4 new Saprolings. A CardDefinition-keyed watch
        // would have seen NONE of the three tokens.
        GameState s = Fresh();
        Put(s, "Shroofus Sproutsire", 0, 1);
        for (int i = 0; i < 3; ++i) { PutSaprolingToken(s, 10 + i); }
        const auto atk = AllOurCreatures(s);
        const CombatDamageResult r = ResolveCombatDamage(s, atk, 0, false);
        CHECK(r.total_damage == 4);
        CHECK(CountSaprolings(s) == 4 + 4);
    }

    SUBCASE("the count is POST-LORD: a Sporecrown makes each 1/1 Saproling connect for 2")
    {
        // Sporecrown Thallid: "Each OTHER creature you control that's a Fungus or Saproling gets
        // +1/+1." So the three tokens and Shroofus are all 2/2; Sporecrown itself is a 2/2 Fungus
        // that also attacks but is NOT a Saproling, so its 2 damage creates nothing.
        GameState s = Fresh();
        Put(s, "Shroofus Sproutsire", 0, 1);
        Put(s, "Sporecrown Thallid", 0, 2);
        for (int i = 0; i < 3; ++i) { PutSaprolingToken(s, 10 + i); }
        const auto atk = AllOurCreatures(s);
        const CombatDamageResult r = ResolveCombatDamage(s, atk, 0, false);
        CHECK(r.total_damage == 2 + 2 + 2 + 2 + 2);          // 4 pumped Saprolings + Sporecrown
        // Only the SAPROLINGS' damage counts: 4 bodies x 2 = 8, never the Fungus lord's 2.
        CHECK(CountSaprolings(s) == 4 + 8);
    }

    SUBCASE("a NON-Saproling attacker's damage is not counted")
    {
        GameState s = Fresh();
        Put(s, "Shroofus Sproutsire", 0, 1);
        Put(s, "Mycoloth", 0, 2);            // 4/4 Fungus, not a Saproling
        const auto atk = AllOurCreatures(s);
        const CombatDamageResult r = ResolveCombatDamage(s, atk, 0, false);
        CHECK(r.total_damage == 1 + 4);
        CHECK(CountSaprolings(s) == 1 + 1);  // only Shroofus's own 1 damage made a token
    }

    SUBCASE("Doubling Season compounds automatically through the CreateToken chokepoint")
    {
        GameState s = Fresh();
        Put(s, "Shroofus Sproutsire", 0, 1);
        Put(s, "Doubling Season", 0, 2);
        for (int i = 0; i < 3; ++i) { PutSaprolingToken(s, 10 + i); }
        const auto atk = AllOurCreatures(s);
        ResolveCombatDamage(s, atk, 0, false);
        CHECK(CountSaprolings(s) == 4 + 8);   // 4 damage x 2^1
    }

    SUBCASE("the tokens are summoning-sick -- they do NOT join the combat that made them")
    {
        GameState s = Fresh();
        Put(s, "Shroofus Sproutsire", 0, 1);
        const auto atk = AllOurCreatures(s);
        const CombatDamageResult r = ResolveCombatDamage(s, atk, 0, false);
        CHECK(r.total_damage == 1);           // the new token dealt nothing this combat
        for (const Permanent& p : s.battlefield)
        { if (p.is_token) { CHECK(p.entered_this_turn); } }
    }

    SUBCASE("it watches only OUR Saprolings -- an opponent's body is not our trigger")
    {
        GameState s = Fresh();
        Put(s, "Shroofus Sproutsire", 0, 1);
        PutSaprolingToken(s, 10);
        s.battlefield.back().controller_index = 1;   // theirs
        s.battlefield.back().owner_index      = 1;
        const auto atk = AllOurCreatures(s);         // only ours attack
        ResolveCombatDamage(s, atk, 0, false);
        CHECK(CountSaprolings(s) == 3);              // Shroofus + their token + our one new token
    }
}

TEST_CASE("Shroofus turns the second main on by itself, not parasitically off Mycoloth")
{
    EnsureCardsLoaded();
    // 2c-bis: combat creates Saprolings, which Utopia Mycon / Psychotrope Thallid / Mycoloth all
    // consume post-combat. Fungus already flips DeckUsesSecondMain via devour, but that clause is
    // gated on MTG_FUNGUS_M2_DEVOUR and a list revision could cut Mycoloth.
    CHECK(Def("Shroofus Sproutsire").params.combat_damage_tokens_per_damage > 0);
}

// ---------------------------------------------------------------------------------------------
// Slimefoot, the Stowaway. The archetype's first NON-COMBAT damage source, and the card whose
// highest-risk failure mode was silent: the death-watcher had to fire for definition-less TOKENS,
// which is ~95% of the Saprolings that ever die here. A CardDefinition-keyed subtype test would
// have compiled, passed every other check, and simply never triggered.
// ---------------------------------------------------------------------------------------------

TEST_CASE("Slimefoot: the drain fires for a definition-less Saproling TOKEN")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Slimefoot, the Stowaway", 0, 1);

    // Exactly what CreateTokenOnce produces: no CardDefinition, subtype on the Card itself.
    Card dead;
    dead.m_name = "1/1 Saproling Token";
    dead.RehashName();
    dead.AddType(CardType::Creature);
    dead.m_subtypes = { "Saproling" };
    REQUIRE(CardDatabase::Instance().LookupCached(dead) == nullptr);   // the trap, made explicit

    OnCreatureDies(s, 0, dead, /*dead_was_token=*/true, /*dead_minus_counters=*/0);
    CHECK(s.players[1].life == 19);
    CHECK(s.players[0].life == 21);
    CHECK(s.opponent_lost_life_this_turn);
}

TEST_CASE("Slimefoot: every Saproling death is a separate trigger -- the board IS the clock")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Slimefoot, the Stowaway", 0, 1);

    Card dead;
    dead.m_name = "1/1 Saproling Token";
    dead.RehashName();
    dead.AddType(CardType::Creature);
    dead.m_subtypes = { "Saproling" };

    for (int i = 0; i < 8; ++i) { OnCreatureDies(s, 0, dead, true, 0); }
    CHECK(s.players[1].life == 12);   // eight Saprolings sacrificed = eight damage, no attack step
    CHECK(s.players[0].life == 28);
}

TEST_CASE("Slimefoot: TWO copies are two independent watchers (per-copy, not max)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Slimefoot, the Stowaway", 0, 1);
    Put(s, "Slimefoot, the Stowaway", 0, 2);   // legend rule is a separate SBA; the watchers stack

    Card dead;
    dead.m_name = "1/1 Saproling Token";
    dead.RehashName();
    dead.AddType(CardType::Creature);
    dead.m_subtypes = { "Saproling" };

    OnCreatureDies(s, 0, dead, true, 0);
    CHECK(s.players[1].life == 18);
}

TEST_CASE("Slimefoot: the watch is SUBTYPE- and CONTROLLER-scoped, and excludes itself")
{
    EnsureCardsLoaded();

    SUBCASE("a dying FUNGUS is not a dying Saproling")
    {
        GameState s = Fresh();
        Put(s, "Slimefoot, the Stowaway", 0, 1);
        OnCreatureDies(s, 0, Def("Thallid").card, false, 0);   // Thallid is a Fungus
        CHECK(s.players[1].life == 20);
    }

    SUBCASE("Slimefoot's OWN death does not trigger it -- it is a Fungus, not a Saproling")
    {
        GameState s = Fresh();
        Put(s, "Slimefoot, the Stowaway", 0, 1);
        OnCreatureDies(s, 0, Def("Slimefoot, the Stowaway").card, false, 0);
        CHECK(s.players[1].life == 20);
    }

    SUBCASE("the OPPONENT's Saproling dying does not trigger our Slimefoot")
    {
        GameState s = Fresh();
        Put(s, "Slimefoot, the Stowaway", 0, 1);
        Card dead;
        dead.m_name = "1/1 Saproling Token";
        dead.RehashName();
        dead.AddType(CardType::Creature);
        dead.m_subtypes = { "Saproling" };
        OnCreatureDies(s, 1, dead, true, 0);   // died under the OPPONENT's control
        CHECK(s.players[1].life == 20);
    }

    SUBCASE("Shroofus Sproutsire IS a Saproling, so its death drains too")
    {
        GameState s = Fresh();
        Put(s, "Slimefoot, the Stowaway", 0, 1);
        OnCreatureDies(s, 0, Def("Shroofus Sproutsire").card, false, 0);
        CHECK(s.players[1].life == 19);
    }
}

TEST_CASE("Slimefoot: the {4} token maker has NO {T} -- repeatable, and legal while summoning-sick")
{
    EnsureCardsLoaded();
    const CardParams& q = Def("Slimefoot, the Stowaway").params;
    REQUIRE(q.pay_token_cost.has_value());
    CHECK(q.pay_token_cost->ManaValue() == 4);

    // The distinction against Sliver Hive's tap_token_cost, asserted rather than trusted: a {T} in
    // the cost would make this once-per-untap AND illegal the turn Slimefoot lands.
    CHECK_FALSE(PermAbilityTaps(PermAbilityMode::PayToken));

    GameState s = Fresh();
    Put(s, "Slimefoot, the Stowaway", 0, 1, /*sick=*/true);
    CHECK(PermAbilitySourceLive(s, 0, 1, PermAbilityMode::PayToken));
    s.battlefield[0].tapped = true;                                  // even tapped
    CHECK(PermAbilitySourceLive(s, 0, 1, PermAbilityMode::PayToken));
}

TEST_CASE("Slimefoot: {4} makes a Saproling, and Doubling Season makes it two")
{
    EnsureCardsLoaded();

    SUBCASE("one activation, one Saproling")
    {
        GameState s = Fresh();
        Put(s, "Slimefoot, the Stowaway", 0, 1);
        ApplyPermAbility(s, 0, 1, PermAbilityMode::PayToken);
        CHECK(CountSaprolings(s) == 1);
    }

    SUBCASE("through the CreateToken chokepoint, so a Season doubles it")
    {
        GameState s = Fresh();
        s.deck_has_token_doubler = true;
        Put(s, "Slimefoot, the Stowaway", 0, 1);
        Put(s, "Doubling Season", 0, 2);
        ApplyPermAbility(s, 0, 1, PermAbilityMode::PayToken);
        CHECK(CountSaprolings(s) == 2);
    }

    SUBCASE("...and each of those tokens is itself future Slimefoot damage")
    {
        GameState s = Fresh();
        Put(s, "Slimefoot, the Stowaway", 0, 1);
        ApplyPermAbility(s, 0, 1, PermAbilityMode::PayToken);
        REQUIRE(CountSaprolings(s) == 1);
        // Find the token we just made and kill it.
        Card tok;
        for (const Permanent& p : s.battlefield)
        { if (p.is_token) { tok = p.card; } }
        OnCreatureDies(s, 0, tok, true, 0);
        CHECK(s.players[1].life == 19);
    }
}

TEST_CASE("DEATHSPORE + SLIMEFOOT: the '-1/-1' clause is a FREE two-for-one sac outlet")
{
    EnsureCardsLoaded();
    // This is the interaction that makes the candidate list a different deck rather than a faster
    // one, and it is why Deathspore Thallid's -1/-1 must NOT be written off as goldfish-inert:
    // sacrifice one Saproling to the outlet (death #1) and shrink a SECOND 1/1 Saproling to 0/0,
    // which dies to the toughness SBA (death #2). Two deaths, no mana, no combat, no summoning
    // sickness -- so with Slimefoot out the whole token board converts to damage at 1 per body.
    //
    // Deathspore itself is not implemented yet; this asserts the half that already exists -- that
    // two Saproling deaths in one activation window are two drains -- so the arithmetic is pinned
    // before the outlet lands.
    GameState s = Fresh();
    Put(s, "Slimefoot, the Stowaway", 0, 1);
    Card tok;
    tok.m_name = "1/1 Saproling Token";
    tok.RehashName();
    tok.AddType(CardType::Creature);
    tok.m_subtypes = { "Saproling" };
    OnCreatureDies(s, 0, tok, true, 0);   // the sacrificed fodder
    OnCreatureDies(s, 0, tok, true, 0);   // the one shrunk to 0/0
    CHECK(s.players[1].life == 18);
}

// ---------------------------------------------------------------------------------------------
// Vitaspore Thallid / Deathspore Thallid -- the two TARGETED sac-outlet payloads.
// ---------------------------------------------------------------------------------------------

TEST_CASE("Vitaspore Thallid: sac a Saproling, haste a creature that could not attack")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Vitaspore Thallid", 0, 1);
    const int sick = Put(s, "Mycoloth", 0, 2, /*sick=*/true);   // a fresh 4/4, cannot attack
    PutSaprolingToken(s, 10);                                   // the fodder
    REQUIRE_FALSE(CanAttackFull(s.battlefield[sick], s.battlefield, 0));

    ApplySacCreatureOutlet(s, 0, /*source_id=*/1, /*victim_id=*/10);

    // The fodder is gone and the summoning-sick body can now attack -- the ranking prefers OUR
    // biggest creature that could not already attack.
    CHECK(CountSaprolings(s) == 0);
    const Permanent& myc = ByNumber(s, 2);
    CHECK(myc.temp_haste);
    CHECK(CanAttackFull(myc, s.battlefield, 0));
}

TEST_CASE("Vitaspore Thallid: the outlet is FREE and repeatable (no mana, no {T})")
{
    EnsureCardsLoaded();
    const CardParams& q = Def("Vitaspore Thallid").params;
    CHECK_FALSE(q.sac_creature_cost.has_value());       // the Utopia Mycon shape
    CHECK(q.sac_creature_requires_subtype == "Saproling");
    CHECK(q.sac_outlet_grants_haste);
    // A FUNGUS, so the subtype filter alone forbids it eating itself.
    CHECK_FALSE(CardHasSubtype(Def("Vitaspore Thallid").card, "Saproling"));
}

TEST_CASE("Deathspore Thallid: the '-1/-1' is a FREE TWO-FOR-ONE, not an inert clause")
{
    EnsureCardsLoaded();

    SUBCASE("alone it is two dead Saprolings and nothing else")
    {
        GameState s = Fresh();
        Put(s, "Deathspore Thallid", 0, 1);
        PutSaprolingToken(s, 10);   // sacrificed as the cost
        PutSaprolingToken(s, 11);   // shrunk to 0/0 and killed by the SBA
        ApplySacCreatureOutlet(s, 0, 1, 10);
        CHECK(CountSaprolings(s) == 0);
    }

    SUBCASE("WITH SLIMEFOOT it is TWO DAMAGE per activation, no mana and no combat")
    {
        // This is the interaction that makes the candidate list a different deck rather than a
        // faster one. One activation = the sacrificed fodder (death 1) + the shrunk body (death 2).
        GameState s = Fresh();
        Put(s, "Deathspore Thallid", 0, 1);
        Put(s, "Slimefoot, the Stowaway", 0, 2);
        PutSaprolingToken(s, 10);
        PutSaprolingToken(s, 11);
        ApplySacCreatureOutlet(s, 0, 1, 10);
        CHECK(s.players[1].life == 18);
        CHECK(s.players[0].life == 22);
        CHECK(CountSaprolings(s) == 0);
    }

    SUBCASE("a Saproling under a SPORECROWN is a 2/2 and correctly SURVIVES -1/-1")
    {
        // The SBA must read the full toughness -- base + lords + auras + equipment -- not the
        // printed 1. Getting this wrong would invent a free kill on every token.
        GameState s = Fresh();
        Put(s, "Deathspore Thallid", 0, 1);
        Put(s, "Slimefoot, the Stowaway", 0, 2);
        Put(s, "Sporecrown Thallid", 0, 3);   // +1/+1 to each other Fungus or Saproling
        PutSaprolingToken(s, 10);
        PutSaprolingToken(s, 11);
        ApplySacCreatureOutlet(s, 0, 1, 10);
        CHECK(CountSaprolings(s) == 1);   // the second token is a 2/2 -> survives as a 1/1
        CHECK(s.players[1].life == 19);   // only the sacrificed fodder drained
    }

    SUBCASE("the outlet is free, and Deathspore is a Zombie Fungus that Sporecrown still pumps")
    {
        const CardParams& q = Def("Deathspore Thallid").params;
        CHECK_FALSE(q.sac_creature_cost.has_value());
        CHECK(q.sac_outlet_minus_power == -1);
        CHECK(q.sac_outlet_minus_tough == -1);
        const Card& c = Def("Deathspore Thallid").card;
        CHECK(CardHasSubtype(c, "Zombie"));
        CHECK(CardHasSubtype(c, "Fungus"));
        CHECK_FALSE(CardHasSubtype(c, "Saproling"));   // so it cannot eat itself
    }
}

TEST_CASE("Both new spore bodies join the EXISTING spore pool byte-identically")
{
    EnsureCardsLoaded();
    // FoldSporeSourceIdentity waives "which body pays" only because every outlet's payload is
    // identical. Adding two more must not break that -- if it did, the pool would silently start
    // conflating outlets that are not interchangeable.
    const CardParams& ref = Def("Thallid").params;
    for (const char* n : { "Vitaspore Thallid", "Deathspore Thallid" })
    {
        const CardParams& q = Def(n).params;
        CHECK_MESSAGE(q.spore_saproling_cost  == ref.spore_saproling_cost,  n);
        CHECK_MESSAGE(q.spore_creates_tokens  == ref.spore_creates_tokens,  n);
        CHECK_MESSAGE(q.spore_token_power     == ref.spore_token_power,     n);
        CHECK_MESSAGE(q.spore_token_toughness == ref.spore_token_toughness, n);
        CHECK_MESSAGE(q.spore_token_color     == ref.spore_token_color,     n);
        CHECK_MESSAGE(q.spore_token_subtypes  == ref.spore_token_subtypes,  n);
        CHECK_MESSAGE(q.spore_upkeep_self     == ref.spore_upkeep_self,     n);
    }
}

TEST_CASE("Deathspore Thallid: the target ranking must not eat its own engine")
{
    EnsureCardsLoaded();
    // REGRESSION GUARD. The first ranking was "anything the shrink kills, cheapest first", which
    // is wrong in a way that only shows up on this card: Deathspore is ITSELF a 1/1, so it tied
    // with its own fodder and -- losing the tie-break on m_number -- killed itself on the first
    // activation. Whose death is worth having is the question, not merely whose death is possible.

    SUBCASE("with a watcher out it kills the Saproling, never itself")
    {
        GameState s = Fresh();
        Put(s, "Deathspore Thallid", 0, 1);
        Put(s, "Slimefoot, the Stowaway", 0, 2);
        PutSaprolingToken(s, 10);
        PutSaprolingToken(s, 11);
        ApplySacCreatureOutlet(s, 0, 1, 10);
        bool outlet_alive = false;
        for (const Permanent& p : s.battlefield)
        { if (p.card.m_number == 1) { outlet_alive = true; } }
        CHECK(outlet_alive);
    }

    SUBCASE("with NO death watcher, killing our own body pays nothing -- prefer the opponent's")
    {
        // Tier 1 (an opponent creature that dies) beats tier 2 (our own body whose death pays
        // nothing), so the free kill goes across the table rather than into our own board.
        GameState s = Fresh();
        Put(s, "Deathspore Thallid", 0, 1);
        PutSaprolingToken(s, 11);
        const int opp = PutSaprolingToken(s, 20);
        s.battlefield[opp].controller_index = 1;
        s.battlefield[opp].owner_index      = 1;
        PutSaprolingToken(s, 10);   // the fodder
        ApplySacCreatureOutlet(s, 0, 1, 10);
        bool ours_alive = false, theirs_alive = false;
        for (const Permanent& p : s.battlefield)
        {
            if (p.card.m_number == 11) { ours_alive = true; }
            if (p.card.m_number == 20) { theirs_alive = true; }
        }
        CHECK(ours_alive);
        CHECK_FALSE(theirs_alive);
    }

    SUBCASE("DeathOfWouldPay is what draws the line, and it reads the BOARD not the card pool")
    {
        GameState s = Fresh();
        Put(s, "Deathspore Thallid", 0, 1);
        const int t = PutSaprolingToken(s, 11);
        CHECK_FALSE(DeathOfWouldPay(s, 0, s.battlefield[t]));   // no watcher yet
        Put(s, "Slimefoot, the Stowaway", 0, 2);
        CHECK(DeathOfWouldPay(s, 0, s.battlefield[t]));         // now it pays
        // ...and it is subtype-scoped: Slimefoot does not pay for a Fungus dying.
        const int f = Put(s, "Thallid", 0, 3);
        CHECK_FALSE(DeathOfWouldPay(s, 0, s.battlefield[f]));
    }
}
