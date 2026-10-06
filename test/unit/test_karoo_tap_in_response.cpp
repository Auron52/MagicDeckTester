// Unit tests for the Karoo TAP-IN-RESPONSE model and the USER's bounce order, which ride ONE lever
// (MTG_BOUNCE_UNTAPPED_FIRST -> KarooTapInResponseOn, heurarm slot BOUNCE_UNTAPPED_FIRST), 2026-10-06.
//
// Rules: a Karoo's "return a land you control to its owner's hand" is a triggered ability (CR 603.2).
// Before it resolves its controller holds priority and may activate mana abilities (CR 605.3a), so the
// land about to be returned can be tapped first; the mana stays in the pool until the phase ends
// (CR 106.4). BounceKarooLand is the ONE land-drop ETB shared by the executor, the rollout and the
// enumeration probe, so these tests exercise both worlds at once.
//
//  1. Lever ON, an untapped Mountain is returned -> {R} floats, the Mountain is in hand.
//  2. Lever ON, the returned land was already TAPPED -> nothing floats (no mana from nowhere).
//  3. Lever OFF -> the untapped Mountain is returned with NO float (byte-identical to before).
//  4. Provider order: lever OFF returns the tapped Mystic Monastery; lever ON returns the untapped
//     Forbidden Orchard (USER: "the monastery will come into play tapped again"), floating one unit of
//     its free colour choice and creating NO second Spirit.
//  5. An untapped land whose tap has a cost (Brushland, pain) is NOT floatable: the lever keeps the
//     old rank against a tapped tapland, and if it must be returned anyway nothing floats.
//  6. A choice-of-two land commits its float NEED-AWARE (the shared real-float colour rule): Spirebluff
//     Canal floats {R} for a hand holding Lightning Bolt, and its first colour ({U}) for an empty hand.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"
#include "ai/DecisionProviders.h"
#include "ai/EngineFlags.h"
#include "ai/HeuristicArm.h"

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

int Put(GameState& s, const std::string& name, int number, bool tapped)
{
    Permanent p;
    p.card             = Def(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    p.tapped           = tapped;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

void Hand(GameState& s, const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

GameState Fresh()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

// The just-played Karoo: LAST battlefield slot, tapped (it enters tapped), as PlayLandFromHand leaves it.
int PlayKaroo(GameState& s, int number)
{
    return Put(s, "Izzet Boilerworks", number, /*tapped=*/true);
}

bool InHand(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { return true; } }
    return false;
}

bool OnBoard(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return true; } }
    return false;
}

int OpponentCreatures(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.controller_index == 1) { ++n; } }
    return n;
}

// Force the lever for this thread (the per-job batch override), restored on scope exit.
struct LeverArm
{
    std::int8_t prev;
    explicit LeverArm(bool on) : prev(heurarm::t_arm[heurarm::BOUNCE_UNTAPPED_FIRST])
    { heurarm::t_arm[heurarm::BOUNCE_UNTAPPED_FIRST] = on ? 1 : 0; }
    ~LeverArm() { heurarm::t_arm[heurarm::BOUNCE_UNTAPPED_FIRST] = prev; }
};

}   // namespace

TEST_CASE("Karoo tap-in-response: an untapped returned Mountain floats {R} (lever ON)")
{
    EnsureCardsLoaded();
    LeverArm on(true);
    REQUIRE(KarooTapInResponseOn());
    GameState s = Fresh();
    Put(s, "Mountain", 1, /*tapped=*/false);
    const int k = PlayKaroo(s, 2);

    BounceKarooLand(s, 0, k);

    CHECK(InHand(s, "Mountain"));
    CHECK_FALSE(OnBoard(s, "Mountain"));
    CHECK(OnBoard(s, "Izzet Boilerworks"));
    CHECK(s.floating_mana.red == 1);
    CHECK(s.floating_mana.Total() == 1);
}

TEST_CASE("Karoo tap-in-response: an already-TAPPED returned land floats nothing")
{
    EnsureCardsLoaded();
    LeverArm on(true);
    GameState s = Fresh();
    Put(s, "Mountain", 1, /*tapped=*/true);
    const int k = PlayKaroo(s, 2);

    BounceKarooLand(s, 0, k);

    CHECK(InHand(s, "Mountain"));
    CHECK(s.floating_mana.Total() == 0);
}

TEST_CASE("Karoo tap-in-response: lever OFF returns the untapped land with no float")
{
    EnsureCardsLoaded();
    LeverArm off(false);
    REQUIRE_FALSE(KarooTapInResponseOn());
    GameState s = Fresh();
    Put(s, "Mountain", 1, /*tapped=*/false);
    const int k = PlayKaroo(s, 2);

    BounceKarooLand(s, 0, k);

    CHECK(InHand(s, "Mountain"));
    CHECK(s.floating_mana.Total() == 0);
}

TEST_CASE("Karoo bounce order: lever OFF returns the tapped Monastery, lever ON the untapped Orchard")
{
    EnsureCardsLoaded();
    GameState s = Fresh();
    const int mon = Put(s, "Mystic Monastery", 1, /*tapped=*/true);   // re-enters TAPPED
    const int orc = Put(s, "Forbidden Orchard", 2, /*tapped=*/false); // re-enters untapped
    const int k   = PlayKaroo(s, 3);
    const std::vector<int> legal = { mon, orc };

    {
        LeverArm off(false);
        const std::vector<int> ranked = ResolveProvider(s).BounceLandCandidates(s, 0, k, legal);
        REQUIRE(!ranked.empty());
        CHECK(ranked.front() == mon);
    }
    {
        LeverArm on(true);
        const std::vector<int> ranked = ResolveProvider(s).BounceLandCandidates(s, 0, k, legal);
        REQUIRE(!ranked.empty());
        CHECK(ranked.front() == orc);

        // ...and the apply realises exactly what the ranking assumed: the Orchard is tapped in
        // response (a full-rainbow source floats one unit of free colour choice), and its Spirit is
        // NOT created a second time -- the turn-start model already assumes the Orchard is tapped.
        const int spirits_before = OpponentCreatures(s);
        BounceKarooLand(s, 0, k);
        CHECK(InHand(s, "Forbidden Orchard"));
        CHECK(OnBoard(s, "Mystic Monastery"));
        CHECK(s.floating_mana.Total() == 1);
        CHECK(s.floating_mana.wild == 1);
        CHECK(OpponentCreatures(s) == spirits_before);
    }
}

TEST_CASE("Karoo tap-in-response: a land whose tap has a cost (painland) is not floatable")
{
    EnsureCardsLoaded();
    LeverArm on(true);
    {
        GameState s = Fresh();
        const int mon  = Put(s, "Mystic Monastery", 1, /*tapped=*/true);
        const int brus = Put(s, "Brushland", 2, /*tapped=*/false);
        const int k    = PlayKaroo(s, 3);
        CHECK_FALSE(KarooBounceFloatable(s, s.battlefield[brus], Def("Brushland")));
        // Returning the Brushland would cost this phase's mana, so the old rank stands: Monastery.
        const std::vector<int> ranked =
            ResolveProvider(s).BounceLandCandidates(s, 0, k, std::vector<int>{ mon, brus });
        REQUIRE(!ranked.empty());
        CHECK(ranked.front() == mon);
    }
    {
        GameState s = Fresh();
        Put(s, "Brushland", 1, /*tapped=*/false);   // the only other land: it must go
        const int k = PlayKaroo(s, 2);
        BounceKarooLand(s, 0, k);
        CHECK(InHand(s, "Brushland"));
        CHECK(s.floating_mana.Total() == 0);
        CHECK(s.players[0].life == 20);             // no pain was paid for a tap that never happened
    }
}

TEST_CASE("Karoo tap-in-response: a choice-of-two land commits its float need-aware")
{
    EnsureCardsLoaded();
    LeverArm on(true);
    {
        GameState s = Fresh();
        Hand(s, "Lightning Bolt", 50);              // the hand wants {R}
        Put(s, "Spirebluff Canal", 1, /*tapped=*/false);
        const int k = PlayKaroo(s, 2);
        BounceKarooLand(s, 0, k);
        CHECK(InHand(s, "Spirebluff Canal"));
        CHECK(s.floating_mana.red == 1);
        CHECK(s.floating_mana.wild == 0);           // a partial choice is COMMITTED, never laundered
        CHECK(s.floating_mana.Total() == 1);
    }
    {
        GameState s = Fresh();                      // no demand -> the first listed colour
        Put(s, "Spirebluff Canal", 1, /*tapped=*/false);
        const int k = PlayKaroo(s, 2);
        BounceKarooLand(s, 0, k);
        CHECK(s.floating_mana.blue == 1);
        CHECK(s.floating_mana.Total() == 1);
    }
}
