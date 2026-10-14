// Every breakpoint continuation, "cast nothing more" included, is open to the search
// (docs/design/bp-empty-arm-deferred-site.md; USER 2026-10-14: "All options should be open to the
// search except those that heuristics in the provider close"). These pin the deferred wave walker's
// contract through TurnSolver::WaveWalkerTraceForTest, which runs the walker exactly as a host does
// with each apply replaced by a fixed report:
//   * MTG_BP_WAVE_EMPTY -- a slot wave 0 never touched hands out its explicit EMPTY arm LAST, after
//     its in-list ranks, and the host SCORES it; a wave-0 slot (whose EMPTY arm wave 0 emitted) and
//     an empty list owe none; =0 retires at the end of the list exactly as before.
//   * MTG_BP_WAVE_UNHOSTED_SITES -- on the node's turn a host that cannot pend still walks site 3.
//   * MTG_BP_NODE_NESTED_SLOTS -- a node-hosted plan's nested indices get slots, the pend index not.
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

void EnsureCardsBw()
{
    static const bool loaded = []
    {
        CardDatabase::Instance().LoadFromJson(ResolveHeuristicDefaultsPath("src/cards/data/cards.json"));
        return true;
    }();
    REQUIRE(loaded);
}

// A base plan casting one card from hand. Ponder is a plain cantrip: PlanOpensBreakpoint marks site 3.
TurnSolver::Plan CastPlan(const std::string& name)
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

// Pin the walker levers for one test, restoring "use the env default" on exit.
struct LeverScope
{
    explicit LeverScope(int empty, int unhosted = 1, int nested = 1, int demote = 1)
    {
        heurarm::t_arm[heurarm::BP_WAVE_EMPTY]          = static_cast<std::int8_t>(empty);
        heurarm::t_arm[heurarm::BP_WAVE_UNHOSTED_SITES] = static_cast<std::int8_t>(unhosted);
        heurarm::t_arm[heurarm::BP_NODE_NESTED_SLOTS]   = static_cast<std::int8_t>(nested);
        heurarm::t_arm[heurarm::BP_NEW_ONLY_DEMOTE]     = static_cast<std::int8_t>(demote);
    }
    ~LeverScope()
    {
        heurarm::t_arm[heurarm::BP_WAVE_EMPTY]          = -1;
        heurarm::t_arm[heurarm::BP_WAVE_UNHOSTED_SITES] = -1;
        heurarm::t_arm[heurarm::BP_NODE_NESTED_SLOTS]   = -1;
        heurarm::t_arm[heurarm::BP_NEW_ONLY_DEMOTE]     = -1;
    }
};

// The (bp_choice, bp_at) sequence the walker handed out for plan `base`, scored hand-outs only.
std::vector<std::pair<int, int>> Scored(const std::vector<TurnSolver::WalkerStep>& t, int base)
{
    std::vector<std::pair<int, int>> out;
    for (const TurnSolver::WalkerStep& s : t)
    { if (s.base == base && s.scored) { out.push_back({ s.bp_choice, s.bp_at }); } }
    return out;
}

constexpr int kE = TurnSolver::kBpEmptyChoice;
constexpr int kD = TurnSolver::kBpDemotedChoice;

}   // namespace

// MTG_BP_NEW_ONLY_DEMOTE: the old-cards-only continuations MTG_BP_NEW_ONLY keeps out of the list are a
// slot's LAST wave -- after its ranks and its EMPTY -- instead of being deleted.
TEST_CASE("the demoted wave comes after a slot's ranks and its EMPTY; =0 deletes it as before")
{
    EnsureCardsBw();
    GameState s;
    const std::vector<TurnSolver::Plan> plans = { CastPlan("Ponder") };
    {
        LeverScope on(1);
        const auto t = TurnSolver::WaveWalkerTraceForTest(s, plans, /*list_len=*/2, /*seen=*/1, false,
                                                          -1, -1, -1, /*demoted_len=*/2);
        const std::vector<std::pair<int, int>> want =
            { { 0, 0 }, { 1, 0 }, { kE, 0 }, { kD + 0, 0 }, { kD + 1, 0 } };
        CHECK(Scored(t, 0) == want);
    }
    {
        LeverScope off(1, 1, 1, /*demote=*/0);
        const auto t = TurnSolver::WaveWalkerTraceForTest(s, plans, 2, 1, false, -1, -1, -1, 2);
        const std::vector<std::pair<int, int>> want = { { 0, 0 }, { 1, 0 }, { kE, 0 } };
        CHECK(Scored(t, 0) == want);
        for (const TurnSolver::WalkerStep& st : t) { CHECK(!TurnSolver::IsBpDemotedChoice(st.bp_choice)); }
    }
}

TEST_CASE("a stillborn slot (empty list) still hands out its demoted wave; its probe is not scored")
{
    EnsureCardsBw();
    GameState s;
    TurnSolver::Plan p = CastPlan("Ponder");
    p.bp_wave0 = true;   // wave 0 fanned it: the walker opens at rank W, past the end of an empty list
    LeverScope on(1);
    const auto t = TurnSolver::WaveWalkerTraceForTest(s, { p }, /*list_len=*/0, /*seen=*/1, false,
                                                      -1, -1, -1, /*demoted_len=*/1);
    REQUIRE(t.size() == 2);
    CHECK(!t[0].scored);                 // rank W: the probe that measured both lengths
    CHECK(t[1].bp_choice == kD);         // then the one demoted entry,
    CHECK(t[1].scored);                  // scored
}

TEST_CASE("MTG_BP_WAVE_NEW_LAST moves a slot's EMPTY after EVERY slot's ranks; the hand-outs are the same")
{
    EnsureCardsBw();
    GameState s;
    const std::vector<TurnSolver::Plan> plans = { CastPlan("Ponder") };
    auto run = [&](int new_last)
    {
        LeverScope on(1);
        heurarm::t_arm[heurarm::BP_WAVE_NEW_LAST] = static_cast<std::int8_t>(new_last);
        // seen = 2: nested discovery opens bp_at 1, whose list (3) outlasts bp_at 0's (2).
        const auto t = TurnSolver::WaveWalkerTraceForTest(s, plans, /*list_len=*/2, /*seen=*/2, false);
        heurarm::t_arm[heurarm::BP_WAVE_NEW_LAST] = -1;
        return Scored(t, 0);
    };
    const std::vector<std::pair<int, int>> per_slot = run(0);
    const std::vector<std::pair<int, int>> last     = run(1);
    // Round-robin over the slots as they exist (bp_at 1 opens after bp_at 0's first apply). Per slot:
    // bp_at 0's EMPTY follows its own last rank, ahead of bp_at 1's ranks 1 and 2.
    const std::vector<std::pair<int, int>> want_per_slot =
        { { 0, 0 }, { 1, 0 }, { 0, 1 }, { kE, 0 }, { 1, 1 }, { 2, 1 }, { kE, 1 } };
    CHECK(per_slot == want_per_slot);
    // New-last: every in-list rank first, then the EMPTYs -- the same seven hand-outs.
    const std::vector<std::pair<int, int>> want_last =
        { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 }, { kE, 0 }, { kE, 1 } };
    CHECK(last == want_last);
}

TEST_CASE("a node-hosted plan's demoted wave is handed out at the pend index, nothing else")
{
    EnsureCardsBw();
    GameState s;
    LeverScope on(1);
    const auto t = TurnSolver::WaveWalkerTraceForTest(s, { CastPlan("Ponder") }, 2, 1, /*host_pends=*/true,
                                                      /*hosted=*/0, /*hosted_at=*/0, /*hosted_want=*/1,
                                                      /*demoted_len=*/0, /*hosted_demoted=*/2);
    const std::vector<std::pair<int, int>> want = { { kD + 0, 0 }, { kD + 1, 0 } };
    CHECK(Scored(t, 0) == want);
    CHECK(t.size() == 2);
}

// No solve is on the stack here, so the node's root-turn test reads every turn as its own: this is the
// node's TURN, on which BpWaveSiteMask drops site 3. A host that cannot pend must still walk it.
TEST_CASE("an un-hosted plan's slot scores its ranks, then its EMPTY, LAST")
{
    EnsureCardsBw();
    GameState s;
    const std::vector<TurnSolver::Plan> plans = { CastPlan("Ponder") };

    {
        LeverScope on(1);
        const auto t = TurnSolver::WaveWalkerTraceForTest(s, plans, /*list_len=*/3, /*seen=*/1,
                                                          /*host_pends=*/false);
        const std::vector<std::pair<int, int>> want = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { kE, 0 } };
        CHECK(Scored(t, 0) == want);
        REQUIRE(!t.empty());
        CHECK(t.back().bp_choice == kE);   // the EMPTY is the slot's last hand-out
    }
    {
        // =0: the committed walker -- the in-list ranks, then retire. Nothing ever scores "stop".
        LeverScope off(0);
        const auto t = TurnSolver::WaveWalkerTraceForTest(s, plans, 3, 1, false);
        const std::vector<std::pair<int, int>> want = { { 0, 0 }, { 1, 0 }, { 2, 0 } };
        CHECK(Scored(t, 0) == want);
        for (const TurnSolver::WalkerStep& st : t) { CHECK(st.bp_choice != kE); }
    }
}

TEST_CASE("an empty continuation list owes no EMPTY: every arm already plays EMPTY there")
{
    EnsureCardsBw();
    GameState s;
    LeverScope on(1);
    const auto t = TurnSolver::WaveWalkerTraceForTest(s, { CastPlan("Ponder") }, /*list_len=*/0, 1, false);
    CHECK(Scored(t, 0).empty());
    for (const TurnSolver::WalkerStep& st : t) { CHECK(st.bp_choice != kE); }
}

TEST_CASE("a wave-0 slot owes no EMPTY (wave 0 emitted its arm); its NESTED slot does")
{
    EnsureCardsBw();
    GameState s;
    TurnSolver::Plan p = CastPlan("Ponder");
    p.bp_wave0 = true;   // wave 0 fanned this plan: ranks 0..W-1 and the EMPTY arm at bp_at 0
    LeverScope on(1);
    // Each apply reports 2 class-on breakpoints, so nested discovery opens bp_at = 1 at rank 0.
    const auto t = TurnSolver::WaveWalkerTraceForTest(s, { p }, /*list_len=*/4, /*seen=*/2, false);
    const std::vector<std::pair<int, int>> sc = Scored(t, 0);
    int empty_at0 = 0, empty_at1 = 0, rank0_at0 = 0;
    for (const auto& [c, at] : sc)
    {
        if (c == kE && at == 0) { ++empty_at0; }
        if (c == kE && at == 1) { ++empty_at1; }
        if (c == 0 && at == 0)  { ++rank0_at0; }
    }
    CHECK(empty_at0 == 0);   // wave 0's own arm covers it
    CHECK(empty_at1 == 1);   // nested index: nobody else ever scores "stop" there
    CHECK(rank0_at0 == 0);   // the wave-0 slot resumes at rank W
}

TEST_CASE("on the node's turn a pending host leaves site 3 to the node; a non-pending host walks it")
{
    EnsureCardsBw();
    GameState s;
    const std::vector<TurnSolver::Plan> plans = { CastPlan("Ponder") };
    {
        LeverScope on(1);
        CHECK(TurnSolver::WaveWalkerTraceForTest(s, plans, 2, 1, /*host_pends=*/true).empty());
        CHECK(!TurnSolver::WaveWalkerTraceForTest(s, plans, 2, 1, /*host_pends=*/false).empty());
    }
    {
        LeverScope off(1, /*unhosted=*/0);   // the turn-keyed mask everywhere: no slot at all
        CHECK(TurnSolver::WaveWalkerTraceForTest(s, plans, 2, 1, /*host_pends=*/false).empty());
    }
}

TEST_CASE("a node-hosted plan's nested indices get slots (ranks, then EMPTY); the pend index does not")
{
    EnsureCardsBw();
    GameState s;
    const std::vector<TurnSolver::Plan> plans = { CastPlan("Ponder") };
    {
        LeverScope on(1);
        // Hosted at index 0; child 0 reached 2 class-on breakpoints -> nested index 1 is owed.
        const auto t = TurnSolver::WaveWalkerTraceForTest(s, plans, /*list_len=*/2, /*seen=*/2,
                                                          /*host_pends=*/true, /*hosted=*/0,
                                                          /*hosted_at=*/0, /*hosted_want=*/2);
        // (The seam's nested list at index 1 is list_len + 1 = 3 long.)
        const std::vector<std::pair<int, int>> want = { { 0, 1 }, { 1, 1 }, { 2, 1 }, { kE, 1 } };
        CHECK(Scored(t, 0) == want);
        for (const TurnSolver::WalkerStep& st : t) { CHECK(st.bp_at != 0); }   // the node owns index 0
    }
    {
        // =0 (the call site does not register the plan): nothing for it on the node's turn.
        LeverScope off(1, 1, /*nested=*/0);
        CHECK(TurnSolver::WaveWalkerTraceForTest(s, plans, 2, 2, true).empty());
    }
}
