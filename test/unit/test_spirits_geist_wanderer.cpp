// Spirits onboarding, integrator B2 (2026-11-13): Patrician Geist and Mausoleum Wanderer.
//   * Patrician Geist: a Spirit lord plus "Spells you cast from your graveyard cost {1} less to cast"
//     (reduces_graveyard_cast_generic), applied inside the shared GraveyardCastSpellCost, generic only,
//     floored at 0, per Geist. A Geist cast or Vial-put EARLIER IN THE SAME PLAN discounts the plan's
//     flashback: the enumerator's same-turn credit (CountSameTurnReducers gy_cast), the ManaPruneBound
//     bail and the selection gate's soft term, so "Geist + flashback Lingering Souls" on four lands is a
//     plan, and both worlds pay it.
//   * Mausoleum Wanderer: "Whenever another Spirit you control enters, this creature gets +1/+1 until end
//     of turn" (own_creature_enters_self_temp_power/_tough + enters_watch_subtypes Spirit) -- a TEMP
//     pump through FireCreatureEnterWatchers, cleared at cleanup; tokens and Vial puts trigger it.
// test/scenarios/spirits_geist_*.json / spirits_wanderer_*.json drive the whole search end to end.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <string>
#include <vector>

namespace
{
const char* const kSouls    = "Lingering Souls";
const char* const kGeist    = "Patrician Geist";
const char* const kWanderer = "Mausoleum Wanderer";

const CardDefinition& GwDef(const std::string& name)
{
    static const bool loaded = [] {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card GwZone(const std::string& name, int number)
{
    GwDef(name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

Permanent& GwPut(GameState& s, const std::string& name, int number, bool sick = false, int controller = 0)
{
    Permanent p;
    p.card = GwDef(name).card;
    p.card.m_number = number;
    p.controller_index = p.owner_index = controller;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return s.battlefield.back();
}

GameState GwBoard(const std::vector<std::string>& lands)
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.phase               = Phase::PreCombatMain;
    s.step                = Step::MainPhase;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(GwZone("Plains", 900 + k)); }
    int n = 100;
    for (const std::string& l : lands) { GwPut(s, l, ++n); }
    return s;
}

int GwCount(const std::vector<Action>& acts, Action::Kind k, const std::string& name)
{
    int n = 0;
    for (const Action& a : acts) { if (a.kind == k && a.card_name.str() == name) { ++n; } }
    return n;
}

int GwSpiritTokens(const GameState& s)
{
    int n = 0;
    for (const Permanent& p : s.battlefield)
    { if (p.controller_index == 0 && p.is_token && CardHasSubtype(p.card, "Spirit")) { ++n; } }
    return n;
}

int GwTappedLands(const GameState& g)
{
    int n = 0;
    for (const Permanent& p : g.battlefield) { if (p.controller_index == 0 && p.card.IsLand() && p.tapped) { ++n; } }
    return n;
}

int GwZoneCount(const std::vector<Card>& z, const std::string& name)
{
    int n = 0;
    for (const Card& c : z) { if (c.m_name.str() == name) { ++n; } }
    return n;
}

const Permanent* GwFind(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return &p; } }
    return nullptr;
}

void GwResolveAll(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

GameState GwRunExecutor(const GameState& s, const TurnSolver::Plan& plan)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, GwResolveAll);
    return exec;
}

// The plan whose actions are exactly the given (kind, name) list, in any order.
const TurnSolver::Plan* GwFindPlan(const std::vector<TurnSolver::Plan>& plans,
                                   const std::vector<std::pair<Action::Kind, std::string>>& want)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() != want.size()) { continue; }
        bool ok = true;
        for (const auto& w : want)
        {
            int need = 0;
            for (const auto& w2 : want) { if (w2 == w) { ++need; } }
            if (GwCount(p.actions, w.first, w.second) != need) { ok = false; break; }
        }
        if (ok) { return &p; }
    }
    return nullptr;
}
}   // namespace

// ---- Patrician Geist: the price ----------------------------------------------------------------------

TEST_CASE("Patrician Geist: the card -- {2}{U} 2/2 Spirit Knight lord; the new param, not reduces_spell_subtype")
{
    const CardDefinition& d = GwDef(kGeist);
    CHECK(d.card.m_mana_cost.ToString() == "{2}{U}");
    CHECK(d.card.m_power.value_or(0) == 2);
    CHECK(d.card.m_toughness.value_or(0) == 2);
    CHECK(CardHasSubtype(d.card, "Spirit"));
    CHECK(CardHasSubtype(d.card, "Knight"));
    CHECK(d.params.reduces_graveyard_cast_generic == 1);
    CHECK(d.params.reduces_spell_subtype.empty());   // would route the deck to GoblinsProvider

    // Lord: a Spirit token gets +1/+1 from the Geist; the Geist does not pump itself.
    GameState s = GwBoard({});
    Permanent& g = GwPut(s, kGeist, 1);
    CHECK(ComputeLordBonus(g.card, s, 0, g.AnimatedAllTypes(), &g) == std::pair<int, int>{0, 0});
    ApplyCastCreatesTokens(s, 0, GwDef(kSouls));
    for (const Permanent& p : s.battlefield)
    {
        if (!p.is_token) { continue; }
        CHECK(ComputeLordBonus(p.card, s, 0, p.AnimatedAllTypes(), &p) == std::pair<int, int>{1, 1});
    }
}

TEST_CASE("Patrician Geist: flashback {1}{B} -> {B} per Geist, floored (two Geists still {B}); only OUR Geists")
{
    GameState s = GwBoard({});
    CHECK(GraveyardCastSpellCost(GwDef(kSouls), s).ToString() == "{1}{B}");
    GwPut(s, kGeist, 1);
    CHECK(GraveyardCastGenericDiscount(s, 0) == 1);
    CHECK(GraveyardCastSpellCost(GwDef(kSouls), s).ToString() == "{B}");
    GwPut(s, kGeist, 2);
    CHECK(GraveyardCastGenericDiscount(s, 0) == 2);
    const ManaCost two = GraveyardCastSpellCost(GwDef(kSouls), s);
    CHECK(two.generic == 0);
    CHECK(two.black == 1);   // coloured pips are never reduced

    // The opponent's Geist is not ours ("Spells YOU cast").
    GameState o = GwBoard({});
    GwPut(o, kGeist, 1, false, 1);
    CHECK(GraveyardCastSpellCost(GwDef(kSouls), o).ToString() == "{1}{B}");

    // A HAND cast is not a graveyard cast: the Geist leaves Lingering Souls' {2}{W} alone.
    CHECK(EffectiveSpellCost(GwDef(kSouls), s).ToString() == "{2}{W}");
    // A non-flashback graveyard cast (a Lurrus-style permission) is discounted from its printed cost.
    CHECK(GraveyardCastSpellCost(GwDef("Supreme Phantom"), s).ToString() == "{U}");
}

TEST_CASE("Patrician Geist ON THE BATTLEFIELD: the flashback candidate is {B}; one Godless Shrine pays it")
{
    GameState s = GwBoard({ "Godless Shrine" });
    GwPut(s, kGeist, 1);
    s.players[0].graveyard.push_back(GwZone(kSouls, 40));
    const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
    REQUIRE(GwCount(acts, Action::Kind::CastFromGraveyard, kSouls) == 1);
    for (const Action& a : acts)
    { if (a.kind == Action::Kind::CastFromGraveyard) { CHECK(a.cost.ToString() == "{B}"); } }

    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* fb = GwFindPlan(plans, {{Action::Kind::CastFromGraveyard, kSouls}});
    REQUIRE(fb != nullptr);
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *fb, true);
    const GameState exec = GwRunExecutor(s, *fb);
    for (const GameState* g : { static_cast<const GameState*>(&roll), &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        CHECK(GwSpiritTokens(*g) == 2);
        CHECK(GwZoneCount(g->players[0].graveyard, kSouls) == 0);
        CHECK(GwTappedLands(*g) == 1);
    }
}

// ---- Patrician Geist: the SAME-TURN discount (completeness) -----------------------------------------

TEST_CASE("Patrician Geist CAST this main discounts the plan's flashback: Geist + flashback on FOUR lands, both worlds")
{
    // {2}{U} + {B} = 4. Priced at the plan's start the flashback is {1}{B} (no Geist out yet): 3 + 2 = 5.
    GameState s = GwBoard({ "Watery Grave", "Godless Shrine", "Plains", "Plains" });
    s.players[0].hand.push_back(GwZone(kGeist, 30));
    s.players[0].graveyard.push_back(GwZone(kSouls, 40));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* both = GwFindPlan(plans, {{Action::Kind::CastFromHand, kGeist},
                                                      {Action::Kind::CastFromGraveyard, kSouls}});
    REQUIRE(both != nullptr);

    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *both, true);
    const GameState exec = GwRunExecutor(s, *both);
    for (const GameState* g : { static_cast<const GameState*>(&roll), &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        CHECK(GwFind(*g, kGeist) != nullptr);
        CHECK(GwSpiritTokens(*g) == 2);                              // the flashback resolved...
        CHECK(GwZoneCount(g->players[0].graveyard, kSouls) == 0);   // ...and Souls is exiled
        CHECK(GwTappedLands(*g) == 4);
        CHECK(g->spells_cast_this_turn == s.spells_cast_this_turn + 2);
    }
}

TEST_CASE("Patrician Geist same-turn credit is not a free lunch: THREE lands cannot pay Geist + flashback")
{
    GameState s = GwBoard({ "Watery Grave", "Godless Shrine", "Plains" });
    s.players[0].hand.push_back(GwZone(kGeist, 30));
    s.players[0].graveyard.push_back(GwZone(kSouls, 40));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    CHECK(GwFindPlan(plans, {{Action::Kind::CastFromHand, kGeist},
                             {Action::Kind::CastFromGraveyard, kSouls}}) == nullptr);
    CHECK(GwFindPlan(plans, {{Action::Kind::CastFromHand, kGeist}}) != nullptr);
    CHECK(GwFindPlan(plans, {{Action::Kind::CastFromGraveyard, kSouls}}) != nullptr);
}

TEST_CASE("Patrician Geist PUT by Aether Vial this main discounts the flashback: Vial(3) + Geist + {B} on ONE land")
{
    GameState s = GwBoard({ "Godless Shrine" });
    Permanent& vial = GwPut(s, "Aether Vial", 7);
    vial.charge_counters = 3;
    s.players[0].hand.push_back(GwZone(kGeist, 30));
    s.players[0].graveyard.push_back(GwZone(kSouls, 40));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* both = GwFindPlan(plans, {{Action::Kind::ActivateVial, kGeist},
                                                      {Action::Kind::CastFromGraveyard, kSouls}});
    REQUIRE(both != nullptr);
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *both, true);
    const GameState exec = GwRunExecutor(s, *both);
    for (const GameState* g : { static_cast<const GameState*>(&roll), &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        CHECK(GwFind(*g, kGeist) != nullptr);
        CHECK(GwSpiritTokens(*g) == 2);
        CHECK(GwZoneCount(g->players[0].graveyard, kSouls) == 0);
        CHECK(GwTappedLands(*g) == 1);
    }
}

// ---- Mausoleum Wanderer --------------------------------------------------------------------------------

TEST_CASE("Mausoleum Wanderer: the card -- {U} 1/1 Spirit; an enter watcher (DefHasCreatureEnterWatcher)")
{
    const CardDefinition& d = GwDef(kWanderer);
    CHECK(d.card.m_mana_cost.ToString() == "{U}");
    CHECK(d.card.m_power.value_or(0) == 1);
    CHECK(d.card.m_toughness.value_or(0) == 1);
    CHECK(d.params.own_creature_enters_self_temp_power == 1);
    CHECK(d.params.own_creature_enters_self_temp_tough == 1);
    CHECK(d.params.own_creature_enters_self_counters == 0);   // NOT a counter (and not the Angels signature)
    CHECK(DefHasCreatureEnterWatcher(d.params));
    CHECK(d.enter_watcher);
}

TEST_CASE("Mausoleum Wanderer: two Spirit TOKENS entering -> +2/+2 until end of turn; gone after cleanup")
{
    GameState s = GwBoard({});
    GwPut(s, kWanderer, 1);
    ApplyCastCreatesTokens(s, 0, GwDef(kSouls));
    const Permanent* w = GwFind(s, kWanderer);
    REQUIRE(w != nullptr);
    CHECK(w->temp_power_bonus == 2);
    CHECK(w->temp_tough_bonus == 2);
    CHECK(w->counters.size() == 0);
    CHECK(w->EffectivePower() == 3);

    (void)TurnSolver::SimulateEndAndStartNextTurnForTest(s);   // the rollout's end step + cleanup
    w = GwFind(s, kWanderer);
    REQUIRE(w != nullptr);
    CHECK(w->temp_power_bonus == 0);
    CHECK(w->EffectivePower() == 1);
}

TEST_CASE("Mausoleum Wanderer: 'another' (no self pump), 'Spirit' (an Angel token does not), 'you control'")
{
    // Its own entry.
    GameState s = GwBoard({});
    GwPut(s, kWanderer, 1);
    FireCreatureEnterWatchers(s, 0, static_cast<int>(s.battlefield.size()) - 1);
    CHECK(GwFind(s, kWanderer)->temp_power_bonus == 0);

    // A non-Spirit creature of ours entering.
    GwPut(s, "Plains", 2);   // keep indices honest: a non-creature permanent in between
    GwPut(s, "Thalia's Lieutenant", 3);
    FireCreatureEnterWatchers(s, 0, static_cast<int>(s.battlefield.size()) - 1);
    CHECK(GwFind(s, kWanderer)->temp_power_bonus == 0);

    // An OPPONENT's Spirit entering.
    GwPut(s, "Supreme Phantom", 4, false, 1);
    FireCreatureEnterWatchers(s, 1, static_cast<int>(s.battlefield.size()) - 1);
    CHECK(GwFind(s, kWanderer)->temp_power_bonus == 0);

    // Our Spirit entering: +1/+1, and a second Wanderer entering pumps the first, not itself.
    GwPut(s, kWanderer, 5);
    FireCreatureEnterWatchers(s, 0, static_cast<int>(s.battlefield.size()) - 1);
    CHECK(s.battlefield[0].temp_power_bonus == 1);
    CHECK(s.battlefield.back().temp_power_bonus == 0);
}

TEST_CASE("Mausoleum Wanderer: a Lingering Souls cast pumps it in BOTH worlds (+2/+2 this turn)")
{
    GameState s = GwBoard({ "Plains", "Plains", "Godless Shrine" });
    GwPut(s, kWanderer, 1);
    s.players[0].hand.push_back(GwZone(kSouls, 30));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* cast = GwFindPlan(plans, {{Action::Kind::CastFromHand, kSouls}});
    REQUIRE(cast != nullptr);
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *cast, true);
    const GameState exec = GwRunExecutor(s, *cast);
    for (const GameState* g : { static_cast<const GameState*>(&roll), &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        const Permanent* w = GwFind(*g, kWanderer);
        REQUIRE(w != nullptr);
        CHECK(w->temp_power_bonus == 2);
        CHECK(w->temp_tough_bonus == 2);
    }
}

TEST_CASE("Mausoleum Wanderer: an Aether Vial PUT of a Spirit pumps it in BOTH worlds")
{
    GameState s = GwBoard({ "Plains" });
    GwPut(s, kWanderer, 1);
    Permanent& vial = GwPut(s, "Aether Vial", 7);
    vial.charge_counters = 2;
    s.players[0].hand.push_back(GwZone("Supreme Phantom", 30));
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* put = GwFindPlan(plans, {{Action::Kind::ActivateVial, "Supreme Phantom"}});
    REQUIRE(put != nullptr);
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, *put, true);
    const GameState exec = GwRunExecutor(s, *put);
    for (const GameState* g : { static_cast<const GameState*>(&roll), &exec })
    {
        INFO("world: " << std::string(g == &roll ? "rollout (ApplyPlan)" : "executor (AIEngine::TakeTurn)"));
        const Permanent* w = GwFind(*g, kWanderer);
        REQUIRE(w != nullptr);
        CHECK(w->temp_power_bonus == 1);
        CHECK(GwFind(*g, "Supreme Phantom") != nullptr);
    }
}
