// CONTROL-EFFECT INJECTION (core/ControlInject.h; docs/design/control-inject-harness.md): every effect kind, the
// grammar, the zone-change routes (dies / leave triggers, indestructible, umbra armor, hexproof), the counters'
// destinations (graveyard / hand / library top, flashback, a copy, "can't be countered"), the tax and life fields,
// and the `skip` round (an opponent's extra turn: TurnSeq does not advance, the goldfisher neither untaps nor draws,
// the opponent still takes its turn).
#include <doctest/doctest.h>
#include "core/OwnRound.h"

#include "ai/AIEngine.h"
#include "ai/ManaPayment.h"
#include "cards/CardDatabase.h"
#include "core/ControlInject.h"
#include "core/GameEngine.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "runner/GoldFishRunner.h"

#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace {

void CiLoadDb()
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card CiCard(const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "not in cards.json: ", name);
    Card c = d->card;
    c.m_number = number;
    return c;
}

Permanent& CiPerm(GameState& s, const std::string& name, int number, int controller = 0)
{
    Permanent p;
    p.card = CiCard(name, number);
    p.controller_index = controller;
    p.owner_index = controller;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

Decklist CiForests()
{
    Decklist deck;
    const CardDefinition* forest = CardDatabase::Instance().Lookup("Forest");
    REQUIRE(forest != nullptr);
    for (int i = 0; i < 40; ++i) { deck.mainboard.push_back(forest->card); }
    return deck;
}

GameState CiFresh(int round = 3)
{
    CiLoadDb();
    GameState s = GoldFishRunner::SetupGame(CiForests(), 8, 0);
    s.players[0].hand.clear();
    s.players[1].hand.clear();
    s.battlefield.clear();
    s.opponent_spawns = nullptr;
    s.turn_number = round;
    return s;
}

struct CiArm
{
    CiArm(const std::string& spec, const GameState& s, bool record = false)
    { ctrlinject::Install(ctrlinject::Parse(spec), record, "unit", 0, s); }
    ~CiArm() { ctrlinject::Clear(); }
};

int CiFind(const GameState& s, int number)
{
    for (int i = 0; i < static_cast<int>(s.battlefield.size()); ++i)
    { if (s.battlefield[static_cast<std::size_t>(i)].card.m_number == number) { return i; } }
    return -1;
}

bool CiIn(const std::vector<Card>& zone, int number)
{
    for (const Card& c : zone) { if (c.m_number == number) { return true; } }
    return false;
}

StackEntry CiSpell(const std::string& name, int number)
{
    StackEntry e;
    e.type = StackEntry::EntryType::Spell;
    e.source = CiCard(name, number);
    e.controller_index = 0;
    return e;
}

} // namespace

TEST_CASE("ctrl_inject: the grammar, its aliases and its refusals")
{
    using namespace ctrlinject;
    const Spec sp = Parse("land:3:1; bigcrt:4; sweep:2; skip:5-6; exile_until:3:creature:0:ret=6; "
                          "tax:2-4:1:noncreature; life:3:5; counter_to_top:4:mv>=3; counter:2:name=Lightning Bolt; "
                          "discard:2:nonland:big; tap:3:land:all; counter_to_hand:3; bounce:4:creature:2");
    REQUIRE(sp.size() == 13);
    CHECK((sp[0].kind == Kind::Destroy && sp[0].cls == Class::Land && sp[0].sel == Sel::Index && sp[0].index == 1
           && sp[0].from == 3 && sp[0].to == 3));
    CHECK((sp[1].kind == Kind::Destroy && sp[1].cls == Class::Creature && sp[1].sel == Sel::Big));
    CHECK((sp[2].kind == Kind::Destroy && sp[2].cls == Class::Creature && sp[2].sel == Sel::All));
    CHECK((sp[3].kind == Kind::Skip && sp[3].from == 5 && sp[3].to == 6));
    CHECK((sp[4].kind == Kind::ExileUntil && sp[4].cls == Class::Creature && sp[4].index == 0 && sp[4].ret == 6));
    CHECK((sp[5].kind == Kind::Tax && sp[5].amount == 1 && sp[5].tax_class == TaxClass::Noncreature
           && sp[5].from == 2 && sp[5].to == 4));
    CHECK((sp[6].kind == Kind::Life && sp[6].amount == 5));
    CHECK((sp[7].kind == Kind::CounterToTop && sp[7].sel == Sel::MvAtLeast && sp[7].mv == 3));
    CHECK((sp[8].kind == Kind::Counter && sp[8].sel == Sel::Name && sp[8].name == "Lightning Bolt"));
    CHECK((sp[9].kind == Kind::Discard && sp[9].cls == Class::Nonland && sp[9].sel == Sel::Big));
    CHECK((sp[10].kind == Kind::Tap && sp[10].cls == Class::Land && sp[10].sel == Sel::All));
    CHECK((sp[11].kind == Kind::CounterToHand && sp[11].sel == Sel::Index && sp[11].index == 0));
    CHECK((sp[12].kind == Kind::Bounce && sp[12].cls == Class::Creature && sp[12].index == 2));
    CHECK(Parse("").empty());
    CHECK_THROWS(Parse("blah:3"));
    CHECK_THROWS(Parse("exile_until:3:creature:0"));        // no return round
    CHECK_THROWS(Parse("exile_until:3-5:creature:0:ret=5")); // return inside its own range
    CHECK_THROWS(Parse("counter:3:big"));                   // the round's later spells are unknown
    CHECK_THROWS(Parse("skip:0"));
    CHECK_THROWS(Parse("skip:3:1"));
    CHECK_THROWS(Parse("tax:3"));
    CHECK_THROWS(Parse("land:4-3"));
    CHECK_THROWS(Parse("destroy:3:creature:wat"));
}

TEST_CASE("ctrl_inject: destroy goes through the dies cascade -- persist returns Kitchen Finks; indestructible stays")
{
    GameState s = CiFresh();
    CiPerm(s, "Forest", 1);
    CiPerm(s, "Kitchen Finks", 2);
    CiPerm(s, "Slippery Bogle", 3);
    CiPerm(s, "Ulamog, the Ceaseless Hunger", 4);
    const int life0 = s.players[0].life;
    {
        CiArm arm("creature:3:0", s);   // targeted: the hexproof Bogle is no candidate, so index 0 = Finks
        const ctrlinject::RoundStart rs = ctrlinject::OnRoundStart(s);
        CHECK(rs.changed);
        CHECK_FALSE(rs.skip);
    }
    const int fi = CiFind(s, 2);
    REQUIRE(fi >= 0);                                            // persist: it died and came back
    CHECK(MinusCountersOn(s.battlefield[static_cast<std::size_t>(fi)]) == 1);
    CHECK(s.players[0].life == life0 + 2);                       // ...and its ETB fired again
    {
        // The persisted Finks re-entered at the end of the battlefield, so the targetable creatures are now
        // [Ulamog, Finks] and index 1 is Finks again.
        CiArm arm("creature:3:1", s);
        ctrlinject::OnRoundStart(s);
    }
    // It already carries a -1/-1 counter, so persist does not return it this time.
    CHECK(CiFind(s, 2) < 0);
    CHECK(CiIn(s.players[0].graveyard, 2));
    {
        CiArm arm("sweep:3", s);        // untargeted: the Bogle dies, Ulamog is indestructible
        ctrlinject::OnRoundStart(s);
    }
    CHECK(CiFind(s, 3) < 0);
    CHECK(CiIn(s.players[0].graveyard, 3));
    CHECK(CiFind(s, 4) >= 0);
    CHECK(CiFind(s, 1) >= 0);
}

TEST_CASE("ctrl_inject: umbra armor destroys the Aura instead (CR 702.89a)")
{
    GameState s = CiFresh();
    CiPerm(s, "Slippery Bogle", 3);
    Permanent& u = CiPerm(s, "Hyena Umbra", 5);
    u.aura_attached_to = 3;
    {
        CiArm arm("sweep:3", s);
        ctrlinject::OnRoundStart(s);
    }
    CHECK(CiFind(s, 3) >= 0);
    CHECK(CiFind(s, 5) < 0);
    CHECK(CiIn(s.players[0].graveyard, 5));
}

TEST_CASE("ctrl_inject: exile_until returns the card as a new object at its round; a token never returns")
{
    GameState s = CiFresh(3);
    CiPerm(s, "Llanowar Elves", 10).tapped = true;
    Permanent& tok = CiPerm(s, "Llanowar Elves", 11);
    tok.is_token = true;
    CiArm arm("exile_until:3:creature:all:ret=5", s);
    ctrlinject::OnRoundStart(s);
    CHECK(CiFind(s, 10) < 0);
    CHECK(CiFind(s, 11) < 0);
    CHECK(CiIn(s.exile, 10));
    CHECK_FALSE(CiIn(s.exile, 11));
    CHECK(ctrlinject::BenefitPending(s));
    s.turn_number = 4;
    ctrlinject::OnRoundStart(s);
    CHECK(CiFind(s, 10) < 0);
    s.turn_number = 5;
    const ctrlinject::RoundStart rs = ctrlinject::OnRoundStart(s);
    CHECK(rs.changed);
    const int bi = CiFind(s, 10);
    REQUIRE(bi >= 0);
    CHECK(s.battlefield[static_cast<std::size_t>(bi)].entered_this_turn);   // a new object (the untap clears it)
    CHECK_FALSE(s.battlefield[static_cast<std::size_t>(bi)].tapped);        // ...untapped, not the old object
    CHECK_FALSE(CiIn(s.exile, 10));
    CHECK(CiFind(s, 11) < 0);
    CHECK_FALSE(ctrlinject::BenefitPending(s));
}

TEST_CASE("ctrl_inject: bounce, tap, discard and life")
{
    GameState s = CiFresh(3);
    CiPerm(s, "Forest", 1);
    CiPerm(s, "Forest", 2);
    CiPerm(s, "Llanowar Elves", 10);
    s.players[0].hand = { CiCard("Lightning Bolt", 20), CiCard("Mountain", 21), CiCard("Light Up the Stage", 22) };
    const int opp0 = s.Opponent().life;
    {
        CiArm arm("bounce:3:creature:0; bigdisc:3; life:3:4", s);
        CHECK(ctrlinject::OnRoundStart(s).changed);
    }
    CHECK(CiFind(s, 10) < 0);
    CHECK(CiIn(s.players[0].hand, 10));
    CHECK(CiIn(s.players[0].graveyard, 22));   // the highest mana value nonland: Light Up the Stage (MV 3)
    CHECK_FALSE(CiIn(s.players[0].hand, 22));
    CHECK(s.Opponent().life == opp0 + 4);
    {
        CiArm arm("discard:3:name=Lightning Bolt; tap:3:land:1", s);
        ctrlinject::OnRoundStart(s);
        CHECK(CiIn(s.players[0].graveyard, 20));
        CHECK(ctrlinject::OnAfterUntap(s));
        CHECK_FALSE(ctrlinject::OnAfterUntap(s));   // once per round
    }
    CHECK_FALSE(s.battlefield[static_cast<std::size_t>(CiFind(s, 1))].tapped);
    CHECK(s.battlefield[static_cast<std::size_t>(CiFind(s, 2))].tapped);
}

TEST_CASE("ctrl_inject: tax -- every class sums, priced through EffectiveSpellCost, ends after its last round")
{
    GameState s = CiFresh(3);
    const CardDefinition* bolt = CardDatabase::Instance().Lookup("Lightning Bolt");
    const CardDefinition* elves = CardDatabase::Instance().Lookup("Llanowar Elves");
    REQUIRE(bolt != nullptr);
    REQUIRE(elves != nullptr);
    const int bolt0 = EffectiveSpellCost(*bolt, s).ManaValue();
    const int elves0 = EffectiveSpellCost(*elves, s).ManaValue();
    CiArm arm("tax:3-4:2:noncreature; tax:3:1", s);
    ctrlinject::OnRoundStart(s);
    CHECK(EffectiveSpellCost(*bolt, s).ManaValue() == bolt0 + 3);
    CHECK(EffectiveSpellCost(*elves, s).ManaValue() == elves0 + 1);
    CHECK(ctrlinject::BenefitPending(s));
    s.turn_number = 4;
    ctrlinject::OnRoundStart(s);
    CHECK(EffectiveSpellCost(*bolt, s).ManaValue() == bolt0 + 2);
    CHECK(EffectiveSpellCost(*elves, s).ManaValue() == elves0);
    s.turn_number = 5;
    CHECK(ctrlinject::OnRoundStart(s).changed);
    CHECK(EffectiveSpellCost(*bolt, s).ManaValue() == bolt0);
    CHECK(s.ctrl_tax_all == 0);
    CHECK(s.ctrl_tax_split == 0);
    CHECK_FALSE(ctrlinject::BenefitPending(s));
}

TEST_CASE("ctrl_inject: counters -- by index, mv>= and name; graveyard, hand, library top")
{
    GameState s = CiFresh(3);
    {
        CiArm arm("counter:3:1", s);   // the SECOND spell to reach resolution this round
        CHECK_FALSE(ctrlinject::OnSpellResolving(s, CiSpell("Lightning Bolt", 30)));
        CHECK(ctrlinject::OnSpellResolving(s, CiSpell("Lightning Bolt", 31)));
        CHECK_FALSE(ctrlinject::OnSpellResolving(s, CiSpell("Lightning Bolt", 32)));
        CHECK(CiIn(s.players[0].graveyard, 31));
        CHECK_FALSE(CiIn(s.players[0].graveyard, 30));
    }
    {
        CiArm arm("counter_to_hand:3:mv>=3", s);
        CHECK_FALSE(ctrlinject::OnSpellResolving(s, CiSpell("Lightning Bolt", 33)));
        CHECK(ctrlinject::OnSpellResolving(s, CiSpell("Light Up the Stage", 34)));
        CHECK(CiIn(s.players[0].hand, 34));
    }
    {
        CiArm arm("counter_to_top:3:name=Lightning Bolt", s);
        CHECK_FALSE(ctrlinject::OnSpellResolving(s, CiSpell("Light Up the Stage", 35)));
        CHECK(ctrlinject::OnSpellResolving(s, CiSpell("Lightning Bolt", 36)));
        REQUIRE(s.players[0].library.size() > 0);
        CHECK(s.players[0].library.RawAt(0).m_number == 36);
        CHECK(s.players[0].library.KnownAt(0));
    }
    {
        CiArm arm("counter:4", s);     // another round: nothing this round
        CHECK_FALSE(ctrlinject::OnSpellResolving(s, CiSpell("Lightning Bolt", 37)));
    }
    {
        CiArm arm("counter:3", s);     // an ability is never a candidate, nor a spell of the other player
        StackEntry ab = CiSpell("Lightning Bolt", 38);
        ab.type = StackEntry::EntryType::Triggered;
        CHECK_FALSE(ctrlinject::OnSpellResolving(s, ab));
        StackEntry theirs = CiSpell("Lightning Bolt", 39);
        theirs.controller_index = 1;
        CHECK_FALSE(ctrlinject::OnSpellResolving(s, theirs));
        CHECK(ctrlinject::OnSpellResolving(s, CiSpell("Lightning Bolt", 40)));
    }
}

TEST_CASE("ctrl_inject: a countered flashback spell is exiled, a copy ceases to exist, an uncounterable spell resolves")
{
    GameState s = CiFresh(3);
    {
        CiArm arm("counter_to_hand:3:0", s);
        StackEntry fb = CiSpell("Faithless Looting", 50);
        fb.cast_with_flashback = true;
        CHECK(ctrlinject::OnSpellResolving(s, fb));
        CHECK(CiIn(s.exile, 50));                     // CR 702.34a beats Remand's "to hand"
        CHECK_FALSE(CiIn(s.players[0].hand, 50));
    }
    {
        CiArm arm("counter:3:0", s);
        StackEntry cp = CiSpell("Lightning Bolt", 51);
        cp.is_copy = true;
        CHECK(ctrlinject::OnSpellResolving(s, cp));
        CHECK_FALSE(CiIn(s.players[0].graveyard, 51));
        CHECK_FALSE(CiIn(s.exile, 51));
    }
    {
        CiArm arm("counter:3:0", s);                  // an index counter is spent on the uncounterable spell
        CHECK_FALSE(ctrlinject::OnSpellResolving(s, CiSpell("Niv-Mizzet, Parun", 52)));
        CHECK_FALSE(ctrlinject::OnSpellResolving(s, CiSpell("Lightning Bolt", 53)));
    }
    {
        CiArm arm("counter:3:mv>=1", s);              // a policy counter waits for the next legal spell
        CHECK_FALSE(ctrlinject::OnSpellResolving(s, CiSpell("Niv-Mizzet, Parun", 54)));
        CHECK(ctrlinject::OnSpellResolving(s, CiSpell("Lightning Bolt", 55)));
    }
}

TEST_CASE("ctrl_inject: skip -- the round advances, TurnSeq does not; no untap, no draw; the opponent's turn happens")
{
    GameState s = CiFresh(2);
    s.on_the_play = true;
    CiPerm(s, "Forest", 1).tapped = true;
    StagedCard sc;
    sc.card = CiCard("Lightning Bolt", 60);
    sc.expiry_turn = s.TurnSeq() + 1;             // Light Up the Stage cast this turn: "until the end of your NEXT turn"
    s.players[0].staged_cards.push_back(sc);
    // The opponent's creature scheduled for round 3 still arrives in the skipped round (its side of the round).
    static const std::vector<OpponentSpawn> spawns = { OpponentSpawn{ 3, 4, 4 } };
    s.opponent_spawns = &spawns;
    CiPerm(s, "Forest", 2, /*controller=*/1);   // an opponent land (the spawn land requirement, if armed)
    const int seq0 = s.TurnSeq();
    const std::size_t hand0 = s.players[0].hand.size();
    const std::size_t lib0 = s.players[0].library.size();
    const std::size_t opp_lib0 = s.players[1].library.size();
    AIEngine ai(MulliganProfile::DefaultProfile(), 0, 0);
    GameEngine engine(ai);
    {
        CiArm arm("skip:3", s);
        MtgTestSeam::GeRunTurn(engine, s);
    }
    CHECK(s.turn_number == 3);
    CHECK(static_cast<int>(s.ctrl_skipped_turns) == 1);
    CHECK(s.TurnSeq() == seq0);
    CHECK(OwnRound(s) == 2);   // the heuristics' clock: the round advanced, the goldfisher's own round did not
    CHECK(s.battlefield[static_cast<std::size_t>(CiFind(s, 1))].tapped);   // no untap step
    CHECK(s.players[0].hand.size() == hand0);                              // no draw step
    CHECK(s.players[0].library.size() == lib0);
    int opp_creatures = 0;
    for (const Permanent& p : s.battlefield) { if (p.controller_index == 1 && p.card.IsCreature()) { ++opp_creatures; } }
    CHECK(opp_creatures == 1);
    if (opp_lib0 > 0) { CHECK(s.players[1].library.size() == opp_lib0 - 1); }   // its turn: its notional draw
    GameState next = s;
    next.AdvanceTurnCounter();
    CHECK(next.turn_number == 4);
    CHECK(next.TurnSeq() == seq0 + 1);
    CHECK(OwnRound(next) == 3);   // its next turn is valued as the unskipped game's round 3
    CHECK(sc.expiry_turn >= next.TurnSeq());   // the staged card is still playable on the goldfisher's NEXT turn

    // Control arm: the same turn unarmed untaps, draws and advances TurnSeq.
    GameState u = CiFresh(2);
    u.on_the_play = true;
    CiPerm(u, "Forest", 1).tapped = true;
    const int useq0 = u.TurnSeq();
    const std::size_t ulib0 = u.players[0].library.size();
    MtgTestSeam::GeRunTurn(engine, u);
    CHECK(u.turn_number == 3);
    CHECK(u.TurnSeq() == useq0 + 1);
    CHECK(u.ctrl_skipped_turns == 0);
    CHECK(OwnRound(u) == u.turn_number);   // no skip: the heuristics' clock IS the round (byte-identical)
    CHECK(u.players[0].library.size() == ulib0 - 1);
}

TEST_CASE("ctrl_inject: the record sees the opponent's own turns -- their life change and a kill there")
{
    // Mogis, God of Slaughter deals 2 at each opponent upkeep (no creature to sacrifice): the passive opponent's
    // own turn moves its life, which the analytic skip check must read as an opponent-turn effect, not a search
    // difference. A skipped round is all opponent turn.
    AIEngine ai(MulliganProfile::DefaultProfile(), 0, 0);
    GameEngine engine(ai);
    {
        GameState s = CiFresh(2);
        s.on_the_play = true;
        s.deck_has_turn_gods = true;
        s.dmg_events_armed = true;   // the damage-event deck gate a real Gods game arms
        CiPerm(s, "Mogis, God of Slaughter", 5);
        const int life0 = s.players[1].life;
        CiArm arm("skip:3", s, /*record=*/true);
        MtgTestSeam::GeRunTurn(engine, s);
        CHECK(s.players[1].life == life0 - 2);
        CHECK(ctrlinject::OppTurnLife(3) == -2);
        CHECK_FALSE(ctrlinject::KillOnOppTurn());
    }
    {
        GameState s = CiFresh(2);
        s.on_the_play = true;
        s.deck_has_turn_gods = true;
        s.dmg_events_armed = true;   // the damage-event deck gate a real Gods game arms
        CiPerm(s, "Mogis, God of Slaughter", 5);
        s.players[1].life = 2;
        CiArm arm("skip:3", s, /*record=*/true);
        MtgTestSeam::GeRunTurn(engine, s);
        CHECK(s.players[1].life <= 0);
        CHECK(ctrlinject::KillOnOppTurn());   // the opponent lost during its own turn of round 3
    }
    {
        // Recording alone (no effect) sees it too: the baseline's regular opponent turn.
        GameState s = CiFresh(2);
        s.on_the_play = true;
        s.deck_has_turn_gods = true;
        s.dmg_events_armed = true;   // the damage-event deck gate a real Gods game arms
        CiPerm(s, "Mogis, God of Slaughter", 5);
        CiArm arm("", s, /*record=*/true);
        MtgTestSeam::GeRunTurn(engine, s);
        CHECK(ctrlinject::OppTurnLife(3) == -2);
    }
}
