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
#include "ai/ManaPayment.h"

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

// ---- OFFER / EXECUTOR PARITY for the viewer's manual tap ------------------------------------
//
// The viewer publishes `taps` per permanent from HumanPreTapFaces (the OFFER) and then executes a
// human's click through ApplyHumanPreTap (the EXECUTOR). They are two functions asking one
// question, so the ONLY safe relation between them is a biconditional: every face the offer
// publishes must be accepted, and every face it withholds must be refused.
//
// This helper checks that relation over a WHOLE board rather than a named source, because the
// defect it exists to catch is not "this card is wrong" but "these two halves disagree" -- which
// is invisible to any test that drives one half alone. Each tap gets a fresh copy of the state:
// a tap mutates (it taps the permanent and adds to the float), so reusing one board would make
// every source after the first fail for the wrong reason.
//
// The token is built and PARSED rather than hand-filling a PreTap, so the assertion runs the exact
// route a human's `tap=` click takes -- parser included.
std::vector<std::string> PreTapOfferMismatches(const GameState& s)
{
    std::vector<std::string> bad;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index != s.active_player_index || p.tapped) { continue; }
        const std::string where = p.card.m_name.str() + "#" + std::to_string(p.card.m_number);
        const std::string faces = HumanPreTapFaces(s, p);
        for (char f : std::string("WUBRGC"))
        {
            const bool offered = faces.find(f) != std::string::npos;
            TurnSolver::PreTap t;
            if (!ParseHumanPreTapToken("tap=" + where + ":" + std::string(1, f), t))
            { bad.push_back(where + ":" + f + " -- the token did not parse"); continue; }
            GameState scratch = s;                    // a tap mutates; never reuse a board
            const std::string why = ApplyHumanPreTap(scratch, t);
            if (offered && !why.empty())
            { bad.push_back(where + ":" + f + " -- OFFERED but refused: " + why); }
            else if (!offered && why.empty())
            { bad.push_back(where + ":" + f + " -- NOT offered but accepted"); }
            else if (offered && scratch.floating_mana.Total() == 0)
            { bad.push_back(where + ":" + f + " -- accepted but added no mana"); }
        }
    }
    return bad;
}

std::string Joined(const std::vector<std::string>& v)
{
    std::string out;
    for (const std::string& s : v) { out += "\n    "; out += s; }
    return out.empty() ? std::string("(none)") : out;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// THE HALF THAT HAD NO TEST.
//
// USER-REPORTED twice (2026-09-25): "Brightcap Badger turning critters into mana producers is not
// working", and then -- after the grant had been verified end to end through the payer -- "Tapping
// with Badger out is still not working?" The second report was right and the first verdict was
// wrong, and the reason the verdict was wrong is structural, not a missed case:
//
// The 22 assertions in "granted Saprolings are REAL mana, through the payer" above drive
// AvailableManaPool, UntappedManaUpperBound, TapForCostShared and HumanPreTapFaces. Every one of
// them passed, and every one of them still passes -- the payer and the OFFER were always correct.
// ApplyHumanPreTap, the half a human's click actually runs, had NO test at all: it re-asked
// CardDatabase for the definition, and every Saproling is a TOKEN, so it refused "has no card
// definition" on exactly the population the card exists to create. Two independently-green halves,
// one broken feature.
//
// So this case asserts the RELATION, not a card. It sweeps the board and requires the offer and
// the executor to agree face by face in BOTH directions, which is the only shape that could have
// failed while both halves' own tests were green. The manual tap is human-play-only -- no digest,
// no GT cell, no protocol replay reaches it -- so a unit test is the only thing that can.
// ---------------------------------------------------------------------------------------------
TEST_CASE("Manual tap: the offer and the executor are the same question (Badger, tokens, lands)")
{
    EnsureCardsLoaded();

    SUBCASE("THE REPORT: with a Badger out, a Saproling TOKEN is offered {G} and the tap takes")
    {
        GameState s = Fresh();
        Put(s, "Brightcap Badger", 0, 50);
        PutSaprolingToken(s, 1000);

        const Permanent& tok = ByNumber(s, 1000);
        CHECK(HumanPreTapFaces(s, tok) == "G");        // the offer -- this was always right

        TurnSolver::PreTap t;
        REQUIRE(ParseHumanPreTapToken("tap=1/1 Saproling Token#1000:G", t));
        CHECK(ApplyHumanPreTap(s, t) == "");           // the executor -- this is what was broken
        CHECK(s.floating_mana.green == 1);
        CHECK(ByNumber(s, 1000).tapped);
    }

    SUBCASE("...and WITHOUT the Badger the same click is refused -- the grant is what opens it")
    {
        GameState s = Fresh();
        PutSaprolingToken(s, 1000);
        CHECK(HumanPreTapFaces(s, ByNumber(s, 1000)) == "");

        TurnSolver::PreTap t;
        REQUIRE(ParseHumanPreTapToken("tap=1/1 Saproling Token#1000:G", t));
        CHECK(ApplyHumanPreTap(s, t) != "");           // refused by BOTH halves, which is parity too
        CHECK(s.floating_mana.Total() == 0);
    }

    SUBCASE("PARITY over a mixed board: tokens, granted Fungi, a real dork and real lands")
    {
        // Every population the grant touches, plus the ones it must not, on one battlefield:
        //   * Saproling tokens          -- no CardDefinition at all (the reported failure)
        //   * Thallid / Sporecrown      -- a real definition, but no mana ability of their own
        //   * Undercellar Myconid       -- already a mana dork; the grant must not downgrade it
        //   * Brightcap Badger itself   -- a Badger Druid, NOT a Fungus: never a source
        //   * Forest / Hickory Woodlot  -- ordinary lands, which must be unaffected by any of it
        GameState s = Fresh();
        Put(s, "Brightcap Badger",    0, 50);
        Put(s, "Utopia Mycon",        0, 60);
        Put(s, "Thallid",             0, 61);
        Put(s, "Sporecrown Thallid",  0, 62);
        Put(s, "Undercellar Myconid", 0, 63);
        Put(s, "Forest",              0, 70);
        Put(s, "Hickory Woodlot",     0, 71);
        for (int i = 0; i < 3; ++i) { PutSaprolingToken(s, 100 + i); }

        const std::vector<std::string> bad = PreTapOfferMismatches(s);
        CHECK_MESSAGE(bad.empty(), "offer/executor disagreed on:", Joined(bad));
    }

    SUBCASE("PARITY holds on the boards where the offer must say NO")
    {
        // Summoning sickness (CR 302.6), a tapped body, an opponent's Badger, and the deck stamp
        // shut. Each closes the offer, so each must close the executor -- the direction that would
        // let a human mint mana the search cannot see.
        GameState sick = Fresh();
        Put(sick, "Brightcap Badger", 0, 50);
        for (int i = 0; i < 3; ++i) { PutSaprolingToken(sick, 100 + i, /*sick=*/true); }
        CHECK_MESSAGE(PreTapOfferMismatches(sick).empty(),
                      "summoning-sick:", Joined(PreTapOfferMismatches(sick)));

        GameState theirs = Fresh();
        Put(theirs, "Brightcap Badger", 1, 50);                       // THEIR Badger
        for (int i = 0; i < 3; ++i) { PutSaprolingToken(theirs, 100 + i); }
        CHECK_MESSAGE(PreTapOfferMismatches(theirs).empty(),
                      "opponent's Badger:", Joined(PreTapOfferMismatches(theirs)));

        GameState stamped = Fresh();
        Put(stamped, "Brightcap Badger", 0, 50);
        for (int i = 0; i < 3; ++i) { PutSaprolingToken(stamped, 100 + i); }
        stamped.deck_has_mana_grant = false;
        CHECK_MESSAGE(PreTapOfferMismatches(stamped).empty(),
                      "grant stamp shut:", Joined(PreTapOfferMismatches(stamped)));
    }

    SUBCASE("Concordant Crossroads lifts sickness for the manual tap too, not just the payer")
    {
        // The payer's version of this is asserted above via AvailableManaPool. The human's click
        // runs a different function, so it gets its own assertion rather than an inference.
        GameState s = Fresh();
        Put(s, "Brightcap Badger",      0, 50);
        Put(s, "Concordant Crossroads", 0, 51);
        PutSaprolingToken(s, 1000, /*sick=*/true);

        CHECK(HumanPreTapFaces(s, ByNumber(s, 1000)) == "G");
        TurnSolver::PreTap t;
        REQUIRE(ParseHumanPreTapToken("tap=1/1 Saproling Token#1000:G", t));
        CHECK(ApplyHumanPreTap(s, t) == "");
        CHECK(s.floating_mana.green == 1);
    }

    SUBCASE("a wide granted board stays in parity -- the payer's 64-source cap is not the offer's")
    {
        // The backtracker caps how many granted tokens one PAYMENT may consider. The manual tap is
        // one permanent at a time and must not inherit that cap: token #79 is as tappable by hand
        // as token #0, or a human on a wide board would find clicks silently dead.
        GameState s = Fresh();
        Put(s, "Brightcap Badger", 0, 50);
        for (int i = 0; i < 80; ++i) { PutSaprolingToken(s, 100 + i); }
        const std::vector<std::string> bad = PreTapOfferMismatches(s);
        CHECK_MESSAGE(bad.empty(), "wide board:", Joined(bad));
    }
}

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

// ---------------------------------------------------------------------------------------------
// Saproling Burst -- the engine's first FADING card. Three things here are easy to get silently
// wrong and each has its own case: the off-by-one in "remove one, or sacrifice if you can't", the
// token P/T tracking the SOURCE's live counter total, and the leaves-the-battlefield sweep that is
// the card's real cost.
// ---------------------------------------------------------------------------------------------

namespace
{
int FadeOn(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield)
    { if (p.card.m_number == number) { return p.fade_counters; } }
    return -1;
}
bool OnBoard(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return true; } }
    return false;
}
// Put a Burst on the battlefield the way the enter site does -- through PutFadeCounters, so the
// doubler applies.
int PutBurst(GameState& s, int number)
{
    const int i = Put(s, "Saproling Burst", 0, number);
    PutFadeCounters(s, s.battlefield[i], Def("Saproling Burst").params.fading_counters);
    return i;
}
}  // namespace

TEST_CASE("Saproling Burst: Fading 7 enters with 7, and 14 under a Doubling Season")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    s.deck_has_counter_doubler = true;

    SUBCASE("plain")
    {
        PutBurst(s, 1);
        CHECK(FadeOn(s, 1) == 7);
    }
    SUBCASE("one Doubling Season -- counters a permanent ENTERS WITH are doubled (CR 121.6)")
    {
        Put(s, "Doubling Season", 0, 99);
        PutBurst(s, 1);
        CHECK(FadeOn(s, 1) == 14);
    }
}

TEST_CASE("Saproling Burst: the sacrifice is on FAILURE TO REMOVE, not on reaching zero")
{
    EnsureCardsLoaded();
    // THE OFF-BY-ONE. Fading 7 takes SEVEN decrements and is sacrificed on the EIGHTH upkeep --
    // living eight of the controller's upkeeps. Reading it as "sacrifice when removing one reaches
    // zero" would cut a whole turn off the card.
    GameState s = Fresh();
    PutBurst(s, 1);
    for (int up = 1; up <= 7; ++up)
    {
        PerformUpkeepFading(s);
        CHECK_MESSAGE(OnBoard(s, 1), "sacrificed too early, at upkeep ", up);
        CHECK_MESSAGE(FadeOn(s, 1) == 7 - up, "wrong count at upkeep ", up);
    }
    CHECK(FadeOn(s, 1) == 0);
    PerformUpkeepFading(s);            // the EIGHTH: cannot remove one
    CHECK_FALSE(OnBoard(s, 1));
    CHECK(s.players[0].graveyard.size() == 1);
}

TEST_CASE("Saproling Burst: the token's P/T tracks the SOURCE's live counter total (CR 604.3)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    PutBurst(s, 1);
    REQUIRE(FadeOn(s, 1) == 7);

    // Pay first, THEN create: an activation at seven leaves six and mints a 6/6, never a 7/7.
    ApplyPermAbility(s, 0, 1, PermAbilityMode::FadeSaproling);
    CHECK(FadeOn(s, 1) == 6);
    REQUIRE(CountSaprolings(s) == 1);
    for (const Permanent& p : s.battlefield)
    { if (p.is_token) { CHECK(p.EffectivePower() == 6); CHECK(p.EffectiveToughness() == 6); } }

    // A second activation shrinks BOTH -- they all track the same live count.
    ApplyPermAbility(s, 0, 1, PermAbilityMode::FadeSaproling);
    CHECK(FadeOn(s, 1) == 5);
    REQUIRE(CountSaprolings(s) == 2);
    for (const Permanent& p : s.battlefield)
    { if (p.is_token) { CHECK(p.EffectivePower() == 5); } }

    // ...and so does the fading upkeep, which is the other site the count can change at.
    PerformUpkeepFading(s);
    CHECK(FadeOn(s, 1) == 4);
    for (const Permanent& p : s.battlefield)
    { if (p.is_token) { CHECK(p.EffectivePower() == 4); } }
}

TEST_CASE("Saproling Burst: at zero counters the tokens are 0/0 and die on the spot")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    PutBurst(s, 1);
    // Seven activations: the last leaves zero counters, so every token it ever made is a 0/0 and
    // the toughness SBA takes them all. "Pop everything" is a trap, which is exactly why K is a
    // searched axis with an interior optimum rather than a greedy max.
    for (int k = 0; k < 7; ++k) { ApplyPermAbility(s, 0, 1, PermAbilityMode::FadeSaproling); }
    CHECK(FadeOn(s, 1) == 0);
    CHECK(CountSaprolings(s) == 0);
}

TEST_CASE("Saproling Burst: a SPORECROWN lifts them off zero -- and then the LTB is what kills them")
{
    EnsureCardsLoaded();
    // The one case where the leaves-the-battlefield clause is not redundant with the 0/0 SBA.
    GameState s = Fresh();
    Put(s, "Sporecrown Thallid", 0, 50);   // +1/+1 to each other Fungus or Saproling
    PutBurst(s, 1);
    for (int k = 0; k < 7; ++k) { ApplyPermAbility(s, 0, 1, PermAbilityMode::FadeSaproling); }
    CHECK(FadeOn(s, 1) == 0);
    CHECK(CountSaprolings(s) == 7);        // 0/0 base + the lord = 1/1, so they survive

    PerformUpkeepFading(s);                // cannot remove one -> sacrifice -> LTB
    CHECK_FALSE(OnBoard(s, 1));
    CHECK(CountSaprolings(s) == 0);        // "destroy all tokens created with this enchantment"
}

TEST_CASE("Saproling Burst: the LTB sweep is per-SOURCE, not a board wipe")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Sporecrown Thallid", 0, 50);
    PutBurst(s, 1);
    PutBurst(s, 2);
    // Drain the FIRST Burst only.
    for (int k = 0; k < 7; ++k) { ApplyPermAbility(s, 0, 1, PermAbilityMode::FadeSaproling); }
    ApplyPermAbility(s, 0, 2, PermAbilityMode::FadeSaproling);   // the second makes one 6/6
    const int before = CountSaprolings(s);
    CHECK(before == 8);

    PerformUpkeepFading(s);
    CHECK_FALSE(OnBoard(s, 1));
    CHECK(OnBoard(s, 2));
    // Only the dead Burst's seven tokens went; the survivor's one is untouched (and shrank by the
    // upkeep decrement to 5/5).
    CHECK(CountSaprolings(s) == 1);
    CHECK(FadeOn(s, 2) == 5);
}

TEST_CASE("Saproling Burst: every token death routes through OnCreatureDies -- Slimefoot drains")
{
    EnsureCardsLoaded();
    // The Burst expiring is a burst of DAMAGE, not merely a board wipe. This is the interaction
    // that would have been silently lost had the sweep just erased the permanents.
    GameState s = Fresh();
    Put(s, "Sporecrown Thallid", 0, 50);
    Put(s, "Slimefoot, the Stowaway", 0, 51);
    PutBurst(s, 1);
    for (int k = 0; k < 7; ++k) { ApplyPermAbility(s, 0, 1, PermAbilityMode::FadeSaproling); }
    REQUIRE(CountSaprolings(s) == 7);
    const int life_before = s.players[1].life;
    PerformUpkeepFading(s);
    CHECK(s.players[1].life == life_before - 7);
}

TEST_CASE("Saproling Burst: activation has no {T}, is legal while sick, and is NOT spore-pooled")
{
    EnsureCardsLoaded();
    const CardParams& q = Def("Saproling Burst").params;
    CHECK(q.fading_counters == 7);
    CHECK(q.fade_saproling_cost == 1);
    CHECK(q.fade_ltb_destroys_created_tokens);
    CHECK_FALSE(PermAbilityTaps(PermAbilityMode::FadeSaproling));
    // It carries NO spore params, so FoldSporeSourceIdentity cannot fold it in with the Thallids --
    // two Bursts on different counts mint different-sized tokens and are genuinely distinct.
    CHECK(q.spore_saproling_cost == 0);

    GameState s = Fresh();
    const int i = Put(s, "Saproling Burst", 0, 1, /*sick=*/true);
    PutFadeCounters(s, s.battlefield[i], q.fading_counters);
    CHECK(PermAbilitySourceLive(s, 0, 1, PermAbilityMode::FadeSaproling));
}

// ---------------------------------------------------------------------------------------------
// Brightcap Badger // Fungus Frolic -- the engine's FIRST adventure card (CR 715).
//
// Nothing in test/unit or test/scenarios covered Player::staged_cards before this file: grep
// m_is_staged / staged_cards across both returned zero. The staged mechanic is load-bearing for
// Light Up the Stage, Apex of Power, Expressive Iteration and Rundvelt, so these cases are its
// first coverage as well as the adventure's.
// ---------------------------------------------------------------------------------------------

TEST_CASE("Brightcap Badger: the card data links both faces and neither is half-authored")
{
    EnsureCardsLoaded();

    const CardDefinition& badger = Def("Brightcap Badger");
    const CardDefinition& frolic = Def("Fungus Frolic");

    SUBCASE("the link is declared in BOTH directions")
    {
        CHECK(badger.params.adventure_face_name   == "Fungus Frolic");
        CHECK(frolic.params.adventure_parent_name == "Brightcap Badger");
    }

    SUBCASE("the faces are genuinely different spells")
    {
        CHECK(badger.card.IsCreature());
        CHECK_FALSE(badger.card.IsInstant());
        CHECK(frolic.card.IsInstant());
        CHECK_FALSE(frolic.card.IsCreature());
        // {3}{G} vs {2}{G} -- the cheaper mode is why neither dominates the other.
        CHECK(badger.card.m_mana_cost.ManaValue() == 4);
        CHECK(frolic.card.m_mana_cost.ManaValue() == 3);
        CHECK(badger.card.m_power.value_or(0)     == 3);
        CHECK(badger.card.m_toughness.value_or(0) == 4);
    }

    SUBCASE("the adventure payload is two Saprolings, not one and not a copy effect")
    {
        CHECK(frolic.params.cast_creates_tokens          == 2);
        CHECK(frolic.params.cast_created_token_power     == 1);
        CHECK(frolic.params.cast_created_token_toughness == 1);
        REQUIRE(frolic.params.cast_created_token_subtypes.size() == 1);
        CHECK(frolic.params.cast_created_token_subtypes[0] == "Saproling");
        CHECK(frolic.params.cast_created_token_color == "G");
    }
}

TEST_CASE("Fungus Frolic: the resolution payload creates two Saprolings, and Season doubles them")
{
    EnsureCardsLoaded();
    const CardDefinition& frolic = Def("Fungus Frolic");

    SUBCASE("bare: exactly two 1/1 green Saproling tokens")
    {
        GameState s = Fresh();
        ApplyCastCreatesTokens(s, 0, frolic);
        CHECK(CountSaprolings(s) == 2);
        REQUIRE(s.battlefield.size() == 2);
        for (const Permanent& p : s.battlefield)
        {
            CHECK(p.is_token);
            CHECK(p.card.m_power.value_or(0) == 1);
            CHECK(p.card.m_toughness.value_or(0) == 1);
            CHECK(CardHasSubtype(p.card, "Saproling"));
            CHECK(p.controller_index == 0);
        }
    }

    SUBCASE("Doubling Season applies -- these ride the universal CreateToken cascade")
    {
        GameState s = Fresh();
        Put(s, "Doubling Season", 0, 90);
        ApplyCastCreatesTokens(s, 0, frolic);
        CHECK(CountSaprolings(s) == 4);
    }

    SUBCASE("two Seasons quadruple, as for every other token this deck makes")
    {
        GameState s = Fresh();
        Put(s, "Doubling Season", 0, 90);
        Put(s, "Doubling Season", 0, 91);
        ApplyCastCreatesTokens(s, 0, frolic);
        CHECK(CountSaprolings(s) == 8);
    }

    SUBCASE("the payload is controller-scoped")
    {
        GameState s = Fresh();
        ApplyCastCreatesTokens(s, 1, frolic);
        REQUIRE(s.battlefield.size() == 2);
        // CountSaprolings is deliberately NOT controller-scoped (it counts the board), so scope
        // the assertion here rather than reading a side into that helper.
        int ours = 0, theirs = 0;
        for (const Permanent& p : s.battlefield)
        { (p.controller_index == 0 ? ours : theirs) += 1; }
        CHECK(ours   == 0);
        CHECK(theirs == 2);
    }

    SUBCASE("a card with no cast_creates_tokens makes nothing -- the param is inert by default")
    {
        GameState s = Fresh();
        ApplyCastCreatesTokens(s, 0, Def("Doubling Season"));
        CHECK(s.battlefield.empty());
    }
}

TEST_CASE("Adventure: the resolved spell EXILES to staged_cards, not to the graveyard")
{
    EnsureCardsLoaded();
    const CardDefinition& frolic = Def("Fungus Frolic");

    SUBCASE("the parent creature is staged, under the SAME per-copy number")
    {
        GameState s = Fresh();
        const bool staged = StageAdventureParent(s, 0, frolic, /*number=*/77);
        CHECK(staged);
        CHECK(s.players[0].graveyard.empty());
        REQUIRE(s.players[0].staged_cards.size() == 1);
        const StagedCard& sc = s.players[0].staged_cards[0];
        // It is the CREATURE that is staged -- casting the adventure again must be impossible,
        // and what you may cast later is the Badger.
        CHECK(sc.card.m_name.str() == "Brightcap Badger");
        CHECK(sc.card.IsCreature());
        // An adventure is ONE physical card: the number travels.
        CHECK(sc.card.m_number == 77);
    }

    SUBCASE("it NEVER expires -- unlike Light Up the Stage's window")
    {
        GameState s = Fresh();
        StageAdventureParent(s, 0, frolic, 77);
        REQUIRE(s.players[0].staged_cards.size() == 1);
        CHECK(s.players[0].staged_cards[0].expiry_turn == std::numeric_limits<int>::max());
        // The merge test is `expiry_turn < turn_number`; INT_MAX can never satisfy it.
        CHECK_FALSE(s.players[0].staged_cards[0].expiry_turn < 100000);
    }

    SUBCASE("it is controller-scoped")
    {
        GameState s = Fresh();
        StageAdventureParent(s, 1, frolic, 77);
        CHECK(s.players[0].staged_cards.empty());
        CHECK(s.players[1].staged_cards.size() == 1);
    }

    SUBCASE("a NON-adventure card declines, so its caller falls through to the graveyard")
    {
        GameState s = Fresh();
        CHECK_FALSE(StageAdventureParent(s, 0, Def("Doubling Season"), 77));
        CHECK(s.players[0].staged_cards.empty());
    }
}

TEST_CASE("Brightcap Badger: the unconditional end-step Saproling")
{
    EnsureCardsLoaded();
    const CardParams& bp = Def("Brightcap Badger").params;

    SUBCASE("it is flagged unconditional -- threshold 0 could NOT express this")
    {
        CHECK(bp.endstep_lifegain_tokens == 1);
        CHECK(bp.endstep_tokens_unconditional);
        // The gate clamps the threshold to a minimum of 1, which is exactly why the flag exists:
        // without it a Badger that gained no life this turn would never trigger.
        CHECK(std::max(1, bp.endstep_lifegain_threshold) == 1);
    }

    SUBCASE("every pre-existing member of the family stays CONDITIONAL")
    {
        // The flag must be false wherever it was not authored, or Resplendent Angel and Ocelot
        // Pride would start triggering on turns they gained nothing -- a silent behaviour change
        // in decks this work never touched.
        for (const std::string& n : { "Resplendent Angel", "Ocelot Pride" })
        {
            const CardDefinition* d = CardDatabase::Instance().Lookup(n);
            if (d == nullptr) { continue; }          // not every deck's cards are in every build
            CHECK_FALSE(d->params.endstep_tokens_unconditional);
        }
    }

    SUBCASE("the token spec is a 1/1 green Saproling")
    {
        CHECK(bp.endstep_token_power     == 1);
        CHECK(bp.endstep_token_toughness == 1);
        CHECK(bp.endstep_token_color     == "G");
        REQUIRE(bp.endstep_token_subtypes.size() == 1);
        CHECK(bp.endstep_token_subtypes[0] == "Saproling");
    }
}

TEST_CASE("Brightcap Badger: the mana grant's SUBTYPE reach is declared, and it is token-safe")
{
    EnsureCardsLoaded();
    const CardParams& bp = Def("Brightcap Badger").params;

    SUBCASE("it names both Fungus and Saproling, and produces green")
    {
        REQUIRE(bp.granted_tap_mana_subtypes.size() == 2);
        CHECK(bp.granted_tap_mana_subtypes[0] == "Fungus");
        CHECK(bp.granted_tap_mana_subtypes[1] == "Saproling");
        CHECK(bp.granted_tap_mana_color == "G");
    }

    SUBCASE("the population it must reach is DEFINITION-LESS -- the reason for the whole design")
    {
        // This is the fact that kills a param-only implementation: a Saproling token has no
        // CardDefinition at all, so any predicate that starts `if (!LookupCached(p.card)) continue;`
        // sees none of them. The subtype must be read off the permanent's own Card.
        GameState s = Fresh();
        const int tok = PutSaprolingToken(s, 1);
        CHECK(s.battlefield[tok].def_absent);
        CHECK(CardDatabase::Instance().LookupCached(s.battlefield[tok].card) == nullptr);
        CHECK(CardHasSubtype(s.battlefield[tok].card, "Saproling"));
    }

    SUBCASE("the Badger grants to others but is NOT itself a Fungus or Saproling")
    {
        // It is a Badger Druid. It taps for nothing; only what it grants to does.
        const Card& c = Def("Brightcap Badger").card;
        CHECK_FALSE(CardHasSubtype(c, "Fungus"));
        CHECK_FALSE(CardHasSubtype(c, "Saproling"));
        CHECK(CardHasSubtype(c, "Druid"));
    }

    SUBCASE("Utopia Mycon IS in reach -- it is a Fungus, so the grant is upside on it")
    {
        CHECK(CardHasSubtype(Def("Utopia Mycon").card, "Fungus"));
    }
}

TEST_CASE("Brightcap Badger x Concordant Crossroads: a fresh token may tap for the grant")
{
    EnsureCardsLoaded();

    // CR 302.6 restricts {T} abilities on a creature that has not been controlled since the
    // controller's most recent turn began. A granted "{T}: Add {G}" is a {T} ability, so a
    // Saproling created this turn cannot use it -- unless something grants haste. That makes
    // Badger x Crossroads a real interaction rather than two independent cards, and it is also
    // why screen 1's Crossroads result is the evidence that bought this build.
    GameState s = Fresh();
    const int tok = PutSaprolingToken(s, 1, /*sick=*/true);
    REQUIRE(s.battlefield[tok].entered_this_turn);

    SUBCASE("without a haste source the fresh token cannot tap")
    {
        CHECK_FALSE(CanTapNow(s.battlefield[tok], s.battlefield));
    }

    SUBCASE("with Concordant Crossroads it can -- no new code, the shared haste oracle resolves it")
    {
        Put(s, "Concordant Crossroads", 0, 2);
        CHECK(CanTapNow(s.battlefield[tok], s.battlefield));
    }

    SUBCASE("a token that has been around since the turn began never needed the haste")
    {
        GameState t = Fresh();
        const int old_tok = PutSaprolingToken(t, 1, /*sick=*/false);
        CHECK(CanTapNow(t.battlefield[old_tok], t.battlefield));
    }
}

// ---------------------------------------------------------------------------------------------
// Brightcap Badger's MANA GRANT, end to end through the real payment API.
//
// These are the cases that decide whether the grant works at all. A param-only implementation
// would pass every data test above and fail every one of these, because the population it must
// reach -- Saproling TOKENS -- has no CardDefinition, and the ~20 open-coded mana-source
// predicates all bail on that.
// ---------------------------------------------------------------------------------------------

TEST_CASE("Brightcap Badger: granted Saprolings are REAL mana, through the payer")
{
    EnsureCardsLoaded();

    auto board_with = [](bool badger, int saprolings, bool sick, bool crossroads)
    {
        GameState s = Fresh();
        if (badger)    { Put(s, "Brightcap Badger", 0, 50); }
        if (crossroads){ Put(s, "Concordant Crossroads", 0, 51); }
        for (int i = 0; i < saprolings; ++i) { PutSaprolingToken(s, 100 + i, sick); }
        return s;
    };

    SUBCASE("WITHOUT a Badger three Saprolings make nothing -- the control")
    {
        GameState s = board_with(false, 3, false, false);
        CHECK(AvailableManaPool(s).Total() == 0);
        CHECK(UntappedManaUpperBound(s, false, 0) == 0);
    }

    SUBCASE("WITH a Badger the same three tokens are three green mana")
    {
        GameState s = board_with(true, 3, false, false);
        CHECK(AvailableManaPool(s).green == 3);
        CHECK(UntappedManaUpperBound(s, false, 0) == 3);
    }

    SUBCASE("the Badger itself is NOT a source -- it is a Badger Druid, not a Fungus")
    {
        GameState s = board_with(true, 0, false, false);
        CHECK(AvailableManaPool(s).Total() == 0);
    }

    SUBCASE("and the payer actually SPENDS them")
    {
        GameState s = board_with(true, 3, false, false);
        ManaCost cost;               // {2}{G} -- Fungus Frolic's own cost, off zero lands
        cost.generic = 2;
        cost.green   = 1;
        CHECK(TapForCostShared(s, cost, /*for_creature=*/false, nullptr, false));
        int tapped = 0;
        for (const Permanent& p : s.battlefield) { if (p.tapped) { ++tapped; } }
        CHECK(tapped == 3);
    }

    SUBCASE("a cost one larger than the board is correctly REFUSED")
    {
        GameState s = board_with(true, 3, false, false);
        ManaCost cost;
        cost.generic = 3;
        cost.green   = 1;            // {3}{G} = 4 > 3 available
        CHECK_FALSE(TapForCostShared(s, cost, /*for_creature=*/false, nullptr, false));
    }

    SUBCASE("SUMMONING SICKNESS holds -- CR 302.6 applies to a granted {T} ability")
    {
        GameState s = board_with(true, 3, /*sick=*/true, false);
        CHECK(AvailableManaPool(s).Total() == 0);
        CHECK(UntappedManaUpperBound(s, false, 0) == 0);
    }

    SUBCASE("Concordant Crossroads lifts it -- THE interaction the screen bought this build for")
    {
        GameState s = board_with(true, 3, /*sick=*/true, /*crossroads=*/true);
        CHECK(AvailableManaPool(s).green == 3);
        CHECK(UntappedManaUpperBound(s, false, 0) == 3);
    }

    SUBCASE("a TAPPED granted body makes nothing")
    {
        GameState s = board_with(true, 3, false, false);
        for (Permanent& p : s.battlefield) { if (p.is_token) { p.tapped = true; } }
        CHECK(AvailableManaPool(s).Total() == 0);
    }

    SUBCASE("the grant is CONTROLLER-scoped -- an opponent's Badger grants us nothing")
    {
        GameState s = Fresh();
        Put(s, "Brightcap Badger", 1, 50);          // THEIR Badger
        for (int i = 0; i < 3; ++i) { PutSaprolingToken(s, 100 + i); }   // OUR tokens
        CHECK(AvailableManaPool(s).Total() == 0);
    }

    SUBCASE("THE SECOND POPULATION: Fungi that HAVE a definition but no mana ability")
    {
        // Missing this halves the card. Thallid, Sporecrown Thallid, Mycoloth, Sporesower Thallid
        // and Utopia Mycon are all Fungi with real CardDefinitions and NO {T}: add mana -- so a
        // resolver that only rescued definition-less tokens would grant them nothing. Utopia
        // Mycon's own mana ability costs a SACRIFICE, not {T}, so the granted tap does not even
        // conflict with it.
        GameState s = Fresh();
        Put(s, "Utopia Mycon",      0, 60);
        Put(s, "Thallid",           0, 61);
        Put(s, "Sporecrown Thallid",0, 62);
        CHECK(AvailableManaPool(s).Total() == 0);   // no Badger: three Fungi make nothing

        Put(s, "Brightcap Badger", 0, 50);
        CHECK(AvailableManaPool(s).green == 3);     // with it: three green
    }

    SUBCASE("a body that ALREADY taps for mana keeps its own, better ability")
    {
        // Undercellar Myconid is a Fungus AND a mana dork that taps for any colour. The grant must
        // not downgrade it to {G}: it can only tap once either way, so replacing a real definition
        // that is already a mana source could only ever lose colour flexibility.
        GameState s = Fresh();
        Put(s, "Brightcap Badger",   0, 50);
        Put(s, "Undercellar Myconid",0, 61);
        const ManaPool pool = AvailableManaPool(s);
        CHECK(pool.Total() == 1);
        CHECK(pool.green == 0);                     // banked as `wild`, not pinned to green
        CHECK(pool.wild  == 1);
    }

    SUBCASE("THE CONTAINMENT PROPERTY: deck_has_mana_grant=false costs one bool and skips it all")
    {
        // This is what makes every other deck in the suite byte-identical by construction rather
        // than by measurement. The smoke's play-changed=0 is the empirical half of the same claim.
        GameState s = board_with(true, 3, false, false);
        s.deck_has_mana_grant = false;
        CHECK(AvailableManaPool(s).Total() == 0);
        CHECK(UntappedManaUpperBound(s, false, 0) == 0);
    }
}

TEST_CASE("Brightcap Badger: the granted-source CAP is lossless, and keeps the memo alive")
{
    EnsureCardsLoaded();

    // The backtracker's failure memo switches off above 64 sources, and it is the guard against a
    // documented 14-hour blow-up. A wide Saproling board would blow past that, so the candidate
    // build admits at most cost.ManaValue() granted tokens. That is lossless because a payment taps
    // at most that many sources and the tokens are interchangeable -- these cases pin both halves.
    GameState s = Fresh();
    Put(s, "Brightcap Badger", 0, 50);
    for (int i = 0; i < 80; ++i) { PutSaprolingToken(s, 100 + i); }

    SUBCASE("a wide board still pays a small cost")
    {
        ManaCost cost;
        cost.generic = 1;
        cost.green   = 1;
        CHECK(TapForCostShared(s, cost, false, nullptr, false));
    }

    SUBCASE("the bound still sees the whole board -- the cap is on the BACKTRACKER, not the bound")
    {
        // UntappedManaUpperBound must NOT be capped: it feeds PaymentManaCovers, which turns a
        // short bound into a proof of unpayability. Capping it would refuse payable costs.
        CHECK(UntappedManaUpperBound(s, false, 0) == 80);
    }

    SUBCASE("a LARGE cost is still payable off the wide board")
    {
        ManaCost cost;
        cost.generic = 19;
        cost.green   = 1;            // 20 mana off 80 interchangeable bodies
        CHECK(TapForCostShared(s, cost, false, nullptr, false));
    }
}

TEST_CASE("The creature-enter watcher cascade is gated per DECK, and the gate is a no-op proof")
{
    EnsureCardsLoaded();

    // GameState::deck_has_creature_enter_watcher gates FireCreatureEnterWatchers' own top-level
    // loop. Unlike an ordinary optimisation this one claims to be byte-identical BY CONSTRUCTION,
    // because the stamp's predicate (DefHasCreatureEnterWatcher) is the SAME function the loop's
    // per-permanent `enter_watcher` byte is computed from. These cases pin that equivalence from
    // both ends: the gate must suppress nothing when a watcher is present, and the stamp must never
    // be able to close while a card that would fire is in the list.

    SUBCASE("a fresh GameState defaults to TRUE -- an unstamped state keeps every trigger")
    {
        // The scenario harness builds a PARTIAL GameState and never stamps. Erring true costs time;
        // erring false silently drops a trigger, which is why every flag in this family defaults on.
        GameState fresh;
        CHECK(fresh.deck_has_creature_enter_watcher);
    }

    SUBCASE("with the gate OPEN a Soul Warden gains life, which is what the gate must never break")
    {
        GameState s = Fresh();
        Put(s, "Soul Warden", 0, 10);
        const int entered = Put(s, "Tukatongue Thallid", 0, 11, /*sick=*/true);
        s.deck_has_creature_enter_watcher = true;
        const int before = s.players[0].life;
        FireCreatureEnterWatchers(s, 0, entered);
        CHECK(s.players[0].life == before + 1);
    }

    SUBCASE("the gate really is the thing doing the gating")
    {
        // Same board, flag down: proves the flag controls this loop rather than some other
        // predicate happening to agree. This configuration is UNREACHABLE in a real game -- a deck
        // holding a Soul Warden always stamps true -- so it asserts the mechanism, not a behaviour
        // we ship.
        GameState s = Fresh();
        Put(s, "Soul Warden", 0, 10);
        const int entered = Put(s, "Tukatongue Thallid", 0, 11, /*sick=*/true);
        s.deck_has_creature_enter_watcher = false;
        const int before = s.players[0].life;
        FireCreatureEnterWatchers(s, 0, entered);
        CHECK(s.players[0].life == before);
    }

    SUBCASE("the stamp predicate agrees with the per-definition byte for EVERY card in the DB")
    {
        // THE LOCKSTEP THAT MAKES THE SKIP SOUND. If a new watcher clause is ever added to
        // FireCreatureEnterWatchers with a term added to DefHasCreatureEnterWatcher but the
        // definition byte left stale (or vice versa), the gate would close on a deck that really
        // does have a watcher and silently drop it. Nothing else in the build checks this.
        int checked = 0;
        for (const std::string& name : CardDatabase::Instance().AllNames())
        {
            const CardDefinition* d = CardDatabase::Instance().Lookup(name);
            REQUIRE(d != nullptr);
            CHECK(d->enter_watcher == DefHasCreatureEnterWatcher(d->params));
            ++checked;
        }
        CHECK(checked > 300);
    }

    SUBCASE("the six terms are each sufficient on their own")
    {
        // One card per lane, so a term deleted from the disjunction fails here rather than in a
        // deck's win rate. Names are cards the repo really ships; a rename fails loudly at Def().
        CHECK(Def("Soul Warden").enter_watcher);              // any_creature_enters_lifegain
        CHECK(Def("Essence Warden").enter_watcher);           // ... the Fungus deck's copy
        CHECK(Def("Suture Priest").enter_watcher);            // own_* + opp_creature_enters_life_loss
        CHECK(Def("Righteous Valkyrie").enter_watcher);       // own_creature_enters_lifegain_toughness
        CHECK(Def("Youthful Valkyrie").enter_watcher);        // own_creature_enters_self_counters
        CHECK(Def("Hamletback Goliath").enter_watcher);       // any_creature_enters_self_counters_power
        // ...and the cards candidate B actually plays must NOT be watchers, which is the whole
        // reason the gate closes on that list.
        CHECK_FALSE(Def("Saproling Burst").enter_watcher);
        CHECK_FALSE(Def("Sporecrown Thallid").enter_watcher);
        CHECK_FALSE(Def("Mycoloth").enter_watcher);
        CHECK_FALSE(Def("Slimefoot, the Stowaway").enter_watcher);   // a DIES watcher, not an enter one
        CHECK_FALSE(Def("Brightcap Badger").enter_watcher);
    }
}

// -------------------------------------------------------------------------------------------
// Brightcap Badger's END STEP trigger -- USER-REPORTED 2026-09-25: "It doesn't even produce
// saprolings at the end of turn."
//
// The trigger reads NO condition ("At the beginning of your end step, create a 1/1 green Saproling
// creature token"), which is exactly what endstep_tokens_unconditional encodes. It shares
// PerformEndStepLifegainTokens with the Ocelot Pride family, and on 2026-09-19 that function gained
// a hoisted early-out -- `if (life_gained_this_turn <= 0) return;` -- justified in its own comment
// as "byte-identical by construction" because "EVERY one of these triggers is worded 'if you gained
// life this turn'". That was true when it was written and the Badger, added afterwards, falsified
// it: the early-out returns before the per-card unconditional check can run, so on a deck that
// gains no life -- which is candidate-B Fungus every game -- the trigger never fired at all.
//
// There was no test that the trigger FIRES; the existing Badger tests check the card data and the
// Frolic payload only. That is the hole this closes.
TEST_CASE("Brightcap Badger: the end-step Saproling arrives WITHOUT any lifegain")
{
    EnsureCardsLoaded();

    SUBCASE("no life gained this turn -- the trigger is unconditional and must still fire")
    {
        GameState s = Fresh();
        Put(s, "Brightcap Badger", 0, 40);
        s.players[0].life_gained_this_turn = 0;   // candidate-B Fungus, every turn
        PerformEndStepLifegainTokens(s);
        CHECK(CountSaprolings(s) == 1);
    }

    SUBCASE("it is still doubled by Doubling Season -- it rides the universal cascade")
    {
        GameState s = Fresh();
        Put(s, "Brightcap Badger", 0, 40);
        Put(s, "Doubling Season", 0, 90);
        s.players[0].life_gained_this_turn = 0;
        PerformEndStepLifegainTokens(s);
        CHECK(CountSaprolings(s) == 2);
    }

    SUBCASE("two Badgers are two triggers")
    {
        GameState s = Fresh();
        Put(s, "Brightcap Badger", 0, 40);
        Put(s, "Brightcap Badger", 0, 41);
        s.players[0].life_gained_this_turn = 0;
        PerformEndStepLifegainTokens(s);
        CHECK(CountSaprolings(s) == 2);
    }

    SUBCASE("controller-scoped: the opponent's Badger does not make US a Saproling")
    {
        GameState s = Fresh();
        Put(s, "Brightcap Badger", 1, 40);
        s.players[0].life_gained_this_turn = 0;
        PerformEndStepLifegainTokens(s);          // active player is 0
        CHECK(CountSaprolings(s) == 0);
    }

    SUBCASE("the CONDITIONAL family is untouched -- no lifegain still means no Cat")
    {
        // The early-out exists for this case and must keep working: Ocelot Pride reads
        // "if you gained life this turn", so a turn with none fires nothing.
        GameState s = Fresh();
        Put(s, "Ocelot Pride", 0, 50);
        s.players[0].life_gained_this_turn = 0;
        PerformEndStepLifegainTokens(s);
        CHECK(s.battlefield.size() == 1);         // just the Pride
    }

    SUBCASE("...and still fires when life WAS gained")
    {
        GameState s = Fresh();
        Put(s, "Ocelot Pride", 0, 50);
        s.players[0].life_gained_this_turn = 3;
        PerformEndStepLifegainTokens(s);
        CHECK(s.battlefield.size() > 1);
    }
}

// -------------------------------------------------------------------------------------------
// Brightcap Badger's MANA GRANT -- USER-REPORTED 2026-09-25: "Brightcap Badger turning critters
// into mana producers is not working."
//
// "Each Fungus and Saproling you control has '{T}: Add {G}.'" The population it exists to affect is
// entirely TOKENS, which have no CardDefinition, so the grant is resolved through the shared mana
// oracle against the permanent's own Card. These assertions pin the end-to-end answer -- what
// AvailableManaPool actually reports -- rather than the predicate in isolation, because the
// predicate was already right when the report came in.
TEST_CASE("Brightcap Badger: the grant turns Saprolings into {G} sources in the real pool")
{
    EnsureCardsLoaded();

    SUBCASE("no Badger -- a Saproling token taps for nothing (the baseline)")
    {
        GameState s = Fresh();
        PutSaprolingToken(s, 60, /*sick=*/false);
        CHECK(AvailableManaPool(s).Total() == 0);
    }

    SUBCASE("Badger on board -- a settled Saproling token is a {G} source")
    {
        GameState s = Fresh();
        Put(s, "Brightcap Badger", 0, 40);
        PutSaprolingToken(s, 60, /*sick=*/false);
        CHECK(AvailableManaPool(s).green >= 1);
    }

    SUBCASE("the grant scales with the board -- four settled Saprolings are four {G}")
    {
        GameState s = Fresh();
        Put(s, "Brightcap Badger", 0, 40);
        for (int i = 0; i < 4; ++i) { PutSaprolingToken(s, 60 + i, /*sick=*/false); }
        CHECK(AvailableManaPool(s).green >= 4);
    }

    SUBCASE("CR 302.6 -- a Saproling created THIS turn cannot use a granted {T}")
    {
        // This is correct behaviour, not a bug, and it is the single most likely thing to look
        // like one at the table: the deck's tokens arrive summoning-sick and the grant is a {T}
        // ability. Pinned so the fix for the report above can never "fix" it by removing the rule.
        GameState s = Fresh();
        Put(s, "Brightcap Badger", 0, 40);
        PutSaprolingToken(s, 60, /*sick=*/true);
        CHECK(AvailableManaPool(s).green == 0);
    }

    SUBCASE("...and a haste grant lifts it -- the Concordant Crossroads interaction")
    {
        GameState s = Fresh();
        Put(s, "Brightcap Badger", 0, 40);
        Put(s, "Concordant Crossroads", 0, 91);
        PutSaprolingToken(s, 60, /*sick=*/true);
        CHECK(AvailableManaPool(s).green >= 1);
    }

    SUBCASE("the grant reaches FUNGUS too, not just Saprolings -- both subtypes are listed")
    {
        GameState s = Fresh();
        Put(s, "Brightcap Badger", 0, 40);
        Put(s, "Thallid", 0, 61, /*sick=*/false);     // a Fungus creature card
        CHECK(AvailableManaPool(s).green >= 1);
    }

    SUBCASE("controller-scoped -- the opponent's Saproling is not OUR mana")
    {
        GameState s = Fresh();
        Put(s, "Brightcap Badger", 0, 40);
        const int idx = PutSaprolingToken(s, 60, /*sick=*/false);
        s.battlefield[static_cast<std::size_t>(idx)].controller_index = 1;
        s.battlefield[static_cast<std::size_t>(idx)].owner_index      = 1;
        CHECK(AvailableManaPool(s).green == 0);
    }
}

// The VIEWER's half of the same question. HumanPreTapFaces is what decides whether a permanent is
// clickable as a mana source in the play GUI, and its own comment says it "MUST match the payer,
// not merely approximate it ... a human offered fewer faces than the search can use would be shown
// a board they cannot play." The user's report is exactly that symptom, so pin the two together.
TEST_CASE("Brightcap Badger: the viewer offers the granted tap the payer would take")
{
    EnsureCardsLoaded();

    SUBCASE("a settled Saproling token is hand-tappable for {G}")
    {
        GameState s = Fresh();
        Put(s, "Brightcap Badger", 0, 40);
        const int idx = PutSaprolingToken(s, 60, /*sick=*/false);
        CHECK(HumanPreTapFaces(s, s.battlefield[static_cast<std::size_t>(idx)]) == "G");
    }

    SUBCASE("...and the engine's pool agrees -- viewer and payer answer the same question")
    {
        GameState s = Fresh();
        Put(s, "Brightcap Badger", 0, 40);
        const int idx = PutSaprolingToken(s, 60, /*sick=*/false);
        const bool viewer_offers = !HumanPreTapFaces(s, s.battlefield[static_cast<std::size_t>(idx)]).empty();
        const bool payer_counts  = AvailableManaPool(s).green >= 1;
        CHECK(viewer_offers == payer_counts);
    }

    SUBCASE("a sick Saproling is offered NOTHING -- and the payer does not count it either")
    {
        GameState s = Fresh();
        Put(s, "Brightcap Badger", 0, 40);
        const int idx = PutSaprolingToken(s, 60, /*sick=*/true);
        CHECK(HumanPreTapFaces(s, s.battlefield[static_cast<std::size_t>(idx)]).empty());
        CHECK(AvailableManaPool(s).green == 0);
    }

    SUBCASE("no Badger -- not clickable")
    {
        GameState s = Fresh();
        const int idx = PutSaprolingToken(s, 60, /*sick=*/false);
        CHECK(HumanPreTapFaces(s, s.battlefield[static_cast<std::size_t>(idx)]).empty());
    }
}
