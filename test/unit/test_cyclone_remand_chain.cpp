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


TEST_CASE("Cyclone cycle traits: rollout and executor preserve identical physical sources for free-hit Remand")
{
    // Ordinary fd-diverge s107008 gi8, normal d3 b10: the extra T4 cycles into a Curator
    // and Remands it. A trait-less rollout paid generic from Mountain, preserving U for
    // another cycle; the executor's live mid-turn traits preserved Mountain instead. Compare
    // source identities, since both payments tap three lands and leave all zones identical.
    EnsureCardsRC();
    heurarm::Clear();
    ArmScope arm(2, -1);
    GameState initial = BaseRC({ "Steam Vents", "Island", "Watery Grave", "Mountain" },
                               { kCurator, kDeluge, kRemand },
                               { "Brainsurge", kCurator, kCurator, "Ponder", "Emrakul, the Aeons Torn" });
    PutRC(initial, kCyclone, 30);
    TurnSolver::Plan plan = SeedPlanRC(kCurator, kCycFcTakeRemand);
    // Isolate the paid cycle/free-cast/Remand itself. No later search is allowed to obscure the
    // first divergent payment by choosing a different continuation or consuming more sources.
    plan.bp_choice = TurnSolver::kBpEmptyChoice;
    plan.bp_all = true;
    GameState roll = initial;
    TurnSolver::ApplyPlan(roll, plan, true);

    GameState exec = initial;
    AIEngine eng(MulliganProfile::DefaultProfile(), 3, 10);
    PlanTraits traits = TurnSolver::ComputePlanTraits(exec, plan.actions);
    REQUIRE(traits.cycle_seed_def == &DefRC(kCurator));
    {
        // TakeTurn installs precisely these pending-seed traits before dispatching its DigDraw.
        PlanTraitsScope scope(&traits);
        const AIEngine::DigOutcome outcome = MtgTestSeam::PerformDigFull(
            eng, exec, kCurator, false, -1, kCycFcTakeRemand, [](GameState& s) { ResolveAllRC(s); });
        REQUIRE(outcome != AIEngine::DigOutcome::Failed);
    }
    ResolveAllRC(exec);
    CHECK(std::count_if(exec.players[0].hand.begin(), exec.players[0].hand.end(),
                       [](const Card& c) { return c.m_name.str() == kCurator; }) == 2);
    CHECK_FALSE(OnBfRC(exec, kCurator));   // the free hit was actually countered back to hand
    CHECK(exec.spells_cast_this_turn == 2);
    CheckSameRC(OfRC(roll, kCurator), OfRC(exec, kCurator));
    std::vector<std::pair<int, bool>> roll_sources, exec_sources;
    for (const Permanent& p : roll.battlefield)
    { if (p.card.IsLand()) { roll_sources.emplace_back(p.card.m_number, p.tapped); } }
    for (const Permanent& p : exec.battlefield)
    { if (p.card.IsLand()) { exec_sources.emplace_back(p.card.m_number, p.tapped); } }
    // Equal counts of three tapped lands hide the bug: WHICH blue provider survives is decisive.
    REQUIRE(roll_sources.size() == exec_sources.size());
    for (std::size_t i = 0; i < roll_sources.size(); ++i)
    {
        INFO("source number ", roll_sources[i].first);
        CHECK(roll_sources[i].first == exec_sources[i].first);
        CHECK(roll_sources[i].second == exec_sources[i].second);
    }
}


TEST_CASE("Cyclone paid seed: new nested seed holds and other pending activation holds survive")
{
    EnsureCardsRC();
    struct RestoreArms
    {
        heurarm::Arm saved = heurarm::t_arm;
        ~RestoreArms() { heurarm::t_arm = saved; }
    } restore;
    heurarm::Clear();
    heurarm::t_arm[heurarm::ACT_LINE_HOLD] = 1;
    ActLinePassScope activation_pass;
    GameState state = BaseRC({ "Island", "Forest", "Wirewood Lodge", "Mountain" }, { kCurator });
    PutRC(state, "Llanowar Elves", 70);
    state.battlefield.back().tapped = true;   // a legal target for the pending Lodge untap
    const TurnSolver::Plan seed = SeedPlanRC(kCurator, kCycFcTake);
    PlanTraits traits = TurnSolver::ComputePlanTraits(state, seed.actions);
    REQUIRE(traits.cycle_seed_def == &DefRC(kCurator));
    // An unrelated pending Lodge activation still owes {G} and its own {T}.
    traits.act_pips[4] = 1;
    traits.act_src_count = 1;
    traits.act_src_nums[0] = state.battlefield[2].card.m_number;
    const PlanTraitsScope original(&traits);
    ManaCost one;
    one.generic = 1;
    ManaCost blue;
    blue.blue = 1;
    // Pay the original seed from float, so all four sources remain available for
    // the later payment whose reservations this regression observes.
    state.floating_mana.blue = 1;
    {
        const CycleSeedPayScope paying;
        REQUIRE(TapForCostDirect(state, blue, false));
    }
    state.players[0].graveyard.push_back(state.players[0].hand.front());
    state.players[0].hand.erase(state.players[0].hand.begin());
    state.players[0].hand.push_back(CardRC(kCurator, 200));   // the same-name free hit
    const GameState fresh = state;
    constexpr std::uint64_t island = 1ull << 0;
    constexpr std::uint64_t other_holds = (1ull << 1) | (1ull << 2);
    CHECK(ActLineHoldMask(state, one) == (island | other_holds));
    {
        const CycleSeedResolvedScope paid(DefRC(kCurator));
        CHECK(ActLineHoldMask(state, one) == other_holds);
        {
            // A NEW continuation chooses the newly revealed Curator as its seed.
            // Sharing its name with the paid seed must not disable this fresh hold.
            PlanTraits next = traits;
            const PlanTraitsScope next_scope(&next);
            CHECK(ActLineHoldMask(state, one) == (island | other_holds));
            REQUIRE(TapForCostDirect(state, one, false));
            CHECK(state.battlefield[3].tapped);   // only Mountain was unreserved
            CHECK_FALSE(state.battlefield[0].tapped);
        }
        CHECK(ActLineHoldMask(state, one) == other_holds);
        REQUIRE(TapForCostDirect(state, one, false));
        CHECK(state.battlefield[0].tapped);       // paid seed's Island is usable now
        CHECK_FALSE(state.battlefield[1].tapped); // pending activation's {G} survives
        CHECK_FALSE(state.battlefield[2].tapped); // pending activation's {T} survives
    }
    // Observe restoration through the payer's mask, using an untouched board.
    CHECK(ActLineHoldMask(fresh, one) == (island | other_holds));
}


TEST_CASE("Cyclone root seed: a resumed cantrip prefix preserves the root payer traits")
{
    EnsureCardsRC();
    heurarm::Clear();
    ArmScope arm(2, -1);
    GameState initial = BaseRC({ "Island", "Steam Vents", "Island", "Watery Grave", "Mountain" },
                               { "Gitaxian Probe", kCurator, kRemand },
                               { "Island", "Brainsurge", kCurator, kCurator, "Ponder" });
    PutRC(initial, kCyclone, 30);
    TurnSolver::Plan plan = SeedPlanRC(kCurator, kCycFcTakeRemand);
    Action probe;
    probe.kind = Action::Kind::CastFromHand;
    probe.card_name = "Gitaxian Probe";
    probe.def = &DefRC("Gitaxian Probe");
    probe.cost = EffectiveSpellCost(*probe.def, initial, 1);
    probe.card_mv = probe.def->card.m_mana_cost.ManaValue();
    plan.actions.insert(plan.actions.begin(), probe);
    plan.bp_all = true;   // the resumed EMPTY child also leaves later cycle/cantrip sites empty
    ManaCost capture_hold, resume_hold;
    GameState complete, resumed;
    REQUIRE(TurnSolver::BpResumeLineHoldForTest(initial, plan, true, capture_hold, resume_hold,
                                               &complete, &resumed));
    REQUIRE(complete.battlefield.size() == resumed.battlefield.size());
    for (std::size_t i = 0; i < complete.battlefield.size(); ++i)
    {
        CHECK(complete.battlefield[i].card.m_number == resumed.battlefield[i].card.m_number);
        CHECK(complete.battlefield[i].tapped == resumed.battlefield[i].tapped);
    }
    // Probe, cycle and Remand spend four mana. Under the executor's live mid-turn
    // traits the scarce-red rank keeps Mountain; an absent resumed trait scope
    // spends Mountain for Remand's generic pip and incorrectly leaves a blue source.
    for (std::size_t i = 0; i < 4; ++i)
    {
        CHECK(complete.battlefield[i].tapped);
        CHECK(resumed.battlefield[i].tapped);
    }
    CHECK_FALSE(complete.battlefield[4].tapped);
    CHECK_FALSE(resumed.battlefield[4].tapped);
    CHECK(NamesRC(complete.players[0].hand) == NamesRC(resumed.players[0].hand));
    CHECK(NamesRC(complete.players[0].graveyard) == NamesRC(resumed.players[0].graveyard));
}


TEST_CASE("Cyclone site 4: a continuation protects its own colored cycle seed while its ritual pays")
{
    EnsureCardsRC();
    heurarm::Clear();
    GameState initial = BaseRC({ "Island", "Island", "Mountain", "Mountain" },
                               { kWraith, kSong, kCurator }, { kWraith, kCurator });
    PutRC(initial, kCyclone, 30);
    // First reach the exact site-4 board, leaving its continuation explicitly empty.
    // The root Wraith seed costs life, so its traits have no blue cycling reservation.
    TurnSolver::Plan prefix = SeedPlanRC(kWraith, kCycFcTake);
    prefix.bp_choice = TurnSolver::kBpEmptyChoice;
    prefix.bp_all = true;
    GameState reached = initial;
    TurnSolver::ApplyPlan(reached, prefix, true);
    REQUIRE(OnBfRC(reached, kWraith));
    const std::vector<TurnSolver::Plan> candidates = TurnSolver::EnumerateBreakpointPlans(reached, true);
    const auto chosen = std::find_if(candidates.begin(), candidates.end(), [](const TurnSolver::Plan& p)
    {
        if (p.actions.size() != 2 || p.bp_choice >= 0 || !p.land_to_play.empty()) { return false; }
        bool song = false, cycle = false;
        for (const Action& a : p.actions)
        {
            if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == kSong)
            { song = true; }
            else if (a.kind == Action::Kind::DigDraw && !a.dig_sacrifice
                     && a.card_name.str() == kCurator && a.ponder_keep == kCycFcTake)
            { cycle = true; }
            else { return false; }
        }
        return song && cycle;
    });
    REQUIRE(chosen != candidates.end());
    TurnSolver::Plan root = SeedPlanRC(kWraith, kCycFcTake);
    root.bp_at = 0;
    root.bp_choice = static_cast<int>(chosen - candidates.begin());
    GameState applied = initial;
    TurnSolver::ApplyPlan(applied, root, true);
    // Child traits reserve an Island while Song pays {2}{R}; that Island then
    // pays the child's Curator cycle. Inheriting only the root Wraith's traits
    // taps both Islands for Song and strands the selected Curator in hand.
    CHECK(OnBfRC(applied, kCurator));
    CHECK(std::count_if(applied.players[0].graveyard.begin(), applied.players[0].graveyard.end(),
                        [](const Card& c) { return c.m_name.str() == kCurator; }) == 1);
    CHECK(std::count_if(applied.players[0].graveyard.begin(), applied.players[0].graveyard.end(),
                        [](const Card& c) { return c.m_name.str() == kSong; }) == 1);
    CHECK(applied.spells_cast_this_turn == 3);   // free Wraith, Song, free Curator
    for (std::size_t i = 0; i < 4; ++i) { CHECK(applied.battlefield[i].tapped); }
}
