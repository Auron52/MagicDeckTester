// MTG_BP_DEV_SEQ -- deviation SEQUENCES in the breakpoint wave walker (docs/design/bp-empty-arm-deferred-site.md
// 6.1). Plan::bp_choice / bp_at carry ONE deviation per apply, so a line needing a non-front continuation at two
// class-on occurrences of one main phase was unfindable at every budget unless the uniform (bp_all) arm happened to
// express it. Goblins seed 1330145 gi144 under MTG_SUBSET_SAC_OUTLETS: T3 main 2 {Skirk Prospector, Siege-Gang}
// needs site 9's sacrifice-three burst (rank 8 once the outlets admitted more continuations ahead of it) and then a
// non-front entry at the site-9 re-open; the search lost the T3 kill at --depth 8 --budget-ms 0.
//
// These pin the contract:
//   * TurnSolver::BpChoiceAt is the one reader of a plan's choice at an occurrence (bp_at, bp_all, bp_pre);
//   * the walker opens a COMPOUND slot off every hand-out that reached a LATENT occurrence (reachable only by
//     targeting it: the site-9 re-open) -- under MTG_BP_DEV_SEQ_ALL any further counted one too -- and off a seeded
//     host-applied variant;
//   * compound slots are handed out only after every slot of fewer deviations retired (wave = deviation count);
//   * =0 restores one deviation per apply exactly.
#include <doctest/doctest.h>

#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "cards/CardDatabase.h"
#include "core/GameState.h"
#include "core/HeuristicDefaults.h"

#include <cstdint>
#include <string>
#include <vector>

namespace
{

void EnsureCardsDs()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// A base plan casting one card from hand. Ponder is a plain cantrip: PlanOpensBreakpoint marks site 3.
TurnSolver::Plan CastPlanDs(const std::string& name)
{
    const CardDefinition* d = CardDatabase::Instance().Lookup(name);
    REQUIRE_MESSAGE(d != nullptr, "card not in cards.json: ", name);
    TurnSolver::Plan p;
    Action a;
    a.kind      = Action::Kind::CastFromHand;
    a.card_name = InternedName(name);
    a.def       = d;
    p.actions.push_back(a);
    return p;
}

struct DevSeqScope
{
    explicit DevSeqScope(int on, int all = 0)
    {
        heurarm::t_arm[heurarm::BP_DEV_SEQ]     = static_cast<std::int8_t>(on);
        heurarm::t_arm[heurarm::BP_DEV_SEQ_ALL] = static_cast<std::int8_t>(all);
        heurarm::t_arm[heurarm::BP_WAVE_EMPTY]  = 1;
        heurarm::t_arm[heurarm::BP_WAVE_UNHOSTED_SITES] = 1;
    }
    ~DevSeqScope()
    {
        heurarm::t_arm[heurarm::BP_DEV_SEQ]     = -1;
        heurarm::t_arm[heurarm::BP_DEV_SEQ_ALL] = -1;
        heurarm::t_arm[heurarm::BP_WAVE_EMPTY]  = -1;
        heurarm::t_arm[heurarm::BP_WAVE_UNHOSTED_SITES] = -1;
    }
};

constexpr int kE = TurnSolver::kBpEmptyChoice;

bool HasStep(const std::vector<TurnSolver::DevSeqStep>& t, const std::vector<int>& pre, int choice, int at)
{
    for (const TurnSolver::DevSeqStep& s : t)
    { if (s.pre == pre && s.bp_choice == choice && s.bp_at == at) { return true; } }
    return false;
}

}   // namespace

TEST_CASE("BpChoiceAt: bp_at, bp_all and a sequence's earlier entries; nothing else is targeted")
{
    TurnSolver::Plan p;
    CHECK(TurnSolver::BpChoiceAt(p, 0) == -1);   // a base plan targets nothing
    p.bp_choice = 3;
    p.bp_at     = 2;
    CHECK(TurnSolver::BpChoiceAt(p, 2) == 3);
    CHECK(TurnSolver::BpChoiceAt(p, 0) == -1);
    CHECK(TurnSolver::BpChoiceAt(p, 1) == -1);
    CHECK(TurnSolver::BpChoiceAt(p, 3) == -1);
    CHECK(TurnSolver::BpChoiceAt(p, -1) == -1);
    p.bp_pre = { 8, -1 };   // rank 8 at occurrence 0, canon at 1, rank 3 at 2
    CHECK(TurnSolver::BpChoiceAt(p, 0) == 8);
    CHECK(TurnSolver::BpChoiceAt(p, 1) == -1);
    CHECK(TurnSolver::BpChoiceAt(p, 2) == 3);
    CHECK(TurnSolver::ClassifyBpOccurrence(p, 0) == TurnSolver::BpOccurrence::Target);
    CHECK(TurnSolver::ClassifyBpOccurrence(p, 1) == TurnSolver::BpOccurrence::PreTarget);
    CHECK(TurnSolver::ClassifyBpOccurrence(p, 2) == TurnSolver::BpOccurrence::Target);
    CHECK(TurnSolver::ClassifyBpOccurrence(p, 3) == TurnSolver::BpOccurrence::Untargeted);
    p.bp_pre = { kE };   // a sentinel is a choice like any rank
    CHECK(TurnSolver::BpChoiceAt(p, 0) == kE);
    TurnSolver::Plan u = p;
    u.bp_all = true;     // the uniform arm ignores bp_pre: one rank everywhere
    CHECK(TurnSolver::BpChoiceAt(u, 0) == 3);
    CHECK(TurnSolver::BpChoiceAt(u, 5) == 3);
}

// Every apply reaches two COUNTED occurrences: the ordinary walk is (rank 0, 1, EMPTY) at occurrence 0 and -- nested
// discovery -- the same at occurrence 1 with the canon at 0. Under MTG_BP_DEV_SEQ_ALL the compound wave then varies
// occurrence 1 under every choice taken at 0, and only after every ordinary hand-out. By default (latent scope) a
// counted occurrence opens nothing: the canon already plays there.
TEST_CASE("MTG_BP_DEV_SEQ_ALL: one compound slot per choice that reached a further occurrence, after every single deviation")
{
    EnsureCardsDs();
    GameState s;
    const std::vector<TurnSolver::Plan> plans = { CastPlanDs("Ponder") };
    auto reach2 = [](const TurnSolver::Plan&) { return 2; };
    {
        DevSeqScope latent_only(1, 0);
        const auto t = TurnSolver::WaveWalkerDevSeqForTest(s, plans, 2, reach2);
        for (const TurnSolver::DevSeqStep& st : t) { CHECK(st.pre.empty()); }
        CHECK(HasStep(t, {}, 0, 1));
    }
    {
        DevSeqScope on(1, 1);
        const auto t = TurnSolver::WaveWalkerDevSeqForTest(s, plans, /*list_len=*/2, reach2);
        // single deviations
        CHECK(HasStep(t, {}, 0, 0));
        CHECK(HasStep(t, {}, 1, 0));
        CHECK(HasStep(t, {}, kE, 0));
        CHECK(HasStep(t, {}, 0, 1));
        // two deviations: every choice at 0 (ranks AND its EMPTY) crossed with occurrence 1's ranks and EMPTY
        for (int c0 : { 0, 1, kE })
        {
            for (int c1 : { 0, 1, kE }) { CHECK(HasStep(t, { c0 }, c1, 1)); }
        }
        // wave = deviation count: no ordinary hand-out after the first compound one
        bool seen_compound = false;
        for (const TurnSolver::DevSeqStep& st : t)
        {
            if (!st.pre.empty()) { seen_compound = true; }
            else { CHECK_MESSAGE(!seen_compound, "an ordinary slot was handed out after a compound one"); }
        }
        CHECK(seen_compound);
        // nothing is handed out twice
        for (std::size_t i = 0; i < t.size(); ++i)
        {
            for (std::size_t j = i + 1; j < t.size(); ++j)
            { CHECK(!(t[i].pre == t[j].pre && t[i].bp_choice == t[j].bp_choice && t[i].bp_at == t[j].bp_at)); }
        }
    }
    {
        DevSeqScope off(0, 1);   // =0 wins over _ALL: one deviation per apply exactly
        const auto t = TurnSolver::WaveWalkerDevSeqForTest(s, plans, 2, reach2);
        for (const TurnSolver::DevSeqStep& st : t) { CHECK(st.pre.empty()); }
        CHECK(HasStep(t, {}, 0, 1));   // the single deviation at occurrence 1 is still there
    }
}

// The site-9 re-open counts its next occurrence only for a plan that targets it, so the apply reports it as LATENT
// (g_bp_seen_latent): the walker must open the compound slot from that alone.
TEST_CASE("a latent occurrence (reachable only by targeting it) opens the compound slot too")
{
    EnsureCardsDs();
    GameState s;
    const std::vector<TurnSolver::Plan> plans = { CastPlanDs("Ponder") };
    DevSeqScope on(1);   // the shipped scope
    // Rank 1 at occurrence 0 reaches occurrence 1 only by targeting it; nothing else goes further.
    auto reach = [](const TurnSolver::Plan& v)
    { return (v.bp_at == 0 && v.bp_choice == 1 && v.bp_pre.empty()) ? -2 : v.bp_at + 1; };
    const auto t = TurnSolver::WaveWalkerDevSeqForTest(s, plans, 2, reach);
    CHECK(HasStep(t, { 1 }, 0, 1));
    CHECK(HasStep(t, { 1 }, 1, 1));
    CHECK(!HasStep(t, { 0 }, 0, 1));   // rank 0 never reached occurrence 1
    CHECK(!HasStep(t, {}, 0, 1));      // and nothing counted it, so no ordinary nested slot either
}

// A variant the HOST applied itself (wave 0's rank, a node child) is seeded: its compound slots open as if the
// walker had handed it out.
TEST_CASE("a seeded host-applied variant opens its compound slots")
{
    EnsureCardsDs();
    GameState s;
    TurnSolver::Plan base = CastPlanDs("Ponder");
    TurnSolver::Plan w0   = base;   // wave 0's rank 0 at occurrence 0
    w0.bp_choice = 0;
    w0.bp_at     = 0;
    const std::vector<TurnSolver::Plan> plans = { base, w0 };
    auto reach = [](const TurnSolver::Plan& v) { return v.bp_at + 1; };   // the walker's own hand-outs go nowhere
    {
        DevSeqScope on(1);   // the shipped scope: the seed's LATENT occurrence 1
        const auto t = TurnSolver::WaveWalkerDevSeqForTest(s, plans, 2, reach, /*seed_pos=*/1, /*seed_reach=*/-2);
        CHECK(HasStep(t, { 0 }, 0, 1));
        CHECK(HasStep(t, { 0 }, kE, 1));
    }
    {
        DevSeqScope all(1, 1);   // _ALL: the seed COUNTED occurrences 1 and 2 -- vary 1, and 2 with the canon at 1
        const auto t = TurnSolver::WaveWalkerDevSeqForTest(s, plans, 2, reach, /*seed_pos=*/1, /*seed_reach=*/3);
        CHECK(HasStep(t, { 0 }, 0, 1));
        CHECK(HasStep(t, { 0 }, kE, 1));
        CHECK(HasStep(t, { 0, -1 }, 1, 2));
    }
}

// Only a test-only extern; the public solver interface gains no API.
extern int DevSeqLocalFslCarryForTest(bool win, bool replay);

TEST_CASE("deviation sequences: local FSL WIN hit carries dirty ancestry")
{
    CHECK(DevSeqLocalFslCarryForTest(true, true) == 31);
    CHECK(DevSeqLocalFslCarryForTest(true, false) == 16);
}
TEST_CASE("deviation sequences: local FSL NOWIN hit carries dirty ancestry")
{
    CHECK(DevSeqLocalFslCarryForTest(false, true) == 31);
    CHECK(DevSeqLocalFslCarryForTest(false, false) == 16);
}
