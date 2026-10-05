// Unit tests for Ardenn, Intrepid Archaeologist's beginning-of-combat attach -- the shared
// FireBeginCombatAttach body (both worlds + the attack projection) and the provider's default
// BeginCombatAttach planner behind it.
//
// WHY THESE ARE UNIT TESTS. The trigger resolves inside combat, where the search has no plan-variant
// branching, so WHICH target and WHICH pieces are a provider decision -- and every one of those
// decisions is a rules or value question with a quiet failure mode: a piece that should have moved
// and did not costs damage silently; a piece that moved and should not have (Grafted Wargear off a
// host, which sacrifices the host) costs a body; a shroud host accepted as a target is an illegal
// attach; an O-Naginata judged on the host's power BEFORE the Bonesplitter in the same trigger is a
// phantom refusal. None of those crashes and none diverges between the two worlds (they share the
// body), so the lockstep harnesses cannot see them. The regression suite cannot either: no suite deck
// carries the card. These pin the behaviour directly, board by board.
//
// Every board here is read through FireBeginCombatAttach(..., live=false), the exact call the attack
// projection makes, so what the tests assert is what the search prices AND what the combat does.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/GameSetup.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"

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

int Put(GameState& s, const std::string& name, int number, bool sick = false)
{
    Permanent p;
    p.card              = Def(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
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

void Attach(GameState& s, int equip_number, int host_number)
{
    for (Permanent& p : s.battlefield)
    { if (p.card.m_number == equip_number) { p.equipped_to = host_number; } }
}

std::vector<int> Attachments(const GameState& s)
{
    std::vector<int> v;
    for (const Permanent& p : s.battlefield) { v.push_back(p.equipped_to); }
    return v;
}

}   // namespace

// ---- the card's reason to exist: the free re-attach before attackers are declared ----------------

TEST_CASE("Ardenn moves the Hammer off its own summoning-sick body onto the attacker, and brings the loose piece too")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    // Ardenn was cast this turn carrying the Hammer (a Sigarda's Aid attach, say); it cannot attack.
    // Kor Duelist can. Hammer -> Duelist is (1+10)x2 = 22 over a bare 1; the loose Bone Saw then adds
    // (1+1+10)x2 - 22 = 2 more. Both move, in that order (biggest flat bonus first).
    const int ardenn  = Put(s, "Ardenn, Intrepid Archaeologist", 10, /*sick=*/true);
    const int duelist = Put(s, "Kor Duelist", 20);
    const int hammer  = Put(s, "Colossus Hammer", 1);
    const int saw     = Put(s, "Bone Saw", 2);
    Attach(s, hammer, ardenn);
    REQUIRE(HasBeginCombatAttachSource(s, 0));
    REQUIRE(BeginCombatProjectedDamage(s, 0) == 1);          // only the bare Duelist swings

    FireBeginCombatAttach(s, 0, /*live=*/false);
    CHECK(AttachedTo(s, hammer) == duelist);
    CHECK(AttachedTo(s, saw)    == duelist);
    CHECK(BeginCombatProjectedDamage(s, 0) == 24);
}

TEST_CASE("two double strikers already split well: nothing moves (concentration is not dominant)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    // A: Duelist + Bone Saw = (1+1)x2 = 4. B: Duelist + Hammer = (1+10)x2 = 22. Ardenn (old) 2.
    // Saw -> B gains 2 and loses 3; Hammer -> A is a wash (+20/-20); everything -> Ardenn is 13+1+1.
    // No move adds damage, so the "may" is declined and the board is untouched.
    Put(s, "Ardenn, Intrepid Archaeologist", 10);
    const int a      = Put(s, "Kor Duelist", 20);
    const int b      = Put(s, "Kor Duelist", 21);
    const int saw    = Put(s, "Bone Saw", 2);
    const int hammer = Put(s, "Colossus Hammer", 1);
    Attach(s, saw, a);
    Attach(s, hammer, b);
    REQUIRE(BeginCombatProjectedDamage(s, 0) == 28);

    const std::vector<int> before = Attachments(s);
    FireBeginCombatAttach(s, 0, /*live=*/false);
    CHECK(Attachments(s) == before);
    CHECK(AttachedTo(s, saw)    == a);
    CHECK(AttachedTo(s, hammer) == b);
}

TEST_CASE("a moved Lightning Greaves makes the summoning-sick Hammer carrier an attacker, and that beats moving the Hammer")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    // The sick Duelist already wears the Hammer. Greaves -> Duelist: it attacks for (1+10)x2 = 22.
    // Hammer -> Ardenn instead: Ardenn swings for 12. The planner must read haste off the board the
    // move produces (CanAttackFull on the scratch board), not off the printed keyword.
    const int ardenn  = Put(s, "Ardenn, Intrepid Archaeologist", 10);
    const int duelist = Put(s, "Kor Duelist", 20, /*sick=*/true);
    const int hammer  = Put(s, "Colossus Hammer", 1);
    const int greaves = Put(s, "Lightning Greaves", 3);
    Attach(s, hammer, duelist);
    REQUIRE(BeginCombatProjectedDamage(s, 0) == 2);          // the Duelist cannot attack yet

    FireBeginCombatAttach(s, 0, /*live=*/false);
    CHECK(AttachedTo(s, greaves) == duelist);
    CHECK(AttachedTo(s, hammer)  == duelist);
    CHECK(AttachedTo(s, hammer)  != ardenn);
    CHECK(BeginCombatProjectedDamage(s, 0) == 24);           // 22 + Ardenn's own 2
}

// ---- the carve-outs ------------------------------------------------------------------------------

TEST_CASE("Grafted Wargear ON a host is never moved (its re-host sacrifices the host); a loose one is")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    // The worn Wargear sits on a sick body, so this-combat arithmetic says "+3 for free" -- and the
    // board would pay a creature for it. The loose copy has no such price and goes to the attacker.
    const int ardenn = Put(s, "Ardenn, Intrepid Archaeologist", 10);
    const int sick   = Put(s, "Puresteel Paladin", 31, /*sick=*/true);
    const int worn   = Put(s, "Grafted Wargear", 4);
    const int loose  = Put(s, "Grafted Wargear", 5);
    Attach(s, worn, sick);

    FireBeginCombatAttach(s, 0, /*live=*/false);
    CHECK(AttachedTo(s, worn)  == sick);
    CHECK(AttachedTo(s, loose) == ardenn);
    CHECK(s.battlefield.size() == 4);                        // nobody was sacrificed
}

TEST_CASE("a shroud host is not a legal target -- the trigger TARGETS (CR 702.18b)")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    // The Duelist wears Greaves: shroud. The Hammer would be worth (1+10)x2 = 22 there against 12 on
    // Ardenn, and it still may not go there. (Balan's attach-all does not target and would.)
    const int ardenn  = Put(s, "Ardenn, Intrepid Archaeologist", 10);
    const int duelist = Put(s, "Kor Duelist", 20);
    const int greaves = Put(s, "Lightning Greaves", 3);
    const int hammer  = Put(s, "Colossus Hammer", 1);
    Attach(s, greaves, duelist);
    REQUIRE(CreatureHasShroud(s.battlefield[1], s));

    FireBeginCombatAttach(s, 0, /*live=*/false);
    CHECK(AttachedTo(s, hammer)  == ardenn);
    CHECK(AttachedTo(s, greaves) == duelist);                // untouched
}

TEST_CASE("O-Naginata is admitted after the Bonesplitter that lifts the 1/1 over its power gate")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    // Judged on the board as it stands the Naginata is illegal on a 1/1 (power 3 required). The
    // planner re-reads the gate after each move it keeps, so the Bonesplitter (+2 -> power 3) goes
    // first and the Naginata follows; the realised attach (ApplyEquip) re-checks the same gate.
    Put(s, "Ardenn, Intrepid Archaeologist", 10, /*sick=*/true);
    const int duelist  = Put(s, "Kor Duelist", 20);
    const int splitter = Put(s, "Bonesplitter", 6);
    const int naginata = Put(s, "O-Naginata", 7);
    REQUIRE_FALSE(CanAttachEquip(s, 0, naginata, duelist));

    FireBeginCombatAttach(s, 0, /*live=*/false);
    CHECK(AttachedTo(s, splitter) == duelist);
    CHECK(AttachedTo(s, naginata) == duelist);
    CHECK(BeginCombatProjectedDamage(s, 0) == 12);           // (1+2+3) x 2
}

TEST_CASE("declines cleanly when nothing gains, and is a no-op on a board without the source")
{
    EnsureCardsLoaded();
    {
        GameState s = Fresh();
        Put(s, "Ardenn, Intrepid Archaeologist", 10);
        const int duelist = Put(s, "Kor Duelist", 20);
        const int hammer  = Put(s, "Colossus Hammer", 1);
        Attach(s, hammer, duelist);                          // already on the best host, nothing loose
        const std::vector<int> before = Attachments(s);
        FireBeginCombatAttach(s, 0, /*live=*/false);
        CHECK(Attachments(s) == before);
    }
    {
        GameState s = Fresh();                               // no Ardenn: the gate is false and the
        Put(s, "Kor Duelist", 20);                           // body must not touch the board
        Put(s, "Colossus Hammer", 1);
        Put(s, "Puresteel Paladin", 30, /*sick=*/true);
        CHECK_FALSE(HasBeginCombatAttachSource(s, 0));
        const std::vector<int> before = Attachments(s);
        FireBeginCombatAttach(s, 0, /*live=*/false);
        CHECK(Attachments(s) == before);
    }
}

TEST_CASE("the provider pick is the projection's pick: the same board resolves identically twice")
{
    EnsureCardsLoaded();
    // The projection applies the trigger on a scratch copy and the combat applies it on the live
    // state; both go through the same body with the same provider, so the two boards must agree
    // attachment for attachment -- that is the lockstep this card leans on.
    GameState s = Fresh();
    Put(s, "Ardenn, Intrepid Archaeologist", 10, /*sick=*/true);
    Put(s, "Kor Duelist", 20);
    Put(s, "Puresteel Paladin", 30);
    const int hammer = Put(s, "Colossus Hammer", 1);
    Put(s, "Kite Shield", 2);
    Put(s, "Bone Saw", 3);
    Put(s, "Golem-Skin Gauntlets", 8);
    Attach(s, hammer, 30);                                   // the Paladin wears the Hammer; the Duelist is bare

    GameState projected = s;
    FireBeginCombatAttach(projected, 0, /*live=*/false);
    GameState realised = s;
    FireBeginCombatAttach(realised, 0, /*live=*/true);       // no chooser installed in a unit test
    CHECK(Attachments(projected) == Attachments(realised));
    CHECK(BeginCombatProjectedDamage(projected, 0) >= BeginCombatProjectedDamage(s, 0));
}
