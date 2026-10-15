// Fluctuator: the USER's cast order, cycling placed last, and a land DRAWN by cycling considerable as
// the drop (USER 2026-10-14). docs/design/fluctuator-cast-order-2026-10-14.md.
//
//   ORDER  land > Unearth > Enlightened Tutor > Drannith Stinger > Fluctuator > Hollow One > a SECOND
//          Fluctuator > CYCLING, by params, names as the final tie-break -- TOTAL: permuting the hand
//          never changes the realised order (MTG_FLUCT_ORDER).
//   DIG    every cycle comes after every cast of its window (a Stinger cast with a cycle chain pings
//          for EVERY card cycled).
//   LAND   the open drop + a land in hand is counted (TurnSolver::DropOpenLandEnablesCast), the one
//          untapped land is cycled LAST while the drop is open (MTG_FLUCT_KEEP_UNTAPPED), and the
//          draft's §4.3 line -- Capital City drawn, then Drannith Stinger, the City dropped to cast
//          it -- is on the site-4 continuation list (MTG_FLUCT_DRAWN_LAND).
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

void EnsureCardsFo()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const FluctuatorProvider& FProv()
{
    static const FluctuatorProvider p;
    return p;
}

// Zone cards are NAME-ONLY placeholders in real play; build hand / library / graveyard that way.
Card FoPh(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}

void FoPut(GameState& s, const std::string& name, int number, bool tapped = false)
{
    Permanent p;
    p.card              = CardDatabase::Instance().Lookup(name)->card;
    p.card.m_number     = number;
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = false;
    p.tapped            = tapped;
    s.battlefield.push_back(p);
}

struct FoArms
{
    explicit FoArms(int order = -1, int drawn = -1, int keep = -1)
    {
        heurarm::Clear();
        if (order >= 0) { heurarm::t_arm[heurarm::FLUCT_ORDER]         = static_cast<std::int8_t>(order); }
        if (drawn >= 0) { heurarm::t_arm[heurarm::FLUCT_DRAWN_LAND]    = static_cast<std::int8_t>(drawn); }
        if (keep >= 0)  { heurarm::t_arm[heurarm::FLUCT_KEEP_UNTAPPED] = static_cast<std::int8_t>(keep); }
    }
    ~FoArms() { heurarm::Clear(); }
};

GameState FoState(const std::vector<std::string>& hand, const std::vector<std::string>& board,
                  int lands_played = 1, const std::vector<std::string>& library_top = {},
                  int library_filler = 20)
{
    EnsureCardsFo();
    GameState s;
    s.m_provider          = &FProv();
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    int num = 1;
    for (const std::string& n : hand) { s.players[0].hand.push_back(FoPh(n, num++)); }
    for (const std::string& n : library_top) { s.players[0].library.push_back(FoPh(n, 100 + num++)); }
    for (int k = 0; k < library_filler; ++k) { s.players[0].library.push_back(FoPh("Polluted Mire", 500 + k)); }
    int bnum = 300;
    for (const std::string& n : board) { FoPut(s, n, bnum++); }
    s.players[0].lands_played_this_turn = lands_played;
    return s;
}

Action Cast(const std::string& name)
{
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = name;
    a.def       = CardDatabase::Instance().Lookup(name);
    if (a.def) { a.cost = a.def->card.m_mana_cost; }
    return a;
}

// The realised order of `names` as every cast-order site computes it: the shared comparator, then
// the deck's set-level arrangement (ApplyDeckCastArrangement).
std::vector<std::string> Realised(const GameState& s, const std::vector<std::string>& names)
{
    std::vector<Action> acts;
    for (const std::string& n : names) { acts.push_back(Cast(n)); }
    std::vector<int> order;
    for (int i = 0; i < static_cast<int>(acts.size()); ++i) { order.push_back(i); }
    std::stable_sort(order.begin(), order.end(),
                     [&](int x, int y) { return CastOrderLess(s, acts[x], acts[y]); });
    ApplyDeckCastArrangement(s, acts, order);
    std::vector<std::string> out;
    for (int i : order) { out.push_back(acts[static_cast<std::size_t>(i)].card_name.str()); }
    return out;
}

const char* kU = "Unearth";
const char* kT = "Enlightened Tutor";
const char* kS = "Drannith Stinger";
const char* kF = "Fluctuator";
const char* kH = "Hollow One";

bool OnBf(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return true; } }
    return false;
}
bool InHandFo(const GameState& s, const std::string& name)
{
    for (const Card& c : s.players[0].hand) { if (c.m_name.str() == name) { return true; } }
    return false;
}

}   // namespace

// ---- PART 1: the order ------------------------------------------------------------------------------

TEST_CASE("Fluct order: the USER's total order, invariant under every permutation of the cast set")
{
    FoArms arms(1);
    const GameState s = FoState({}, { "Polluted Mire", "Smoldering Crater" });
    const std::vector<std::string> want = { kU, kT, kS, kF, kH, kF };   // the second Fluctuator after Hollow One
    std::vector<std::string> set = { kH, kF, kS, kT, kU, kF };
    std::sort(set.begin(), set.end());
    int perms = 0;
    do
    {
        CHECK(Realised(s, set) == want);
        ++perms;
    } while (std::next_permutation(set.begin(), set.end()));
    CHECK(perms == 360);   // 6! / 2! (two Fluctuators)
}

TEST_CASE("Fluct order: a Fluctuator already on the battlefield makes the cast one the SECOND (after Hollow One)")
{
    FoArms arms(1);
    const GameState s = FoState({}, { kF, "Polluted Mire", "Smoldering Crater" });
    CHECK(Realised(s, { kF, kH, kS }) == std::vector<std::string>{ kS, kH, kF });
    CHECK(Realised(s, { kH, kF, kS }) == std::vector<std::string>{ kS, kH, kF });
    // Ranks are distinct for different cards (no two names compare equal on rank alone).
    const DecisionProvider& p = FProv();
    const int ru = p.CastOrderRank(s, *CardDatabase::Instance().Lookup(kU));
    const int rt = p.CastOrderRank(s, *CardDatabase::Instance().Lookup(kT));
    const int rs = p.CastOrderRank(s, *CardDatabase::Instance().Lookup(kS));
    const int rh = p.CastOrderRank(s, *CardDatabase::Instance().Lookup(kH));
    const int rf = p.CastOrderRank(s, *CardDatabase::Instance().Lookup(kF));
    CHECK(ru < rt); CHECK(rt < rs); CHECK(rs < rh); CHECK(rh < rf);
}

TEST_CASE("Fluct order: MTG_FLUCT_ORDER=0 restores the generic tiers (creatures first)")
{
    FoArms arms(0);
    const GameState s = FoState({}, { "Polluted Mire", "Smoldering Crater" });
    const std::vector<std::string> got = Realised(s, { kU, kF, kS });
    // Generic: creature tier 10 before noncreature 20 -> the Stinger leads; the USER's order would
    // put Unearth first.
    CHECK(got.front() == kS);
    CHECK(FProv().MainPhaseOverride(s, *CardDatabase::Instance().Lookup(kS)) == std::nullopt);
}

TEST_CASE("Fluct order: single main -- every card is Main1 under the order")
{
    FoArms arms(1);
    const GameState s = FoState({}, {});
    for (const char* n : { kU, kT, kS, kF, kH })
    {
        const auto mp = FProv().MainPhaseOverride(s, *CardDatabase::Instance().Lookup(n));
        REQUIRE(mp.has_value());
        CHECK(*mp == DecisionProvider::MainPhase::Main1);
    }
}

// ---- PART 1: cycling placed last -----------------------------------------------------------------

TEST_CASE("Fluct dig: every cycle comes after every cast of its window (the Stinger pings for EVERY cycle)")
{
    FoArms arms(1, 1, 1);
    // Fluctuator out, R + 1 untapped; the hand is the Stinger plus pure fodder.
    GameState pre = FoState({ kS, "Forsake the Worldly", "Forsake the Worldly", "Forsake the Worldly" },
                            { kF, "Smoldering Crater", "Polluted Mire" },
                            /*lands_played=*/1, {}, /*library_filler=*/8);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(pre, /*is_pre_combat=*/true);
    const TurnSolver::Plan* stinger = nullptr;
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.bp_choice >= 0 || p.dig_choice == 0) { continue; }
        if (p.actions.size() == 1 && p.actions[0].kind == Action::Kind::CastFromHand
            && p.actions[0].card_name.str() == kS) { stinger = &p; break; }
    }
    REQUIRE(stinger != nullptr);
    GameState s = pre;
    TurnSolver::ApplyPlan(s, *stinger, /*is_pre_combat=*/true);
    CHECK(OnBf(s, kS));
    const int cycled = static_cast<int>(s.players[0].graveyard.size());
    CHECK(cycled >= 3);
    CHECK(s.players[1].life == 20 - cycled);   // a cycle before the cast would ping for nothing
}

// ---- PART 2: the open drop counted -----------------------------------------------------------------

TEST_CASE("Fluct drawn land: DropOpenLandEnablesCast -- untapped Capital City pays the Stinger, a tapped land never does")
{
    EnsureCardsFo();
    // Fluctuator out, no red: Polluted Mire (B) + Blasted Landscape (C), and the draft's own §4.3 pair
    // Polluted Mire + Irrigated Farmland (W/U). Payment is COLOUR-EXACT: the pool's dual `wild` would
    // read the Farmland pair as already paying {1}{R}.
    for (const char* second : { "Blasted Landscape", "Irrigated Farmland" })
    {
        INFO("second land: " << second);
        const GameState city = FoState({ kS, "Capital City" }, { kF, "Polluted Mire", second }, 0);
        CHECK(TurnSolver::DropOpenLandEnablesCast(city));
        const std::string cc = "Capital City";
        CHECK(TurnSolver::DropOpenLandEnablesCast(city, &cc));
        const std::string pm = "Polluted Mire";
        CHECK_FALSE(TurnSolver::DropOpenLandEnablesCast(city, &pm));   // not in hand
        // Drop already spent: nothing to consider.
        const GameState spent = FoState({ kS, "Capital City" }, { kF, "Polluted Mire", second }, 1);
        CHECK_FALSE(TurnSolver::DropOpenLandEnablesCast(spent));
        // A tapped-entering red land adds nothing THIS turn.
        const GameState tapped = FoState({ kS, "Smoldering Crater" }, { kF, "Polluted Mire", second }, 0);
        CHECK_FALSE(TurnSolver::DropOpenLandEnablesCast(tapped));
    }
    // Already castable without the drop: the land changes nothing.
    const GameState already = FoState({ kS, "Capital City" }, { kF, "Smoldering Crater", "Irrigated Farmland" }, 0);
    CHECK_FALSE(TurnSolver::DropOpenLandEnablesCast(already));
}

TEST_CASE("Fluct keep-untapped: the ONE untapped land is cycled LAST while the drop is open and no Stinger is out")
{
    FoArms arms(1, 1, 1);
    const DecisionProvider& p = FProv();
    bool sac = false;
    {   // drop open, no Stinger: a tapped Mire and Forsake go before the City
        GameState s = FoState({ "Capital City", "Polluted Mire", "Hollow One" }, { kF, "Irrigated Farmland" }, 0);
        CHECK(p.SelectDigSource(s, AvailableManaPool(s), sac) == "Polluted Mire");
        s.players[0].hand.erase(s.players[0].hand.begin() + 1);   // Mire cycled
        CHECK(p.SelectDigSource(s, AvailableManaPool(s), sac) == "Hollow One");
        s.players[0].hand.erase(s.players[0].hand.begin() + 1);   // Hollow One cycled
        CHECK(p.SelectDigSource(s, AvailableManaPool(s), sac) == "Capital City");   // ordered last, never refused
    }
    {   // the drop is spent: the City is ordinary land fodder again (rank 3, ahead of Hollow One)
        GameState s = FoState({ "Capital City", "Hollow One" }, { kF, "Irrigated Farmland" }, 1);
        CHECK(p.SelectDigSource(s, AvailableManaPool(s), sac) == "Capital City");
    }
    {   // a Stinger on the battlefield: "everything is still free game"
        GameState s = FoState({ "Capital City", "Hollow One" }, { kF, kS, "Irrigated Farmland" }, 0);
        CHECK(p.SelectDigSource(s, AvailableManaPool(s), sac) == "Capital City");
    }
    {   // =0: the old order (lands rank 3)
        FoArms off(1, 1, 0);
        GameState s = FoState({ "Capital City", "Hollow One" }, { kF, "Irrigated Farmland" }, 0);
        CHECK(p.SelectDigSource(s, AvailableManaPool(s), sac) == "Capital City");
    }
}

TEST_CASE("Fluct drawn land: the §4.3 line (drop the drawn Capital City, cast the drawn Stinger) is on the site-4 list")
{
    FoArms arms(1, 1, 1);
    // The state right after the chain drew Capital City and then Drannith Stinger, drop still open.
    const GameState s = FoState({ "Capital City", kS, "Forsake the Worldly" },
                                { kF, "Polluted Mire", "Irrigated Farmland" }, 0);
    const std::vector<TurnSolver::Plan> cands = TurnSolver::EnumerateBreakpointPlans(s, /*is_pre_combat=*/true);
    bool found = false;
    for (const TurnSolver::Plan& c : cands)
    {
        if (!c.land_decided || c.land_to_play != "Capital City") { continue; }
        for (const Action& a : c.actions)
        { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == kS) { found = true; } }
    }
    CHECK(found);
}

// Fluctuator out, Polluted Mire + Blasted Landscape untapped (no red, no dual), NO land in hand at
// plan time, so the plan cannot drop. The chain draws Capital City and Drannith Stinger in either
// order, then Mires. Hook OFF: no re-solve can see the drop -- the land draw never re-solves and the
// Stinger reads "uncastable" -- so the Stinger stays in hand (protected: no Unearth) and the turn deals
// nothing. Hook ON: the re-solve opens (at the Stinger draw when the City came first, at the City
// draw when it came second) and the base plan's continuation drops the City and casts the Stinger.
static void CheckDrawnLandLine(const std::vector<std::string>& top)
{
    const std::vector<std::string> hand = { "Forsake the Worldly", "Forsake the Worldly", "Forsake the Worldly" };
    const std::vector<std::string> board = { kF, "Polluted Mire", "Blasted Landscape" };
    for (int on : { 0, 1 })
    {
        FoArms arms(1, on, on);
        const GameState pre = FoState(hand, board, /*lands_played=*/0, top, /*library_filler=*/25);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(pre, true);
        const TurnSolver::Plan* base = nullptr;
        for (const TurnSolver::Plan& p : plans)
        {
            if (p.bp_choice >= 0 || p.dig_choice == 0 || !p.actions.empty()) { continue; }
            base = &p; break;
        }
        REQUIRE(base != nullptr);
        GameState s = pre;
        TurnSolver::ApplyPlan(s, *base, true);
        INFO("hook=" << on << " top=" << top[0]);
        if (on == 0)
        {
            CHECK_FALSE(OnBf(s, kS));
            CHECK(s.players[1].life == 20);
        }
        else
        {
            CHECK(OnBf(s, kS));
            CHECK(OnBf(s, "Capital City"));
            CHECK(s.players[1].life < 20);   // the rest of the chain pings
        }
    }
}

TEST_CASE("Fluct drawn land: City drawn first, then the Stinger -- the Stinger draw counts the open drop")
{
    CheckDrawnLandLine({ "Capital City", kS });
}

TEST_CASE("Fluct drawn land: the Stinger drawn first, then the City -- the City draw itself re-solves")
{
    CheckDrawnLandLine({ kS, "Capital City" });
}

TEST_CASE("Fluct drawn land: a drawn TAPPED land is offered as the drop at the chain's end (next turn's mana)")
{
    // No Fluctuator yet: cycling costs {2}. Three untapped lands, the drop open, no land in hand at plan
    // time; one {2} cycle of Forsake draws a tapped Smoldering Crater. Hook OFF: the drop is lost.
    // Hook ON: the chain-end re-solve offers it; the list's front (the base plan's continuation) plays it.
    const std::vector<std::string> hand = { "Forsake the Worldly", kS };
    const std::vector<std::string> board = { "Polluted Mire", "Blasted Landscape", "Irrigated Farmland" };
    for (int on : { 0, 1 })
    {
        FoArms arms(1, on, on);
        const GameState pre = FoState(hand, board, 0, { "Smoldering Crater" }, 10);
        // The plan that casts nothing and digs (dig_choice 1 = dig while affordable).
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(pre, true);
        const TurnSolver::Plan* dig = nullptr;
        for (const TurnSolver::Plan& p : plans)
        {
            if (p.bp_choice >= 0 || !p.actions.empty() || !p.land_to_play.empty()) { continue; }
            if (p.dig_choice == 1) { dig = &p; break; }
        }
        if (dig == nullptr) { WARN("no dig-while-affordable plan on this board"); continue; }
        GameState s = pre;
        TurnSolver::ApplyPlan(s, *dig, true);
        INFO("hook=" << on);
        const bool cycled = !InHandFo(s, "Forsake the Worldly");
        REQUIRE(cycled);
        if (on == 0) { CHECK(InHandFo(s, "Smoldering Crater")); CHECK(s.players[0].lands_played_this_turn == 0); }
        else         { CHECK(OnBf(s, "Smoldering Crater"));     CHECK(s.players[0].lands_played_this_turn == 1); }
    }
}
