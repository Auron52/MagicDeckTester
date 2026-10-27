// Unpredictable Cyclone, STAGE 5 (2026-10-27): defects found by the analyze-deck Stage 5a mismatch harnesses.
//
//   * fd-diverge s93143 gi143 (d5 b20): turn 7, Seething Song paid by Steam Vents + Mountain + Island leaves
//     Cascade Bluffs and RRRRR floating. The committed line then cycles Curator of Mysteries ({U}): the filter's
//     "{U/R}, {T}: Add {U}{U}" fed by a floating {R} pays it. The rollout's cycle seed pays through
//     TapForCostDirect and succeeds; the executor's PerformDigFull refused BEFORE paying, on the
//     AvailableManaPool CanPay probe, which books the filter only as its own taps -- so the Curator was never
//     cycled, the Street Wraith it would have drawn never walked to Emrakul, and the proven turn-7 kill was lost.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsS5()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card CardS5(const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c     = d->card;
    c.m_number = number;
    return c;
}

void PutS5(GameState& s, const std::string& name, int number, bool tapped)
{
    Permanent p;
    p.card              = CardS5(name, number);
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = false;
    p.tapped            = tapped;
    s.battlefield.push_back(p);
}

int CountS5(const std::vector<Card>& zone, const std::string& name)
{
    int n = 0;
    for (const Card& c : zone) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

// The s93143 gi143 turn-7 board after Seething Song: three lands tapped, Cascade Bluffs untapped, RRRRR
// floating, a Curator of Mysteries out (its scry watcher) and one in hand; Street Wraith on top of the library.
GameState FilterFloatState()
{
    static const CycloneProvider cp;
    GameState s;
    s.m_provider          = &cp;
    s.active_player_index = 0;
    s.turn_number         = 7;
    s.players[0].life     = 16;
    s.players[1].life     = 16;
    s.players[0].lands_played_this_turn = 1;
    PutS5(s, "Steam Vents", 45, true);
    PutS5(s, "Mountain", 25, true);
    PutS5(s, "Island", 21, true);
    PutS5(s, "Cascade Bluffs", 10, false);
    PutS5(s, "Curator of Mysteries", 12, false);
    s.floating_mana.red = 5;
    s.players[0].hand.push_back(CardS5("Curator of Mysteries", 13));
    s.players[0].library.push_back(CardS5("Street Wraith", 49));
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(CardS5("Island", 900 + k)); }
    return s;
}

}   // namespace

TEST_CASE("Cyclone stage 5: a cycle paid through a filter fed by floating mana -- executor matches the rollout")
{
    EnsureCardsS5();
    const CardDefinition* curator = CardDatabase::Instance().Lookup("Curator of Mysteries");
    REQUIRE(curator != nullptr);
    REQUIRE(curator->params.cycling_cost.has_value());

    // The rollout's payer (the cycle seed's TapForCostDirect) pays {U} here: one floating {R} feeds the filter.
    {
        GameState s = FilterFloatState();
        CHECK(TapForCostDirect(s, *curator->params.cycling_cost, false));
    }
    // The executor's cycle (the committed line's DigDraw replay) must pay it too -- lockstep.
    {
        GameState s = FilterFloatState();
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        const AIEngine::DigOutcome o =
            MtgTestSeam::PerformDigFull(eng, s, "Curator of Mysteries", false, /*scry_pin=*/0, kCycFcDefault, {});
        CHECK(o != AIEngine::DigOutcome::Failed);
        CHECK(CountS5(s.players[0].hand, "Curator of Mysteries") == 0);
        CHECK(CountS5(s.players[0].graveyard, "Curator of Mysteries") == 1);
        CHECK(CountS5(s.players[0].hand, "Street Wraith") == 1);   // the cycling draw (scry j = 0 keeps it)
    }
}
