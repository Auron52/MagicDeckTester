// Hydras: THE USER'S CAST ORDER (MTG_HYDRAS_CAST_ORDER, default ON; USER 2026-11-10: "Doublers and Gargos
// should be before the Hydras as you mentioned.") and the per-job levers the P1 A/B reads.
//
//   1. ON: Vernal Bloom < Gauntlet of Power = Nissa < the three land fetches < Gargos < Lightning Greaves <
//      Garruk < Kalonian Hydra < the four X Hydras; the order is TOTAL (name tie-break).
//   2. =0: every card ranks exactly as the generic tiers (DeckProvider == GenericProvider), and the name
//      tie-break is off.
//   3. MTG_HYDRAS_SECOND_MAIN=0 (the measurement arm) turns Garruk's main 2 off; unset keeps it on.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "runner/GoldFishRunner.h"

#include <string>
#include <vector>

namespace
{
const CardDefinition& DefHco(const std::string& name)
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

struct ArmHco
{
    heurarm::Arm saved;
    ArmHco() : saved(heurarm::t_arm) {}
    ArmHco& Set(heurarm::Slot slot, int v) { heurarm::t_arm[static_cast<int>(slot)] = static_cast<std::int8_t>(v); return *this; }
    ~ArmHco() { heurarm::t_arm = saved; }
};

const std::vector<std::pair<const char*, int>>& Expected()
{
    static const std::vector<std::pair<const char*, int>> k = {
        {"Vernal Bloom", 1},
        {"Gauntlet of Power", 2},     {"Nissa, Who Shakes the World", 2},
        {"Nature's Lore", 3},         {"Search for Tomorrow", 3},          {"Three Visits", 3},
        {"Gargos, Vicious Watcher", 4},
        {"Lightning Greaves", 5},     {"Garruk, Primal Hunter", 6},       {"Kalonian Hydra", 7},
        {"Neverwinter Hydra", 8},     {"Primordial Hydra", 8},  {"Steelbane Hydra", 8},  {"Voracious Hydra", 8},
    };
    return k;
}
}   // namespace

TEST_CASE("Hydras cast order: ON by default -- doublers, fetches, Gargos, fixed costs, X Hydras last")
{
    const HydrasProvider hp;
    GameState s;
    s.m_provider = &hp;
    ArmHco arm;   // unset -> the env default (ON)
    for (const auto& [name, r] : Expected())
    { CHECK_MESSAGE(hp.CastOrderRank(s, DefHco(name)) == r, name); }
    CHECK(hp.CastOrderTotalByName());
    // The USER's ruling, stated as relations: every doubler and Gargos before every Hydra.
    const int gargos = hp.CastOrderRank(s, DefHco("Gargos, Vicious Watcher"));
    for (const char* h : { "Kalonian Hydra", "Neverwinter Hydra", "Primordial Hydra", "Steelbane Hydra",
                           "Voracious Hydra" })
    {
        CHECK(gargos < hp.CastOrderRank(s, DefHco(h)));
        for (const char* d : { "Vernal Bloom", "Gauntlet of Power", "Nissa, Who Shakes the World" })
        { CHECK(hp.CastOrderRank(s, DefHco(d)) < hp.CastOrderRank(s, DefHco(h))); }
    }
    CHECK(hp.CastOrderTierName(1) != nullptr);
    CHECK(hp.CastOrderTierName(8) != nullptr);
}

TEST_CASE("Hydras cast order: =0 restores the generic tiers byte-for-byte")
{
    const HydrasProvider hp;
    const GenericProvider gp;
    GameState s;
    s.m_provider = &hp;
    ArmHco arm;
    arm.Set(heurarm::HYDRAS_CAST_ORDER, 0);
    for (const auto& [name, r] : Expected())
    {
        (void)r;
        CHECK_MESSAGE(hp.CastOrderRank(s, DefHco(name)) == gp.CastOrderRank(s, DefHco(name)), name);
    }
    CHECK_FALSE(hp.CastOrderTotalByName());
    // Generic, for the record: Gargos 8 (subtype reducer), creatures 10, the rest 20.
    CHECK(hp.CastOrderRank(s, DefHco("Gargos, Vicious Watcher")) == 8);
    CHECK(hp.CastOrderRank(s, DefHco("Primordial Hydra")) == 10);
    CHECK(hp.CastOrderRank(s, DefHco("Vernal Bloom")) == 20);
}

TEST_CASE("Hydras: MTG_HYDRAS_SECOND_MAIN=0 is the measurement arm that turns Garruk's main 2 off")
{
    Decklist deck;
    Card g = DefHco("Garruk, Primal Hunter").card;
    g.m_number = 1;
    deck.mainboard.push_back(g);
    {
        ArmHco arm;
        CHECK(GoldFishRunner::DeckUsesSecondMain(deck));
    }
    {
        ArmHco arm;
        arm.Set(heurarm::HYDRAS_SECOND_MAIN, 0);
        CHECK_FALSE(GoldFishRunner::DeckUsesSecondMain(deck));
    }
}
