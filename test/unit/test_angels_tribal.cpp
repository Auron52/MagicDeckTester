// Unit tests for the Angels onboarding (2026-09-17). These pin the parts of the deck that a game
// log CANNOT show -- counters, the anthem's on/off edge, and above all the ORDERING between a
// replacement effect and the enter triggers that read its result -- because every one of them is
// silently wrong in a way that still compiles, still runs, and still produces plausible numbers.
//
//  1. Giada's "enters with an additional +1/+1 counter for each Angel you already control" is a
//     REPLACEMENT EFFECT (CR 614), so the counters must already be on the body when the enter
//     TRIGGERS resolve. Righteous Valkyrie gains life equal to the entrant's TOUGHNESS, so the
//     ordering is directly observable as a life total -- which is the only reason it is testable.
//     Placing the replacement below the trigger cascade compiles and is wrong only here.
//  2. The off-by-one in "each Angel you ALREADY control": the entrant never counts itself, the
//     watcher (Giada) does. A lone Giada + an entering Angel is +1/+1, not +2/+2.
//  3. The shared enters_watch_subtypes filter actually FILTERS: a non-Angel entering pays nothing
//     to Bishop of Wings / Seraph Sanctuary, and Righteous Valkyrie's ["Angel","Cleric"] OR-list
//     pays on a Human Cleric.
//  4. Righteous Valkyrie's anthem is keyed to gamesetup::StartingLife() + 7, NOT a literal 27 --
//     the bug a 2HG job (starting_life 30) would otherwise hide -- and it switches on at the edge.
//  5. Lyra's static lifelink GRANT reaches other Angels and excludes non-Angels, resolved through
//     CreatureHasLifelink (the one oracle the combat damage site consults).
//  6. Resplendent Angel's end-step trigger demands 5 CUMULATIVE life across the turn, not a single
//     5-life event, and does not fire at 4.
//  7. Youthful Valkyrie counters itself on ANOTHER Angel entering, never on its own entry.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/GameSetup.h"
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
    s.battlefield.push_back(p);
    RefreshDevotionCreatures(s);
    return static_cast<int>(s.battlefield.size()) - 1;
}

// Route an enter through the SAME universal cascade both worlds use -- which is the whole point:
// the replacement and the triggers must interleave here exactly as they do in a real game.
int Enter(GameState& s, const std::string& name, int controller, int number)
{
    const int idx = Put(s, name, controller, number, /*sick=*/true);
    FireEtbWatchers(s, controller, idx);
    return idx;
}

int PlusCounters(const Permanent& p)
{
    int n = 0;
    for (const Counter& c : p.counters) { if (c.type == Counter::Type::PlusOnePlusOne) { n += c.count; } }
    return n;
}

const Permanent& ByNumber(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return p; } }
    static Permanent none;
    REQUIRE_MESSAGE(false, "permanent not found: ", number);
    return none;
}

// Effective toughness INCLUDING lords/anthems -- what Righteous Valkyrie's trigger reads.
int LiveToughness(const GameState& s, int number)
{
    const Permanent& p = ByNumber(s, number);
    return p.EffectiveToughness()
         + ComputeLordBonus(p.card, s, p.controller_index, p.is_animated, &p).second;
}

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    return s;
}

}   // namespace

TEST_CASE("Giada: 'each Angel you ALREADY control' excludes the entrant and includes Giada")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Giada, Font of Hope", 0, 1);
    // Lone Giada: the entering Angel sees exactly ONE already-controlled Angel (Giada herself).
    Enter(s, "Archangel of Thune", 0, 20);
    CHECK(PlusCounters(ByNumber(s, 20)) == 1);          // a 4/5, NOT a 5/6

    // A second Angel entering now sees Giada + the Archangel = 2.
    Enter(s, "Legion Angel", 0, 21);
    CHECK(PlusCounters(ByNumber(s, 21)) == 2);
}

TEST_CASE("Giada: her OWN entry gets nothing ('each OTHER Angel'), but a second Giada does")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Archangel of Thune", 0, 20);
    Enter(s, "Giada, Font of Hope", 0, 1);
    CHECK(PlusCounters(ByNumber(s, 1)) == 0);           // no Giada out yet to replace her own entry

    // A SECOND Giada is an "other Angel" relative to the first, and sees 2 Angels already out.
    Enter(s, "Giada, Font of Hope", 0, 2);
    CHECK(PlusCounters(ByNumber(s, 2)) == 2);
}

TEST_CASE("Giada does not size up a NON-Angel (the subtype gate on the replacement)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Giada, Font of Hope", 0, 1);
    Enter(s, "Bishop of Wings", 0, 30);                 // Human Cleric, not an Angel
    CHECK(PlusCounters(ByNumber(s, 30)) == 0);
}

// THE ORDERING TEST. This is the one that catches a replacement applied below the trigger cascade:
// both placements compile, produce plausible boards, and only the LIFE TOTAL tells them apart.
TEST_CASE("ORDERING: Giada's counters land BEFORE Righteous Valkyrie reads the entrant's toughness")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int life0 = s.players[0].life;
    Put(s, "Giada, Font of Hope", 0, 1);                // Angel -> counts toward "already control"
    Put(s, "Righteous Valkyrie", 0, 2);                 // Angel Cleric -> also counts

    // Legion Angel is a printed 4/3 with no lifegain watcher of its own, deliberately: it isolates
    // the ordering with no feedback (its ETB wish whiffs harmlessly on an empty sideboard). Two
    // Angels already out => +2/+2 counters => a 6/5, so Righteous Valkyrie must gain 5, not the
    // printed 3. Get the ordering wrong and the life is 3.
    Enter(s, "Legion Angel", 0, 20);
    CHECK(PlusCounters(ByNumber(s, 20)) == 2);
    CHECK(LiveToughness(s, 20) == 5);
    CHECK(s.players[0].life == life0 + 5);
}

// The same ordering, with the deck's real feedback loop switched on. Worth pinning separately
// because the extra counter is NOT a bug and looks like one: the entering Archangel of Thune is
// itself a "whenever you gain life" watcher, so Righteous Valkyrie's gain immediately puts a
// counter on every creature we control -- including the Archangel that just arrived.
TEST_CASE("ORDERING: an entering Archangel of Thune feeds its own arrival back through the gain")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int life0 = s.players[0].life;
    Put(s, "Giada, Font of Hope", 0, 1);
    Put(s, "Righteous Valkyrie", 0, 2);

    // Printed 3/4. Giada's replacement makes it a 5/6 BEFORE the triggers read it, so Righteous
    // Valkyrie gains 6 -- that gain is the ordering proof. Thune then sees a life-gain EVENT and
    // pumps the whole team, itself included, for a third counter.
    Enter(s, "Archangel of Thune", 0, 20);
    CHECK(s.players[0].life == life0 + 6);              // <- the ordering assertion
    CHECK(PlusCounters(ByNumber(s, 20)) == 3);          // 2 as-enters + 1 from its own trigger
    CHECK(PlusCounters(ByNumber(s, 1)) == 1);           // Giada got the team counter too
    CHECK(LiveToughness(s, 20) == 7);
}

TEST_CASE("Righteous Valkyrie: the ['Angel','Cleric'] OR-filter pays on a Human Cleric")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int life0 = s.players[0].life;
    Put(s, "Righteous Valkyrie", 0, 2);
    Enter(s, "Bishop of Wings", 0, 30);                 // Human CLERIC, toughness 4
    CHECK(s.players[0].life == life0 + 4);
}

TEST_CASE("enters_watch_subtypes FILTERS: a non-Angel, non-Cleric pays nothing")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int life0 = s.players[0].life;
    Put(s, "Bishop of Wings", 0, 10);                   // "an ANGEL you control enters" -> 4 life
    Put(s, "Seraph Sanctuary", 0, 11);                  // "an ANGEL you control enters" -> 1 life
    Put(s, "Righteous Valkyrie", 0, 12);                // Angel or Cleric
    // Llanowar Elves is an Elf Druid -- neither Angel nor Cleric. NOT Soul Warden: that is a Human
    // CLERIC, so Righteous Valkyrie correctly pays on it, which is a genuine hit of the OR-filter
    // rather than a leak (this test asserted otherwise and was wrong).
    Enter(s, "Llanowar Elves", 0, 40);
    CHECK(s.players[0].life == life0);                  // Bishop/Sanctuary/Valkyrie all silent
}

TEST_CASE("...but a Human CLERIC does hit Righteous Valkyrie's OR-filter, and only it")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int life0 = s.players[0].life;
    Put(s, "Bishop of Wings", 0, 10);                   // Angel-only -> silent
    Put(s, "Seraph Sanctuary", 0, 11);                  // Angel-only -> silent
    Put(s, "Righteous Valkyrie", 0, 12);                // Angel or Cleric -> pays the toughness
    Enter(s, "Soul Warden", 0, 40);                     // Human CLERIC, a 1/1
    CHECK(s.players[0].life == life0 + 1);
}

TEST_CASE("Bishop of Wings + Seraph Sanctuary pay per Angel, as SEPARATE life-gain events")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int life0 = s.players[0].life;
    Put(s, "Bishop of Wings", 0, 10);
    Put(s, "Seraph Sanctuary", 0, 11);                  // a LAND watcher -- the loop scans all permanents
    const int thune = Put(s, "Archangel of Thune", 0, 12);
    Enter(s, "Legion Angel", 0, 20);
    // 4 (Bishop) + 1 (Sanctuary) = 5 life, in TWO events -> two Thune team pumps.
    CHECK(s.players[0].life == life0 + 5);
    CHECK(PlusCounters(s.battlefield[thune]) == 2);
}

TEST_CASE("Youthful Valkyrie counters itself on ANOTHER Angel, never on its own entry")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int yv = Enter(s, "Youthful Valkyrie", 0, 5);
    CHECK(PlusCounters(s.battlefield[yv]) == 0);        // its own entry is not "another"

    Enter(s, "Legion Angel", 0, 20);
    CHECK(PlusCounters(ByNumber(s, 5)) == 1);
    Enter(s, "Bishop of Wings", 0, 30);                 // not an Angel
    CHECK(PlusCounters(ByNumber(s, 5)) == 1);

    // A SECOND Youthful Valkyrie pumps the first (a Valkyrie is itself an Angel) but not itself.
    Enter(s, "Youthful Valkyrie", 0, 6);
    CHECK(PlusCounters(ByNumber(s, 5)) == 2);
    CHECK(PlusCounters(ByNumber(s, 6)) == 0);
}

TEST_CASE("Righteous Valkyrie's anthem is RELATIVE to starting life, and switches on at the edge")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Righteous Valkyrie", 0, 2);
    Put(s, "Bishop of Wings", 0, 30);                   // a plain 1/4 body to read

    const int start = gamesetup::StartingLife();
    s.players[0].life = start + 6;                      // one BELOW the threshold
    CHECK(LiveToughness(s, 30) == 4);

    s.players[0].life = start + 7;                      // exactly at it
    CHECK(LiveToughness(s, 30) == 6);                   // +2/+2

    // SELF-INCLUSIVE ("creatures you control", not "other"): the Valkyrie buffs itself too.
    CHECK(LiveToughness(s, 2) == 4 + 2);

    // A second copy stacks additively (not legendary).
    Put(s, "Righteous Valkyrie", 0, 3);
    CHECK(LiveToughness(s, 30) == 8);

    // The threshold is starting-life relative: at exactly `start` the anthem is off however
    // large `start` is. This is the assertion that fails if anyone writes a literal 27.
    s.players[0].life = start;
    CHECK(LiveToughness(s, 30) == 4);
}

TEST_CASE("Lyra grants lifelink to OTHER Angels, and nothing to non-Angels")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Lyra Dawnbringer", 0, 1);
    Put(s, "Youthful Valkyrie", 0, 2);                  // Angel, no printed lifelink
    Put(s, "Bishop of Wings", 0, 3);                    // Human Cleric
    Put(s, "Youthful Valkyrie", 1, 4);                  // an Angel the OPPONENT controls

    CHECK(CreatureHasLifelink(ByNumber(s, 2), s));      // granted
    CHECK_FALSE(CreatureHasLifelink(ByNumber(s, 3), s));// not an Angel
    CHECK_FALSE(CreatureHasLifelink(ByNumber(s, 4), s));// not ours
    CHECK(CreatureHasLifelink(ByNumber(s, 1), s));      // Lyra's own printed lifelink

    // ...and the +1/+1 half reaches the same set, excluding Lyra herself ("Other Angels").
    CHECK(LiveToughness(s, 2) == 3 + 1);
    CHECK(LiveToughness(s, 1) == 5);
}

TEST_CASE("Resplendent Angel: 5 CUMULATIVE life arms the end step; 4 does not")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Resplendent Angel", 0, 1);
    const std::size_t before = s.battlefield.size();

    GainLife(s, 0, 4);                                  // one event of 4 -- below the threshold
    PerformEndStepLifegainTokens(s);
    CHECK(s.battlefield.size() == before);              // no token

    GainLife(s, 0, 1);                                  // a SECOND event; 5 cumulative across the turn
    CHECK(s.players[0].life_gained_this_turn == 5);
    PerformEndStepLifegainTokens(s);
    CHECK(s.battlefield.size() == before + 1);
    const Permanent& tok = s.battlefield.back();
    CHECK(tok.is_token);
    CHECK(tok.card.IsCreature());
    CHECK(tok.EffectivePower() == 4);
    CHECK(tok.card.HasKeyword(Keyword::Flying));        // read by Serra the Benevolent's +2
    CHECK(tok.card.HasKeyword(Keyword::Vigilance));
    bool is_angel = false;
    for (const std::string& sub : tok.card.m_subtypes) { if (sub == "Angel") { is_angel = true; } }
    CHECK_MESSAGE(is_angel, "the end-step token MUST be an Angel -- the whole tribal payoff keys on it");
}

TEST_CASE("Ocelot Pride is unchanged by the threshold param (default 1 = the historical '>0')")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Ocelot Pride", 0, 1);
    const std::size_t before = s.battlefield.size();
    GainLife(s, 0, 1);                                  // ONE life is still enough for Ocelot Pride
    PerformEndStepLifegainTokens(s);
    CHECK(s.battlefield.size() == before + 1);
}

// ---- Lyra, Archangel of Dawn (2026-09-18) ------------------------------------------------------
// "Whenever you gain life, put a +1/+1 counter on each Angel you control." Archangel of Thune's
// watcher narrowed by the new lifegain_counters_subtypes RECIPIENT filter. Every check below is
// invisible in a game log until it is wrong by a whole subtype.
TEST_CASE("Lyra Archangel of Dawn counters each ANGEL, herself included, and skips non-Angels")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int lyra   = Put(s, "Lyra, Archangel of Dawn", 0, 1);
    const int bishop = Put(s, "Bishop of Wings", 0, 2);          // Human Cleric -- NOT an Angel
    const int youth  = Put(s, "Youthful Valkyrie", 0, 3);        // Angel

    GainLife(s, 0, 1);                                            // ONE life-gain event
    CHECK_MESSAGE(PlusCounters(s.battlefield[lyra]) == 1,
                  "Lyra is an Angel, so she matches her OWN filter and counters herself");
    CHECK(PlusCounters(s.battlefield[youth]) == 1);
    CHECK_MESSAGE(PlusCounters(s.battlefield[bishop]) == 0,
                  "Bishop of Wings is a Human Cleric -- the whole point of the narrowing");
}

TEST_CASE("Lyra Archangel of Dawn fires once per life-gain EVENT, not per point of life")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int lyra = Put(s, "Lyra, Archangel of Dawn", 0, 1);

    GainLife(s, 0, 5);                                            // one event of five life
    CHECK_MESSAGE(PlusCounters(s.battlefield[lyra]) == 1, "five life in ONE event is ONE counter");
    GainLife(s, 0, 1);
    GainLife(s, 0, 1);                                            // two more events
    CHECK(PlusCounters(s.battlefield[lyra]) == 3);
}

TEST_CASE("Lyra Archangel of Dawn reaches Angel TOKENS and bodies that entered this turn")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Lyra, Archangel of Dawn", 0, 1);
    // Resplendent Angel's end-step token is a 4/4 Angel; make one the same way the engine does.
    const std::size_t before = s.battlefield.size();
    CreateToken(s, 0, /*power=*/4, /*toughness=*/4, { "Angel" }, "W", { "Flying", "Vigilance" });
    REQUIRE(s.battlefield.size() == before + 1);
    const int tok = static_cast<int>(s.battlefield.size()) - 1;
    REQUIRE(s.battlefield[tok].is_token);

    GainLife(s, 0, 1);
    CHECK_MESSAGE(PlusCounters(s.battlefield[tok]) == 1,
                  "an Angel TOKEN is an Angel -- Serra's -3 and Resplendent Angel both make these");
}

TEST_CASE("Archangel of Thune is UNCHANGED by the filter: an empty list still hits every creature")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Archangel of Thune", 0, 1);
    const int bishop = Put(s, "Bishop of Wings", 0, 2);           // Human Cleric

    GainLife(s, 0, 1);
    CHECK_MESSAGE(PlusCounters(s.battlefield[bishop]) == 1,
                  "Thune has NO recipient filter, so a non-Angel must still be countered -- this is "
                  "the regression guard for the whole narrowing change");
}

TEST_CASE("Thune and Lyra Archangel of Dawn stack: a non-Angel gets only Thune's counter")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Archangel of Thune", 0, 1);
    Put(s, "Lyra, Archangel of Dawn", 0, 2);
    const int bishop = Put(s, "Bishop of Wings", 0, 3);           // Human Cleric
    const int angel  = Put(s, "Youthful Valkyrie", 0, 4);         // Angel

    GainLife(s, 0, 1);                                            // ONE event, TWO watchers
    CHECK_MESSAGE(PlusCounters(s.battlefield[angel]) == 2, "an Angel is countered by both watchers");
    CHECK_MESSAGE(PlusCounters(s.battlefield[bishop]) == 1, "a non-Angel is countered by Thune only");
}
