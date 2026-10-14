// THE REPLICATE COUNT IS A SEARCHED DECISION (2026-10-14, docs/design/slivers-replicate-searched.md).
//
// The USER's no-greedy rule (2026-09-30) put a replicate-count fan into CollectActions, but the count
// was still picked, not searched, in two ways:
//   1. MTG_REPLICATE_SIG -- the autonomous powerset dedup keyed a hand cast by NAME, so every class of
//      plans differing only in a pinned count collapsed to its highest-valued member: with Hatchery
//      Sliver ON THE BATTLEFIELD, "Cloudshredder with no copy, Muscle with one" could not be a plan
//      whenever the ordering hint ranked another split higher.
//   2. MTG_REPLICATE_PENDING -- a Sliver cast after a SAME-PLAN Hatchery (cast from hand first) had no
//      fan at all, because CanReplicate reads the board at the node; resolution made the greedy max,
//      and the first post-Hatchery Sliver to resolve took every spare copy.
// Each case checks the fixed enumeration AND the control arm (the lever off), which must differ.
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"
#include "core/SpellEffects.h"

#include <string>
#include <vector>

namespace
{

void EnsureCardsRep()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

Card RepCard(const std::string& name, int number)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    Card c = d->card;
    c.m_number = number;
    return c;
}

void RepPut(GameState& s, const std::string& name, int number)
{
    Permanent p;
    p.card              = RepCard(name, number);
    p.controller_index  = 0;
    p.owner_index       = 0;
    p.entered_this_turn = false;
    p.tapped            = false;
    s.battlefield.push_back(p);
}

struct RepArms
{
    RepArms(int sig, int pending)
    {
        heurarm::t_arm[heurarm::REPLICATE_SIG]     = static_cast<std::int8_t>(sig);
        heurarm::t_arm[heurarm::REPLICATE_PENDING] = static_cast<std::int8_t>(pending);
    }
    ~RepArms() { heurarm::Clear(); }
};

// slivers_vial, main 1, the land drop made, `hives` Sliver Hives + `territories` Unclaimed Territories
// untapped (every one pays any colour for a Sliver spell), the given hand and creatures.
GameState Board(int hives, int territories, const std::vector<std::string>& hand,
                const std::vector<std::string>& creatures)
{
    static const VialProvider vp;
    EnsureCardsRep();
    GameState s;
    s.m_provider          = &vp;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.players[0].life     = 20;
    s.players[1].life     = 20;
    s.players[0].lands_played_this_turn = 1;
    int num = 40;
    for (int i = 0; i < hives; ++i)       { RepPut(s, "Sliver Hive", num++); }
    for (int i = 0; i < territories; ++i) { RepPut(s, "Unclaimed Territory", num++); }
    for (const std::string& c : creatures) { RepPut(s, c, num++); }
    num = 100;
    for (const std::string& h : hand) { s.players[0].hand.push_back(RepCard(h, num++)); }
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(RepCard("Unclaimed Territory", 900 + k)); }
    return s;
}

const Action* Cast(const TurnSolver::Plan& p, const std::string& name)
{
    for (const Action& a : p.actions)
    { if (a.kind == Action::Kind::CastFromHand && a.card_name.str() == name) { return &a; } }
    return nullptr;
}

// The plan casting exactly the named cards (one each) at the given pinned counts (-1 = unpinned).
const TurnSolver::Plan* PlanWith(const std::vector<TurnSolver::Plan>& plans,
                                 const std::vector<std::pair<std::string, int>>& casts)
{
    for (const TurnSolver::Plan& p : plans)
    {
        if (p.actions.size() != casts.size()) { continue; }
        bool ok = true;
        for (const auto& c : casts)
        {
            const Action* a = Cast(p, c.first);
            if (a == nullptr || a->replicate_count != c.second) { ok = false; break; }
        }
        if (ok) { return &p; }
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

TEST_CASE("Replicate searched: Hatchery ON the battlefield -- both splits of the spare copy are plans")
{
    // Six mana, Hatchery in play: Cloudshredder {R}{W} and Muscle {1}{G} cost 4, one copy of either
    // fits in the other two. Which spell takes it is the decision.
    heurarm::Clear();
    const GameState pre = Board(3, 3, { "Cloudshredder Sliver", "Muscle Sliver" }, { "Hatchery Sliver" });
    REQUIRE(CanReplicate(*CardDatabase::Instance().Lookup("Muscle Sliver"), pre.battlefield, 0));
    {
        RepArms on(1, 1);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(pre, true);
        const TurnSolver::Plan* muscle_copy = PlanWith(plans, { { "Cloudshredder Sliver", 0 }, { "Muscle Sliver", 1 } });
        const TurnSolver::Plan* cloud_copy  = PlanWith(plans, { { "Cloudshredder Sliver", 1 }, { "Muscle Sliver", 0 } });
        REQUIRE(muscle_copy != nullptr);
        REQUIRE(cloud_copy != nullptr);
        GameState a = pre, b = pre;
        TurnSolver::ApplyPlan(a, *muscle_copy, true);
        TurnSolver::ApplyPlan(b, *cloud_copy, true);
        CHECK(CountOnBattlefield(a, "Muscle Sliver") == 2);
        CHECK(CountOnBattlefield(a, "Cloudshredder Sliver") == 1);
        CHECK(CountOnBattlefield(b, "Muscle Sliver") == 1);
        CHECK(CountOnBattlefield(b, "Cloudshredder Sliver") == 2);
    }
    {
        // Control: the collapsed signature keeps ONE plan per cast-name class, so the two splits can
        // never both be offered.
        RepArms off(0, 1);
        const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(pre, true);
        const bool both = PlanWith(plans, { { "Cloudshredder Sliver", 0 }, { "Muscle Sliver", 1 } }) != nullptr
                       && PlanWith(plans, { { "Cloudshredder Sliver", 1 }, { "Muscle Sliver", 0 } }) != nullptr;
        CHECK_FALSE(both);
    }
}

TEST_CASE("Replicate searched: a Sliver cast after a SAME-PLAN Hatchery gets the count fan")
{
    // Eight mana, nothing in play, hand Hatchery -> Cloudshredder -> Muscle (that order, so Hatchery
    // resolves first): Hatchery {1}{G} + Cloudshredder + Muscle + one copy = 8.
    heurarm::Clear();
    const GameState pre = Board(4, 4, { "Hatchery Sliver", "Cloudshredder Sliver", "Muscle Sliver" }, {});
    REQUIRE_FALSE(CanReplicate(*CardDatabase::Instance().Lookup("Muscle Sliver"), pre.battlefield, 0));
    RepArms on(1, 1);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(pre, true);
    const TurnSolver::Plan* muscle_copy = PlanWith(plans,
        { { "Hatchery Sliver", 0 }, { "Cloudshredder Sliver", 0 }, { "Muscle Sliver", 1 } });
    const TurnSolver::Plan* cloud_copy = PlanWith(plans,
        { { "Hatchery Sliver", 0 }, { "Cloudshredder Sliver", 1 }, { "Muscle Sliver", 0 } });
    REQUIRE(muscle_copy != nullptr);
    REQUIRE(cloud_copy != nullptr);
    GameState a = pre, b = pre;
    TurnSolver::ApplyPlan(a, *muscle_copy, true);
    TurnSolver::ApplyPlan(b, *cloud_copy, true);
    CHECK(CountOnBattlefield(a, "Hatchery Sliver") == 1);
    CHECK(CountOnBattlefield(a, "Muscle Sliver") == 2);          // the pinned copy went to Muscle
    CHECK(CountOnBattlefield(a, "Cloudshredder Sliver") == 1);
    CHECK(CountOnBattlefield(b, "Muscle Sliver") == 1);
    CHECK(CountOnBattlefield(b, "Cloudshredder Sliver") == 2);
    // "No copy at all" is a plan too (the count the greedy max could never choose with spare mana).
    CHECK(PlanWith(plans, { { "Hatchery Sliver", 0 }, { "Muscle Sliver", 0 } }) != nullptr);
    // The unpinned cast survives unchanged, with and without the granter in the plan.
    CHECK(PlanWith(plans, { { "Hatchery Sliver", 0 }, { "Muscle Sliver", -1 } }) != nullptr);
    CHECK(PlanWith(plans, { { "Muscle Sliver", -1 } }) != nullptr);
    // A pin with no granter in the plan could never be granted: it is not offered.
    for (const TurnSolver::Plan& p : plans)
    {
        if (Cast(p, "Hatchery Sliver") != nullptr) { continue; }
        for (const Action& x : p.actions) { CHECK(x.replicate_count < 0); }
    }
}

TEST_CASE("Replicate searched: MTG_REPLICATE_PENDING=0 restores the unpinned-only cast after Hatchery")
{
    heurarm::Clear();
    const GameState pre = Board(4, 4, { "Hatchery Sliver", "Cloudshredder Sliver", "Muscle Sliver" }, {});
    RepArms off(1, 0);
    const std::vector<TurnSolver::Plan> plans = TurnSolver::EnumerateMainPlans(pre, true);
    bool any_pin = false;
    for (const TurnSolver::Plan& p : plans)
    {
        for (const Action& x : p.actions)
        { if (x.card_name.str() != "Hatchery Sliver" && x.replicate_count >= 0) { any_pin = true; } }
    }
    CHECK_FALSE(any_pin);
}

TEST_CASE("Replicate searched: no granter in hand -- the enumeration is unchanged")
{
    // No Hatchery anywhere: the fan is never emitted, so both levers are inert.
    heurarm::Clear();
    const GameState pre = Board(4, 4, { "Cloudshredder Sliver", "Muscle Sliver" }, {});
    std::vector<std::uint64_t> keys[2];
    for (int arm = 0; arm < 2; ++arm)
    {
        RepArms r(arm, arm);
        for (const TurnSolver::Plan& p : TurnSolver::EnumerateMainPlans(pre, true))
        {
            for (const Action& x : p.actions) { CHECK(x.replicate_count < 0); }
            GameState s = pre;
            TurnSolver::ApplyPlan(s, p, true);
            keys[arm].push_back(TurnSolver::DedupKeyOf(s));
        }
    }
    CHECK(keys[0] == keys[1]);
}
