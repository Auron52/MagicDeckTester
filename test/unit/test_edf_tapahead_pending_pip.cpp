// MTG_TAPAHEAD_PENDING_PIP_AUTO -- the ETB-untap tap-ahead commits a dual land's banked unit to the pending
// cast's own uncovered pip in autonomous play too.
//
// The board is EDF held-out gi 147 (seed 5300148, d0) turn 4: Brushland carrying Wild Growth, Kitchen
// ({G} or {U}, the only blue), Eldrazi Displacer; hand Cloud of Faeries {1}{U} beside a green-heavy hand
// (three Overgrowth, Living Wish, Wild Growth). Before paying Cloud the executor taps lands ahead into the
// float (the ETB untap will give them back). The need model reads the hand's demand -- green -- and
// committed Kitchen's unit to {G}, so Cloud's own {U} had no source and the cast was dropped.
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

void EnsureCardsTp()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const EldraziFlickerProvider& ProvTp()
{
    static const EldraziFlickerProvider p;
    return p;
}

Card PhTp(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

GameState Board147(bool second_blue = false)
{
    EnsureCardsTp();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = 20;
    s.deck_has_land_aura  = true;
    s.m_provider          = &ProvTp();
    int num = 1;
    for (const char* nm : { "Overgrowth", "Cloud of Faeries", "Living Wish", "Overgrowth", "Overgrowth",
                            "Wild Growth" })
    { s.players[0].hand.push_back(PhTp(nm, num++)); }
    for (int i = 0; i < 20; ++i) { s.players[0].library.push_back(PhTp("Brushland", num++)); }
    auto put = [&](const char* nm, int host) -> int
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(nm)->card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        p.aura_attached_to  = host;
        s.battlefield.push_back(p);
        return p.card.m_number;
    };
    const int brush = put("Brushland", 0);
    put("Wild Growth", brush);
    put("Kitchen", 0);
    if (second_blue) { put("Yavimaya Coast", 0); }   // another {U} source: the pip stays covered
    put("Eldrazi Displacer", 0);
    s.players[0].lands_played_this_turn = 1;
    return s;
}

// Tap ahead for Cloud exactly as the executor does before its payment, then pay Cloud.
bool CloudPaysAfterTapAhead(int arm, int* banked_blue, bool second_blue = false)
{
    GameState s = Board147(second_blue);
    const ManaCost cloud = CardDatabase::Instance().Lookup("Cloud of Faeries")->card.m_mana_cost;
    heurarm::t_arm[heurarm::TAPAHEAD_PENDING_PIP_AUTO] = static_cast<std::int8_t>(arm);
    EtbUntapTapAheadIntoFloat(s, 0, 2, ColoredPipReserveMask(cloud), &cloud, nullptr);
    heurarm::t_arm[heurarm::TAPAHEAD_PENDING_PIP_AUTO] = -1;
    *banked_blue = s.floating_mana.blue;
    return TapForCostDirect(s, cloud, /*for_creature=*/true);
}

}   // namespace

TEST_CASE("EDF: the untap tap-ahead banks the pending cast's own {U} in autonomous play (MTG_TAPAHEAD_PENDING_PIP_AUTO)")
{
    int blue_on = 0, blue_off = 0;
    const bool on  = CloudPaysAfterTapAhead(1, &blue_on);
    const bool off = CloudPaysAfterTapAhead(0, &blue_off);
    CHECK(blue_on >= 1);
    CHECK(on);
    // Precondition: the demand-only commit banks Kitchen as {G} and strands Cloud's {U}.
    CHECK_MESSAGE(blue_off == 0, "the lever-off arm should commit the only blue land to {G}");
    CHECK_FALSE(off);
}

// Only when nothing else covers the pip: with Yavimaya Coast also untapped (a second {U} source the
// autonomous tap-ahead leaves up), the lever changes nothing -- the demand model's commit stands and
// Cloud still pays off the Coast.
TEST_CASE("EDF: with the pip still covered elsewhere the tap-ahead commit is unchanged (MTG_TAPAHEAD_PENDING_PIP_AUTO)")
{
    int blue_on = 0, blue_off = 0;
    const bool on  = CloudPaysAfterTapAhead(1, &blue_on, /*second_blue=*/true);
    const bool off = CloudPaysAfterTapAhead(0, &blue_off, /*second_blue=*/true);
    CHECK(blue_on == blue_off);
    CHECK(on);
    CHECK(off);
}
