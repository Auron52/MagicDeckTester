// MTG_SAC_FODDER_MENU -- the plan-menu mana gates count the sac-for-mana fodder an outlet CAST IN THE SAME PLAN
// makes available. The §2b payer eats a Goblin through Skirk Prospector ("Sacrifice a Goblin: Add {R}") whenever
// the board cannot pay otherwise, but EnumeratePlansCore credited that supply only through an outlet already on
// the battlefield, so "cast Skirk, then pay Lightning Bolt by eating a Goblin" was unfindable at every budget
// (mana oracle ENUM-GAP; Goblins s30125177 gi125177 T2 m1 is the board below). These pin: the plan reaches the
// menu with the lever on and not with it off; the apply really pays it (Skirk first, a body eaten for the Bolt);
// a board with no outlet anywhere enumerates identically both ways. docs/design/sac-fodder-menu-2026-11-18.md.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace
{

void LoadSfm()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card CardSfm(const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c = d->card;
    c.m_number = number;
    return c;
}

void PutSfm(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = CardSfm(name, number);
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

struct SacFodderMenuArm
{
    explicit SacFodderMenuArm(int on) { heurarm::t_arm[heurarm::SAC_FODDER_MENU] = static_cast<std::int8_t>(on); }
    ~SacFodderMenuArm() { heurarm::t_arm[heurarm::SAC_FODDER_MENU] = -1; }
};

// Goblins s30125177 gi125177 T2 main 1 with the land drop spent: one untapped Mountain and a Goblin Lackey; hand
// Lightning Bolt, Skirk Prospector, Siege-Gang Commander.
GameState SkirkInHandBoard(const GoblinsProvider& prov, bool with_skirk = true)
{
    LoadSfm();
    GameState s;
    s.active_player_index        = 0;
    s.turn_number                = 2;
    s.phase                      = Phase::PreCombatMain;
    s.players[0].life            = 20;
    s.players[1].life            = 20;
    s.m_provider                 = &prov;
    s.deck_has_sac_mana_outlet   = true;
    s.deck_has_endstep_reanimate = false;   // both default true, and a death that pays keeps the payer OFF
    s.deck_has_upkeep_reanimate  = false;   // (SacOutletEatHasValue)
    s.players[0].lands_played_this_turn = 1;
    PutSfm(s, "Mountain", 36);
    PutSfm(s, "Goblin Lackey", 10);
    s.players[0].hand.push_back(CardSfm("Lightning Bolt", 1));
    if (with_skirk) { s.players[0].hand.push_back(CardSfm("Skirk Prospector", 57)); }
    s.players[0].hand.push_back(CardSfm("Siege-Gang Commander", 54));
    for (int i = 0; i < 20; ++i) { s.players[0].library.push_back(CardSfm("Mountain", 100 + i)); }
    return s;
}

bool CastsAll(const TurnSolver::Plan& p, const std::vector<std::string>& names)
{
    std::vector<std::string> casts;
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand) { casts.push_back(a.card_name.str()); } }
    if (casts.size() != names.size()) { return false; }
    for (const std::string& n : names)
    { if (std::find(casts.begin(), casts.end(), n) == casts.end()) { return false; } }
    return true;
}

const TurnSolver::Plan* FindPlan(const std::vector<TurnSolver::Plan>& plans, const std::vector<std::string>& names)
{
    for (const TurnSolver::Plan& p : plans) { if (CastsAll(p, names)) { return &p; } }
    return nullptr;
}

std::vector<std::string> MenuKeys(const std::vector<TurnSolver::Plan>& plans)
{
    std::vector<std::string> keys;
    for (const TurnSolver::Plan& p : plans)
    {
        std::string k;
        for (const Action& a : p.actions) { k += std::to_string(static_cast<int>(a.kind)) + ":" + a.card_name.str() + ";"; }
        keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

}  // namespace

TEST_CASE("Sac fodder menu: {cast Skirk Prospector + cast Lightning Bolt} off one Mountain reaches the menu only with the lever")
{
    const GoblinsProvider prov;
    const GameState s = SkirkInHandBoard(prov);
    {
        SacFodderMenuArm arm(0);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
        CHECK(FindPlan(plans, { "Lightning Bolt", "Skirk Prospector" }) == nullptr);
        CHECK(FindPlan(plans, { "Skirk Prospector" }) != nullptr);
    }
    SacFodderMenuArm arm(1);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* both = FindPlan(plans, { "Lightning Bolt", "Skirk Prospector" });
    REQUIRE(both != nullptr);
    // Siege-Gang's {3}{R}{R} stays off: one Mountain + three bodies (Lackey, Skirk, nothing else) is not five.
    CHECK(FindPlan(plans, { "Skirk Prospector", "Siege-Gang Commander" }) == nullptr);

    // The apply realises it: Skirk resolves off the Mountain, then a Goblin is eaten for the Bolt.
    GameState t = s;
    TurnSolver::ApplyPlan(t, *both, /*is_pre_combat=*/true);
    CHECK(t.players[1].life == 17);
    CHECK(t.players[0].hand.size() == 1);   // only Siege-Gang left
    int goblins = 0;
    for (const Permanent& p : t.battlefield)
    { if (p.controller_index == 0 && p.card.IsCreature()) { ++goblins; } }
    CHECK(goblins == 1);                    // Lackey + Skirk entered, one of them eaten
}

TEST_CASE("Sac fodder menu: inert without a sac-for-mana outlet on the board or in hand")
{
    const GoblinsProvider prov;
    const GameState s = SkirkInHandBoard(prov, /*with_skirk=*/false);
    std::vector<std::string> off, on;
    {
        SacFodderMenuArm arm(0);
        off = MenuKeys(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true));
    }
    {
        SacFodderMenuArm arm(1);
        on = MenuKeys(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true));
    }
    CHECK(off == on);
    CHECK_FALSE(on.empty());
}

TEST_CASE("Sac fodder menu: inert with the payer's own lever off (MTG_SAC_OUTLET_PAY=0: nothing eats a body)")
{
    const GoblinsProvider prov;
    const GameState s = SkirkInHandBoard(prov);
    heurarm::t_arm[heurarm::SAC_OUTLET_PAY] = 0;
    SacFodderMenuArm arm(1);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    heurarm::t_arm[heurarm::SAC_OUTLET_PAY] = -1;
    CHECK(FindPlan(plans, { "Lightning Bolt", "Skirk Prospector" }) == nullptr);
}

TEST_CASE("Sac fodder menu: the apply's payable-order walk casts the plan's outlet BEFORE the cast its fodder pays")
{
    const GoblinsProvider prov;
    const GameState s = SkirkInHandBoard(prov);
    auto cast = [&](const std::string& name, int hand_index)
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(name);
        REQUIRE(d != nullptr);
        Action a;
        a.kind       = Action::Kind::CastFromHand;
        a.card_name  = InternedName(name);
        a.def        = d;
        a.cost       = d->card.m_mana_cost;
        a.hand_index = hand_index;
        a.is_noncreature = !d->card.IsCreature();
        return a;
    };
    // Bolt first (the order a rank would give a 1-mana burn spell): Bolt takes the Mountain, Skirk has no source.
    const std::vector<Action> acts = { cast("Lightning Bolt", 0), cast("Skirk Prospector", 1) };
    {
        SacFodderMenuArm arm(0);
        std::vector<int> order = { 0, 1 };
        ApplyPayableCastOrder(s, acts, order);
        CHECK(order == std::vector<int>{ 0, 1 });   // no domain admits it: the given order stays
    }
    SacFodderMenuArm arm(1);
    std::vector<int> order = { 0, 1 };
    ApplyPayableCastOrder(s, acts, order);
    CHECK(order == std::vector<int>{ 1, 0 });       // Skirk off the Mountain, then a Goblin eaten for the Bolt
    // A plan that pays from the board alone keeps its order (fodder is the last resort).
    GameState two = s;
    PutSfm(two, "Mountain", 37);
    std::vector<int> keep = { 0, 1 };
    ApplyPayableCastOrder(two, acts, keep);
    CHECK(keep == std::vector<int>{ 0, 1 });
}

TEST_CASE("Sac fodder menu: beside a LIVE Skirk the walk keeps the outlet for last (Lackey before Matron)")
{
    // s30125176 T2 m1: two Mountains and a live Skirk Prospector; the plan casts Goblin Matron {2}{R} and Goblin
    // Lackey {R}. Matron first eats Skirk itself (the only body) and Lackey has no source; Lackey first pays off a
    // Mountain and is then eaten with Skirk for Matron.
    const GoblinsProvider prov;
    LoadSfm();
    GameState s;
    s.active_player_index        = 0;
    s.turn_number                = 2;
    s.phase                      = Phase::PreCombatMain;
    s.players[0].life            = 20;
    s.players[1].life            = 20;
    s.m_provider                 = &prov;
    s.deck_has_sac_mana_outlet   = true;
    s.deck_has_endstep_reanimate = false;
    s.deck_has_upkeep_reanimate  = false;
    s.players[0].lands_played_this_turn = 1;
    PutSfm(s, "Mountain", 28);
    PutSfm(s, "Skirk Prospector", 56);
    PutSfm(s, "Mountain", 31);
    s.players[0].hand.push_back(CardSfm("Goblin Matron", 14));
    s.players[0].hand.push_back(CardSfm("Goblin Lackey", 10));
    for (int i = 0; i < 20; ++i) { s.players[0].library.push_back(CardSfm("Mountain", 100 + i)); }
    auto cast = [&](const std::string& name, int hand_index)
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(name);
        REQUIRE(d != nullptr);
        Action a;
        a.kind       = Action::Kind::CastFromHand;
        a.card_name  = InternedName(name);
        a.def        = d;
        a.cost       = d->card.m_mana_cost;
        a.hand_index = hand_index;
        return a;
    };
    const std::vector<Action> acts = { cast("Goblin Matron", 0), cast("Goblin Lackey", 1) };
    {
        SacFodderMenuArm arm(0);
        std::vector<int> order = { 0, 1 };
        ApplyPayableCastOrder(s, acts, order);
        CHECK(order == std::vector<int>{ 0, 1 });
    }
    SacFodderMenuArm arm(1);
    std::vector<int> order = { 0, 1 };
    ApplyPayableCastOrder(s, acts, order);
    CHECK(order == std::vector<int>{ 1, 0 });
}
