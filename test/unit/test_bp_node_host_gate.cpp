// MTG_BP_NODE_HOST_GATE -- the breakpoint node's wave stand-down is gated on the HOST, not only on
// the TURN (docs/design/bp-node-wave-drop-in-lookahead-host.md; USER 2026-11-21: "Okay, if there is a
// potential hole in our design it should be closed.").
//
// BpNodeHostsThisTurn is turn-keyed (g_condemn_root_turn, +1 under MTG_BP_NODE_HOST2), so on the
// root turn and root+1 BpWaveSiteMask drops the node's sites (site 3 by default). Only a host that
// really pends at the node (host_pends = node_host_here) may take that drop. These pin, through
// TurnSolver::WalkerSiteMaskForTest:
//   * a NON-hosting walker keeps site 3 on root / root+1 turns in EVERY apply with the lever on
//     (rollouts and derivations included), and drops it outside a searched apply with it off
//     (today's MTG_BP_WAVE_UNHOSTED_SITES-only behaviour);
//   * a HOSTING walker is unchanged: it drops site 3 on node turns with the lever on or off;
//   * on a turn the node does not host, nobody drops anything.
#include <doctest/doctest.h>

#include "ai/HeuristicArm.h"
#include "ai/TurnSolver.h"
#include "core/GameState.h"

#include <cstdint>

namespace
{

constexpr int kSite3 = 1 << 3;

struct GateScope
{
    explicit GateScope(int gate, int unhosted = 1)
    {
        heurarm::t_arm[heurarm::BP_NODE_HOST_GATE]       = static_cast<std::int8_t>(gate);
        heurarm::t_arm[heurarm::BP_WAVE_UNHOSTED_SITES]  = static_cast<std::int8_t>(unhosted);
        heurarm::t_arm[heurarm::BP_NODE]                 = 1;
        heurarm::t_arm[heurarm::BP_NODE_KEEPWAVE3]       = 0;
    }
    ~GateScope()
    {
        heurarm::t_arm[heurarm::BP_NODE_HOST_GATE]       = -1;
        heurarm::t_arm[heurarm::BP_WAVE_UNHOSTED_SITES]  = -1;
        heurarm::t_arm[heurarm::BP_NODE]                 = -1;
        heurarm::t_arm[heurarm::BP_NODE_KEEPWAVE3]       = -1;
    }
};

bool Has3(int m) { return (m & kSite3) != 0; }

}  // namespace

TEST_CASE("BP_NODE_HOST_GATE: a non-hosting walker keeps the node's sites on the node's turns, in every apply")
{
    GameState s;
    s.turn_number = 5;
    const int t = s.TurnSeq();
    for (const int root : { t, t - 1 })   // the root turn, and root+1 (MTG_BP_NODE_HOST2, default ON)
    {
        CAPTURE(root);
        {
            GateScope on(1);
            CHECK(Has3(TurnSolver::WalkerSiteMaskForTest(s, /*host_pends=*/false, /*searched=*/false, root)));
            CHECK(Has3(TurnSolver::WalkerSiteMaskForTest(s, /*host_pends=*/false, /*searched=*/true,  root)));
        }
        {
            GateScope off(0);   // today: only a SEARCHED non-hosting apply keeps site 3
            CHECK(!Has3(TurnSolver::WalkerSiteMaskForTest(s, false, /*searched=*/false, root)));
            CHECK(Has3(TurnSolver::WalkerSiteMaskForTest(s, false, /*searched=*/true, root)));
        }
        {
            GateScope on_no_unhosted(1, /*unhosted=*/0);   // the gate alone closes both
            CHECK(Has3(TurnSolver::WalkerSiteMaskForTest(s, false, false, root)));
            CHECK(Has3(TurnSolver::WalkerSiteMaskForTest(s, false, true, root)));
        }
        {
            GateScope off_all(0, 0);   // the fully turn-keyed mask
            CHECK(!Has3(TurnSolver::WalkerSiteMaskForTest(s, false, false, root)));
            CHECK(!Has3(TurnSolver::WalkerSiteMaskForTest(s, false, true, root)));
        }
    }
}

TEST_CASE("BP_NODE_HOST_GATE: a hosting walker is unchanged -- it leaves the node's sites to the node")
{
    GameState s;
    s.turn_number = 5;
    const int t = s.TurnSeq();
    for (const int gate : { 1, 0 })
    {
        CAPTURE(gate);
        GateScope g(gate);
        for (const int root : { t, t - 1, -1 })
        {
            CAPTURE(root);
            CHECK(!Has3(TurnSolver::WalkerSiteMaskForTest(s, /*host_pends=*/true, false, root)));
            CHECK(!Has3(TurnSolver::WalkerSiteMaskForTest(s, /*host_pends=*/true, true,  root)));
        }
    }
}

TEST_CASE("BP_NODE_HOST_GATE: off the node's turns nobody drops the node's sites")
{
    GameState s;
    s.turn_number = 5;
    const int t = s.TurnSeq();
    for (const int gate : { 1, 0 })
    {
        CAPTURE(gate);
        GateScope g(gate);
        for (const int root : { t + 1, t - 2 })   // the walker's turn is neither root nor root+1
        {
            CAPTURE(root);
            for (const bool hp : { true, false })
            {
                for (const bool sa : { true, false })
                { CHECK(Has3(TurnSolver::WalkerSiteMaskForTest(s, hp, sa, root))); }
            }
        }
    }
}

TEST_CASE("BP_NODE_HOST_GATE: the lever reads and is overridable")
{
    CHECK(heurarm::SlotOf("MTG_BP_NODE_HOST_GATE") == heurarm::BP_NODE_HOST_GATE);
}
