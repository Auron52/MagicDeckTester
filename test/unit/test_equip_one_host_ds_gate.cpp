// The double-strike gate of the USER's one-host equip rule (MTG_EQUIP_ONE_HOST) must see a double
// striker the frame can CAST (MTG_EQUIP_DS_GATE_CASTS, docs/design/kitty-one-host-rootcause.md).
//
// USER RULING 2026-10-11: *"There is no double strike so we should always choose exactly one creature
// to equip (not summoning-sick preferred) and put everything on that creature."* The rule is gated on
// FrameHasDoubleStrikeSource, which checks the battlefield AND the cast candidates. Inside
// CollectActions the cast candidates' Action::def is filled only by the closing resolve pass, after
// the equip block, so the cast half of the gate was dead: a Kor Duelist in hand never disarmed the
// rule, the rule chose the old Puresteel Paladin as the one host, and "Colossus Hammer + Jitte onto
// the Kor Duelist cast this turn" was never enumerated (KittyEquipment v1 gi149 T4: T5 instead of T4
// at every depth and budget). Each case below FAILS on the committed tree (77e094a2) in its
// `fix` arm and documents what the dead gate did in its `dead` arm.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsOh()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card CardOh(const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c     = d->card;
    c.m_number = number;
    c.RehashName();
    return c;
}

const DecisionProvider& KittyProv()
{
    static const EquipmentProvider prov;
    return prov;
}

struct BoardOh
{
    GameState s;
    int next = 1;
    BoardOh()
    {
        EnsureCardsOh();
        s.active_player_index = 0;
        s.turn_number         = 4;
        s.players[0].life = s.players[1].life = gamesetup::StartingLife();
        s.m_provider = &KittyProv();
        s.players[0].lands_played_this_turn = 1;   // the land drop is spent: no land-fold variants
        for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(CardOh("Plains", 900 + k)); }
    }
    int Put(const std::string& name, int attached_to = 0)
    {
        Permanent p;
        p.card              = CardOh(name, next++);
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        p.equipped_to       = attached_to;
        s.battlefield.push_back(p);
        return p.card.m_number;
    }
    int Hand(const std::string& name) { s.players[0].hand.push_back(CardOh(name, next)); return next++; }
};

// arm: {one host, ds gate sees casts}; -1 = the binary's default.
// swing: MTG_EQUIP_DS_SWING_FIRST (the doctrine amendment), off unless asked for.
std::vector<TurnSolver::Plan> PlansOh(const GameState& s, int one_host, int gate, int swing = 0)
{
    heurarm::t_arm[heurarm::EQUIP_ONE_HOST]       = static_cast<std::int8_t>(one_host);
    heurarm::t_arm[heurarm::EQUIP_DS_GATE_CASTS]  = static_cast<std::int8_t>(gate);
    heurarm::t_arm[heurarm::EQUIP_DS_SWING_FIRST] = static_cast<std::int8_t>(swing);
    std::vector<TurnSolver::Plan> v = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    heurarm::t_arm[heurarm::EQUIP_ONE_HOST]       = -1;
    heurarm::t_arm[heurarm::EQUIP_DS_GATE_CASTS]  = -1;
    heurarm::t_arm[heurarm::EQUIP_DS_SWING_FIRST] = -1;
    return v;
}

bool PlanEquips(const TurnSolver::Plan& p, int equip, int host)
{
    for (const Action& a : p.actions)
    {
        if (a.kind == Action::Kind::Equip && a.sac_source_id == equip && a.sac_victim_id == host)
        { return true; }
    }
    return false;
}

bool PlanCasts(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return true; } }
    return false;
}

// Some plan casts `cast` and equips `equip` onto `host` in the same main phase.
bool AnyLine(const std::vector<TurnSolver::Plan>& plans, const std::string& cast, int equip, int host)
{
    for (const TurnSolver::Plan& p : plans)
    { if (PlanCasts(p, cast) && PlanEquips(p, equip, host)) { return true; } }
    return false;
}

bool AnyEquipOnto(const std::vector<TurnSolver::Plan>& plans, int equip, int host)
{
    for (const TurnSolver::Plan& p : plans) { if (PlanEquips(p, equip, host)) { return true; } }
    return false;
}

}   // namespace

TEST_CASE("one-host ds gate: a Kor Duelist in HAND disarms the rule (gi149 T4 shape)")
{
    // The gi149 T4 continuation board: the Greaves has resolved and drawn the Duelist off the
    // Paladin's trigger. Hammer + Jitte + Greaves = 3 artifacts, so metalcraft makes every equip {0};
    // one untapped Plains pays the Duelist's {W}. The kill is Hammer + Jitte + Greaves on the hasted
    // Duelist: 11 first-strike, +2 charge counters -> +4/+4, 15 regular = 26, plus the Paladin's 2.
    BoardOh b;
    b.Put("Puresteel Paladin");
    const int hammer = b.Put("Colossus Hammer");
    const int jitte  = b.Put("Umezawa's Jitte");
    b.Put("Lightning Greaves");
    b.Put("Plains");
    const int duelist = b.Hand("Kor Duelist");
    REQUIRE(EquipCostGenericNow(b.s, 0, *CardDatabase::Instance().Lookup("Colossus Hammer"), duelist) == 0);

    const std::vector<TurnSolver::Plan> fix  = PlansOh(b.s, 1, 1);
    const std::vector<TurnSolver::Plan> dead = PlansOh(b.s, 1, 0);
    const std::vector<TurnSolver::Plan> off  = PlansOh(b.s, 0, 0);
    CHECK_MESSAGE(AnyLine(fix, "Kor Duelist", hammer, duelist), "Hammer onto the Duelist cast this turn");
    CHECK_MESSAGE(AnyLine(fix, "Kor Duelist", jitte, duelist),  "Jitte onto the Duelist cast this turn");
    // The rule disabled offers it too: the fix restores exactly what the rule's premise excludes.
    CHECK(AnyLine(off, "Kor Duelist", hammer, duelist));
    // The committed tree's dead gate: the one host is the Paladin, and the Duelist never gets a rider.
    CHECK_FALSE(AnyEquipOnto(dead, hammer, duelist));
    CHECK_FALSE(AnyEquipOnto(dead, jitte, duelist));
    // The doctrine amendment keeps it: the Greaves on the battlefield can haste the Duelist.
    const std::vector<TurnSolver::Plan> amend = PlansOh(b.s, 1, 1, 1);
    CHECK(AnyLine(amend, "Kor Duelist", hammer, duelist));
    CHECK(AnyLine(amend, "Kor Duelist", jitte, duelist));
}

TEST_CASE("one-host ds gate: a Balan in HAND disarms the rule and the Bonesplitters may MOVE to her (gi738 T4 shape)")
{
    // gi738 T4: both Bonesplitters already on the Paladin, the Greaves loose, four Plains for Balan's
    // {2}{W}{W}. Two Bonesplitters + Greaves on Balan = three Equipment -> double strike: (3 + 4) x 2
    // = 14, plus the Paladin's 2 = 16. With the dead gate the Bonesplitters stay on the Paladin and
    // Balan (one Equipment, the Greaves) has no double strike: 6 + 3 = 9.
    BoardOh b;
    const int paladin = b.Put("Puresteel Paladin");
    const int bs1 = b.Put("Bonesplitter", paladin);
    const int bs2 = b.Put("Bonesplitter", paladin);
    b.Put("Lightning Greaves");
    for (int k = 0; k < 4; ++k) { b.Put("Plains"); }
    const int balan = b.Hand("Balan, Wandering Knight");

    const std::vector<TurnSolver::Plan> fix  = PlansOh(b.s, 1, 1);
    const std::vector<TurnSolver::Plan> dead = PlansOh(b.s, 1, 0);
    CHECK(AnyLine(fix, "Balan, Wandering Knight", bs1, balan));
    CHECK(AnyLine(fix, "Balan, Wandering Knight", bs2, balan));
    CHECK_FALSE(AnyEquipOnto(dead, bs1, balan));
    CHECK_FALSE(AnyEquipOnto(dead, bs2, balan));
}

TEST_CASE("one-host ds gate: a castable NON-double-striker leaves the rule in force")
{
    // Two creatures that can attack: the Skyhunter (lower id, nothing attached) and the Paladin
    // carrying a Bonesplitter. The rule's ranking picks the Paladin (it already holds Equipment), so
    // the Hammer is offered only onto it. With the rule OFF the consolidation doctrine's tie-break
    // (equal rider delta, lowest id) offers the Skyhunter instead -- which is what makes this case
    // discriminate. A castable second Paladin in hand is NOT a double striker, so the repaired gate
    // must leave the rule armed exactly as before.
    BoardOh b;
    const int sky     = b.Put("Armored Skyhunter");
    const int paladin = b.Put("Puresteel Paladin");
    b.Put("Bonesplitter", paladin);
    const int hammer = b.Put("Colossus Hammer");
    b.Put("Umezawa's Jitte");
    b.Put("Plains");
    b.Put("Plains");
    b.Hand("Puresteel Paladin");

    const std::vector<TurnSolver::Plan> fix = PlansOh(b.s, 1, 1);
    const std::vector<TurnSolver::Plan> off = PlansOh(b.s, 0, 0);
    CHECK(AnyEquipOnto(fix, hammer, paladin));
    CHECK_FALSE(AnyEquipOnto(fix, hammer, sky));
    CHECK(AnyEquipOnto(off, hammer, sky));
}

TEST_CASE("ds swing-first: a sick double striker with NO haste source does not take a FREE rider from the Paladin (gi66 shape)")
{
    // kitty ship gi66 (seed 10100067) T4, after the third Hammer resolved: metalcraft is on, every equip is
    // {0}, the Kor Duelist is castable but nothing can haste it. Three Hammers on the Paladin are 32 vs 20;
    // on the Duelist they are worth nothing this turn. The consolidation doctrine offers the riders only to
    // its top double-strike-potential host -- the Duelist -- so the Paladin line is never enumerated.
    BoardOh b;
    const int paladin = b.Put("Puresteel Paladin");
    const int h1 = b.Put("Colossus Hammer");
    const int h2 = b.Put("Colossus Hammer");
    b.Put("Colossus Hammer");
    b.Put("Plains");
    const int duelist = b.Hand("Kor Duelist");
    REQUIRE(EquipCostGenericNow(b.s, 0, *CardDatabase::Instance().Lookup("Colossus Hammer"), paladin) == 0);

    const std::vector<TurnSolver::Plan> doctrine = PlansOh(b.s, 1, 1, 0);
    const std::vector<TurnSolver::Plan> amend    = PlansOh(b.s, 1, 1, 1);
    CHECK_FALSE(AnyEquipOnto(doctrine, h1, paladin));       // the hole the amendment closes
    CHECK(AnyEquipOnto(amend, h1, paladin));
    CHECK(AnyEquipOnto(amend, h2, paladin));
    CHECK_FALSE(AnyEquipOnto(amend, h1, duelist));           // a FREE rider is not banked on the sick body
}

TEST_CASE("ds swing-first: a PAID rider is still offered to the sick double striker (banking stays searched)")
{
    // No metalcraft (one artifact), so Bonesplitter's equip is {1}. Two Plains: {W} Duelist + {1} equip.
    // Banking the equip on the Duelist spends this turn's mana instead of next turn's -- a real trade, so
    // both hosts must be offered.
    BoardOh b;
    const int paladin = b.Put("Puresteel Paladin");
    const int bs = b.Put("Bonesplitter");
    b.Put("Plains");
    b.Put("Plains");
    const int duelist = b.Hand("Kor Duelist");
    REQUIRE(EquipCostGenericNow(b.s, 0, *CardDatabase::Instance().Lookup("Bonesplitter"), paladin) == 1);

    const std::vector<TurnSolver::Plan> amend = PlansOh(b.s, 1, 1, 1);
    CHECK(AnyEquipOnto(amend, bs, paladin));
    CHECK(AnyLine(amend, "Kor Duelist", bs, duelist));
}
