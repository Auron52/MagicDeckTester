// Unpredictable Cyclone onboarding, PHASE 1 (2026-10-26): the foundations.
//
//   * The Tier-1/2 card data (Watery Grave, Xander's Lounge, Brainsurge, Street Wraith).
//   * Street Wraith's LIFE-COST cycling ("Cycling--Pay 2 life"): CardParams::cycling_life_cost, probed
//     and paid at every cycle site through CyclingLifePayable / CyclingPayable / PayCyclingLife. Pinned
//     in BOTH worlds -- the executor's PerformDig and the rollout's dig loop (TurnSolver::ApplyPlan) --
//     at the life > N boundary (paying to 0 loses at the next state-based check, before the draw: the
//     sound prune).
//   * CycloneProvider's routing signature (Cyclone's replacement param, OR Emrakul's extra turn AND
//     Curator's cycle-or-discard scry), on synthetic registered cards -- the real entries carrying the
//     params arrive in phases 2/3 -- and its TutorSearchWidth coverage fix.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "deck/DeckLoader.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{

constexpr const char* kTestReplacement = "Cyclone Phase1 Test Replacement";
constexpr const char* kTestExtraTurn   = "Cyclone Phase1 Test Extra Turn";
constexpr const char* kTestCycleScry   = "Cyclone Phase1 Test Cycle Scry";

void EnsureCardsCy()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        // Synthetic carriers of the three signature params (no real card carries them yet). Unique
        // names, so no decklist and no other test can meet them.
        const CardDefinition* base = CardDatabase::Instance().Lookup("Island");
        if (base != nullptr)
        {
            const CardDefinition copy = *base;
            CardDatabase::Instance().Register(kTestReplacement, [copy]()
            {
                CardDefinition d = copy;
                d.card.m_name = InternedName(std::string(kTestReplacement));
                d.params.cycle_draw_exile_shared_type_freecast = true;
                return d;
            });
            CardDatabase::Instance().Register(kTestExtraTurn, [copy]()
            {
                CardDefinition d = copy;
                d.card.m_name = InternedName(std::string(kTestExtraTurn));
                d.params.cast_trigger_extra_turns = 1;
                return d;
            });
            CardDatabase::Instance().Register(kTestCycleScry, [copy]()
            {
                CardDefinition d = copy;
                d.card.m_name = InternedName(std::string(kTestCycleScry));
                d.params.cycle_or_discard_trigger_scry = 1;
                return d;
            });
        }
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefCy(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardCy(const std::string& name, int number)
{
    Card c     = DefCy(name).card;
    c.m_number = number;
    return c;
}

void PutLand(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card              = CardCy(name, number);
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = false;
    p.tapped            = false;
    s.battlefield.push_back(p);
}

int CountIn(const std::vector<Card>& zone, const std::string& name)
{
    int n = 0;
    for (const Card& c : zone) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

// Turn 4, the drop made, two Islands untapped, `wraiths` Street Wraiths in hand, ten Islands in the
// library, our life `life`. Routed to CycloneProvider (== Generic behaviour).
GameState WraithState(int life, int wraiths)
{
    static const CycloneProvider cp;
    GameState s;
    s.m_provider          = &cp;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = life;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    PutLand(s, "Island", 40);
    PutLand(s, "Island", 41);
    int num = 100;
    for (int k = 0; k < wraiths; ++k) { s.players[0].hand.push_back(CardCy("Street Wraith", num++)); }
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(CardCy("Island", 900 + k)); }
    return s;
}

const char* RouteOf(const std::vector<std::string>& names)
{
    Decklist deck;
    int num = 1;
    for (const std::string& n : names)
    {
        for (int k = 0; k < 4; ++k) { deck.mainboard.push_back(CardCy(n, num++)); }
    }
    return DetectDecisionProvider(deck).Name();
}

}   // namespace

TEST_CASE("Cyclone phase 1: card data for the four new entries")
{
    EnsureCardsCy();
    const CardDefinition& wraith = DefCy("Street Wraith");
    CHECK(wraith.card.m_mana_cost.black == 2);
    CHECK(wraith.card.m_mana_cost.generic == 3);
    CHECK(wraith.card.m_power == 3);
    CHECK(wraith.card.m_toughness == 4);
    CHECK(wraith.card.HasKeyword(Keyword::Landwalk));   // pasted verbatim from Scryfall
    CHECK(wraith.card.HasKeyword(Keyword::Swampwalk));
    REQUIRE(wraith.params.cycling_cost.has_value());
    CHECK(wraith.params.cycling_cost->ManaValue() == 0);   // "Pay 2 life" -- no mana
    CHECK(wraith.params.cycling_life_cost == 2);

    const CardDefinition& surge = DefCy("Brainsurge");
    CHECK(surge.card.m_mana_cost.ManaValue() == 3);
    CHECK(surge.params.draw == 4);
    CHECK(surge.params.put_back_from_hand == 2);

    const CardDefinition& grave = DefCy("Watery Grave");
    CHECK(grave.params.etb_pay_life_to_untap == 2);
    CHECK(grave.params.produces.size() == 2);

    const CardDefinition& lounge = DefCy("Xander's Lounge");
    CHECK(lounge.params.enters_tapped);
    REQUIRE(lounge.params.cycling_cost.has_value());
    CHECK(lounge.params.cycling_cost->ManaValue() == 3);
    CHECK(lounge.params.cycling_life_cost == 0);
    CHECK(lounge.params.produces.size() == 3);
}

TEST_CASE("Cyclone phase 1: life-cost cycling is payable only while life > N")
{
    EnsureCardsCy();
    const CardDefinition& wraith = DefCy("Street Wraith");
    const CardDefinition& lounge = DefCy("Xander's Lounge");
    GameState s = WraithState(3, 1);
    const ManaPool empty;
    CHECK(CyclingLifePayable(s, 0, wraith));
    CHECK(CyclingPayable(s, 0, wraith, empty));           // the mana half is {0}
    s.players[0].life = 2;
    CHECK_FALSE(CyclingLifePayable(s, 0, wraith));        // paying to 0 loses before the draw
    CHECK_FALSE(CyclingPayable(s, 0, wraith, empty));
    s.players[0].life = 1;
    CHECK(CyclingLifePayable(s, 0, lounge));              // no life cost -> never life-gated
    CHECK_FALSE(CyclingPayable(s, 0, lounge, empty));     // ...but {3} is not in an empty pool
    PayCyclingLife(s, 0, lounge);
    CHECK(s.players[0].life == 1);                        // a no-op without a life cost
    s.players[0].life = 9;
    PayCyclingLife(s, 0, wraith);
    CHECK(s.players[0].life == 7);
}

TEST_CASE("Cyclone phase 1: the executor's PerformDig pays the life and refuses at life <= 2")
{
    EnsureCardsCy();
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    {
        GameState s = WraithState(20, 1);
        MtgTestSeam::PerformDig(eng, s, "Street Wraith", /*is_sacrifice=*/false);
        CHECK(s.players[0].life == 18);
        CHECK(CountIn(s.players[0].hand, "Street Wraith") == 0);
        CHECK(CountIn(s.players[0].graveyard, "Street Wraith") == 1);
        CHECK(CountIn(s.players[0].hand, "Island") == 1);         // drew a card
        CHECK(s.players[0].library.size() == 9);
        int untapped = 0;
        for (const Permanent& p : s.battlefield) { if (!p.tapped) { ++untapped; } }
        CHECK(untapped == 2);                                     // no mana spent
    }
    {
        GameState s = WraithState(2, 1);
        CHECK_FALSE(MtgTestSeam::PerformDig(eng, s, "Street Wraith", /*is_sacrifice=*/false));
        CHECK(s.players[0].life == 2);
        CHECK(CountIn(s.players[0].hand, "Street Wraith") == 1);  // refused: nothing moved
        CHECK(s.players[0].library.size() == 10);
    }
}

TEST_CASE("Cyclone phase 1: the rollout dig loop pays the life in lockstep and stops at the boundary")
{
    EnsureCardsCy();
    heurarm::Clear();
    TurnSolver::Plan dig;      // "dig while affordable" -- the affordability filter is the only gate
    dig.land_decided = true;
    dig.dig_choice   = 1;
    // PHASE 3: CycloneProvider SEARCHES its cycles (CyclesAreSearched) -- inside the search window a cycle is
    // a plan's cycle seed, and the legacy dig loop runs only as the PLAYOUT policy, i.e. for the horizon
    // leaf's plan. That is the loop this test pins, so the plan is marked as one.
    dig.horizon_leaf = true;
    {   // life 3, two Wraiths: the first cycle pays 3 -> 1, the second is refused (1 <= 2).
        GameState s = WraithState(3, 2);
        TurnSolver::ApplyPlan(s, dig, /*is_pre_combat=*/true);
        CHECK(s.players[0].life == 1);
        CHECK(CountIn(s.players[0].graveyard, "Street Wraith") == 1);
        CHECK(CountIn(s.players[0].hand, "Street Wraith") == 1);
        CHECK(s.players[0].library.size() == 9);
    }
    {   // life 2: the filter (SelectDigSource -> CyclingPayable) never picks the Wraith.
        GameState s = WraithState(2, 1);
        TurnSolver::ApplyPlan(s, dig, /*is_pre_combat=*/true);
        CHECK(s.players[0].life == 2);
        CHECK(CountIn(s.players[0].hand, "Street Wraith") == 1);
        CHECK(s.players[0].library.size() == 10);
    }
    {   // the executor twin from the same state realises the same cycle.
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        GameState a = WraithState(5, 1), b = WraithState(5, 1);
        TurnSolver::ApplyPlan(a, dig, /*is_pre_combat=*/true);
        MtgTestSeam::PerformDig(eng, b, "Street Wraith", /*is_sacrifice=*/false);
        CHECK(a.players[0].life == b.players[0].life);
        CHECK(a.players[0].life == 3);
        CHECK(a.players[0].hand.size() == b.players[0].hand.size());
        CHECK(a.players[0].library.size() == b.players[0].library.size());
    }
}

TEST_CASE("Cyclone phase 1: CycloneProvider routing signature")
{
    EnsureCardsCy();
    // The replacement alone routes, above anti (Scalding Tarn's fetch_land_types).
    CHECK(std::string(RouteOf({ kTestReplacement, "Scalding Tarn", "Brainstorm" })) == "Cyclone");
    // Emrakul's extra turn AND Curator's scry together route (a list that cuts Cyclone)...
    CHECK(std::string(RouteOf({ kTestExtraTurn, kTestCycleScry, "Scalding Tarn" })) == "Cyclone");
    // ...but neither alone does: an Eldrazi-ramp list, a generic cycling list.
    CHECK(std::string(RouteOf({ kTestExtraTurn, "Scalding Tarn" })) == "AntiLifegain");
    CHECK(std::string(RouteOf({ kTestCycleScry, "Scalding Tarn" })) == "AntiLifegain");
    // The phase-1 cards trip nothing: today's list still rides AntiLifegain via the Tarn.
    CHECK(std::string(RouteOf({ "Street Wraith", "Brainsurge", "Watery Grave", "Xander's Lounge",
                                "Scalding Tarn" })) == "AntiLifegain");
}

TEST_CASE("Cyclone phase 1: TutorSearchWidth is the coverage width, =0 restores the base")
{
    EnsureCardsCy();
    heurarm::Clear();
    const CycloneProvider cp;
    const GenericProvider gp;
    CHECK(cp.TutorSearchWidth() == 32);
    heurarm::t_arm[heurarm::CYCLONE_TUTOR_WIDTH] = 0;
    CHECK(cp.TutorSearchWidth() == gp.TutorSearchWidth());
    heurarm::Clear();
}
