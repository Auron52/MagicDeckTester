// Unpredictable Cyclone: the Remand ceiling follow-up (USER 2026-10-28).
//
//   * REMAND-ON-REMAND (Action::remand_chain_by; DecisionProvider::AdmitRemandOnRemand): Remand A targets any
//     spell X, Remand B targets A. B resolves first (A back to hand, B draws), then X resolves normally.
//     CycloneProvider admits it when the hand-cast ceiling reaches Remand's own MV (2). A's target carries no
//     ceiling and no free-cast prune (A is countered, X resolves). Hand casts AND the Cyclone free cast
//     (kCycFcTakeRemandChain), executor and rollout in lockstep (the shared CastAndResolveRemandChain).
//   * A REMAND-ONLY INERT TARGET (DecisionProvider::AdmitRemandOnlyCast): a goldfish_inert card (Toxic Deluge)
//     may be cast only with a Remand riding it, admitted under the ceiling behind MTG_CYCLONE_REMAND_INERT.
//     Never a bare cast. Keyed on goldfish_inert, never a name.
//   * Ceilings -1 and 1 offer neither (the shipped play is untouched).
#include <doctest/doctest.h>

#include "ai/AIEngine.h"
#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "ai/ValueArm.h"
#include "cards/CardDatabase.h"
#include "core/EffectHandler.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"
#include "mtg_test_seam.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

constexpr const char* kRemand  = "Remand";
constexpr const char* kDeluge  = "Toxic Deluge";
constexpr const char* kSong    = "Seething Song";
constexpr const char* kCyclone = "Unpredictable Cyclone";
constexpr const char* kCurator = "Curator of Mysteries";
constexpr const char* kWraith  = "Street Wraith";
constexpr const char* kHunger  = "Ulamog, the Ceaseless Hunger";

void EnsureCardsRC()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& DefRC(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

Card CardRC(const std::string& name, int number)
{
    Card c     = DefRC(name).card;
    c.m_number = number;
    return c;
}

void PutRC(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = CardRC(name, number);
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

const CycloneProvider& ProvRC() { static const CycloneProvider cp; return cp; }

// Per-job arms, restored on scope exit.
struct ArmScope
{
    ArmScope(int ceiling, int inert)
    {
        valuearm::t_arm.cyclone_remand_max_mv = ceiling;
        valuearm::t_arm.cyclone_remand_inert  = inert;
    }
    ~ArmScope()
    {
        valuearm::t_arm.cyclone_remand_max_mv = valuearm::Arm::kCycloneRemandUnset;
        valuearm::t_arm.cyclone_remand_inert  = -1;
    }
};

// Round 4, land drop made, the given untapped lands, `top` over 20 Islands, the Cyclone deck's stamps.
GameState BaseRC(const std::vector<std::string>& lands, const std::vector<std::string>& hand,
                 const std::vector<std::string>& top = {})
{
    GameState s;
    s.m_provider          = &ProvRC();
    s.active_player_index = 0;
    s.turn_number         = 4;
    s.on_the_play         = true;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    s.deck_has_extra_turn = true;
    s.deck_has_gy_shuffle = true;
    s.deck_has_annihilator = true;
    s.deck_has_cast_target_trigger = true;
    s.deck_has_cycle_draw_replacement = true;
    s.deck_has_discard_scry_watcher = true;
    int num = 40;
    for (const std::string& l : lands) { PutRC(s, l, num++); }
    num = 100;
    for (const std::string& h : hand) { s.players[0].hand.push_back(CardRC(h, num++)); }
    num = 500;
    for (const std::string& t : top) { s.players[0].library.push_back(CardRC(t, num++)); }
    for (int k = 0; k < 20; ++k) { s.players[0].library.push_back(CardRC("Island", 900 + k)); }
    return s;
}

void ResolveAllRC(GameState& st)
{
    while (!st.stack.empty())
    {
        const StackEntry e = st.stack.back();
        st.stack.pop_back();
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.source);
        if (d != nullptr) { EffectHandler::Resolve(st, e, *d); }
    }
}

std::vector<std::string> NamesRC(const std::vector<Card>& cards)
{
    std::vector<std::string> v;
    for (const Card& c : cards) { v.push_back(c.m_name.str()); }
    std::sort(v.begin(), v.end());
    return v;
}

bool OnBfRC(const GameState& s, const std::string& name)
{
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { return true; } }
    return false;
}

struct OutRC
{
    std::vector<std::string> hand, graveyard;
    std::vector<int> lib;
    int spells = 0, drawn = 0, life = 0, tapped = 0;
    bool x_on_bf = false;
};

OutRC OfRC(const GameState& s, const std::string& x)
{
    OutRC o;
    o.hand      = NamesRC(s.players[0].hand);
    o.graveyard = NamesRC(s.players[0].graveyard);
    for (const Card& c : s.players[0].library) { o.lib.push_back(c.m_number); }
    o.spells    = s.spells_cast_this_turn;
    o.drawn     = s.players[0].cards_drawn_this_turn;
    o.life      = s.players[0].life;
    for (const Permanent& p : s.battlefield) { if (p.card.IsLand() && p.tapped) { ++o.tapped; } }
    o.x_on_bf   = OnBfRC(s, x);
    return o;
}

void CheckSameRC(const OutRC& r, const OutRC& e)
{
    CHECK(r.hand == e.hand);
    CHECK(r.graveyard == e.graveyard);
    CHECK(r.lib == e.lib);
    CHECK(r.spells == e.spells);
    CHECK(r.drawn == e.drawn);
    CHECK(r.life == e.life);
    CHECK(r.tapped == e.tapped);
    CHECK(r.x_on_bf == e.x_on_bf);
}

std::pair<OutRC, OutRC> BothWorldsRC(const GameState& s, const TurnSolver::Plan& plan, const std::string& x)
{
    GameState roll = s;
    TurnSolver::ApplyPlan(roll, plan, /*is_pre_combat=*/true);
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
    MtgTestSeam::CommitPhasePlan(eng, plan, true);
    GameState exec = s;
    eng.TakeTurn(exec, true, ResolveAllRC);
    return { OfRC(roll, x), OfRC(exec, x) };
}

// The one-cast plan whose only action is a remand variant of `x` (`chain` = a Remand-on-Remand one).
const TurnSolver::Plan* OnlyRemandOf(const std::vector<TurnSolver::Plan>& plans, const std::string& x, bool chain)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() != 1) { continue; }
        const Action& a = p.actions[0];
        if (a.kind != Action::Kind::CastFromHand || a.card_name.str() != x || a.remand_by.empty()) { continue; }
        if (a.remand_chain_by.empty() == chain) { continue; }
        return &p;
    }
    return nullptr;
}

int CountActs(const std::vector<Action>& acts, const std::string& x, bool chain, bool any_remand = true)
{
    int n = 0;
    for (const Action& a : acts)
    {
        if (a.kind != Action::Kind::CastFromHand || a.card_name.str() != x) { continue; }
        if (!any_remand) { if (a.remand_by.empty()) { ++n; } continue; }
        if (a.remand_by.empty()) { continue; }
        if (!a.remand_chain_by.empty() == chain) { ++n; }
    }
    return n;
}

}   // namespace

TEST_CASE("Cyclone remand ceiling: the provider gates (Remand-on-Remand at >= 2, an inert target at >= 3 + switch)")
{
    EnsureCardsRC();
    GameState s = BaseRC({}, {});
    {
        ArmScope a(1, -1);
        CHECK_FALSE(ProvRC().AdmitRemandOnRemand(s, 0, DefRC(kRemand)));
        CHECK_FALSE(ProvRC().AdmitRemandOnlyCast(s, 0, DefRC(kDeluge)));
    }
    {
        ArmScope a(-1, 1);
        CHECK_FALSE(ProvRC().AdmitRemandOnRemand(s, 0, DefRC(kRemand)));
        CHECK_FALSE(ProvRC().AdmitRemandOnlyCast(s, 0, DefRC(kDeluge)));
    }
    {
        ArmScope a(2, 1);
        CHECK(ProvRC().AdmitRemandOnRemand(s, 0, DefRC(kRemand)));
        CHECK_FALSE(ProvRC().AdmitRemandOnlyCast(s, 0, DefRC(kDeluge)));   // MV 3 > 2
    }
    {
        ArmScope a(3, 0);
        CHECK(ProvRC().AdmitRemandOnRemand(s, 0, DefRC(kRemand)));
        CHECK_FALSE(ProvRC().AdmitRemandOnlyCast(s, 0, DefRC(kDeluge)));   // the switch is off
    }
    {
        ArmScope a(3, 1);
        CHECK(ProvRC().AdmitRemandOnlyCast(s, 0, DefRC(kDeluge)));
        CHECK_FALSE(ProvRC().AdmitRemandOnlyCast(s, 0, DefRC("Ponder")));  // not inert: never a remand-only cast
    }
    // The generic default: no other deck is opted in.
    const GenericProvider base;
    CHECK_FALSE(base.AdmitRemandOnRemand(s, 0, DefRC(kRemand)));
    CHECK_FALSE(base.AdmitRemandOnlyCast(s, 0, DefRC(kDeluge)));
}

TEST_CASE("Cyclone remand ceiling: the enumeration -- chains only at >= 2 (any target), the inert target only at 3 + switch")
{
    EnsureCardsRC();
    heurarm::Clear();
    const std::vector<std::string> lands = { "Island", "Island", "Island", "Island", "Island", "Swamp", "Mountain", "Mountain" };
    const std::vector<std::string> hand  = { "Ponder", kSong, kDeluge, kRemand, kRemand };
    {
        ArmScope a(1, 1);
        GameState s = BaseRC(lands, hand);
        const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
        CHECK(CountActs(acts, "Ponder", /*chain=*/false) > 0);   // the shipped MV-1 rider
        CHECK(CountActs(acts, "Ponder", /*chain=*/true) == 0);
        CHECK(CountActs(acts, kSong, /*chain=*/true) == 0);
        CHECK(CountActs(acts, kDeluge, false) == 0);
        CHECK(CountActs(acts, kDeluge, false, /*any_remand=*/false) == 0);   // never a bare inert cast
    }
    {
        ArmScope a(-1, 1);
        GameState s = BaseRC(lands, hand);
        const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
        for (const Action& x : acts) { CHECK(x.remand_by.empty()); }
    }
    {
        ArmScope a(2, 1);
        GameState s = BaseRC(lands, hand);
        const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
        CHECK(CountActs(acts, "Ponder", true) > 0);     // a plain cantrip X
        CHECK(CountActs(acts, kSong, true) > 0);        // MV 3: A's target has no ceiling
        CHECK(CountActs(acts, kSong, false) == 0);      // ...but a single Remand on it still does
        CHECK(CountActs(acts, kDeluge, true) == 0);     // an inert card never resolves: never a chain target
        CHECK(CountActs(acts, kDeluge, false) == 0);    // MV 3 > 2
        for (const Action& x : acts)
        {
            if (!x.remand_chain_by.empty())
            {
                CHECK(x.remand_by.str() == kRemand);
                CHECK(x.remand_chain_by.str() == kRemand);
                const ManaCost xc = DefRC(x.card_name.str()).card.m_mana_cost;
                CHECK(x.cost.ManaValue() == xc.ManaValue() + 4);   // X + {1}{U} + {1}{U}
            }
        }
    }
    {
        ArmScope a(3, 0);
        GameState s = BaseRC(lands, hand);
        const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
        CHECK(CountActs(acts, kSong, false) > 0);       // MV 3 single rider at ceiling 3
        CHECK(CountActs(acts, kDeluge, false) == 0);    // "3 without Deluge"
    }
    {
        ArmScope a(3, 1);
        GameState s = BaseRC(lands, hand);
        const std::vector<Action> acts = TurnSolver::CollectActionsForTest(s, true);
        CHECK(CountActs(acts, kDeluge, false) > 0);     // "3 with Deluge": cast it, Remand it
        CHECK(CountActs(acts, kDeluge, true) == 0);
        CHECK(CountActs(acts, kDeluge, false, /*any_remand=*/false) == 0);   // still never a bare cast
        for (const Action& x : acts)
        {
            if (x.card_name.str() == kDeluge) { CHECK(x.cost.ManaValue() == 5); }   // {2}{B} + {1}{U}; X = 0 life
        }
    }
}

TEST_CASE("Cyclone remand ceiling: Remand-on-Remand on a hand-cast Seething Song -- it resolves, A back, B draws (both worlds)")
{
    EnsureCardsRC();
    heurarm::Clear();
    ArmScope a(2, -1);
    GameState s = BaseRC({ "Island", "Island", "Island", "Island", "Mountain", "Mountain", "Mountain" },
                         { kSong, kRemand, kRemand }, { kWraith });
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = OnlyRemandOf(plans, kSong, /*chain=*/true);
    REQUIRE(p != nullptr);
    const auto [r, e] = BothWorldsRC(s, *p, kSong);
    CHECK(r.hand == std::vector<std::string>{ kRemand, kWraith });   // A back, B drew the Wraith
    CHECK(r.graveyard == std::vector<std::string>{ kRemand, kSong });  // B; the Song RESOLVED
    CHECK(r.spells == 3);
    CHECK(r.drawn == 1);
    CheckSameRC(r, e);
}

TEST_CASE("Cyclone remand ceiling: Remand-on-Remand on Ponder -- B draws first, then Ponder resolves (both worlds)")
{
    EnsureCardsRC();
    heurarm::Clear();
    ArmScope a(2, -1);
    GameState s = BaseRC({ "Island", "Island", "Island", "Island", "Island" },
                         { "Ponder", kRemand, kRemand }, { kWraith, kCurator, "Mountain", "Swamp" });
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = OnlyRemandOf(plans, "Ponder", /*chain=*/true);
    REQUIRE(p != nullptr);
    const auto [r, e] = BothWorldsRC(s, *p, "Ponder");
    CHECK(r.drawn == 2);                                  // B's draw, then Ponder's
    CHECK(r.spells == 3);
    CHECK(std::count(r.hand.begin(), r.hand.end(), std::string(kWraith)) == 1);   // B drew the old top
    CHECK(std::count(r.hand.begin(), r.hand.end(), std::string(kRemand)) == 1);   // A back
    CHECK(r.graveyard == std::vector<std::string>{ "Ponder", kRemand });
    CheckSameRC(r, e);
}

TEST_CASE("Cyclone remand ceiling: a void chain (only one Remand left) casts X plainly in both worlds")
{
    EnsureCardsRC();
    heurarm::Clear();
    ArmScope a(2, -1);
    GameState s = BaseRC({ "Island", "Island", "Island", "Island", "Mountain", "Mountain", "Mountain" },
                         { kSong, kRemand, kRemand }, { kWraith });
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = OnlyRemandOf(plans, kSong, /*chain=*/true);
    REQUIRE(p != nullptr);
    GameState s1 = s;
    s1.players[0].hand.pop_back();   // one Remand gone before the cast
    const auto [r, e] = BothWorldsRC(s1, *p, kSong);
    CHECK(r.hand == std::vector<std::string>{ kRemand });
    CHECK(r.graveyard == std::vector<std::string>{ kSong });
    CHECK(r.spells == 1);
    CHECK(r.drawn == 0);
    CheckSameRC(r, e);
}

TEST_CASE("Cyclone remand ceiling: Toxic Deluge as a Remand-only target -- countered to hand, no life paid (both worlds)")
{
    EnsureCardsRC();
    heurarm::Clear();
    ArmScope a(3, 1);
    GameState s = BaseRC({ "Island", "Island", "Swamp", "Island", "Island" }, { kDeluge, kRemand }, { kWraith });
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, true);
    const TurnSolver::Plan* p = OnlyRemandOf(plans, kDeluge, /*chain=*/false);
    REQUIRE(p != nullptr);
    const auto [r, e] = BothWorldsRC(s, *p, kDeluge);
    CHECK(r.hand == std::vector<std::string>{ kWraith, kDeluge });
    CHECK(r.graveyard == std::vector<std::string>{ kRemand });
    CHECK(r.life == 20);
    CHECK(r.spells == 2);
    CHECK(r.drawn == 1);
    CHECK(r.tapped == 5);
    CheckSameRC(r, e);
    // No plan ever casts the Deluge without the rider.
    for (const TurnSolver::Plan& q : plans)
    {
        for (const Action& x : q.actions)
        { if (x.card_name.str() == kDeluge && x.kind == Action::Kind::CastFromHand) { CHECK_FALSE(x.remand_by.empty()); } }
    }
}

namespace
{
TurnSolver::Plan SeedPlanRC(const std::string& name, int fc)
{
    TurnSolver::Plan p;
    p.land_decided = true;
    Action a;
    a.kind          = Action::Kind::DigDraw;
    a.dig_sacrifice = false;
    a.card_name     = name;
    a.def           = &DefRC(name);
    a.hand_index    = -1;
    a.devour_count  = -1;
    a.ponder_keep   = fc;
    p.actions.push_back(a);
    return p;
}
}   // namespace

TEST_CASE("Cyclone remand ceiling: take + Remand-on-Remand on a free-cast hit -- the hit resolves, A back, B draws (both worlds)")
{
    EnsureCardsRC();
    heurarm::Clear();
    ArmScope arm(2, -1);
    for (const char* hit : { kCurator, kHunger })   // Ulamog: the single free-cast Remand is pruned, the chain is not
    {
        auto make = [&]
        {
            GameState s = BaseRC({ "Island", "Island", "Island", "Island" }, { kWraith, kRemand, kRemand },
                                 { hit, "Island", "Island" });
            PutRC(s, kCyclone, 30);
            return s;
        };
        GameState ex = make(), ro = make();
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::PerformDigFull(eng, ex, kWraith, false, -1, kCycFcTakeRemandChain,
                                    [](GameState& g) { ResolveAllRC(g); });
        ResolveAllRC(ex);
        TurnSolver::ApplyPlan(ro, SeedPlanRC(kWraith, kCycFcTakeRemandChain), /*is_pre_combat=*/true);
        CHECK(OnBfRC(ex, hit));                                          // the hit resolved
        CHECK(std::count_if(ex.players[0].hand.begin(), ex.players[0].hand.end(),
                            [](const Card& c) { return c.m_name.str() == kRemand; }) == 1);   // A back
        CHECK(ex.players[0].cards_drawn_this_turn == 1);                 // B's draw
        CHECK(ex.spells_cast_this_turn == 3);                            // hit + A + B
        CheckSameRC(OfRC(ro, hit), OfRC(ex, hit));
    }
    {   // Ceiling 1: the chain pin degrades to a plain take (no Remand cast) in both worlds.
        ArmScope a1(1, -1);
        auto make = []
        {
            GameState s = BaseRC({ "Island", "Island", "Island", "Island" }, { kWraith, kRemand, kRemand },
                                 { kCurator, "Island" });
            PutRC(s, kCyclone, 30);
            return s;
        };
        GameState ex = make(), ro = make();
        AIEngine eng(MulliganProfile::DefaultProfile(), 3, 20);
        MtgTestSeam::PerformDigFull(eng, ex, kWraith, false, -1, kCycFcTakeRemandChain,
                                    [](GameState& g) { ResolveAllRC(g); });
        ResolveAllRC(ex);
        TurnSolver::ApplyPlan(ro, SeedPlanRC(kWraith, kCycFcTakeRemandChain), true);
        CHECK(ex.spells_cast_this_turn == 1);
        CheckSameRC(OfRC(ro, kCurator), OfRC(ex, kCurator));
    }
}

TEST_CASE("Cyclone remand ceiling: the cycle seed carries the chain pin only at >= 2 with two Remands")
{
    EnsureCardsRC();
    heurarm::Clear();
    auto chains = [](int ceiling, int remands)
    {
        ArmScope a(ceiling, -1);
        std::vector<std::string> hand = { kWraith };
        for (int k = 0; k < remands; ++k) { hand.push_back(kRemand); }
        GameState s = BaseRC({ "Island", "Island", "Island", "Island" }, hand, { kHunger, kCurator });
        PutRC(s, kCyclone, 30);
        int n = 0;
        for (const Action& x : TurnSolver::CollectActionsForTest(s, true))
        { if (IsCycleSeed(x) && x.ponder_keep == kCycFcTakeRemandChain) { ++n; } }
        return n;
    };
    CHECK(chains(1, 2) == 0);
    CHECK(chains(-1, 2) == 0);
    CHECK(chains(2, 1) == 0);
    CHECK(chains(2, 2) > 0);
}
