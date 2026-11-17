// The land-Aura main-1 line (MTG_PO_LAND_AURA_DOMAIN) -- docs/design/land-aura-main1-line.md.
//
// Mode (b), the APPLY: Sea Creatures s8810001 gi64 T2. A plan {Utopia Sprawl -> a Forest, Worn Powerstone} is
// admitted by the enumeration (Sprawl first: another land pays its {G}, then the enchanted host taps for two),
// but CastOrderLess realises Powerstone first, which takes every land, so the Sprawl was dropped in both
// worlds. The payable-order decider re-orders only an order its domain admits, and a land Aura was not in it.
//
// Mode (a), the ENUMERATION: Sea Creatures s8820001 gi39 T2 {Utopia Sprawl, Overgrowth}. An aura-only subset
// reaches SubsetPayableSequential, which walks the rank order and then the payable-order decider's ENUMERATION
// site; with no land Aura in that domain the rank order (the 3-drop first) was the only order walked.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/ManaPayment.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameSetup.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <cstdint>
#include <string>
#include <vector>

namespace
{

void EnsureCardsLoaded()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

const CardDefinition& Def(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    return *d;
}

void Put(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card             = Def(name).card;
    p.card.m_number    = number;
    p.controller_index = 0;
    p.owner_index      = 0;
    s.battlefield.push_back(p);
}

void Hand(GameState& s, const std::string& name, int number)
{
    Card c = Def(name).card;
    c.m_number = number;
    s.players[0].hand.push_back(c);
}

constexpr int kForestA = 20;
constexpr int kForestB = 21;
constexpr int kIsland  = 22;

// Forest, Forest, Island untapped (three mana, land drop spent); `second` + Utopia Sprawl in hand. Four mana of
// casts on three lands: payable only Sprawl first, paid by the OTHER Forest, its host then tapping for two.
GameState Board(const std::string& second, const DecisionProvider* prov = nullptr)
{
    GameState s;
    if (prov != nullptr) { s.m_provider = prov; }
    s.active_player_index = 0;
    s.turn_number         = 3;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 1;
    s.deck_has_land_aura  = true;
    for (int k = 0; k < 20; ++k)
    {
        Card c = Def("Island").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    Put(s, "Forest", kForestA);
    Put(s, "Forest", kForestB);
    Put(s, "Island", kIsland);
    Hand(s, "Utopia Sprawl", 40);
    Hand(s, second, 41);
    return s;
}

struct ArmPin
{
    heurarm::Slot slot;
    std::int8_t   prev;
    ArmPin(heurarm::Slot s_, bool on) : slot(s_), prev(heurarm::t_arm[s_]) { heurarm::t_arm[s_] = on ? 1 : 0; }
    ~ArmPin() { heurarm::t_arm[slot] = prev; }
};

// The plan casting exactly {Utopia Sprawl -> a Forest, `second`} (Sprawl's colour `colour_x`, 0 = any), with the
// indices of the two casts.
const TurnSolver::Plan* FindPair(const std::vector<TurnSolver::Plan>& plans, const std::string& second,
                                 int& sprawl, int& other, int colour_x = 0)
{
    for (const TurnSolver::Plan& p : plans)
    {
        int sp = -1, ot = -1, casts = 0;
        for (int i = 0; i < static_cast<int>(p.actions.size()); ++i)
        {
            const Action& a = p.actions[static_cast<std::size_t>(i)];
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            ++casts;
            if (a.card_name.str() == "Utopia Sprawl"
                && (a.enchant_target == kForestA || a.enchant_target == kForestB)
                && (colour_x == 0 || a.chosen_x == colour_x)) { sp = i; }
            if (a.card_name.str() == second) { ot = i; }
        }
        if (sp >= 0 && ot >= 0 && casts == 2) { sprawl = sp; other = ot; return &p; }
    }
    return nullptr;
}

int CountOnBattlefield(const GameState& s, const std::string& name)
{
    int n = 0;
    for (const Permanent& p : s.battlefield) { if (p.card.m_name.str() == name) { ++n; } }
    return n;
}

}   // namespace

TEST_CASE("Land-Aura domain (b): {Worn Powerstone, Utopia Sprawl} is realised Sprawl first")
{
    EnsureCardsLoaded();
    const GameState s = Board("Worn Powerstone");
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
    int sp = -1, pw = -1;
    const TurnSolver::Plan* plan = FindPair(plans, "Worn Powerstone", sp, pw);
    REQUIRE(plan != nullptr);

    // The rank order (Powerstone first) does not pay; the decider moves the Sprawl ahead of it.
    std::vector<int> order = { pw, sp };
    ApplyPayableCastOrder(s, plan->actions, order);
    CHECK(order == std::vector<int>{ sp, pw });

    // The rollout's apply realises BOTH casts.
    {
        GameState t = s;
        TurnSolver::ApplyPlan(t, *plan, true);
        CHECK(CountOnBattlefield(t, "Utopia Sprawl") == 1);
        CHECK(CountOnBattlefield(t, "Worn Powerstone") == 1);
    }

    // The lever off: a land Aura does not admit the order, so it is kept as given (and the Aura dropped).
    ArmPin off(heurarm::PO_LAND_AURA_DOMAIN, false);
    std::vector<int> off_order = { pw, sp };
    ApplyPayableCastOrder(s, plan->actions, off_order);
    CHECK(off_order == std::vector<int>{ pw, sp });
}

namespace
{

constexpr int kPoolHost  = 2;    // Breeding Pool, carrying the T1 Utopia Sprawl (locked blue)
constexpr int kOldSprawl = 46;
constexpr int kForest9   = 9;    // this turn's land drop

// Sea Creatures s8820001 gi39, T2 main 1, as the game log has it: T1 Misty Rainforest fetched Breeding Pool #2
// and Utopia Sprawl went on it (blue); T2 played Forest #9. Untapped: the Pool (two mana: {G}/{U} + the
// Sprawl's {U}) and the Forest ({G}) -- three mana. Hand: a second Utopia Sprawl and Overgrowth ({2}{G}), four
// mana of casts. Payable ONLY Sprawl first: the Forest pays its {G}, it rides the Pool, and the Pool then taps
// for THREE ({G} + {U} + the new Sprawl's colour), which is Overgrowth. Overgrowth first spends all three.
GameState Board39(const DecisionProvider* prov)
{
    GameState s;
    s.m_provider          = prov;
    s.active_player_index = 0;
    s.turn_number         = 2;
    s.players[0].life     = gamesetup::StartingLife();
    s.players[1].life     = gamesetup::StartingLife();
    s.players[0].lands_played_this_turn = 0;   // the Forest is this turn's land drop, still in hand
    s.deck_has_land_aura  = true;
    for (int k = 0; k < 20; ++k)
    {
        Card c = Def("Island").card;
        c.m_number = 900 + k;
        s.players[0].library.push_back(c);
    }
    Put(s, "Breeding Pool", kPoolHost);
    Put(s, "Utopia Sprawl", kOldSprawl);
    for (Permanent& p : s.battlefield)
    {
        if (p.card.m_number != kOldSprawl) { continue; }
        p.aura_attached_to = kPoolHost;
        p.chosen_color     = static_cast<int8_t>(Color::Blue);
    }
    Hand(s, "Forest", kForest9);
    Hand(s, "Utopia Sprawl", 40);
    Hand(s, "Overgrowth", 41);
    return s;
}

}   // namespace

namespace
{

// The gi39 board AFTER the land drop (Forest #9 on the battlefield), as the enumeration's walk sees it.
GameState Board39Played(const DecisionProvider* prov)
{
    GameState s = Board39(prov);
    std::vector<Card>& hand = s.players[0].hand;
    for (std::size_t i = 0; i < hand.size(); ++i)
    {
        if (hand[i].m_number != kForest9) { continue; }
        hand.erase(hand.begin() + static_cast<std::ptrdiff_t>(i));
        break;
    }
    Put(s, "Forest", kForest9);
    s.players[0].lands_played_this_turn = 1;
    return s;
}

// The plan casting exactly {Utopia Sprawl -> sprawl_host, Overgrowth -> og_host}.
const TurnSolver::Plan* FindHosts(const std::vector<TurnSolver::Plan>& plans, int sprawl_host, int og_host,
                                  int& sprawl, int& og)
{
    for (const TurnSolver::Plan& p : plans)
    {
        int sp = -1, ot = -1, casts = 0;
        for (int i = 0; i < static_cast<int>(p.actions.size()); ++i)
        {
            const Action& a = p.actions[static_cast<std::size_t>(i)];
            if (a.kind != Action::Kind::CastFromHand) { continue; }
            ++casts;
            if (a.card_name.str() == "Utopia Sprawl" && a.enchant_target == sprawl_host) { sp = i; }
            if (a.card_name.str() == "Overgrowth" && a.enchant_target == og_host) { ot = i; }
        }
        if (sp >= 0 && ot >= 0 && casts == 2) { sprawl = sp; og = ot; return &p; }
    }
    return nullptr;
}

// The {Utopia Sprawl -> Pool, Overgrowth -> og_host} actions on the post-land board, with the cast indices.
// The enumerator emits one variant per host and then folds host variants of equal outcome, so the same-host
// plan is the one it returns; a split-host variant is built from it by re-targeting Overgrowth.
TurnSolver::Plan PairPlan(const GameState& post, int og_host, int& sp, int& og)
{
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(post, /*is_pre_combat=*/true);
    const TurnSolver::Plan* plan = FindHosts(plans, kPoolHost, kPoolHost, sp, og);
    REQUIRE(plan != nullptr);
    TurnSolver::Plan p = *plan;
    p.actions[static_cast<std::size_t>(og)].enchant_target = og_host;
    return p;
}

// SubsetPayableSequential's retry, as the enumeration runs it: the walk's rank order (Overgrowth first -- the
// two share a rank under the Sea provider and the name tie decides) does not pay; the payable-order decider at
// the ENUMERATION site (ApplyPayableCastOrderAt) must hand back the Sprawl-first order, which the real
// sequential walk then pays. Lever off: the decider declines and the order is kept, so the subset is refused
// (ef-walk-fail) -- the defect.
void CheckEnumerationRetry(int og_host)
{
    static const SeaCreaturesProvider sea;
    const GameState post = Board39Played(&sea);
    int sp = -1, og = -1;
    const TurnSolver::Plan plan = PairPlan(post, og_host, sp, og);
    const std::vector<Action>& acts = plan.actions;
    const std::vector<int> rank_order = { og, sp };
    CHECK_FALSE(TurnSolver::OrderPaysSequentially(post, acts, rank_order));
    const std::vector<InternedName> no_exits;
    {
        std::vector<int> order = rank_order;
        ApplyPayableCastOrderAt(post, acts, order, no_exits, 0);
        CHECK(order == std::vector<int>{ sp, og });
        CHECK(TurnSolver::OrderPaysSequentially(post, acts, order));
    }
    {
        ArmPin off(heurarm::PO_LAND_AURA_DOMAIN, false);
        std::vector<int> order = rank_order;
        ApplyPayableCastOrderAt(post, acts, order, no_exits, 0);
        CHECK(order == rank_order);
    }
}

}   // namespace

TEST_CASE("Land-Aura domain (a): the aura-only subset {Utopia Sprawl, Overgrowth} is enumerated (gi39 board)")
{
    EnsureCardsLoaded();
    // The whole turn from the pre-land board: main 1 offers the pair and the rollout's apply casts both.
    static const SeaCreaturesProvider sea;
    const GameState s = Board39(&sea);
    {
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/true);
        int sp = -1, og = -1;
        const TurnSolver::Plan* plan = FindHosts(plans, kPoolHost, kPoolHost, sp, og);
        REQUIRE(plan != nullptr);
        GameState t = s;
        TurnSolver::ApplyPlan(t, *plan, true);
        CHECK(CountOnBattlefield(t, "Utopia Sprawl") == 2);
        CHECK(CountOnBattlefield(t, "Overgrowth") == 1);
    }
    // The mechanism, at the enumeration site: both Auras on the Pool.
    //
    // (Why not "lever off -> the pair is not enumerated" on this board: in this synthetic state the flat-pool
    // rescue SubsetPayableWithFilters, which pays land Auras FIRST and floats the Pool's spare {U}, admits the
    // subset before the sequential walk is consulted. In the recorded game the same subsets fell through it to
    // the walk -- the MTG_DBG_MULTI trace labels them ef-walk-fail -- for a reason this board does not
    // reproduce; see docs/design/land-aura-main1-line.md, "Unit tests".)
    CheckEnumerationRetry(kPoolHost);
}

TEST_CASE("Land-Aura domain (c): split hosts {Utopia Sprawl -> Pool, Overgrowth -> Forest} (gi39 board)")
{
    EnsureCardsLoaded();
    // The two Auras on DIFFERENT lands: the Forest pays the Sprawl (and Overgrowth goes on it, tapped); the Pool,
    // carrying two Sprawls, taps for Overgrowth's three. Two distinct hosts used to refuse the hold, so the
    // decider never admitted the order. Enumeration-site retry first ...
    CheckEnumerationRetry(kForest9);
    // ... then the APPLY site: the rank order is realised Sprawl first, and the rollout's apply casts both,
    // each Aura on the host the plan named.
    static const SeaCreaturesProvider sea;
    const GameState post = Board39Played(&sea);
    int sp = -1, og = -1;
    const TurnSolver::Plan split = PairPlan(post, kForest9, sp, og);
    const TurnSolver::Plan* plan = &split;
    {
        std::vector<int> order = { og, sp };
        ApplyPayableCastOrder(post, plan->actions, order);
        CHECK(order == std::vector<int>{ sp, og });
    }
    {
        ArmPin off(heurarm::PO_LAND_AURA_DOMAIN, false);
        std::vector<int> order = { og, sp };
        ApplyPayableCastOrder(post, plan->actions, order);
        CHECK(order == std::vector<int>{ og, sp });
    }
    GameState t = post;
    TurnSolver::ApplyPlan(t, *plan, true);
    CHECK(CountOnBattlefield(t, "Utopia Sprawl") == 2);
    CHECK(CountOnBattlefield(t, "Overgrowth") == 1);
    for (const Permanent& p : t.battlefield)
    {
        if (p.card.m_name.str() == "Overgrowth") { CHECK(p.aura_attached_to == kForest9); }
    }
}
