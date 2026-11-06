// MTG_DUP_ACTION_FOLD (TurnSolver.cpp, DupActFold): a class of k independent candidates equal in every field
// but `eval` walks only its k + 1 canonical arrangements (highest eval first), because EnumeratePlans'
// signature dedup already keeps exactly that arrangement out of every other one. The claim to test is that the
// RETURNED plan list is unchanged -- set and order -- with the fold on and off, on the measured board that
// has such twins: a Melira Pod persist-loop board in main 2, where a free sac outlet's canonical single sac
// and the persist block's explicit K=1 sac of the same Kitchen Finks coincide (eval 0 vs 1).
#include <doctest/doctest.h>

#include "ai/DecisionProviders.h"
#include "ai/TurnSolver.h"
#include "ai/ValueArm.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <string>
#include <vector>

namespace
{
void EnsureCardsDaf()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}
Card PhDaf(const std::string& name, int number)
{
    REQUIRE_MESSAGE(CardDatabase::Instance().Lookup(name) != nullptr, "card not in cards.json: ", name);
    Card c;
    c.m_name = name;
    c.RehashName();
    c.m_number = number;
    return c;
}
const MeliraPodProvider& PodDaf()
{
    static const MeliraPodProvider p;
    return p;
}
// One permanent of the measured board: name, tapped, entered this turn, +1/+1 counters.
struct DafPerm { const char* name; bool tapped; bool fresh; int plus; };
// The Melira d3/b3 tail line's T6 main-2 board (seed 10372713029942101962), opponent at 12, Reveillark in
// hand: Celes is the loop closer, the three Feeders and the three Finks all differ (counters / tap / age),
// and the canonical single sac of every outlet picks the counter-less Finks -- which the persist block also
// emits as its explicit K=1 sac, so identical pairs reach the walk.
GameState MakeDaf()
{
    EnsureCardsDaf();
    const DafPerm board[] = {
        { "Carrion Feeder", true, false, 30 }, { "Carrion Feeder", true, false, 18 },
        { "Orzhov Basilica", true, false, 0 }, { "Caves of Koilos", false, false, 0 },
        { "Blooming Marsh", true, false, 0 },  { "Caves of Koilos", false, false, 0 },
        { "Kitchen Finks", true, false, 16 },  { "Carrion Feeder", true, false, 16 },
        { "Darkbore Pathway", true, true, 0 }, { "Celes, Rune Knight", false, true, 16 },
        { "Bloodthrone Vampire", false, true, 4 }, { "Kitchen Finks", false, true, 1 },
        { "Kitchen Finks", false, true, 0 } };
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 6;
    s.m_provider          = &PodDaf();
    s.players[0].life     = 56;
    s.players[1].life     = 12;
    int num = 1;
    s.players[0].hand.push_back(PhDaf("Reveillark", num++));
    for (int k = 0; k < 10; ++k) { s.players[0].library.push_back(PhDaf("Blooming Marsh", num++)); }
    for (const DafPerm& d : board)
    {
        Permanent p;
        p.card              = CardDatabase::Instance().Lookup(d.name)->card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = d.fresh;
        p.tapped            = d.tapped;
        if (d.plus > 0) { p.counters.Add(Counter::Type::PlusOnePlusOne, d.plus); }
        s.battlefield.push_back(p);
    }
    s.players[0].lands_played_this_turn = 1;
    return s;
}
std::vector<TurnSolver::Plan> PlansWithFold(const GameState& s, int arm)
{
    TurnSolver::ClearPerGameCaches();   // no memo may serve the other arm's list
    valuearm::t_arm.dup_action_fold = arm;
    std::vector<TurnSolver::Plan> v = TurnSolver::EnumerateMainPlans(s, /*is_pre_combat=*/false);
    valuearm::t_arm.dup_action_fold = -1;
    return v;
}
}   // namespace

TEST_CASE("Dup-action fold: equal-but-eval sac twins exist, and the plan list is unchanged (set and order)")
{
    const GameState s = MakeDaf();
    // The premise: at least one pair of candidates equal in every field but `eval` (else this proves nothing).
    const std::vector<Action> cands = TurnSolver::CollectActionsForTest(s, /*is_pre_combat=*/false);
    int twins = 0;
    for (std::size_t i = 0; i < cands.size(); ++i)
    {
        for (std::size_t j = i + 1; j < cands.size(); ++j)
        {
            Action x = cands[i];
            x.eval = cands[j].eval;
            if (x == cands[j]) { ++twins; }
        }
    }
    REQUIRE(twins >= 1);
    const std::vector<TurnSolver::Plan> off = PlansWithFold(s, 0);
    const std::vector<TurnSolver::Plan> on  = PlansWithFold(s, 1);
    REQUIRE(off.size() > 1);
    CHECK(on.size() == off.size());
    CHECK(on == off);
}
