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
//  5. An untapped land whose tap has a cost (Brushland, pain) is NOT a no-loss bounce: the lever keeps the
//     old rank against a tapped tapland, and if it must be returned anyway nothing floats.
//  6. A choice-of-two land commits its float NEED-AWARE (the shared real-float colour rule): Spirebluff
//     Canal floats {R} for a hand holding Lightning Bolt, and its first colour ({U}) for an empty hand.
//  7. MTG_KAROO_FLOAT_RESTRICTED_C: a creature-only-colour land floats its plain {C}, never a colour.
//  8. MTG_KAROO_FLOAT_LINE_COLOUR: a choice land commits the colour the rest of the applying line owes.
//  9. MTG_KAROO_FLOAT_PAINLAND_C / _OWED: a painland floats its painless {C}, or the owed colour for its damage.
#include <doctest/doctest.h>

#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/SpellEffects.h"
#include "core/HeuristicDefaults.h"
#include "ai/DecisionProviders.h"
#include "ai/EngineFlags.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"           // LineUnpaidCostScope (the line-colour float test)

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

namespace
{
struct SlotArm
{
    int slot; std::int8_t prev;
    SlotArm(int sl, bool on) : slot(sl), prev(heurarm::t_arm[sl]) { heurarm::t_arm[sl] = on ? 1 : 0; }
    ~SlotArm() { heurarm::t_arm[slot] = prev; }
};
}   // namespace

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
        // MTG_KAROO_FLOAT_PAINLAND_C off: the old refusal, nothing floats.
        SlotArm pc(heurarm::KAROO_FLOAT_PAINLAND_C, false);
        GameState s = Fresh();
        Put(s, "Brushland", 1, /*tapped=*/false);   // the only other land: it must go
        const int k = PlayKaroo(s, 2);
        BounceKarooLand(s, 0, k);
        CHECK(InHand(s, "Brushland"));
        CHECK(s.floating_mana.Total() == 0);
        CHECK(s.players[0].life == 20);             // no pain was paid for a tap that never happened
    }
}

// MTG_KAROO_FLOAT_PAINLAND_C / _OWED (heurarm KAROO_FLOAT_PAINLAND_C, KAROO_FLOAT_PAINLAND_OWED). A painland's
// "{T}: Add {C}" is a separate painless ability, so the bounce floats it; when the rest of the line owes a colour the
// land makes, the coloured mode (and its damage) is taken instead.
TEST_CASE("Karoo tap-in-response: a returned painland floats its painless {C}, or the owed colour for pain")
{
    EnsureCardsLoaded();
    LeverArm on(true);
    SlotArm pc(heurarm::KAROO_FLOAT_PAINLAND_C, true);
    SlotArm lc(heurarm::KAROO_FLOAT_LINE_COLOUR, true);
    {
        SlotArm po(heurarm::KAROO_FLOAT_PAINLAND_OWED, true);
        GameState s = Fresh();                      // nothing owed -> the free {C}
        Put(s, "Brushland", 1, /*tapped=*/false);
        const int k = PlayKaroo(s, 2);
        BounceKarooLand(s, 0, k);
        CHECK(InHand(s, "Brushland"));
        CHECK(s.floating_mana.colorless == 1);
        CHECK(s.floating_mana.Total() == 1);
        CHECK(s.players[0].life == 20);
    }
    ManaCost owed_g; owed_g.green = 1;
    {
        SlotArm po(heurarm::KAROO_FLOAT_PAINLAND_OWED, true);
        LineUnpaidCostScope luc(owed_g);
        GameState s = Fresh();                      // the line owes {G} -> {G}, 1 damage
        Put(s, "Brushland", 1, /*tapped=*/false);
        const int k = PlayKaroo(s, 2);
        BounceKarooLand(s, 0, k);
        CHECK(s.floating_mana.green == 1);
        CHECK(s.floating_mana.Total() == 1);
        CHECK(s.players[0].life == 19);
    }
    {
        SlotArm po(heurarm::KAROO_FLOAT_PAINLAND_OWED, false);
        LineUnpaidCostScope luc(owed_g);
        GameState s = Fresh();                      // owed lever off -> always the free {C}
        Put(s, "Brushland", 1, /*tapped=*/false);
        const int k = PlayKaroo(s, 2);
        BounceKarooLand(s, 0, k);
        CHECK(s.floating_mana.colorless == 1);
        CHECK(s.floating_mana.Total() == 1);
        CHECK(s.players[0].life == 20);
    }
    {
        SlotArm po(heurarm::KAROO_FLOAT_PAINLAND_OWED, true);
        LineUnpaidCostScope luc(owed_g);
        GameState s = Fresh();                      // a creature-only pain land (Voldaren Estate) never floats a colour
        Put(s, "Voldaren Estate", 1, /*tapped=*/false);
        const int k = PlayKaroo(s, 2);
        BounceKarooLand(s, 0, k);
        CHECK(s.floating_mana.colorless == 1);
        CHECK(s.floating_mana.Total() == 1);
        CHECK(s.players[0].life == 20);
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

// MTG_KAROO_FLOAT_RESTRICTED_C (heurarm KAROO_FLOAT_RESTRICTED_C). Minotaur mana-clean s30284922 T5: a second Karoo
// returned an untapped Unclaimed Territory with nothing floated (colour-for-creatures-only lands were not floatable),
// and the continuation's Deathbellow Raider {1}{R} was one short. The Territory's "{T}: Add {C}" is unrestricted, so
// it floats exactly {C} -- never a colour, which would launder the creature-only restriction.

TEST_CASE("Karoo tap-in-response: a returned Unclaimed Territory floats its plain {C}, never a colour")
{
    EnsureCardsLoaded();
    LeverArm on(true);
    SlotArm c(heurarm::KAROO_FLOAT_RESTRICTED_C, true);
    REQUIRE(KarooFloatRestrictedCOn());
    GameState s = Fresh();
    Put(s, "Unclaimed Territory", 1, /*tapped=*/false);
    const int k = PlayKaroo(s, 2);

    BounceKarooLand(s, 0, k);

    CHECK(InHand(s, "Unclaimed Territory"));
    CHECK(s.floating_mana.colorless == 1);
    CHECK(s.floating_mana.Total() == 1);
    CHECK(s.floating_creature_mana.Total() == 0);
}

TEST_CASE("Karoo tap-in-response: restricted-{C} lever OFF returns Unclaimed Territory with no float")
{
    EnsureCardsLoaded();
    LeverArm on(true);
    SlotArm c(heurarm::KAROO_FLOAT_RESTRICTED_C, false);
    REQUIRE_FALSE(KarooFloatRestrictedCOn());
    GameState s = Fresh();
    Put(s, "Unclaimed Territory", 1, /*tapped=*/false);
    const int k = PlayKaroo(s, 2);

    BounceKarooLand(s, 0, k);

    CHECK(InHand(s, "Unclaimed Territory"));
    CHECK(s.floating_mana.Total() == 0);
}

// MTG_KAROO_FLOAT_LINE_COLOUR (heurarm KAROO_FLOAT_LINE_COLOUR). Minotaur mana-clean s30289322 T4: Rakdos Carnarium #46
// returned Blood Crypt, which floated its first listed colour {B}, and the continuation's Boros Reckoner ({R}{R} after
// Ragemonger) was dropped on {B}{R}. The float now commits the colour the REST OF THE APPLYING LINE owes, net of the
// float; with nothing owed it keeps the old commit.
TEST_CASE("Karoo tap-in-response: a choice land commits the colour the applying line still owes")
{
    EnsureCardsLoaded();
    LeverArm on(true);
    ManaCost owed_rr; owed_rr.red = 2;
    {
        SlotArm c(heurarm::KAROO_FLOAT_LINE_COLOUR, true);
        LineUnpaidCostScope luc(owed_rr);
        GameState s = Fresh();
        Put(s, "Blood Crypt", 1, /*tapped=*/false);
        const int k = PlayKaroo(s, 2);
        BounceKarooLand(s, 0, k);
        CHECK(InHand(s, "Blood Crypt"));
        CHECK(s.floating_mana.red == 1);
        CHECK(s.floating_mana.Total() == 1);
    }
    {
        // Net of the float: the line owes one {R} and one already floats -> nothing more owed -> first listed {B}.
        SlotArm c(heurarm::KAROO_FLOAT_LINE_COLOUR, true);
        ManaCost owed_r; owed_r.red = 1;
        LineUnpaidCostScope luc(owed_r);
        GameState s = Fresh();
        s.floating_mana.Add(Color::Red, 1);
        Put(s, "Blood Crypt", 1, /*tapped=*/false);
        const int k = PlayKaroo(s, 2);
        BounceKarooLand(s, 0, k);
        CHECK(s.floating_mana.black == 1);
        CHECK(s.floating_mana.red == 1);
    }
    {
        SlotArm c(heurarm::KAROO_FLOAT_LINE_COLOUR, false);   // lever OFF -> the first listed colour
        LineUnpaidCostScope luc(owed_rr);
        GameState s = Fresh();
        Put(s, "Blood Crypt", 1, /*tapped=*/false);
        const int k = PlayKaroo(s, 2);
        BounceKarooLand(s, 0, k);
        CHECK(s.floating_mana.black == 1);
        CHECK(s.floating_mana.Total() == 1);
    }
}
