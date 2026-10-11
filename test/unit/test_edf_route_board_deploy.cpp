// MTG_EDF_ROUTE_BOARD_DEPLOY -- the COMBO OFF route still deploys from the lands when the loop is already
// on the battlefield.
//
// Eldrazi Displacer + Cloud of Faeries in play, Essence Depleter in play, four one-mana lands untapped,
// Training Grounds in hand, opponent on 5. As it stands the loop LOSES mana: Displacer's {2}{C} against
// Cloud's two-land refund. The route went straight to its loop, which casts only from the float, so the
// reducer was never paid for and the trial stalled (the shape of EDF gi 1408 t5: "0 deploys, 61 blinks
// without a step firing" with Training Grounds in hand). Paid off the lands first, Training Grounds
// makes the blink {C} and the drain {C}: the loop gains a mana an iteration and Depleter drains five.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsRb()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const EldraziFlickerProvider& ProvRb()
{
    static const EldraziFlickerProvider p;
    return p;
}

Card PhRb(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

// `first_num`: the route's trial memo keys a board on card NUMBERS (in a real game a number is one
// card), so two synthetic boards that differ only in a hand card's NAME must not share numbering.
GameState MakeRb(const std::vector<std::string>& hand, int first_num)
{
    EnsureCardsRb();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = 5;
    s.deck_has_land_aura  = true;
    s.m_provider          = &ProvRb();
    int num = first_num;
    for (const std::string& nm : hand) { s.players[0].hand.push_back(PhRb(nm, num++)); }
    for (int i = 0; i < 20; ++i) { s.players[0].library.push_back(PhRb("Brushland", num++)); }
    for (const char* nm : { "Mariposa Military Base", "Brushland", "Brushland", "Yavimaya Coast",
                            "Eldrazi Displacer", "Cloud of Faeries", "Essence Depleter" })
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(nm)->card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        s.battlefield.push_back(p);
    }
    s.players[0].lands_played_this_turn = 1;
    return s;
}

int TrialRb(const GameState& s, int arm)
{
    heurarm::t_arm[heurarm::EDF_ROUTE_BOARD_DEPLOY] = static_cast<std::int8_t>(arm);
    const int r = EdfComboRouteTrial(s, 0);
    heurarm::t_arm[heurarm::EDF_ROUTE_BOARD_DEPLOY] = -1;
    return r;
}

int ApplyRb(GameState& s, int arm)
{
    heurarm::t_arm[heurarm::EDF_ROUTE_BOARD_DEPLOY] = static_cast<std::int8_t>(arm);
    const int r = EdfComboRouteApply(s, 0);
    heurarm::t_arm[heurarm::EDF_ROUTE_BOARD_DEPLOY] = -1;
    return r;
}

}   // namespace

TEST_CASE("EDF route: a reducer in hand is deployed onto a loop already in play (MTG_EDF_ROUTE_BOARD_DEPLOY)")
{
    const GameState s = MakeRb({ "Training Grounds" }, 1);
    CHECK_MESSAGE(TrialRb(s, 0) == 0, "precondition: the straight-to-loop route stalls");
    CHECK(TrialRb(s, 1) == 1);   // WIN
}

// The live apply replays the trial's deploy order and really kills.
TEST_CASE("EDF route: the live apply realises the board-deploy kill (MTG_EDF_ROUTE_BOARD_DEPLOY)")
{
    GameState s = MakeRb({ "Training Grounds" }, 101);
    CHECK(ApplyRb(s, 1) == 1);
    CHECK(s.players[1].life <= 0);
    bool tg = false;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == "Training Grounds") { tg = true; } }
    CHECK(tg);
}

// Nothing to deploy: the lever leaves the route's verdict alone (no reducer, no Aura, no land drop).
TEST_CASE("EDF route: with nothing to deploy the board loop's verdict is unchanged (MTG_EDF_ROUTE_BOARD_DEPLOY)")
{
    const GameState s = MakeRb({ "Emiel the Blessed" }, 201);
    CHECK(TrialRb(s, 1) == TrialRb(s, 0));
}
