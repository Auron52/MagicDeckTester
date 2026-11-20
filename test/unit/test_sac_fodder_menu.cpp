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
#include <set>
#include <string>
#include <utility>
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
struct SacFodderInertArm
{
    explicit SacFodderInertArm(int on) { heurarm::t_arm[heurarm::SAC_FODDER_INERT_PRUNE] = static_cast<std::int8_t>(on); }
    ~SacFodderInertArm() { heurarm::t_arm[heurarm::SAC_FODDER_INERT_PRUNE] = -1; }
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
    // The prefix test: Siege-Gang's own three tokens cannot pay its {3}{R}{R}. Off one Mountain + Lackey + Skirk
    // nothing before it brings enough, so {Bolt, Skirk, Siege-Gang} stays off even though the aggregate (with the
    // smallest maker dropped as "last") would count Siege-Gang's four bodies.
    CHECK(FindPlan(plans, { "Lightning Bolt", "Skirk Prospector", "Siege-Gang Commander" }) == nullptr);

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

namespace
{
Action SfmCast(const std::string& name, int hand_index)
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
}
GameState SfmBoard(const DecisionProvider& prov)
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
    s.deck_has_endstep_reanimate = false;
    s.deck_has_upkeep_reanimate  = false;
    s.players[0].lands_played_this_turn = 1;
    for (int i = 0; i < 20; ++i) { s.players[0].library.push_back(CardSfm("Forest", 100 + i)); }
    return s;
}
}  // namespace

TEST_CASE("Sac fodder menu: an outlet arriving after every outlet was eaten makes bodies eatable again (Skirk, Skirk, Lackey)")
{
    // Goblins s30125489 T1: one Mountain, hand Skirk Prospector x2 + Goblin Lackey. Skirk off the Mountain, the second
    // Skirk eats the first, the Lackey eats the second. Skirk > Lackey > Skirk strands the second Skirk (the Lackey ate
    // the only outlet), so the walk must move the Lackey after the second Skirk.
    const GoblinsProvider prov;
    GameState s = SfmBoard(prov);
    PutSfm(s, "Mountain", 33);
    s.players[0].hand.push_back(CardSfm("Skirk Prospector", 56));
    s.players[0].hand.push_back(CardSfm("Goblin Lackey", 10));
    s.players[0].hand.push_back(CardSfm("Skirk Prospector", 57));
    const std::vector<Action> acts = { SfmCast("Skirk Prospector", 0), SfmCast("Goblin Lackey", 1),
                                       SfmCast("Skirk Prospector", 2) };
    SacFodderMenuArm arm(1);
    std::vector<int> order = { 0, 1, 2 };
    ApplyPayableCastOrder(s, acts, order);
    CHECK(order == std::vector<int>{ 0, 2, 1 });
}

TEST_CASE("Sac fodder menu: an outlet that is not its own fodder (Utopia Mycon) is never eaten by the walk")
{
    // Fungus s30125447 T2: Forest + Ancient Tomb + a live Utopia Mycon ("Sacrifice a Saproling"; Mycon is a Fungus).
    // Thallid {G} + Undercellar Myconid {2}{G} is four mana off three: only Myconid FIRST pays, its ETB Saproling eaten
    // for the Thallid. Counting Mycon as a body read Thallid-first as payable and the apply dropped the Myconid.
    const FungusProvider prov;
    GameState s = SfmBoard(prov);
    PutSfm(s, "Forest", 25);
    PutSfm(s, "Utopia Mycon", 57);
    PutSfm(s, "Ancient Tomb", 3);
    s.players[0].hand.push_back(CardSfm("Thallid", 40));
    s.players[0].hand.push_back(CardSfm("Undercellar Myconid", 53));
    const std::vector<Action> acts = { SfmCast("Thallid", 0), SfmCast("Undercellar Myconid", 1) };
    SacFodderMenuArm arm(1);
    std::vector<int> order = { 0, 1 };
    ApplyPayableCastOrder(s, acts, order);
    CHECK(order == std::vector<int>{ 1, 0 });
}

TEST_CASE("Sac fodder menu: the EXACT walk (producer domain) models a plan-made outlet's fodder too")
{
    // Fungus s30125352 T3: Hickory Woodlot ({G}{G}) + Sol Ring; Utopia Mycon {G}, Undercellar Myconid {2}{G}, Wild
    // Growth {G} on the Woodlot (a land Aura -> the producer domain -> MTG_PAYABLE_ORDER_EXACT's walk). Five mana off
    // four: Mycon first, Myconid, then its Saproling eaten for the Wild Growth. The exact walk knew no fodder, read
    // every order unpayable and kept one that drops the Mycon.
    const FungusProvider prov;
    GameState s = SfmBoard(prov);
    PutSfm(s, "Hickory Woodlot", 33);
    s.battlefield.back().counters.Add(Counter::Type::Depletion, 2);
    PutSfm(s, "Sol Ring", 46);
    s.players[0].hand.push_back(CardSfm("Undercellar Myconid", 53));
    s.players[0].hand.push_back(CardSfm("Wild Growth", 58));
    s.players[0].hand.push_back(CardSfm("Utopia Mycon", 57));
    Action wg = SfmCast("Wild Growth", 1);
    wg.enchant_target = 33;
    const std::vector<Action> acts = { SfmCast("Undercellar Myconid", 0), wg, SfmCast("Utopia Mycon", 2) };
    std::vector<int> off_order = { 0, 1, 2 };
    {
        SacFodderMenuArm arm(0);
        ApplyPayableCastOrder(s, acts, off_order);
    }
    SacFodderMenuArm arm(1);
    std::vector<int> order = { 0, 1, 2 };
    ApplyPayableCastOrder(s, acts, order);
    REQUIRE(order.size() == 3);
    CHECK(off_order == std::vector<int>{ 0, 1, 2 });   // lever off: no order pays from the board alone
    CHECK(order[2] == 1);   // the Wild Growth last, paid by the Myconid's Saproling (Mycon is in by then)
    CHECK(order != off_order);
}

// THE MOVE-ORDER TIER (Plan::sacfod_admit). Goblins gi 620281 T1, the land drop spent: one Mountain, hand Skirk
// Prospector + Goblin Lackey. {Skirk, Lackey} is payable only by Skirk eating itself for the Lackey's {R}, i.e. the
// turn ends with the Lackey alone -- yet its value counted both bodies, so it sorted ahead of {Lackey} and a budgeted
// pass spent its deepest units on it (a held-out A/B loss, 3 -> 4). It stays on the menu (findable), flagged, and
// sorts after every board-payable plan of its wins tier.
TEST_CASE("Sac fodder menu: a plan payable only by eating bodies is flagged and sorted after the board-payable plans")
{
    LoadSfm();
    const GoblinsProvider prov;
    GameState s;
    s.active_player_index        = 0;
    s.turn_number                = 1;
    s.phase                      = Phase::PreCombatMain;
    s.players[0].life            = 20;
    s.players[1].life            = 20;
    s.m_provider                 = &prov;
    s.deck_has_sac_mana_outlet   = true;
    s.deck_has_endstep_reanimate = false;
    s.deck_has_upkeep_reanimate  = false;
    s.players[0].lands_played_this_turn = 1;
    PutSfm(s, "Mountain", 36);
    s.players[0].hand.push_back(CardSfm("Skirk Prospector", 57));
    s.players[0].hand.push_back(CardSfm("Goblin Lackey", 10));
    for (int i = 0; i < 20; ++i) { s.players[0].library.push_back(CardSfm("Mountain", 100 + i)); }
    {
        SacFodderMenuArm arm(0);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
        CHECK(FindPlan(plans, { "Skirk Prospector", "Goblin Lackey" }) == nullptr);
        for (const TurnSolver::Plan& p : plans) { CHECK_FALSE(p.sacfod_admit); }
    }
    SacFodderMenuArm arm(1);
    {
        // {Skirk, Lackey} off one Mountain eats Skirk for the Lackey: the turn ends with the Lackey alone, which
        // {Lackey} reaches with Skirk still in hand -- dominated (MTG_SAC_FODDER_INERT_PRUNE, default ON).
        const std::vector<TurnSolver::Plan> pruned = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
        CHECK(FindPlan(pruned, { "Skirk Prospector", "Goblin Lackey" }) == nullptr);
        CHECK(FindPlan(pruned, { "Goblin Lackey" }) != nullptr);
    }
    SacFodderInertArm inert(0);   // the tag and the ordering, on the plan the prune would remove
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    const TurnSolver::Plan* both  = FindPlan(plans, { "Skirk Prospector", "Goblin Lackey" });
    const TurnSolver::Plan* lack  = FindPlan(plans, { "Goblin Lackey" });
    const TurnSolver::Plan* skirk = FindPlan(plans, { "Skirk Prospector" });
    REQUIRE(both != nullptr);
    REQUIRE(lack != nullptr);
    REQUIRE(skirk != nullptr);
    CHECK(both->sacfod_admit);
    CHECK_FALSE(lack->sacfod_admit);
    CHECK_FALSE(skirk->sacfod_admit);
    // The sorted base menu puts every board-payable plan before the body-eating one (same wins tier: nothing wins on
    // T1). EnumerateMainPlans appends breakpoint variants after the base list, so the FIRST occurrences are compared.
    auto first = [&](const TurnSolver::Plan* want)
    {
        for (std::size_t i = 0; i < plans.size(); ++i) { if (&plans[i] == want) { return i; } }
        return plans.size();
    };
    CHECK(first(lack) < first(both));
    CHECK(first(skirk) < first(both));
}

// THE LAND-AURA HOST FOLD (Fungus gi 630182 T2 m1, d8 b0 off 4 / on 5; gi 630865 is the same shape). Board Hickory
// Woodlot (two depletion counters) + Ancient Tomb, the land drop spent; hand Wild Growth, Utopia Mycon, Undercellar
// Myconid. {Mycon, Wild Growth -> Tomb, Myconid} pays off the board (GG + CC + the Aura's G = 5) and is the T4 line.
// The fodder credit also admits {Mycon, Wild Growth -> Woodlot, Myconid} (the Myconid's Saproling eaten for the
// Aura), and the autonomous signature keys a land Aura by NAME, so the two host variants were one dedup class and the
// body-eating one -- emitted first -- replaced the board-payable one: the T4 line left the menu at every budget.
TEST_CASE("Sac fodder menu: a body-eating land-Aura host never folds away the board-payable host (Wild Growth)")
{
    LoadSfm();
    const FungusProvider prov;
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
    PutSfm(s, "Hickory Woodlot", 34);
    {
        Counter c; c.type = Counter::Type::Depletion; c.count = 2;
        s.battlefield.back().counters.Add(c);
    }
    PutSfm(s, "Ancient Tomb", 1);
    s.players[0].hand.push_back(CardSfm("Utopia Mycon", 57));
    s.players[0].hand.push_back(CardSfm("Wild Growth", 59));
    s.players[0].hand.push_back(CardSfm("Undercellar Myconid", 7));
    for (int i = 0; i < 20; ++i) { s.players[0].library.push_back(CardSfm("Forest", 100 + i)); }
    auto host_of = [](const TurnSolver::Plan& p) -> int
    {
        for (const Action& a : p.actions) { if (a.card_name.str() == "Wild Growth") { return a.enchant_target; } }
        return 0;
    };
    auto hosts = [&](const std::vector<TurnSolver::Plan>& plans)
    {
        // DISTINCT Wild Growth hosts of every {Mycon, Wild Growth, Myconid} (breakpoint variants repeat a plan).
        std::set<int> out;
        for (const TurnSolver::Plan& p : plans)
        {
            if (!CastsAll(p, { "Utopia Mycon", "Wild Growth", "Undercellar Myconid" })) { continue; }
            out.insert(host_of(p));
            if (host_of(p) == 1) { CHECK_FALSE(p.sacfod_admit); }   // board-payable: never demoted
        }
        return out;
    };
    {
        SacFodderMenuArm arm(0);
        const std::set<int> h = hosts(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true));
        CHECK(h == std::set<int>{ 1 });   // the Tomb (the Woodlot host is unpayable without the fodder)
    }
    SacFodderMenuArm arm(1);
    // Both hosts, each its own plan: the board-payable Tomb host and the body-eating Woodlot host.
    const std::set<int> h = hosts(TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true));
    CHECK(h == std::set<int>{ 1, 34 });
}

// THE FODDER WALK GATE (Goblins s30125009 T1 m1, mana oracle PHANTOM). Empty board, the Mountain just played; hand
// Goblin Lackey x2, Skirk Prospector. The aggregate credit reads {Lackey, Skirk, Lackey} as 1 + 2 = 3 against 3, but no
// order pays it: the Mountain pays Skirk, Skirk eats itself for one Lackey and the second Lackey has no source (an
// outlet's own body goes last). The plan the apply realises in full -- {Skirk, Lackey}, Skirk eaten for the Lackey --
// stays on the menu.
TEST_CASE("Sac fodder menu: the fodder walk gate refuses a body-eating plan no order pays, keeps the one that pays")
{
    LoadSfm();
    const GoblinsProvider prov;
    GameState s;
    s.active_player_index        = 0;
    s.turn_number                = 1;
    s.phase                      = Phase::PreCombatMain;
    s.players[0].life            = 20;
    s.players[1].life            = 20;
    s.m_provider                 = &prov;
    s.deck_has_sac_mana_outlet   = true;
    s.deck_has_endstep_reanimate = false;
    s.deck_has_upkeep_reanimate  = false;
    s.players[0].lands_played_this_turn = 1;
    PutSfm(s, "Mountain", 36);
    s.players[0].hand.push_back(CardSfm("Goblin Lackey", 10));
    s.players[0].hand.push_back(CardSfm("Skirk Prospector", 57));
    s.players[0].hand.push_back(CardSfm("Goblin Lackey", 11));
    for (int i = 0; i < 20; ++i) { s.players[0].library.push_back(CardSfm("Mountain", 100 + i)); }
    SacFodderMenuArm arm(1);
    SacFodderInertArm inert(0);   // the walk gate itself (with the prune, both plans below are dominated anyway)
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    CHECK(FindPlan(plans, { "Goblin Lackey", "Skirk Prospector", "Goblin Lackey" }) == nullptr);
    const TurnSolver::Plan* both = FindPlan(plans, { "Skirk Prospector", "Goblin Lackey" });
    REQUIRE(both != nullptr);
    CHECK(both->sacfod_admit);
    GameState t = s;
    TurnSolver::ApplyPlan(t, *both, /*is_pre_combat=*/true);
    CHECK(t.players[0].hand.size() == 1);   // one Lackey left: Skirk and the other Lackey were cast
}

// MTG_SAC_FODDER_INERT_PRUNE -- the card-data premises (CardDatabase.cpp's audited whitelists).
TEST_CASE("Sac fodder menu: the inert-fodder audit classifies the Goblins list")
{
    LoadSfm();
    auto D = [](const char* n) -> const CardDefinition&
    {
        const CardDefinition* d = CardDatabase::Instance().Lookup(std::string(n));
        REQUIRE_MESSAGE(d != nullptr, n);
        return *d;
    };
    for (const char* n : { "Goblin Lackey", "Goblin Piledriver", "Goblin King", "Stingscourger" })
    { CHECK_MESSAGE(D(n).fodder_inert, n); CHECK_MESSAGE(D(n).fodder_inert_outlet, n); }
    // An ETB, a death trigger, a tutor, a haste grant (Krenko taps the turn Chieftain is out), a reducer, an outlet.
    for (const char* n : { "Mogg War Marshal", "Goblin Matron", "Goblin Chieftain", "Goblin Warchief",
                           "Siege-Gang Commander", "Skirk Prospector", "Krenko, Mob Boss", "Pashalik Mons",
                           "Goblin Chainwhirler", "Muxus, Goblin Grandee" })
    { CHECK_FALSE_MESSAGE(D(n).fodder_inert, n); }
    CHECK(D("Skirk Prospector").fodder_inert_outlet);
    CHECK_FALSE(D("Utopia Mycon").fodder_inert_outlet);   // spore counters: more than an outlet
    // Watchers that act from the battlefield: audited off it, not on it.
    for (const char* n : { "Krenko, Mob Boss", "Pashalik Mons", "Rundvelt Hordemaster", "Three Tree City",
                           "Cavern of Souls", "Goblin Warchief", "Mogg War Marshal" })
    { CHECK_FALSE_MESSAGE(D(n).fodder_board_audited, n); CHECK_MESSAGE(D(n).fodder_deck_audited, n); }
    for (const char* n : { "Mountain", "Lightning Bolt", "Siege-Gang Commander", "Goblin Matron", "Skirk Prospector",
                           "Goblin Chieftain", "Aether Vial", "Goblin Lackey", "Twinshot Sniper" })
    { CHECK_MESSAGE(D(n).fodder_board_audited, n); }
    // Brightcap Badger grants a fresh Saproling a {T}: Add {G} -- never audited.
    CHECK_FALSE(D("Brightcap Badger").fodder_deck_audited);
}

namespace
{
// Two Mountains and nothing else on the board; hand Skirk Prospector, Goblin Lackey, Lightning Bolt.
GameState TwoMountainsSkirkLackeyBolt(const GoblinsProvider& prov)
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
    s.deck_has_endstep_reanimate = false;
    s.deck_has_upkeep_reanimate  = false;
    s.players[0].lands_played_this_turn = 1;
    PutSfm(s, "Mountain", 36);
    PutSfm(s, "Mountain", 37);
    s.players[0].hand.push_back(CardSfm("Skirk Prospector", 57));
    s.players[0].hand.push_back(CardSfm("Goblin Lackey", 10));
    s.players[0].hand.push_back(CardSfm("Lightning Bolt", 1));
    for (int i = 0; i < 20; ++i) { s.players[0].library.push_back(CardSfm("Mountain", 100 + i)); }
    return s;
}
}  // namespace

TEST_CASE("Sac fodder menu: a plan that must eat a body it casts for nothing else is dominated (inert prune)")
{
    const GoblinsProvider prov;
    SacFodderMenuArm arm(1);
    const GameState s = TwoMountainsSkirkLackeyBolt(prov);
    // {Skirk, Lackey, Bolt} off two Mountains must eat Lackey (or Skirk) for the Bolt: {Skirk, Bolt} and
    // {Lackey, Bolt} reach the same board with a card more in hand.
    {
        SacFodderInertArm inert(0);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
        CHECK(FindPlan(plans, { "Skirk Prospector", "Goblin Lackey", "Lightning Bolt" }) != nullptr);
    }
    {
        SacFodderInertArm inert(1);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
        CHECK(FindPlan(plans, { "Skirk Prospector", "Goblin Lackey", "Lightning Bolt" }) == nullptr);
        CHECK(FindPlan(plans, { "Skirk Prospector", "Lightning Bolt" }) != nullptr);
        CHECK(FindPlan(plans, { "Goblin Lackey", "Lightning Bolt" }) != nullptr);
        CHECK(FindPlan(plans, { "Skirk Prospector", "Goblin Lackey" }) != nullptr);
    }
    // A creature counter on the board (Krenko counts the Lackey while it is out): no prune. (A death watcher such as
    // Pashalik Mons keeps the payer itself off -- SacOutletEatHasValue -- so it never reaches the prune.)
    {
        SacFodderInertArm inert(1);
        GameState t = s;
        PutSfm(t, "Krenko, Mob Boss", 70);
        t.battlefield.back().tapped = true;
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(t, /*is_pre_combat=*/true);
        CHECK(FindPlan(plans, { "Skirk Prospector", "Goblin Lackey", "Lightning Bolt" }) != nullptr);
    }
}
