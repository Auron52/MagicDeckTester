// Unit tests for the LandfallElementals "lfe-land" cluster (2026-10-18): the landfall SELF payoffs and
// the Horizon land.
//
//   Akoum Hellhound     {R}     0/1  Landfall -- this creature gets +2/+2 until end of turn.
//   Brushfire Elemental {R}{G}  1/1  Haste; can't be blocked by power <= 2 (inert); same landfall pump.
//   Mossborn Hydra      {2}{G}  0/0  Trample (inert); enters with a +1/+1 counter; Landfall -- double
//                                    the number of +1/+1 counters on this creature.
//   Waterlogged Grove   Land         {T}, Pay 1 life: Add {G} or {U}.  {1}, {T}, Sacrifice: draw a card.
//
// What is guarded here, every item of which fails SILENTLY in a game (a pump that never fires, a 0/0
// that dies on entry, a pump that leaks into the next turn, a doubling that adds instead of doubling):
//   1. The printed data and params are as Scryfall says; the three creatures are landfall WATCHERS
//      (the per-definition byte that both the landfall dispatch and the searched land-drop position
//      axis key on) and the land is not.
//   2. The pump: +2/+2 per land, PLAYED or PUT (a land search activation, EnterLand), stacking, and
//      EXPIRING at the executor's real cleanup step while +1/+1 counters persist.
//   3. The Hydra: enters as a 1/1 on every enter route (a cast in the rollout and in the executor, a
//      generic put, an Aether Vial put), 2/2 beside Metallic Mimic; each landfall doubles (1->2->4->8);
//      Doubling Season turns the doubling's PUT into x3.
//   4. Land-drop timing: "cast the watcher, THEN play the land" is enumerated for each creature and,
//      applied, pumps / doubles; the land-first plan stays enumerated beside it.
//   5. Waterlogged Grove: a coloured tap costs 1 life (no {C} mode); the shape is Fiery Islet's.
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/LandPlay.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameEngine.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void LoadLfe()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefLfe(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

GameState FreshLfe()
{
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 5;
    s.game_seed           = 4242;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    return s;
}

// A permanent ALREADY on the battlefield (entered on an earlier turn unless `sick`). Placed raw -- no
// enter cascade -- so a Hydra placed this way carries exactly the counters the test gives it.
int PutLfe(GameState& s, const std::string& name, int number, bool sick = false)
{
    Permanent p;
    p.card              = DefLfe(name).card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = sick;
    s.battlefield.push_back(p);
    return static_cast<int>(s.battlefield.size()) - 1;
}

Card CardLfe(const std::string& name, int number)
{
    Card c = DefLfe(name).card;
    c.m_number = number;
    return c;
}

const Permanent* ByNum(const GameState& s, int number)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_number == number) { return &p; } }
    return nullptr;
}

const Permanent* ByName(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return &p; } }
    return nullptr;
}

// A one-cast plan the rollout's ApplyPlan and the executor's committed-line pop both accept.
TurnSolver::Plan CastPlan(const std::string& name)
{
    TurnSolver::Plan plan;
    plan.land_decided = true;
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = name;
    a.def       = &DefLfe(name);
    plan.actions.push_back(a);
    return plan;
}

// The executor's stack resolution (GameEngine::ResolveStack's loop, through the shared EffectHandler)
// -- what TakeTurn's resolve callback is in the real game.
void ResolveStackLfe(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

// The executor's real end-of-turn cleanup (GameEngine::CleanupStep, private) through its only public
// door: resume the current turn AT cleanup and stop before the next turn is played.
void RunExecutorCleanup(GameState& s)
{
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    GameEngine ge(eng);
    ge.SetStopAfterTurn(s.turn_number);
    ge.PlayOutFrom(s, s.turn_number + 5, GameEngine::ResumeAt::Cleanup);
}

constexpr const char* kHellhound = "Akoum Hellhound";
constexpr const char* kBrushfire = "Brushfire Elemental";
constexpr const char* kHydra     = "Mossborn Hydra";
constexpr const char* kGrove     = "Waterlogged Grove";

}   // namespace

TEST_CASE("LFE land cluster: printed data and params as Scryfall says")
{
    LoadLfe();
    const CardDefinition& h = DefLfe(kHellhound);
    CHECK(h.card.m_mana_cost.red == 1);
    CHECK(h.card.m_mana_cost.ManaValue() == 1);
    CHECK(h.card.m_power == 0);
    CHECK(h.card.m_toughness == 1);
    CHECK(CardHasSubtype(h.card, "Elemental"));
    CHECK(CardHasSubtype(h.card, "Dog"));
    CHECK(h.params.landfall_self_pump_power == 2);
    CHECK(h.params.landfall_self_pump_toughness == 2);
    CHECK(h.landfall_watcher);

    const CardDefinition& b = DefLfe(kBrushfire);
    CHECK(b.card.m_mana_cost.red == 1);
    CHECK(b.card.m_mana_cost.green == 1);
    CHECK(b.card.m_mana_cost.ManaValue() == 2);
    CHECK(b.card.m_power == 1);
    CHECK(b.card.m_toughness == 1);
    CHECK(b.card.HasKeyword(Keyword::Haste));
    CHECK(CardHasSubtype(b.card, "Elemental"));
    CHECK(b.params.landfall_self_pump_power == 2);
    CHECK(b.landfall_watcher);

    const CardDefinition& y = DefLfe(kHydra);
    CHECK(y.card.m_mana_cost.generic == 2);
    CHECK(y.card.m_mana_cost.green == 1);
    CHECK(y.card.m_power == 0);
    CHECK(y.card.m_toughness == 0);
    CHECK(y.card.HasKeyword(Keyword::Trample));
    CHECK(CardHasSubtype(y.card, "Elemental"));
    CHECK(CardHasSubtype(y.card, "Hydra"));
    CHECK(y.params.enters_with_plus_counters == 1);
    CHECK(y.params.landfall_double_self_counters);
    CHECK_FALSE(y.params.doubles_counters);   // NOT Doubling Season's replacement (and not a Fungus term)
    CHECK(y.landfall_watcher);

    // Waterlogged Grove: Fiery Islet's exact shape with {G}/{U}, and no {C} mode.
    const CardDefinition& g = DefLfe(kGrove);
    const CardDefinition& fi = DefLfe("Fiery Islet");
    CHECK(g.card.IsLand());
    CHECK(g.tmpl == fi.tmpl);
    CHECK(g.params.tap_self_damage == fi.params.tap_self_damage);
    CHECK(g.params.tap_self_damage == 1);
    REQUIRE(g.params.sacrifice_draw_cost.has_value());
    CHECK(g.params.sacrifice_draw_cost->ManaValue() == 1);
    CHECK(g.params.produces.size() == 2);
    CHECK(std::find(g.params.produces.begin(), g.params.produces.end(), Color::Green) != g.params.produces.end());
    CHECK(std::find(g.params.produces.begin(), g.params.produces.end(), Color::Blue) != g.params.produces.end());
    CHECK(std::find(g.params.produces.begin(), g.params.produces.end(), Color::Colorless) == g.params.produces.end());
    CHECK_FALSE(g.landfall_watcher);
}

TEST_CASE("Akoum Hellhound: +2/+2 per land PLAYED or PUT, stacking -- and gone at the real cleanup")
{
    LoadLfe();
    GameState s = FreshLfe();
    PutLfe(s, "Mountain", 10);
    PutLfe(s, kHellhound, 1);
    CHECK(ByNum(s, 1)->EffectivePower() == 0);
    // Route 1: the land drop.
    s.players[0].hand.push_back(CardLfe("Forest", 2));
    REQUIRE(PlayLandFromHand(s, 0, DefLfe("Forest"), LandPlayOptions{}));
    CHECK(ByNum(s, 1)->EffectivePower() == 2);
    CHECK(ByNum(s, 1)->EffectiveToughness() == 3);
    // Route 2: a land put by an effect -- stacks with the first.
    EnterLand(s, DefLfe("Mountain"), 3);
    CHECK(ByNum(s, 1)->EffectivePower() == 4);
    CHECK(ByNum(s, 1)->EffectiveToughness() == 5);
    CHECK(ByNum(s, 1)->counters.empty());   // a pump, never a counter
    // Until END OF TURN: the executor's own cleanup step clears it.
    RunExecutorCleanup(s);
    REQUIRE(ByNum(s, 1) != nullptr);
    CHECK(ByNum(s, 1)->EffectivePower() == 0);
    CHECK(ByNum(s, 1)->EffectiveToughness() == 1);
}

TEST_CASE("Landfall self pump: a Terramorphic Expanse crack is TWO entries' worth only when it fetches")
{
    LoadLfe();
    GameState s = FreshLfe();
    PutLfe(s, kHellhound, 1);
    PutLfe(s, kBrushfire, 2);
    // The Expanse entering is a landfall (its own entry).
    s.players[0].hand.push_back(CardLfe("Terramorphic Expanse", 3));
    s.players[0].library.push_back(CardLfe("Mountain", 50));
    s.players[0].library.push_back(CardLfe("Forest", 51));
    REQUIRE(PlayLandFromHand(s, 0, DefLfe("Terramorphic Expanse"), LandPlayOptions{}));
    CHECK(ByNum(s, 1)->EffectivePower() == 2);
    CHECK(ByNum(s, 2)->EffectivePower() == 3);
    // ...and cracking it puts a basic onto the battlefield: a second landfall.
    for (Permanent& p : s.battlefield) { if (p.card.m_number == 3) { p.tapped = true; } }   // caller paid {T}
    ApplyPermAbility(s, 0, 3, PermAbilityMode::SacLandSearch);
    CHECK(ByName(s, "Terramorphic Expanse") == nullptr);
    CHECK(ByNum(s, 1)->EffectivePower() == 4);
    CHECK(ByNum(s, 2)->EffectivePower() == 5);
    CHECK(ByNum(s, 2)->EffectiveToughness() == 5);
}

TEST_CASE("Landfall self pump: Gruul Turf's own entry pumps once; the land it bounces does not un-pump")
{
    LoadLfe();
    GameState s = FreshLfe();
    PutLfe(s, "Mountain", 10);
    PutLfe(s, "Forest", 11);
    PutLfe(s, kHellhound, 1);
    s.players[0].hand.push_back(CardLfe("Gruul Turf", 2));
    REQUIRE(PlayLandFromHand(s, 0, DefLfe("Gruul Turf"), LandPlayOptions{}));
    CHECK(ByNum(s, 1)->EffectivePower() == 2);   // one land entered (the Turf); leaving fires nothing
    int lands = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.IsLand()) { ++lands; } }
    CHECK(lands == 2);                           // the Turf in, one land back to hand
}

TEST_CASE("Mossborn Hydra: enters with its +1/+1 counter on EVERY enter route (both worlds, puts)")
{
    LoadLfe();
    GameState base = FreshLfe();
    PutLfe(base, "Forest", 10);
    PutLfe(base, "Forest", 11);
    PutLfe(base, "Mountain", 12);
    base.players[0].hand.push_back(CardLfe(kHydra, 1));
    for (int i = 0; i < 5; ++i) { base.players[0].library.push_back(CardLfe("Swords to Plowshares", 60 + i)); }

    // The ROLLOUT's cast (TurnSolver::ApplyPlan -> apply_one's creature-enter branch).
    GameState rollout = base;
    TurnSolver::ApplyPlan(rollout, CastPlan(kHydra), true);
    REQUIRE(ByNum(rollout, 1) != nullptr);
    CHECK(PlusCountersOn(*ByNum(rollout, 1)) == 1);
    CHECK(ByNum(rollout, 1)->EffectivePower() == 1);
    CHECK(ByNum(rollout, 1)->EffectiveToughness() == 1);   // a 1/1, not a dead 0/0

    // The EXECUTOR's cast (EffectHandler::EnterBattlefield), via the committed-line pop.
    GameState exec = base;
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, CastPlan(kHydra), true);
    eng.TakeTurn(exec, true, ResolveStackLfe);
    REQUIRE(ByNum(exec, 1) != nullptr);
    CHECK(PlusCountersOn(*ByNum(exec, 1)) == 1);
    CHECK(exec.stack.empty());

    // A generic put (PutCardOntoBattlefield: persist / reanimation shape).
    GameState put = FreshLfe();
    PutCardOntoBattlefield(put, 0, CardLfe(kHydra, 7), "test put");
    REQUIRE(ByNum(put, 7) != nullptr);
    CHECK(PlusCountersOn(*ByNum(put, 7)) == 1);

    // An Aether Vial put (three charge counters, a three-drop).
    GameState vial = FreshLfe();
    const int vi = PutLfe(vial, "Aether Vial", 20);
    vial.battlefield[static_cast<std::size_t>(vi)].charge_counters = 3;
    vial.players[0].hand.push_back(CardLfe(kHydra, 8));
    TurnSolver::Plan vp;
    vp.land_decided = true;
    Action va;
    va.kind      = Action::Kind::ActivateVial;
    va.card_name = kHydra;
    vp.actions.push_back(va);
    TurnSolver::ApplyPlan(vial, vp, true);
    REQUIRE(ByNum(vial, 8) != nullptr);
    CHECK(PlusCountersOn(*ByNum(vial, 8)) == 1);
}

TEST_CASE("Mossborn Hydra: Metallic Mimic naming Elemental -> enters with TWO; each landfall doubles")
{
    LoadLfe();
    GameState s = FreshLfe();
    const int mi = PutLfe(s, "Metallic Mimic", 30);
    s.battlefield[static_cast<std::size_t>(mi)].chosen_subtype_id = SubtypeRegistry::Instance().Id("Elemental");
    PutCardOntoBattlefield(s, 0, CardLfe(kHydra, 1), "test put");
    REQUIRE(ByNum(s, 1) != nullptr);
    CHECK(PlusCountersOn(*ByNum(s, 1)) == 2);
    EnterLand(s, DefLfe("Forest"), 40);
    CHECK(PlusCountersOn(*ByNum(s, 1)) == 4);
    EnterLand(s, DefLfe("Forest"), 41);
    CHECK(PlusCountersOn(*ByNum(s, 1)) == 8);
    CHECK(ByNum(s, 1)->EffectivePower() == 8);
}

TEST_CASE("Mossborn Hydra: three landfalls compound 1 -> 2 -> 4 -> 8, and the counters survive cleanup")
{
    LoadLfe();
    GameState s = FreshLfe();
    PutCardOntoBattlefield(s, 0, CardLfe(kHydra, 1), "test put");
    REQUIRE(PlusCountersOn(*ByNum(s, 1)) == 1);
    s.players[0].hand.push_back(CardLfe("Forest", 2));
    REQUIRE(PlayLandFromHand(s, 0, DefLfe("Forest"), LandPlayOptions{}));
    CHECK(PlusCountersOn(*ByNum(s, 1)) == 2);
    EnterLand(s, DefLfe("Forest"), 3);
    CHECK(PlusCountersOn(*ByNum(s, 1)) == 4);
    EnterLand(s, DefLfe("Mountain"), 4);
    CHECK(PlusCountersOn(*ByNum(s, 1)) == 8);
    // One merged counter entry (the sim key folds the counter list), and no temp pump at all.
    CHECK(ByNum(s, 1)->counters.size() == 1);
    CHECK(ByNum(s, 1)->temp_power_bonus == 0);
    RunExecutorCleanup(s);
    REQUIRE(ByNum(s, 1) != nullptr);
    CHECK(PlusCountersOn(*ByNum(s, 1)) == 8);   // counters are permanent
    CHECK(ByNum(s, 1)->EffectivePower() == 8);
}

TEST_CASE("Mossborn Hydra: doubling is a PUT, so Doubling Season makes it x3 (and doubles the entry)")
{
    LoadLfe();
    GameState s = FreshLfe();
    PutLfe(s, "Doubling Season", 30);
    PutCardOntoBattlefield(s, 0, CardLfe(kHydra, 1), "test put");
    REQUIRE(ByNum(s, 1) != nullptr);
    CHECK(PlusCountersOn(*ByNum(s, 1)) == 2);   // the enters-with counter is doubled (CR 121.6)
    EnterLand(s, DefLfe("Forest"), 40);
    CHECK(PlusCountersOn(*ByNum(s, 1)) == 6);   // put 2 more, doubled to 4: 2 + 4
}

TEST_CASE("Mossborn Hydra: a Hydra with no +1/+1 counter doubles nothing")
{
    LoadLfe();
    GameState s = FreshLfe();
    const int hi = PutLfe(s, kHydra, 1);       // raw placement: no enter cascade, no counter
    s.battlefield[static_cast<std::size_t>(hi)].temp_tough_bonus = 1;   // kept alive by hand for the test
    EnterLand(s, DefLfe("Forest"), 40);
    CHECK(PlusCountersOn(*ByNum(s, 1)) == 0);
}

TEST_CASE("Land-drop timing: 'cast the landfall creature, THEN play the land' is enumerated and pays off")
{
    LoadLfe();
    // Brushfire (haste): cast off Mountain + Forest, then the Forest from hand -- a 3/3 that attacks now.
    {
        GameState s = FreshLfe();
        PutLfe(s, "Mountain", 10);
        PutLfe(s, "Forest", 11);
        s.players[0].hand.push_back(CardLfe(kBrushfire, 1));
        s.players[0].hand.push_back(CardLfe("Forest", 2));
        const TurnSolver::Plan* after = nullptr;
        bool land_first = false;
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        for (const TurnSolver::Plan& p : plans)
        {
            bool casts = false;
            for (const Action& a : p.actions) { if (a.card_name.str() == kBrushfire) { casts = true; } }
            if (!casts || p.land_to_play != "Forest") { continue; }
            if (p.land_slot != 0) { if (after == nullptr) { after = &p; } }
            else                  { land_first = true; }
        }
        CHECK(land_first);
        REQUIRE(after != nullptr);
        GameState t = s;
        TurnSolver::ApplyPlan(t, *after, true);
        const Permanent* bf = ByNum(t, 1);
        REQUIRE(bf != nullptr);
        CHECK(bf->EffectivePower() == 3);
        CHECK(bf->EffectiveToughness() == 3);
        CHECK(bf->entered_this_turn);
        CHECK(CanAttackFull(*bf, t.battlefield, 0));   // haste: it swings the turn it is cast
    }
    // Mossborn Hydra: cast off three lands, then the land -- it keeps TWO counters for every later turn.
    {
        GameState s = FreshLfe();
        PutLfe(s, "Forest", 10);
        PutLfe(s, "Forest", 11);
        PutLfe(s, "Mountain", 12);
        s.players[0].hand.push_back(CardLfe(kHydra, 1));
        s.players[0].hand.push_back(CardLfe("Forest", 2));
        const TurnSolver::Plan* after = nullptr;
        const TurnSolver::Plan* first = nullptr;
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
        for (const TurnSolver::Plan& p : plans)
        {
            bool casts = false;
            for (const Action& a : p.actions) { if (a.card_name.str() == kHydra) { casts = true; } }
            if (!casts || p.land_to_play != "Forest") { continue; }
            if (p.land_slot != 0) { if (after == nullptr) { after = &p; } }
            else if (first == nullptr) { first = &p; }
        }
        REQUIRE(first != nullptr);
        REQUIRE(after != nullptr);
        GameState a = s, b = s;
        TurnSolver::ApplyPlan(a, *after, true);
        TurnSolver::ApplyPlan(b, *first, true);
        REQUIRE(ByNum(a, 1) != nullptr);
        REQUIRE(ByNum(b, 1) != nullptr);
        CHECK(PlusCountersOn(*ByNum(a, 1)) == 2);
        CHECK(PlusCountersOn(*ByNum(b, 1)) == 1);
    }
}

TEST_CASE("Waterlogged Grove: every tap costs 1 life (no {C} mode), green or blue")
{
    LoadLfe();
    GameState s = FreshLfe();
    PutLfe(s, kGrove, 1);
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    ManaPool avail = AvailableManaPool(s);
    ManaCost g;
    g.green = 1;
    REQUIRE(MtgTestSeam::TapForCost(eng, s, g, avail, false));
    CHECK(s.players[0].life == 19);
    CHECK(ByNum(s, 1)->tapped);

    GameState u = FreshLfe();
    PutLfe(u, kGrove, 1);
    ManaPool avail_u = AvailableManaPool(u);
    ManaCost blue;
    blue.blue = 1;
    REQUIRE(MtgTestSeam::TapForCost(eng, u, blue, avail_u, false));
    CHECK(u.players[0].life == 19);

    GameState c = FreshLfe();
    PutLfe(c, kGrove, 1);
    ManaPool avail_c = AvailableManaPool(c);
    ManaCost one;
    one.generic = 1;
    REQUIRE(MtgTestSeam::TapForCost(eng, c, one, avail_c, false));
    CHECK(c.players[0].life == 19);   // a generic pip still pays life: the land has no painless {C}
}
