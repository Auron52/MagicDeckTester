// Unit cover for the USER's KittyEquipment cast order (MTG_KE_ORDER, default ON), as ruled 2026-10-15
// (docs/design/cast-order-review-2026-10-14.md, "KittyEquipment -- SETTLED"; docs/design/cast-order-rankings.md,
// "KittyEquipment -- USER ruling 2026-10-15"):
//
//   USER: "I guess v1s order is okay except thatwe should split the equipment. The creatures going after
//   equipment is not all that important." and "Sram should be main 1 like Puresteel."
//
//   v1: land > Sol Ring > Puresteel Paladin > Stoneforge Mystic > Kor Duelist > Kemba, Kha Regent > Armored
//   Skyhunter > Balan, Wandering Knight > Bonesplitter > Colossus Hammer > O-Naginata > Shadowspear >
//   Lightning Greaves > Umezawa's Jitte > Grafted Wargear > Loxodon Warhammer.
//
// "Split the equipment" = every Equipment its own slot: class, then CHEAPEST mana value first, then the card
// NAME (DecisionProvider::CastOrderTotalByName) -- never the old 8-bit name hash and never hand order. So:
//   1. each list (v1, v2-puresteel-hammer, v3-adelbert-gauntlets) realises exactly its order;
//   2. permuting the hand (the plan vector) never changes the realised order;
//   3. the comparator is TOTAL over every distinct name, a card the rank does not know falling to its NAME;
//   4. the key is class*4096 + mv*256 (no hash), so a class + mana value tie is broken by the name alone;
//   5. every rank decodes to a tier name (the report no longer prints "(unclassified)");
//   6. Sram and Puresteel are pinned to Main1 (removal stays Main2);
//   7. lever off is the generic order, byte-for-byte.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <algorithm>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace
{

void EnsureCardsKE()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefKE(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

// Force one lever for this thread, restored on scope exit.
struct ArmKE
{
    heurarm::Slot slot;
    ArmKE(heurarm::Slot s, bool on) : slot(s) { heurarm::t_arm[slot] = on ? 1 : 0; }
    ~ArmKE() { heurarm::t_arm[slot] = -1; }
};

const EquipmentProvider& ProvKE()
{
    static const EquipmentProvider p;
    return p;
}

// A pre-combat main with the land drop made and 40 untapped Plains -- enough mana that the payable-order
// fallback never has a reason to move anything -- under the Equipment provider.
GameState BoardKE()
{
    EnsureCardsKE();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 9;
    s.players[0].life = s.players[1].life = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.m_provider = &ProvKE();
    for (int k = 0; k < 40; ++k)
    {
        Permanent p;
        p.card             = DefKE("Plains").card;
        p.card.m_number    = 300 + k;
        p.controller_index = 0;
        p.owner_index      = 0;
        s.battlefield.push_back(p);
    }
    for (int k = 0; k < 20; ++k)
    {
        Card c = DefKE("Plains").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    return s;
}

// A canonical-route plan (searched_order false) casting `names` in VECTOR order.
TurnSolver::Plan PlanKE(const std::vector<std::string>& names)
{
    TurnSolver::Plan plan;
    plan.land_decided = true;
    for (const std::string& n : names)
    {
        Action a;
        a.kind      = Action::Kind::CastFromHand;
        a.card_name = n;
        a.def       = &DefKE(n);
        plan.actions.push_back(a);
    }
    plan.searched_order = false;
    return plan;
}

Action CastKE(const std::string& name)
{
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = name;
    a.def       = &DefKE(name);
    return a;
}

// Identity, reverse, and 40 fixed-seed shuffles of `v`.
std::vector<std::vector<std::string>> PermutationsKE(const std::vector<std::string>& v)
{
    std::vector<std::vector<std::string>> out;
    out.push_back(v);
    out.emplace_back(v.rbegin(), v.rend());
    std::mt19937 rng(20261015u);
    for (int k = 0; k < 40; ++k)
    {
        std::vector<std::string> p = v;
        std::shuffle(p.begin(), p.end(), rng);
        out.push_back(p);
    }
    return out;
}

// Every ordered pair of DISTINCT names: exactly one direction compares less (the order is total).
void CheckTotalKE(const GameState& s, const std::vector<std::string>& names)
{
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        for (std::size_t j = 0; j < names.size(); ++j)
        {
            if (i == j || names[i] == names[j]) { continue; }
            const bool ab = CastOrderLess(s, CastKE(names[i]), CastKE(names[j]));
            const bool ba = CastOrderLess(s, CastKE(names[j]), CastKE(names[i]));
            CHECK_MESSAGE(ab != ba, "tie between ", names[i], " and ", names[j]);
        }
    }
}

// The nonland, castable main deck of each list in its realised order (removal is NeverCast).
const std::vector<std::string> kV1 = {
    "Sol Ring", "Puresteel Paladin", "Stoneforge Mystic", "Kor Duelist", "Kemba, Kha Regent",
    "Armored Skyhunter", "Balan, Wandering Knight", "Bonesplitter", "Colossus Hammer", "O-Naginata",
    "Shadowspear", "Lightning Greaves", "Umezawa's Jitte", "Grafted Wargear", "Loxodon Warhammer",
};

// v2-puresteel-hammer: Sigarda's Aid (attach on ETB) > Puresteel > Sram > Cid (reducer) > the {0} Equipment by
// name > the {1} Equipment by name > Dwalin (hone on ENTER, after the Equipment).
const std::vector<std::string> kV2 = {
    "Sigarda's Aid", "Puresteel Paladin", "Sram, Senior Edificer", "Cid, Freeflier Pilot",
    "Accorder's Shield", "Bone Saw", "Cathar's Shield", "Kite Shield", "Spidersilk Net",
    "Colossus Hammer", "Deconstruction Hammer", "Dragonfire Blade", "Golem-Skin Gauntlets", "Shadowspear",
    "Skateboard", "Dwalin, Weaponmaster",
};

// v3-adelbert-gauntlets: v2 with Adelbert Steiner (a host, class 7) in Cid's place.
const std::vector<std::string> kV3 = {
    "Sigarda's Aid", "Puresteel Paladin", "Sram, Senior Edificer", "Adelbert Steiner",
    "Accorder's Shield", "Bone Saw", "Cathar's Shield", "Kite Shield", "Spidersilk Net",
    "Colossus Hammer", "Deconstruction Hammer", "Dragonfire Blade", "Golem-Skin Gauntlets", "Shadowspear",
    "Skateboard", "Dwalin, Weaponmaster",
};

void CheckRealisedKE(const std::vector<std::string>& order)
{
    const GameState s = BoardKE();
    for (const std::vector<std::string>& perm : PermutationsKE(order))
    { CHECK(TurnSolver::RealisedNonSacCastOrder(s, PlanKE(perm)) == order); }
    // The rank alone is non-decreasing along the list (the name only breaks a class + mana value tie).
    for (std::size_t i = 1; i < order.size(); ++i)
    {
        CAPTURE(order[i - 1]);
        CAPTURE(order[i]);
        CHECK(ProvKE().CastOrderRank(s, DefKE(order[i - 1])) <= ProvKE().CastOrderRank(s, DefKE(order[i])));
    }
}

}   // namespace

TEST_CASE("KittyEquipment cast order: v1 realises the USER's 2026-10-15 order, whatever the hand order")
{
    ArmKE arm(heurarm::KE_ORDER, true);
    const GameState s = BoardKE();   // loads cards.json first
    CHECK(ProvKE().CastOrderTotalByName());
    CHECK(ProvKE().LandDropCastOrderRank() == 0);   // land first
    CHECK(ProvKE().LandDropCastOrderRank() < ProvKE().CastOrderRank(s, DefKE("Sol Ring")));
    CheckRealisedKE(kV1);
    // Two copies of one card stay together, in place.
    CHECK(TurnSolver::RealisedNonSacCastOrder(
              s, PlanKE({"Colossus Hammer", "Bonesplitter", "Colossus Hammer", "Sol Ring"}))
          == std::vector<std::string>{"Sol Ring", "Bonesplitter", "Colossus Hammer", "Colossus Hammer"});
}

TEST_CASE("KittyEquipment cast order: v2 and v3 realise the same rule, whatever the hand order")
{
    ArmKE arm(heurarm::KE_ORDER, true);
    CheckRealisedKE(kV2);
    CheckRealisedKE(kV3);
}

TEST_CASE("KittyEquipment cast order: TOTAL over every list's names, an unknown card falls to its name")
{
    ArmKE arm(heurarm::KE_ORDER, true);
    const GameState s = BoardKE();
    std::vector<std::string> names = kV1;
    names.insert(names.end(), kV2.begin(), kV2.end());
    names.insert(names.end(), kV3.begin(), kV3.end());
    // Two creatures the deck does not hold: hosts by class (7) at Kor Duelist's mana value, so the
    // three share one rank and the NAME orders them.
    names.push_back("Goblin Guide");
    names.push_back("Monastery Swiftspear");
    CheckTotalKE(s, names);
    CHECK(ProvKE().CastOrderRank(s, DefKE("Goblin Guide")) == ProvKE().CastOrderRank(s, DefKE("Kor Duelist")));
    CHECK(TurnSolver::RealisedNonSacCastOrder(s, PlanKE({"Monastery Swiftspear", "Kor Duelist", "Goblin Guide"}))
          == std::vector<std::string>{"Goblin Guide", "Kor Duelist", "Monastery Swiftspear"});
}

TEST_CASE("KittyEquipment cast order: the key is class then mana value -- the name hash is gone")
{
    ArmKE arm(heurarm::KE_ORDER, true);
    const GameState s = BoardKE();
    const EquipmentProvider& p = ProvKE();
    auto rank = [&](const char* n) { return p.CastOrderRank(s, DefKE(n)); };
    // Equipment (class 8): one key per mana value, shared by every Equipment of that cost.
    CHECK(rank("Bonesplitter") == 8 * 4096 + 1 * 256);
    for (const char* n : {"Colossus Hammer", "O-Naginata", "Shadowspear"}) { CHECK(rank(n) == rank("Bonesplitter")); }
    CHECK(rank("Lightning Greaves") == 8 * 4096 + 2 * 256);
    CHECK(rank("Umezawa's Jitte") == rank("Lightning Greaves"));
    CHECK(rank("Grafted Wargear") == 8 * 4096 + 3 * 256);
    CHECK(rank("Loxodon Warhammer") == rank("Grafted Wargear"));
    CHECK(rank("Bone Saw") == 8 * 4096);
    // Hosts (class 7): Skyhunter and Balan share mana value 4, so the name puts Skyhunter first.
    CHECK(rank("Armored Skyhunter") == 7 * 4096 + 4 * 256);
    CHECK(rank("Balan, Wandering Knight") == rank("Armored Skyhunter"));
    // The role classes, in the USER's order.
    CHECK(rank("Sol Ring") == 1 * 4096 + 1 * 256);
    CHECK(rank("Sigarda's Aid") / 4096 == 2);
    CHECK(rank("Puresteel Paladin") / 4096 == 3);
    CHECK(rank("Sram, Senior Edificer") / 4096 == 4);
    CHECK(rank("Cid, Freeflier Pilot") / 4096 == 5);
    CHECK(rank("Stoneforge Mystic") / 4096 == 6);
    CHECK(rank("Dwalin, Weaponmaster") / 4096 == 9);
}

TEST_CASE("KittyEquipment cast order: every rank decodes to a tier name")
{
    ArmKE arm(heurarm::KE_ORDER, true);
    const GameState s = BoardKE();
    std::vector<std::string> names = kV1;
    names.insert(names.end(), kV2.begin(), kV2.end());
    names.insert(names.end(), kV3.begin(), kV3.end());
    names.push_back("Swords to Plowshares");
    names.push_back("Unexpectedly Absent");
    for (const std::string& n : names)
    {
        const char* t = ProvKE().CastOrderTierName(ProvKE().CastOrderRank(s, DefKE(n)));
        CHECK_MESSAGE(t != nullptr, n);
        if (t != nullptr) { CHECK_MESSAGE(std::strlen(t) > 0, n); }
    }
    const char* eq = ProvKE().CastOrderTierName(ProvKE().CastOrderRank(s, DefKE("Bonesplitter")));
    REQUIRE(eq != nullptr);
    CHECK(std::string(eq).find("EQUIPMENT") != std::string::npos);
}

TEST_CASE("KittyEquipment main phase: Sram and Puresteel are Main1 (USER), removal Main2, the rest unpinned")
{
    using MP = DecisionProvider::MainPhase;
    const GameState s = BoardKE();
    {
        ArmKE arm(heurarm::KE_ORDER, true);
        CHECK(ProvKE().MainPhaseOverride(s, DefKE("Sram, Senior Edificer")) == MP::Main1);
        CHECK(ProvKE().MainPhaseOverride(s, DefKE("Puresteel Paladin")) == MP::Main1);
        CHECK(ProvKE().MainPhaseOverride(s, DefKE("Swords to Plowshares")) == MP::Main2);
        CHECK(ProvKE().MainPhaseOverride(s, DefKE("Unexpectedly Absent")) == MP::Main2);
        CHECK_FALSE(ProvKE().MainPhaseOverride(s, DefKE("Kor Duelist")).has_value());
        CHECK_FALSE(ProvKE().MainPhaseOverride(s, DefKE("Bonesplitter")).has_value());
    }
    {
        ArmKE arm(heurarm::KE_ORDER, false);
        for (const char* n : {"Sram, Senior Edificer", "Puresteel Paladin", "Swords to Plowshares", "Kor Duelist"})
        { CHECK_FALSE(ProvKE().MainPhaseOverride(s, DefKE(n)).has_value()); }
    }
}

TEST_CASE("KittyEquipment cast order: lever off is the generic order")
{
    ArmKE arm(heurarm::KE_ORDER, false);
    const GameState s = BoardKE();
    static const GenericProvider generic;
    std::vector<std::string> names = kV1;
    names.insert(names.end(), kV2.begin(), kV2.end());
    for (const std::string& n : names)
    { CHECK(ProvKE().CastOrderRank(s, DefKE(n)) == generic.CastOrderRank(s, DefKE(n))); }
    CHECK_FALSE(ProvKE().CastOrderTotalByName());
    CHECK_FALSE(ProvKE().OrderOpaqueCastsByRank());
    CHECK(ProvKE().LandDropCastOrderRank() == -1);
    CHECK(ProvKE().CastOrderTierName(8 * 4096 + 256) == nullptr);
}
