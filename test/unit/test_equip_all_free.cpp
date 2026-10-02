// Unit tests for the "equip all free to <host>" bundle -- Action::Kind::AttachAllFreeEquipment's
// shared selection predicate (FreeAttachableEquipment) and its apply (ApplyAttachAllFreeEquipment).
//
// WHY THESE ARE UNIT TESTS, and why they are the primary pin for this feature.
//
// The bundle exists because N independent equip digits cannot fit the play viewer's plan-space
// bound (14 loose pieces x ~4 hosts is 4^14 against 65,536), so a viewer-side macro that queued N
// separate `equip=` clicks could only ever bundle the few the valve had left in the menu -- USER,
// 2026-10-02: *"Equip all free was available, but it only equipped a few."* One enumerated action
// replaces the product. But collapsing N activations into one action means the bundle is now the
// thing deciding WHICH pieces move, and every one of those decisions is a rules question with a
// quiet failure mode:
//
//   * a piece whose equip cost is NOT {0} would be attached for free -- a rules violation, and the
//     exact line Balan's attach-all is allowed to cross (it has a printed ability) and this is not;
//   * a piece that is ALREADY ATTACHED would be silently stripped off its current host, throwing
//     away that host's rider;
//   * Lightning Greaves' shroud would make every LATER equip in the turn illegal;
//   * Grafted Wargear would commit the host to a future sacrifice;
//   * an O-Naginata would be refused a host the bundle itself is about to make legal.
//
// None of those crashes, none diverges loudly, and none moves a regression digest -- the action is
// emitted only under HumanPlayActive(), so no autonomous game ever executes it and the whole
// regression suite is byte-identical by construction. That is exactly why the suite cannot be the
// test: there is no seed-driven run that exercises this code at all. These tests are.
//
// They call the SHARED predicate and the SHARED apply directly, which is the point: the enumeration
// gate, the menu label's count, the published viewer affordance and the apply all read
// FreeAttachableEquipment, so a test of that function is a test of what the menu promises AND of
// what the board then does. A menu entry promising an attach the apply declines is the defect class
// CanAttachEquip was added to close; this bundles it N at a time.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/GameSetup.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"

#include <algorithm>
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

int Put(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = Def(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
    RefreshDevotionCreatures(s);
    return number;
}

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    return s;
}

int AttachedTo(const GameState& s, int equip_number)
{
    for (const Permanent& p : s.battlefield)
    { if (p.card.m_number == equip_number) { return p.equipped_to; } }
    return -1;   // not on the battlefield at all (distinguishable from 0 = unattached)
}

std::vector<int> SelectedIds(const GameState& s, int host)
{
    std::vector<int> ids;
    for (const std::pair<int, int>& pc : FreeAttachableEquipment(s, 0, host))
    { ids.push_back(pc.first); }
    return ids;
}

bool Has(const std::vector<int>& v, int x)
{ return std::find(v.begin(), v.end(), x) != v.end(); }

}   // namespace

// ---- the happy path, and the metalcraft precondition that makes the gesture worth having --------

TEST_CASE("equip-all-free attaches every FREE unattached piece, and returns how many")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    // Puresteel Paladin + >= 3 artifacts = metalcraft, so EVERY Equipment is equip {0} regardless
    // of its printed cost. That is the whole board shape this gesture is for: Colossus Hammer's
    // printed equip is {8} and Kite Shield's is {3}, and here both are free.
    const int host = Put(s, "Kor Duelist", 10);
    Put(s, "Puresteel Paladin", 11);
    const int hammer = Put(s, "Colossus Hammer", 1);
    const int kite1  = Put(s, "Kite Shield", 2);
    const int kite2  = Put(s, "Kite Shield", 3);
    const int saw    = Put(s, "Bone Saw", 4);

    REQUIRE(EquipCostGenericNow(s, 0, Def("Colossus Hammer"), host) == 0);   // metalcraft is live

    const std::vector<int> picked = SelectedIds(s, host);
    CHECK(picked.size() == 4);
    CHECK(ApplyAttachAllFreeEquipment(s, 0, host) == 4);
    CHECK(AttachedTo(s, hammer) == host);
    CHECK(AttachedTo(s, kite1)  == host);
    CHECK(AttachedTo(s, kite2)  == host);
    CHECK(AttachedTo(s, saw)    == host);
    // Idempotent: nothing is left loose, so a second call is a clean no-op rather than a re-attach.
    CHECK(ApplyAttachAllFreeEquipment(s, 0, host) == 0);
}

TEST_CASE("equip-all-free will NOT attach a piece that is not free -- no metalcraft, no bundle")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    // The SAME four-Equipment board as above MINUS Puresteel Paladin, so metalcraft is off and
    // every printed equip cost stands: Colossus Hammer {8}, Kite Shield {3}, Bone Saw {1}. Not one
    // Equipment in this deck's main list has a printed equip of {0}, so with the grant gone the
    // bundle must be EMPTY -- and this pair of tests is each other's control: the only difference
    // between a full bundle and no bundle at all is whether the equips are genuinely free.
    //
    // That is the line Balan's attach-all is allowed to cross and this is not. Balan has a printed
    // ability that says "attach", so bypassing Colossus Hammer's {8} is its whole point; this
    // gesture is shorthand for N Equip ACTIVATIONS, so doing the same would be a rules violation.
    const int host   = Put(s, "Kor Duelist", 10);
    const int hammer = Put(s, "Colossus Hammer", 1);
    const int kite   = Put(s, "Kite Shield", 2);
    const int saw    = Put(s, "Bone Saw", 4);

    REQUIRE(EquipCostGenericNow(s, 0, Def("Colossus Hammer"), host) == 8);
    REQUIRE(EquipCostGenericNow(s, 0, Def("Bone Saw"), host)        == 1);

    CHECK(SelectedIds(s, host).empty());
    CHECK(ApplyAttachAllFreeEquipment(s, 0, host) == 0);
    CHECK(AttachedTo(s, saw)    == 0);     // all three still loose
    CHECK(AttachedTo(s, hammer) == 0);
    CHECK(AttachedTo(s, kite)   == 0);
}

// ---- the four carve-outs ------------------------------------------------------------------------

TEST_CASE("equip-all-free never STRIPS an already-attached piece")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int host  = Put(s, "Kor Duelist", 10);
    const int other = Put(s, "Kor Duelist", 12);
    Put(s, "Puresteel Paladin", 11);
    const int worn  = Put(s, "Colossus Hammer", 1);
    const int loose = Put(s, "Bone Saw", 4);
    Put(s, "Kite Shield", 5);                       // 3rd artifact: metalcraft, so both are free
    const int loose2 = Put(s, "Spidersilk Net", 6);
    // The Hammer is already on the OTHER Duelist. Moving it is a real decision -- it trades away
    // that creature's +10/+10 -- so it belongs to the per-piece `equip=` offer, not to a bulk
    // convenience. A gesture that silently un-equipped a double-striker would be the opposite of
    // convenient.
    for (Permanent& p : s.battlefield)
    { if (p.card.m_number == worn) { p.equipped_to = other; } }

    const std::vector<int> picked = SelectedIds(s, host);
    CHECK(picked.size() == 3);               // the three loose pieces, not the worn Hammer
    CHECK(Has(picked, loose));
    CHECK(Has(picked, loose2));
    CHECK_FALSE(Has(picked, worn));
    CHECK(ApplyAttachAllFreeEquipment(s, 0, host) == 3);
    CHECK(AttachedTo(s, worn)   == other);   // untouched
    CHECK(AttachedTo(s, loose)  == host);
    CHECK(AttachedTo(s, loose2) == host);
}

TEST_CASE("equip-all-free skips a shroud-granting piece, which would lock out every later equip")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int host = Put(s, "Kor Duelist", 10);
    Put(s, "Puresteel Paladin", 11);
    // Lightning Greaves' printed equip is {0}, so it passes the freeness test on its own merits --
    // this carve-out is about CONSEQUENCE, not price. Attaching it gives the host shroud, and a
    // shrouded creature is not a legal equip target (CR 702.18b), so a bundle that included it
    // would spend the player's whole turn's worth of equips on whichever pieces happened to sort
    // after it. The per-piece offer still models the shroud dance; the bundle does not try to.
    const int greaves = Put(s, "Lightning Greaves", 1);
    const int saw     = Put(s, "Bone Saw", 4);
    Put(s, "Kite Shield", 5);                        // 3rd artifact -> metalcraft is live
    REQUIRE(EquipCostGenericNow(s, 0, Def("Lightning Greaves"), host) == 0);
    REQUIRE(EquipCostGenericNow(s, 0, Def("Bone Saw"), host)          == 0);   // free HERE

    const std::vector<int> picked = SelectedIds(s, host);
    CHECK(picked.size() == 2);
    CHECK(Has(picked, saw));
    CHECK_FALSE(Has(picked, greaves));
    CHECK(ApplyAttachAllFreeEquipment(s, 0, host) == 2);
    CHECK(AttachedTo(s, greaves) == 0);
}

TEST_CASE("equip-all-free skips Grafted Wargear, whose equip commits the host to a sacrifice")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int host = Put(s, "Kor Duelist", 10);
    Put(s, "Puresteel Paladin", 11);
    // Also printed equip {0}. The price is paid LATER -- "whenever Grafted Wargear becomes
    // unattached from a permanent, sacrifice that permanent" -- so it is not a free action in any
    // sense the player would recognise, and a bulk gesture must not sign them up for it.
    const int wargear = Put(s, "Grafted Wargear", 1);
    const int saw     = Put(s, "Bone Saw", 4);
    Put(s, "Kite Shield", 5);                        // 3rd artifact -> metalcraft is live
    REQUIRE(EquipCostGenericNow(s, 0, Def("Grafted Wargear"), host) == 0);

    const std::vector<int> picked = SelectedIds(s, host);
    CHECK(picked.size() == 2);
    CHECK_FALSE(Has(picked, wargear));
    CHECK(ApplyAttachAllFreeEquipment(s, 0, host) == 2);
    CHECK(AttachedTo(s, wargear) == 0);
    CHECK(AttachedTo(s, saw)     == host);
}

TEST_CASE("equip-all-free is a no-op when the host is not a creature it controls")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    Put(s, "Puresteel Paladin", 11);
    const int saw = Put(s, "Bone Saw", 4);
    Put(s, "Plains", 20);
    // A land host, and a host that is not on the battlefield at all. Both are the stranded-outlet
    // pattern: full no-op, never a phantom attach.
    CHECK(SelectedIds(s, 20).empty());
    CHECK(ApplyAttachAllFreeEquipment(s, 0, 20)   == 0);
    CHECK(ApplyAttachAllFreeEquipment(s, 0, 9999) == 0);
    CHECK(AttachedTo(s, saw) == 0);
}

// ---- the min-power gate, which is the one case where ORDER decides legality --------------------

TEST_CASE("equip-all-free lifts its own O-Naginata over the power gate, in attach order")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    // Kor Duelist is a 1/1. O-Naginata "can be attached only to a creature with power 3 or
    // greater", so judged on the board as it stands it is ILLEGAL here -- and judged that way it
    // would be dropped from the bundle even though the Bonesplitter in the SAME bundle makes it
    // legal. That is the mistake the Equip enumeration's `reachable_bonus` credit exists to undo,
    // and the bundle has to make the same correction or it silently under-delivers.
    const int host = Put(s, "Kor Duelist", 10);
    Put(s, "Puresteel Paladin", 11);
    const int splitter = Put(s, "Bonesplitter", 1);    // +2/+0, printed equip {1} -> free here
    const int naginata = Put(s, "O-Naginata", 2);      // +3/+0, equip_min_power 3
    const int net      = Put(s, "Spidersilk Net", 3);  // 3rd artifact -> metalcraft; +0 power

    REQUIRE(EquipGatePowerOf(s.battlefield[0], s) == 1);          // the host really is a 1/1
    REQUIRE_FALSE(CanAttachEquip(s, 0, naginata, host));          // ...and really is gated out now

    const std::vector<int> picked = SelectedIds(s, host);
    REQUIRE(picked.size() == 3);
    // ORDER, not just membership: the gated piece must come LAST, or the apply's own re-check
    // declines the Naginata and the count the menu promised is wrong by one. The Net contributes
    // nothing to the gate (+0 power) -- the Bonesplitter's +2 is what lifts the 1/1 to 3.
    CHECK(picked.back() == naginata);
    CHECK(Has(picked, splitter));
    CHECK(Has(picked, net));
    CHECK(ApplyAttachAllFreeEquipment(s, 0, host) == 3);
    CHECK(AttachedTo(s, splitter) == host);
    CHECK(AttachedTo(s, naginata) == host);
}

TEST_CASE("...but it does NOT promise a gated piece the bundle cannot actually lift")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    // The Naginata alone: nothing in the bundle adds power, the 1/1 host stays a 1/1, and the gate
    // is genuinely unreachable. The predicate must say so rather than offer an attach the apply
    // would decline -- the count in the menu label is computed from this same list.
    const int host = Put(s, "Kor Duelist", 10);
    Put(s, "Puresteel Paladin", 11);
    Put(s, "Kite Shield", 12);                        // +0/+3 -- no POWER for the gate
    Put(s, "Spidersilk Net", 13);                     // +0/+2 -- likewise; 3 artifacts = metalcraft
    const int naginata = Put(s, "O-Naginata", 2);

    const std::vector<int> picked = SelectedIds(s, host);
    CHECK_FALSE(Has(picked, naginata));
    CHECK(ApplyAttachAllFreeEquipment(s, 0, host) == 2);   // the two shields, and only those
    CHECK(AttachedTo(s, naginata) == 0);
}

// ---- the invariant the three other readers rely on ---------------------------------------------

TEST_CASE("the predicate and the apply agree exactly -- one list, one count, no over-promise")
{
    EnsureCardsLoaded();
    // The enumeration's "offer it at all" gate, the menu label's count, main.cpp's published
    // `free_equip_all` affordance and the apply ALL read FreeAttachableEquipment. If its size ever
    // disagreed with what the apply attaches, the viewer would show a number the board then
    // contradicts -- so assert the equality directly, over a board holding one of every shape.
    GameState s = Fresh();
    const int host  = Put(s, "Kor Duelist", 10);
    const int other = Put(s, "Kor Duelist", 12);
    Put(s, "Puresteel Paladin", 11);
    Put(s, "Colossus Hammer", 1);                      // free under metalcraft
    Put(s, "Kite Shield", 2);                          // free under metalcraft
    Put(s, "Lightning Greaves", 3);                    // free, carved out (shroud)
    Put(s, "Grafted Wargear", 4);                      // free, carved out (sacrifice)
    Put(s, "O-Naginata", 5);                           // gated, lifted by the Hammer's +10
    const int worn = Put(s, "Bone Saw", 6);            // already attached elsewhere
    for (Permanent& p : s.battlefield)
    { if (p.card.m_number == worn) { p.equipped_to = other; } }

    const std::size_t promised = FreeAttachableEquipment(s, 0, host).size();
    const int         realised = ApplyAttachAllFreeEquipment(s, 0, host);
    CHECK(static_cast<int>(promised) == realised);
    CHECK(realised == 3);                              // Hammer, Kite Shield, O-Naginata
    CHECK(AttachedTo(s, 3) == 0);                      // Greaves untouched
    CHECK(AttachedTo(s, 4) == 0);                      // Wargear untouched
    CHECK(AttachedTo(s, worn) == other);               // not stripped
}
