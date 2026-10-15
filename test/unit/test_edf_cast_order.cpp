// The USER-approved EldraziDisplacerFlicker cast-order package (2026-10-14; docs/design/
// edf-cast-order-2026-10-14.md). One TEST_CASE group per part, each A/B'd on its own heurarm slot:
//   1 MTG_EDF_CAST_ORDER          the TOTAL order, by params, the card name the last tie-break
//   2 MTG_EDF_AURA_RANGE          a land Aura that costs mana net this turn moves behind the untappers
//                                 ONLY when the order is otherwise unpayable
//   3 MTG_EDF_SHROUD_STACK        Trace goes behind every other land Aura sharing its host
//   4 MTG_EDF_ROUTE_AURA_ORDER    the COMBO OFF route deploys land Auras in the cast order
//   5 MTG_EDF_ROUTE_LOOP_UNTAPPER the route casts a second untapper whose LOOP nets more
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

void EnsureCardsCo()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const EldraziFlickerProvider& ProvCo()
{
    static const EldraziFlickerProvider p;
    return p;
}

Card PhCo(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

// Sets one slot for the scope of the guard, restores "use the env default" after.
struct ArmGuard
{
    heurarm::Slot slot;
    ArmGuard(heurarm::Slot s, int v) : slot(s) { heurarm::t_arm[s] = static_cast<std::int8_t>(v); }
    ~ArmGuard() { heurarm::t_arm[slot] = -1; }
};

GameState BaseState(int turn, int lib = 20)
{
    EnsureCardsCo();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = turn;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = 20;
    s.deck_has_land_aura  = true;
    s.m_provider          = &ProvCo();
    for (int i = 0; i < lib; ++i) { s.players[0].library.push_back(PhCo("Brushland", 900 + i)); }
    return s;
}

int PutCo(GameState& s, const char* name, int number, int host = 0, bool tapped = false)
{
    Permanent p;
    p.card              = CardDatabase::Instance().Lookup(name)->card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = false;
    p.tapped            = tapped;
    p.aura_attached_to  = host;
    s.battlefield.push_back(p);
    return number;
}

Action CastCo(GameState& s, const std::string& name, int number, int host = 0)
{
    s.players[0].hand.push_back(PhCo(name, number));
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    Action a;
    a.kind           = Action::Kind::CastFromHand;
    a.card_name      = name;
    a.def            = d;
    a.cost           = d->card.m_mana_cost;
    a.is_noncreature = !d->card.IsCreature();
    a.card_mv        = d->card.m_mana_cost.ManaValue();
    a.enchant_target = host;
    return a;
}

// The order both apply worlds realise for `acts` (all casts): the shared comparator, then the deck's
// set-level arrangement -- exactly the AIEngine clean branch / ApplyPlanDirect.
std::vector<std::string> Realised(const GameState& s, const std::vector<Action>& acts)
{
    std::vector<int> order;
    for (int i = 0; i < static_cast<int>(acts.size()); ++i) { order.push_back(i); }
    std::stable_sort(order.begin(), order.end(), [&](int x, int y)
    { return CastOrderLess(s, acts[static_cast<std::size_t>(x)], acts[static_cast<std::size_t>(y)]); });
    ApplyDeckCastArrangement(s, acts, order);
    std::vector<std::string> names;
    for (int i : order) { names.push_back(acts[static_cast<std::size_t>(i)].card_name.str()); }
    return names;
}

const std::vector<std::string>& EveryEdfCast()
{
    static const std::vector<std::string> v = {
        "Wild Growth", "Fertile Ground", "Trace of Abundance", "Overgrowth", "Cloud of Faeries",
        "Peregrine Drake", "Training Grounds", "Eldrazi Displacer", "Emiel the Blessed",
        "Dimensional Infiltrator", "Essence Depleter", "Vexing Shusher", "Living Wish", "Eladamri's Call" };
    return v;
}

}   // namespace

// ---- part 1 ---------------------------------------------------------------------------------------

TEST_CASE("EDF order (part 1): every cast lands in the USER-approved TOTAL order")
{
    GameState s = BaseState(4);
    std::vector<Action> acts;
    int num = 100;
    for (const std::string& nm : EveryEdfCast()) { acts.push_back(CastCo(s, nm, num++)); }
    // No lands: nothing pays, so the range (part 2) has no payable alternative and leaves the order.
    CHECK(Realised(s, acts) == EveryEdfCast());
}

TEST_CASE("EDF order (part 1): permuting the hand does not change the realised order")
{
    GameState s = BaseState(4);
    std::vector<Action> base;
    int num = 100;
    for (const std::string& nm : EveryEdfCast()) { base.push_back(CastCo(s, nm, num++)); }
    // Every rotation and its reverse: 28 hand orders, one realised order.
    for (std::size_t r = 0; r < base.size(); ++r)
    {
        std::vector<Action> acts = base;
        std::rotate(acts.begin(), acts.begin() + static_cast<long>(r), acts.end());
        CHECK(Realised(s, acts) == EveryEdfCast());
        std::reverse(acts.begin(), acts.end());
        CHECK(Realised(s, acts) == EveryEdfCast());
    }
}

TEST_CASE("EDF order (part 1): the untappers no longer tie -- Cloud before Drake in either hand order")
{
    GameState s = BaseState(4);
    const Action cloud = CastCo(s, "Cloud of Faeries", 10);
    const Action drake = CastCo(s, "Peregrine Drake", 42);
    const std::vector<std::string> want = { "Cloud of Faeries", "Peregrine Drake" };
    CHECK(Realised(s, { cloud, drake }) == want);
    CHECK(Realised(s, { drake, cloud }) == want);
    // The 09-15 ranks (=0): the tie goes to hand order, which is the defect.
    ArmGuard g(heurarm::EDF_CAST_ORDER, 0);
    CHECK(Realised(s, { drake, cloud }) == std::vector<std::string>{ "Peregrine Drake", "Cloud of Faeries" });
}

TEST_CASE("EDF order (part 1): the comparator's name tie-break is total and strict")
{
    GameState s = BaseState(4);
    const Action a = CastCo(s, "Eldrazi Displacer", 18);
    const Action b = CastCo(s, "Emiel the Blessed", 21);
    // Equal keys, different cards: exactly one of a<b, b<a.
    CHECK(CastOrderLessRanked(s, a, 7, b, 7));
    CHECK_FALSE(CastOrderLessRanked(s, b, 7, a, 7));
    CHECK_FALSE(CastOrderLessRanked(s, a, 7, a, 7));   // irreflexive; copies keep plan order
    {
        ArmGuard g(heurarm::EDF_CAST_ORDER, 0);
        CHECK_FALSE(CastOrderLessRanked(s, a, 7, b, 7));
        CHECK_FALSE(CastOrderLessRanked(s, b, 7, a, 7));
    }
}

TEST_CASE("EDF order (part 1): MTG_EDF_PAYLOAD_FIRST=0 still means reducer first")
{
    GameState s = BaseState(4);
    const Action tg    = CastCo(s, "Training Grounds", 51);
    const Action cloud = CastCo(s, "Cloud of Faeries", 10);
    const Action disp  = CastCo(s, "Eldrazi Displacer", 18);
    CHECK(Realised(s, { disp, tg, cloud })
          == std::vector<std::string>{ "Cloud of Faeries", "Training Grounds", "Eldrazi Displacer" });
    ArmGuard g(heurarm::EDF_PAYLOAD_FIRST, 0);
    CHECK(Realised(s, { disp, tg, cloud })
          == std::vector<std::string>{ "Training Grounds", "Cloud of Faeries", "Eldrazi Displacer" });
}

// ---- part 3 ---------------------------------------------------------------------------------------

TEST_CASE("EDF order (part 3): Trace goes behind an Overgrowth on the SAME land, ahead of one elsewhere")
{
    GameState s = BaseState(4);
    const int coast = PutCo(s, "Yavimaya Coast", 57);
    const int brush = PutCo(s, "Brushland", 6);
    // Enough mana that the stack pays either way (part 3b moves only an order that pays).
    PutCo(s, "Brushland", 7);
    PutCo(s, "Kitchen", 29);
    PutCo(s, "Adarkar Wastes", 1);
    PutCo(s, "Mariposa Military Base", 35);
    const Action trace_c = CastCo(s, "Trace of Abundance", 47, coast);
    const Action og_c    = CastCo(s, "Overgrowth", 38, coast);
    const Action og_b    = CastCo(s, "Overgrowth", 39, brush);
    // One host: the legal stack, Overgrowth first.
    CHECK(Realised(s, { trace_c, og_c }) == std::vector<std::string>{ "Overgrowth", "Trace of Abundance" });
    CHECK(Realised(s, { og_c, trace_c }) == std::vector<std::string>{ "Overgrowth", "Trace of Abundance" });
    // Different hosts: the USER's funding order, Trace first.
    CHECK(Realised(s, { og_b, trace_c }) == std::vector<std::string>{ "Trace of Abundance", "Overgrowth" });
    // Lever off: Trace first on the shared host too (the second Aura would be re-homed).
    ArmGuard g(heurarm::EDF_SHROUD_STACK, 0);
    CHECK(Realised(s, { og_c, trace_c }) == std::vector<std::string>{ "Trace of Abundance", "Overgrowth" });
}

namespace
{
// The Auras attached to the permanent numbered `host` after an apply.
std::vector<std::string> AurasOn(const GameState& s, int host)
{
    std::vector<std::string> v;
    for (const Permanent& p : s.battlefield) { if (p.aura_attached_to == host) { v.push_back(p.card.m_name.str()); } }
    std::sort(v.begin(), v.end());
    return v;
}
GameState StackBoard(TurnSolver::Plan& plan, int& coast)
{
    GameState s = BaseState(4);
    coast = PutCo(s, "Yavimaya Coast", 57);
    PutCo(s, "Brushland", 6);
    PutCo(s, "Brushland", 7);
    PutCo(s, "Kitchen", 29);
    PutCo(s, "Adarkar Wastes", 1);
    PutCo(s, "Mariposa Military Base", 35);
    s.players[0].lands_played_this_turn = 1;
    // Plan (hand) order Trace first -- the rank order too: only the host rule puts Overgrowth first.
    plan.actions.push_back(CastCo(s, "Trace of Abundance", 47, coast));
    plan.actions.push_back(CastCo(s, "Overgrowth", 38, coast));
    return s;
}
}   // namespace

// End to end through the rollout apply (ApplyPlanDirect, which shares the arrangement with the executor):
// both Auras declared on Yavimaya Coast really end on it.
TEST_CASE("EDF order (part 3): the apply stacks Overgrowth then Trace on the ONE land both declare")
{
    {
        TurnSolver::Plan plan;
        int coast = 0;
        GameState s = StackBoard(plan, coast);
        TurnSolver::ApplyPlan(s, plan, /*is_pre_combat=*/true);
        CHECK(AurasOn(s, coast) == std::vector<std::string>{ "Overgrowth", "Trace of Abundance" });
    }
    {
        // =0: Trace resolves first and shrouds the Coast, so the Overgrowth is re-homed elsewhere.
        ArmGuard g(heurarm::EDF_SHROUD_STACK, 0);
        TurnSolver::Plan plan;
        int coast = 0;
        GameState s = StackBoard(plan, coast);
        TurnSolver::ApplyPlan(s, plan, /*is_pre_combat=*/true);
        CHECK(AurasOn(s, coast) == std::vector<std::string>{ "Trace of Abundance" });
    }
}

// Part 3b: when the legal stack cannot be paid -- Overgrowth's {G}{G} bonus cannot pay Trace's {R/W}, and
// here Overgrowth's own cost takes the only white source -- the order stays as given (Trace first; the
// Overgrowth is then re-homed at resolution, which is what played before part 3).
TEST_CASE("EDF order (part 3b): the same-land move is taken only when the moved order pays")
{
    GameState s = BaseState(4);
    const int coast = PutCo(s, "Yavimaya Coast", 57);
    PutCo(s, "Brushland", 6);
    PutCo(s, "Kitchen", 29);
    PutCo(s, "Kitchen", 30);
    const Action trace_c = CastCo(s, "Trace of Abundance", 47, coast);
    const Action og_c    = CastCo(s, "Overgrowth", 38, coast);
    CHECK_FALSE(TurnSolver::OrderPaysSequentially(s, { og_c, trace_c }, { 0, 1 }));
    CHECK(Realised(s, { og_c, trace_c }) == std::vector<std::string>{ "Trace of Abundance", "Overgrowth" });
    ArmGuard g(heurarm::EDF_SHROUD_STACK_PAYS, 0);   // =0: move unconditionally (part 3 as first built)
    CHECK(Realised(s, { og_c, trace_c }) == std::vector<std::string>{ "Overgrowth", "Trace of Abundance" });
}

// ---- part 2 ---------------------------------------------------------------------------------------

namespace
{
// Probe aura_drake: five untapped lands, no outlet; hand Fertile Ground + Peregrine Drake.
GameState AuraDrakeBoard(std::vector<Action>& acts, int extra_lands)
{
    GameState s = BaseState(4);
    const int brush = PutCo(s, "Brushland", 6);
    PutCo(s, "Yavimaya Coast", 57);
    PutCo(s, "Kitchen", 29);
    PutCo(s, "Adarkar Wastes", 1);
    PutCo(s, "Mariposa Military Base", 35);
    for (int i = 0; i < extra_lands; ++i) { PutCo(s, "Brushland", 7 + i); }
    s.players[0].lands_played_this_turn = 1;
    acts.push_back(CastCo(s, "Fertile Ground", 25, brush));
    acts.push_back(CastCo(s, "Peregrine Drake", 42));
    return s;
}
}   // namespace

TEST_CASE("EDF order (part 2): {Fertile Ground, Drake} on five lands is cast Drake-first")
{
    std::vector<Action> acts;
    const GameState s = AuraDrakeBoard(acts, 0);
    CHECK_FALSE(TurnSolver::OrderPaysSequentially(s, acts, { 0, 1 }));   // the Aura's slot strands the Drake
    CHECK(TurnSolver::OrderPaysSequentially(s, acts, { 1, 0 }));         // the Drake's refund pays the Aura
    CHECK(Realised(s, acts) == std::vector<std::string>{ "Peregrine Drake", "Fertile Ground" });
    ArmGuard g(heurarm::EDF_AURA_RANGE, 0);
    CHECK(Realised(s, acts) == std::vector<std::string>{ "Fertile Ground", "Peregrine Drake" });
}

TEST_CASE("EDF order (part 2): the Aura keeps its slot whenever its slot pays")
{
    std::vector<Action> acts;
    const GameState s = AuraDrakeBoard(acts, 2);   // seven lands: both orders pay
    CHECK(TurnSolver::OrderPaysSequentially(s, acts, { 0, 1 }));
    CHECK(Realised(s, acts) == std::vector<std::string>{ "Fertile Ground", "Peregrine Drake" });
}

TEST_CASE("EDF order (part 2): Wild Growth never moves -- it costs nothing net")
{
    GameState s = BaseState(4);
    const int brush = PutCo(s, "Brushland", 6);
    PutCo(s, "Yavimaya Coast", 57);
    PutCo(s, "Kitchen", 29);
    PutCo(s, "Adarkar Wastes", 1);
    std::vector<Action> acts;
    acts.push_back(CastCo(s, "Wild Growth", 53, brush));
    acts.push_back(CastCo(s, "Peregrine Drake", 42));
    // Four lands: neither order pays the pair, and Wild Growth (mana value 1, adds 1) has no range to
    // walk -- it is never a candidate to move.
    CHECK_FALSE(TurnSolver::OrderPaysSequentially(s, acts, { 0, 1 }));
    CHECK(Realised(s, acts) == std::vector<std::string>{ "Wild Growth", "Peregrine Drake" });
}

// ---- part 4 ---------------------------------------------------------------------------------------

TEST_CASE("EDF route (part 4): the deploy casts land Auras in the cast order, not any-colour first")
{
    GameState s = BaseState(4);
    PutCo(s, "Brushland", 6);
    PutCo(s, "Yavimaya Coast", 57);
    // Hand order: Overgrowth, Trace (any-colour), Wild Growth, Fertile Ground (any-colour).
    for (const char* nm : { "Overgrowth", "Trace of Abundance", "Wild Growth", "Fertile Ground" })
    { s.players[0].hand.push_back(PhCo(nm, 600 + static_cast<int>(s.players[0].hand.size()))); }
    // Default: the USER's order -- Wild Growth (hand index 2) first.
    CHECK(EdfComboRouteFirstAuraPick(s, 0) == 2);
    // =0: the first any-colour Aura in hand order, Trace (index 1).
    ArmGuard g(heurarm::EDF_ROUTE_AURA_ORDER, 0);
    CHECK(EdfComboRouteFirstAuraPick(s, 0) == 1);
}

TEST_CASE("EDF route (part 4): Fertile Ground before Trace whatever the hand order")
{
    GameState s = BaseState(4);
    PutCo(s, "Brushland", 6);
    for (const char* nm : { "Trace of Abundance", "Overgrowth", "Fertile Ground" })
    { s.players[0].hand.push_back(PhCo(nm, 700 + static_cast<int>(s.players[0].hand.size()))); }
    CHECK(EdfComboRouteFirstAuraPick(s, 0) == 2);
}

// ---- part 5 ---------------------------------------------------------------------------------------

namespace
{
// Probe t4: Yavimaya Coast + Wild Growth (2), Kitchen (1), Adarkar Wastes (1); Eldrazi Displacer and
// Essence Depleter in play; hand Cloud of Faeries + Peregrine Drake; opponent on 20.
GameState T4Board(int first_num)
{
    GameState s = BaseState(4);
    s.players[0].hand.push_back(PhCo("Cloud of Faeries", first_num));
    s.players[0].hand.push_back(PhCo("Peregrine Drake", first_num + 1));
    const int coast = PutCo(s, "Yavimaya Coast", first_num + 2);
    PutCo(s, "Wild Growth", first_num + 3, coast);
    PutCo(s, "Kitchen", first_num + 4);
    PutCo(s, "Adarkar Wastes", first_num + 5);
    PutCo(s, "Eldrazi Displacer", first_num + 6);
    PutCo(s, "Essence Depleter", first_num + 7);
    s.players[0].lands_played_this_turn = 1;
    return s;
}
int TrialCo(const GameState& s, int arm)
{
    ArmGuard g(heurarm::EDF_ROUTE_LOOP_UNTAPPER, arm);
    return EdfComboRouteTrial(s, 0);
}
}   // namespace

TEST_CASE("EDF route (part 5): the Drake's net-positive loop is deployed though its cast refund is short")
{
    // Distinct card numbers per arm: the trial memo keys a board on numbers (see the route's BoardKey).
    CHECK_MESSAGE(TrialCo(T4Board(2000), 0) != 1, "precondition: the refund rule skips the Drake");
    CHECK(TrialCo(T4Board(3000), 1) == 1);   // WIN
}

TEST_CASE("EDF route (part 5): the live apply realises the kill")
{
    GameState s = T4Board(4000);
    ArmGuard g(heurarm::EDF_ROUTE_LOOP_UNTAPPER, 1);
    CHECK(EdfComboRouteApply(s, 0) == 1);
    CHECK(s.players[1].life <= 0);
    bool drake = false;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == "Peregrine Drake") { drake = true; } }
    CHECK(drake);
}

// ---- part 4b --------------------------------------------------------------------------------------

namespace
{
// The A/B's shipped game seed 11300164 gi 163, the OLD arm's T3 board (it went off that turn): Conservatory
// carrying Wild Growth, Yavimaya Coast; hand Aether Hub (the land drop), Peregrine Drake, Overgrowth, Emiel,
// Cloud of Faeries, a second Wild Growth; the library is the 32 cards that go-off drew, in order. In the
// USER's Aura order the route casts the Wild Growth first: it takes the best host and Yavimaya Coast pays for
// it, Overgrowth has to go on the Hub, and the Coast -- the only blue land the untap set will refresh -- is
// spent, so the Drake finds no {U}. Overgrowth first lands on the Coast and the loop runs.
GameState G163Board(int first_num)
{
    GameState s = BaseState(3, 0);
    int num = first_num;
    for (const char* nm : { "Aether Hub", "Peregrine Drake", "Overgrowth", "Emiel the Blessed",
                            "Cloud of Faeries", "Wild Growth" })
    { s.players[0].hand.push_back(PhCo(nm, num++)); }
    for (const char* nm : { "Eldrazi Displacer", "Aether Hub", "Fertile Ground", "Shivan Gorge", "Living Wish",
                            "Mariposa Military Base", "Emiel the Blessed", "Conservatory", "Wild Growth", "Wild Growth",
                            "Living Wish", "Trace of Abundance", "Eldrazi Displacer", "Brushland", "Living Wish",
                            "Trace of Abundance", "Cloud of Faeries", "Trace of Abundance", "Adarkar Wastes",
                            "Overgrowth", "Fertile Ground", "Brushland", "Aether Hub", "Peregrine Drake",
                            "Mariposa Military Base", "Fertile Ground", "Yavimaya Coast", "Overgrowth",
                            "Peregrine Drake", "Fertile Ground", "Peregrine Drake", "Trace of Abundance" })
    { s.players[0].library.push_back(PhCo(nm, num++)); }
    for (int i = 0; i < 8; ++i) { s.players[0].library.push_back(PhCo("Brushland", num++)); }
    for (const char* nm : { "Azorius Chancery", "Adarkar Wastes", "Cloud of Faeries", "Eldrazi Displacer",
                            "Mariposa Military Base", "Vexing Shusher", "Essence Depleter", "Dimensional Infiltrator" })
    { s.players[0].sideboard.push_back(PhCo(nm, num++)); }
    for (int i = 0; i < 50; ++i) { s.players[1].library.push_back(PhCo("Brushland", num++)); }
    s.opponent_library_dealt = true;
    s.players[0].life = 19;
    const int cons = PutCo(s, "Conservatory", num++);
    PutCo(s, "Wild Growth", num++, cons);
    PutCo(s, "Yavimaya Coast", num++);
    return s;
}
int TrialPerms(const GameState& s, int arm)
{
    ArmGuard g(heurarm::EDF_ROUTE_AURA_PERMS, arm);
    return EdfComboRouteTrial(s, 0);
}
}   // namespace

TEST_CASE("EDF route (part 4b): another Aura order is tried when the USER's order kills nothing")
{
    CHECK_MESSAGE(TrialPerms(G163Board(5000), 0) != 1, "precondition: the USER's Aura order alone does not kill");
    CHECK(TrialPerms(G163Board(6000), 1) == 1);   // WIN (Overgrowth first)
    {
        ArmGuard g(heurarm::EDF_ROUTE_AURA_ORDER, 0);   // the pre-part-4 order (hand order here) kills too
        CHECK(TrialPerms(G163Board(7000), 0) == 1);
    }
}

// ---- the PROPOSED tutor order (default OFF) -------------------------------------------------------

TEST_CASE("EDF order (proposal): MTG_EDF_TUTOR_LIBRARY_FIRST puts Eladamri's Call before Living Wish")
{
    GameState s = BaseState(4);
    const Action wish = CastCo(s, "Living Wish", 31);
    const Action call = CastCo(s, "Eladamri's Call", 17);
    CHECK(Realised(s, { call, wish }) == std::vector<std::string>{ "Living Wish", "Eladamri's Call" });   // USER order
    ArmGuard g(heurarm::EDF_TUTOR_LIBRARY_FIRST, 1);
    CHECK(Realised(s, { wish, call }) == std::vector<std::string>{ "Eladamri's Call", "Living Wish" });
}
