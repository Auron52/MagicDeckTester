// Unpredictable Cyclone, the USER's reference games (2026-10-28).
//
//   * claude_s8_gi7 T4, the MANUAL TAP: Island, Island, Mountain, Mountain; Seething Song ({2}{R}) pays
//     Unpredictable Cyclone, then Curator of Mysteries is cycled ({U}). The payer spent BOTH Islands on the
//     Song and left a Mountain, so the cycle could not be paid -- in the search (the plan's cycle seed runs
//     after its casts and silently no-ops) and in the viewer (the human had to tap Mountain, Mountain, Island
//     by hand). The plan's cycle is a trailing activation to the casts' payer: PlanTraits::cycle_seed_pips
//     feeds ActLineHoldMask, which holds an Island. Built off the CARD (not Action::cost), so the executor's
//     traits over its recorded DigDraw equal the rollout's over the plan's seed (lockstep).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/PlanContext.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsR()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card CardR(const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c     = d->card;
    c.m_number = number;
    return c;
}

void PutR(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card              = CardR(name, number);
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = false;
    s.battlefield.push_back(p);
}

// The s8 gi7 turn-4 board, before the line.
GameState S8T4()
{
    static const CycloneProvider cp;
    GameState s;
    s.m_provider          = &cp;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.players[0].life     = 20;
    s.players[1].life     = 15;
    s.players[0].lands_played_this_turn = 1;
    s.deck_has_cycle_draw_replacement = true;
    s.deck_has_discard_scry_watcher   = true;
    s.deck_has_extra_turn             = true;
    PutR(s, "Island", 20);
    PutR(s, "Island", 21);
    PutR(s, "Mountain", 27);
    PutR(s, "Mountain", 26);
    s.players[0].hand.push_back(CardR("Curator of Mysteries", 11));
    s.players[0].hand.push_back(CardR("Seething Song", 43));
    s.players[0].hand.push_back(CardR("Unpredictable Cyclone", 57));
    s.players[0].library.push_back(CardR("Brainstorm", 600));
    s.players[0].library.push_back(CardR("Emrakul, the Aeons Torn", 601));
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(CardR("Island", 900 + k)); }
    return s;
}

Action CycleOf(const std::string& name, bool with_def_and_cost)
{
    Action a;
    a.kind          = Action::Kind::DigDraw;
    a.dig_sacrifice = false;
    a.card_name     = name;
    a.hand_index    = -1;
    if (with_def_and_cost)
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(name);
        REQUIRE(d != nullptr);
        a.def  = d;
        a.cost = *d->params.cycling_cost;
    }
    return a;
}

int UntappedNamed(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (!p.tapped && p.card.m_name.str() == name) { ++n; } }
    return n;
}

}   // namespace

TEST_CASE("Cyclone refs s8 gi7: the plan's cycle seed holds its colour while the casts pay")
{
    EnsureCardsR();
    const CardDefinition* song = CardDatabase::Instance().Lookup("Seething Song");
    REQUIRE(song != nullptr);

    // Traits: the plan's seed (def + cost set, as CollectActions emits it) and the executor's recorded
    // DigDraw (name only) give the SAME hold inputs.
    GameState s = S8T4();
    const PlanTraits t_plan = TurnSolver::ComputePlanTraits(s, { CycleOf("Curator of Mysteries", true) });
    const PlanTraits t_rec  = TurnSolver::ComputePlanTraits(s, { CycleOf("Curator of Mysteries", false) });
    CHECK(t_plan.cycle_seed_def != nullptr);
    CHECK(t_plan.cycle_seed_pips[1] == 1);   // {U}
    CHECK(t_plan.cycle_seed_hand_floor == 1);
    CHECK(t_rec.cycle_seed_def == t_plan.cycle_seed_def);
    for (int c = 0; c < 5; ++c) { CHECK(t_rec.cycle_seed_pips[c] == t_plan.cycle_seed_pips[c]); }
    CHECK(t_rec.cycle_seed_hand_floor == t_plan.cycle_seed_hand_floor);

    // Without the seed in scope the Song taps both Islands (the defect); with it, an Island stays up.
    {
        GameState g = S8T4();
        REQUIRE(TapForCostDirect(g, song->card.m_mana_cost, false));
        CHECK(UntappedNamed(g, "Island") == 0);
    }
    {
        GameState g = S8T4();
        const PlanTraitsScope _pts(&t_plan);
        REQUIRE(TapForCostDirect(g, song->card.m_mana_cost, false));
        CHECK(UntappedNamed(g, "Island") == 1);
        CHECK(UntappedNamed(g, "Mountain") == 0);
        // ...and the seed's own payment is not held against itself: it takes that Island.
        const CycleSeedPayScope _csp;
        CHECK(TapForCostDirect(g, *CardDatabase::Instance().Lookup("Curator of Mysteries")->params.cycling_cost, false));
        CHECK(UntappedNamed(g, "Island") == 0);
    }
    // Released once the cycler has left the hand (a later payment in the same scope is not held): the payment
    // is then exactly the one the same traits make with no seed at all.
    {
        PlanTraits t_none = t_plan;
        t_none.cycle_seed_def = nullptr;
        GameState g0 = S8T4();
        g0.players[0].hand.erase(g0.players[0].hand.begin());   // the Curator was cycled
        GameState g1 = g0;
        {
            const PlanTraitsScope _pts(&t_none);
            REQUIRE(TapForCostDirect(g0, song->card.m_mana_cost, false));
        }
        {
            const PlanTraitsScope _pts(&t_plan);
            REQUIRE(TapForCostDirect(g1, song->card.m_mana_cost, false));
        }
        for (std::size_t i = 0; i < g0.battlefield.size(); ++i)
        { CHECK(g0.battlefield[i].tapped == g1.battlefield[i].tapped); }
    }
}

TEST_CASE("Cyclone refs s8 gi7: the whole line -- Song, Cyclone, cycle Curator -- applies in the rollout")
{
    EnsureCardsR();
    GameState s = S8T4();
    // The plan as CollectActions builds it: the two casts plus the Curator seed (take the free cast).
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/true);
    TurnSolver::Plan plan;
    plan.land_decided = true;
    for (const char* want : { "Seething Song", "Unpredictable Cyclone" })
    {
        bool found = false;
        for (const Action& a : acts)
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == want && a.remand_by.empty())
            { plan.actions.push_back(a); found = true; break; }
        }
        REQUIRE_MESSAGE(found, "no cast action for ", want);
    }
    bool seeded = false;
    for (const Action& a : acts)
    {
        if (a.kind == Action::Kind::DigDraw && a.card_name.str() == "Curator of Mysteries" && a.ponder_keep == kCycFcTake)
        { plan.actions.push_back(a); seeded = true; break; }
    }
    REQUIRE(seeded);
    TurnSolver::ApplyPlan(s, plan, /*is_pre_combat=*/true);
    int curators_gy = 0;
    for (const Card& c : s.players[0].graveyard) { if (c.m_name.str() == "Curator of Mysteries") { ++curators_gy; } }
    CHECK(curators_gy == 1);                       // the cycle happened
    bool emrakul = false;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == "Emrakul, the Aeons Torn") { emrakul = true; } }
    CHECK(emrakul);                                // the walk passed Brainstorm and cast Emrakul free
    CHECK(s.pending_extra_turns + s.extra_turns_taken >= 1);
}
