// Dragons mana-oracle engine follow-ups (docs/design/dragons-eng-2026-11-26.md), each pinned on a small board with its
// lever ON and OFF:
//   * MTG_GYRETURN_TAP_DEBIT -- the plan menus do not offer a graveyard-return rebuy (Haven of the Spirit Dragon's
//     "{2}, {T}, Sacrifice") that only the source's own mana could pay.
//   * MTG_DIG_FLOAT_HAND_DEMAND -- a sacrifice-draw dig's generic is paid from a spare source, not from a floating
//     colour a card in hand needs, so the dig's draw continuation can still cast it.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/EngineFlags.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{
void EnsureDeCards()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// Permanents with distinct ids 1.. in order; controller 0; untapped, not sick.
GameState DeBoard(const std::vector<std::string>& names)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    int num = 1;
    for (const std::string& n : names)
    {
        const CardDefinition* def = CardDatabase::Instance().Lookup(n);
        REQUIRE_MESSAGE(def != nullptr, "card not in cards.json: ", n);
        Permanent p;
        p.card             = def->card;
        p.card.m_number    = num++;
        p.controller_index = 0;
        p.owner_index      = 0;
        s.battlefield.push_back(p);
    }
    s.players[0].life = s.players[1].life = 20;
    return s;
}

void DeHand(GameState& s, const std::vector<std::string>& names, int first_num = 100)
{
    int num = first_num;
    for (const std::string& n : names)
    {
        Card c = CardDatabase::Instance().Lookup(n)->card;
        c.m_number = num++;
        s.players[0].hand.push_back(c);
    }
}

void DeLibrary(GameState& s, const std::vector<std::string>& names, int first_num = 300)
{
    int num = first_num;
    for (const std::string& n : names)
    {
        Card c = CardDatabase::Instance().Lookup(n)->card;
        c.m_number = num++;
        s.players[0].library.push_back(c);
    }
}

struct ArmPin
{
    heurarm::Slot slot;
    ArmPin(heurarm::Slot s, int v) : slot(s) { heurarm::t_arm[slot] = static_cast<std::int8_t>(v); }
    ~ArmPin() { heurarm::t_arm[slot] = -1; }
};

bool AnyGyReturn(const std::vector<TurnSolver::Plan>& plans)
{
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& a : p.actions)
        { if (a.kind == Action::Kind::GraveyardReturnAbility) { return true; } }
    }
    return false;
}

GameState HavenRebuyBoard(const std::vector<std::string>& names)
{
    static const DragonsProvider prov;
    GameState s = DeBoard(names);
    s.m_provider = &prov;
    Card g = CardDatabase::Instance().Lookup("Glorybringer")->card;
    g.m_number = 200;
    s.players[0].graveyard.push_back(g);
    s.players[0].lands_played_this_turn = 1;   // the drop is spent: nothing in the plan adds mana
    DeLibrary(s, std::vector<std::string>(20, "Mountain"));
    return s;
}
} // namespace

// ---- MTG_GYRETURN_TAP_DEBIT -------------------------------------------------------------------------------------------
// Dragons gate (dragons-ogap, 100,000 games): once an unactivated rebuy was reported, 4,422 more PHANTOM plans showed --
// the menu offered "{2}, {T}, Sacrifice: return a Dragon" on boards where only the Haven itself could pay the {2}. Both
// apply worlds hold the Haven while paying (its {T} is the cost), so such a plan applies as the same plan without the
// rebuy: a dead branch. The self-funding debit already took a Shivan Gorge's own {C} out of its plan's pool; the lever
// extends it to the gy-return source.
TEST_CASE("MTG_GYRETURN_TAP_DEBIT: a Haven rebuy only the Haven could pay is not offered; one two other lands pay is")
{
    EnsureDeCards();
    {
        // Haven + one Mountain: the {2} has one legal source.
        GameState s = HavenRebuyBoard({ "Haven of the Spirit Dragon", "Mountain" });
        {
            ArmPin on(heurarm::GYRETURN_TAP_DEBIT, 1);
            CHECK_FALSE(AnyGyReturn(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true)));
        }
        {
            ArmPin off(heurarm::GYRETURN_TAP_DEBIT, 0);   // the historical menu offers the dead branch
            CHECK(AnyGyReturn(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true)));
        }
    }
    {
        // Haven + two Mountains: the Mountains pay the {2}, so the rebuy is a real option under both arms.
        GameState s = HavenRebuyBoard({ "Haven of the Spirit Dragon", "Mountain", "Mountain" });
        ArmPin on(heurarm::GYRETURN_TAP_DEBIT, 1);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
        REQUIRE(AnyGyReturn(plans));
        // ...and the offered plan really activates: Glorybringer comes back.
        bool returned = false;
        for (const TurnSolver::Plan& p : plans)
        {
            bool has = false;
            for (const Action& a : p.actions) { if (a.kind == Action::Kind::GraveyardReturnAbility) { has = true; } }
            if (!has) { continue; }
            GameState g = s;
            TurnSolver::ApplyPlan(g, p, /*is_pre_combat=*/true);
            for (const Card& c : g.players[0].hand) { if (c.m_name.str() == "Glorybringer") { returned = true; } }
        }
        CHECK(returned);
    }
    {
        // A Bolt beside it on Haven + two Mountains: {Bolt + rebuy} needs three mana that are not the Haven -- the debit
        // refuses that pair, and still offers each alone.
        GameState s = HavenRebuyBoard({ "Haven of the Spirit Dragon", "Mountain", "Mountain" });
        DeHand(s, { "Lightning Bolt" });
        ArmPin on(heurarm::GYRETURN_TAP_DEBIT, 1);
        bool pair = false, bolt = false, rebuy = false;
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true))
        {
            bool b = false, r = false;
            for (const Action& a : p.actions)
            {
                if (a.kind == Action::Kind::GraveyardReturnAbility) { r = true; }
                if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == "Lightning Bolt") { b = true; }
            }
            pair = pair || (b && r); bolt = bolt || (b && !r); rebuy = rebuy || (r && !b);
        }
        CHECK_FALSE(pair);
        CHECK(bolt);
        CHECK(rebuy);
    }
}

// ---- MTG_DIG_FLOAT_HAND_DEMAND ----------------------------------------------------------------------------------------
// Dragons s30096713 T3 {land=Gruul Turf} (APPLY-DROP x262 in the dragons-ogap gate): the Karoo float left {R}; the trailing
// Mind Stone dig ({1}, {T}, Sacrifice: draw) paid its {1} with that {R} while Haven of the Spirit Dragon's {C} stayed up,
// and the dig's continuation then had no red for the Lightning Bolt in hand. The dig's payment now reads the hand's pips
// as the line-float rung's later demand (DigContDemandScope), so the {C} pays the generic and the {R} keeps floating.
namespace
{
GameState DigPayBoard(bool bolt_in_hand)
{
    static const DragonsProvider prov;
    GameState s = DeBoard({ "Haven of the Spirit Dragon", "Mind Stone" });
    s.m_provider = &prov;
    s.battlefield[1].tapped = true;   // the dig's own {T}, paid before the mana half (both worlds)
    s.floating_mana.red = 1;          // the Karoo float
    if (bolt_in_hand) { DeHand(s, { "Lightning Bolt" }); }
    return s;
}
ManaCost One() { ManaCost c; c.generic = 1; return c; }
} // namespace

TEST_CASE("MTG_DIG_FLOAT_HAND_DEMAND: the dig's {1} taps Haven's {C} and keeps the {R} a held Bolt needs")
{
    EnsureDeCards();
    {
        ArmPin on(heurarm::DIG_FLOAT_HAND_DEMAND, 1);
        GameState s = DigPayBoard(true);
        {
            const DigContDemandScope _dcd(s);
            REQUIRE(g_dig_cont_demand != nullptr);
            REQUIRE(TapForCostDirect(s, One(), /*for_creature=*/false));
        }
        CHECK(g_dig_cont_demand == nullptr);   // restored
        CHECK(s.floating_mana.red == 1);
        CHECK(s.battlefield[0].tapped);        // Haven paid the generic
    }
    {
        ArmPin off(heurarm::DIG_FLOAT_HAND_DEMAND, 0);   // the historical drain: the float pays first
        GameState s = DigPayBoard(true);
        {
            const DigContDemandScope _dcd(s);
            CHECK(g_dig_cont_demand == nullptr);
            REQUIRE(TapForCostDirect(s, One(), /*for_creature=*/false));
        }
        CHECK(s.floating_mana.red == 0);
        CHECK_FALSE(s.battlefield[0].tapped);
    }
    {
        // Nothing in hand wants red: no demand is bound and the float pays, as before.
        ArmPin on(heurarm::DIG_FLOAT_HAND_DEMAND, 1);
        GameState s = DigPayBoard(false);
        {
            const DigContDemandScope _dcd(s);
            CHECK(g_dig_cont_demand == nullptr);
            REQUIRE(TapForCostDirect(s, One(), /*for_creature=*/false));
        }
        CHECK(s.floating_mana.red == 0);
        CHECK_FALSE(s.battlefield[0].tapped);
    }
}
