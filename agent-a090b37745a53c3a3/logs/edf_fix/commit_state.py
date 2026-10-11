#!/usr/bin/env python3
"""Reconstruct the working tree for commit k of the EDF fix series from the saved FINAL files.

usage: commit_state.py <k> [--check]
  k = 0 tip, 1 E5, 2 E1, 3 E2a, 4 E2b, 5 E3, 6 E4 (final).
  Writes the source files for state k into the worktree (and adds/removes the new test files).
  --check: verify state 0 reproduces HEAD exactly and state 6 the saved finals, writing nothing.
State k = final with every fix > k reverted, reverting from the last fix down so nested blocks peel cleanly.
"""
import os, subprocess, sys

W = "/workspaces/MagicDeckTester/.claude/worktrees/agent-a090b37745a53c3a3"
FIN = W + "/logs/edf_fix/final"
FILES = {"TS": "src/ai/TurnSolver.cpp", "DP": "src/ai/DecisionProviders.cpp",
         "HA": "src/ai/HeuristicArm.h", "CM": "CMakeLists.txt", "T0": "test/unit/test_edf_d0_play.cpp"}
NEWTESTS = {1: "test/unit/test_edf_prospective_null.cpp", 2: "test/unit/test_edf_untap_credit.cpp",
            3: "test/unit/test_edf_prepay_energy.cpp", 4: "test/unit/test_edf_aura_payer.cpp",
            5: "test/unit/test_edf_route_board_deploy.cpp", 6: "test/unit/test_edf_goff_verify.cpp"}

def head(path):
    return subprocess.run(["git", "-C", W, "show", "HEAD:" + path], capture_output=True, text=True, check=True).stdout

def cut(txt, start, end, repl="", end_inclusive=False):
    i = txt.index(start)
    assert txt.count(start) == 1, ("start not unique", start[:60])
    j = txt.index(end, i)
    if end_inclusive:
        j += len(end)
    return txt[:i] + repl + txt[j:]

def sub1(txt, old, new):
    assert txt.count(old) == 1, ("not unique", old[:80], txt.count(old))
    return txt.replace(old, new)

def sub_all(txt, old, new, n):
    assert txt.count(old) == n, ("count", old[:80], txt.count(old), n)
    return txt.replace(old, new)

def ha_drop(txt, slot, name):
    lines = txt.split("\n")
    out = [l for l in lines if not l.startswith("    %s," % slot) and l != '        "%s",' % name]
    assert len(lines) - len(out) == 2, (slot, len(lines) - len(out))
    return "\n".join(out)

def cm_drop(txt, test):
    return sub1(txt, "    %s\n" % test, "")

# ---- revert one fix ---------------------------------------------------------------------------------
def revert(k, f):
    TS, DP, HA, CM, T0 = f["TS"], f["DP"], f["HA"], f["CM"], f["T0"]
    if k == 6:   # E4
        TS = cut(TS, "    // MTG_EDF_GOFF_VERIFY_UNPROJECTED (DEFAULT ON; =0 disables): see the !projected branch below.\n",
                 "    if (has_extra_lethal && state.spells_cast_this_turn == 0\n        && !s_no_edf_goff_cut")
        TS = cut(TS, "            else\n            {\n                if (s_goff_cut_dbg)\n                {\n"
                     "                    std::fprintf(stderr, \"[edf-goff-cut] t%d k=%d projected=0\\n\",",
                 "            best      = saved_best;\n            best_mask = saved_mask;\n"
                 "            best_sel  = saved_sel;\n            best_fill_j = saved_fj;\n"
                 "            best_fill_action = saved_fa;\n        }\n        else if (s_goff_cut_dbg && blink_k == 0)",
                 repl="            else if (s_goff_cut_dbg)\n            {\n"
                      "                std::fprintf(stderr, \"[edf-goff-cut] t%d k=%d projected=0\\n\",\n"
                      "                             state.turn_number, blink_k);\n            }\n")
        HA = ha_drop(HA, "EDF_GOFF_VERIFY_UNPROJECTED", "MTG_EDF_GOFF_VERIFY_UNPROJECTED")
        CM = cm_drop(CM, NEWTESTS[6])
    elif k == 5:   # E3
        DP = cut(DP, "    // MTG_EDF_ROUTE_BOARD_DEPLOY's deploy attempts only (see Run): a deploy pays from the lands, so the\n",
                 "    int         blink_run = 0;")
        DP = cut(DP, "            // Ctx::protect_blink (deploy attempts only): fire while the lands can still pay the next\n",
                 "            while (!OpponentHasLost(cx.s) && keeps_blink() && Activate(cx, f)) { ++fired; }\n",
                 repl="            while (!OpponentHasLost(cx.s) && Activate(cx, f)) { ++fired; }\n", end_inclusive=True)
        DP = cut(DP, "// THE LOOP ALREADY ON THE BOARD STILL GETS ITS DEPLOY (MTG_EDF_ROUTE_BOARD_DEPLOY",
                 "    const GameState snapshot = cx.s;\n    const Ctx saved = cx;\n    for (int attempt = 0; attempt < 2; ++attempt)",
                 repl="static int Run(Ctx& cx, int forced = -1)\n{\n    if (RouteWon(cx)) { return 1; }\n"
                      "    const Board b0 = ScanBoard(cx);\n"
                      "    if (b0.outlet != 0 && b0.payload != 0) { cx.attempt_used = -1; return Loop(cx); }\n")
        DP = cut(DP, "    // A lever that changes the TRIAL'S ANSWER for the same board must be in the key: this memo is\n",
                 "    Mix(h, static_cast<std::uint64_t>(s.turn_number));")
        HA = ha_drop(HA, "EDF_ROUTE_BOARD_DEPLOY", "MTG_EDF_ROUTE_BOARD_DEPLOY")
        CM = cm_drop(CM, NEWTESTS[5])
    elif k == 4:   # E2b
        TS = cut(TS, "// MTG_SOLVE_AURA_PAYER_CHECK (DEFAULT ON; =0 disables): see the PAE block in SolveUncached.\n",
                 "static bool IsEtbUntapCast(")
        TS = cut(TS, "            // A LAND-AURA SUBSET THE BOARD GATES PASS STILL MEETS THE REAL PAYER\n",
                 "        }\n        if (mana_cache_on && !mc_hit) { mc_last_gmask = gmask;")
        HA = ha_drop(HA, "SOLVE_AURA_PAYER_CHECK", "MTG_SOLVE_AURA_PAYER_CHECK")
        CM = cm_drop(CM, NEWTESTS[4])
    elif k == 3:   # E2a
        TS = cut(TS, "// MTG_PREPAY_ENERGY_RESTORE (DEFAULT ON; =0 disables): a rolled-back prepay solve gives back the {E}\n",
                 "bool TurnSolver::BatchPrepayMainCasts(GameState& state, const std::vector<Action>& acts,")
        TS = cut(TS, "    // ...and the ENERGY a coloured Aether Hub tap spends (MTG_PREPAY_ENERGY_RESTORE",
                 "\n    // Whole-turn depletion reservation")
        n = 0
        for ind in (16, 12, 8):
            line = "\n" + " " * ind + "if (energy_restore) { state.players[active].energy_counters = ea; }\n"
            c = TS.count(line); n += c
            TS = TS.replace(line, "\n")
        assert n == 5, n
        TS = sub1(TS, "            int                    best_ea   = state.players[active].energy_counters;\n", "")
        TS = sub1(TS, "                    best_ea       = state.players[active].energy_counters;\n", "")
        TS = sub1(TS, "            if (energy_restore) { state.players[active].energy_counters = best_ea; }\n", "")
        HA = ha_drop(HA, "PREPAY_ENERGY_RESTORE", "MTG_PREPAY_ENERGY_RESTORE")
        CM = cm_drop(CM, NEWTESTS[3])
    elif k == 2:   # E1
        TS = cut(TS, "// THE SAME-TURN LAND UNTAP OF AN ETB-UNTAP CREATURE, CREDITED BY THE REAL PAYER\n",
                 "static bool SubsetPayableWithFiltersImpl(const GameState& state, const std::vector<Action>& cands,\n"
                 "                                         const std::vector<int>& sel, bool tap_sources)")
        TS = cut(TS, "    // MTG_SOLVE_UNTAP_CREDIT (see SolveUntapCreditOn): a subset holding an ETB-untap cast is walked in\n",
                 "    // Pay each selected cast's mana cost with real sources; taps persist across casts in cp, so a\n")
        TS = sub1(TS, "        for (int j : walk_ord)\n", "        for (int j : sel)\n")
        TS = cut(TS, "            // MTG_SOLVE_UNTAP_CREDIT: the executor's tap-ahead for an untapper",
                 "            bool cost_ok = TapForCostDirect(cp, bill, for_creature);\n")
        TS = sub1(TS, "            // ...and its resolution: the ETB untap, after the creature's own payment.\n"
                      "            if (untapper)\n"
                      "            { EtbUntapLands(cp, cp.active_player_index, def->params.etb_untap_lands, /*log_ledger=*/false); }\n", "")
        TS = cut(TS, "    // MTG_SOLVE_UNTAP_CREDIT (see SolveUntapCreditOn): a subset that casts an ETB-untap creature is the\n",
                 "    if (aura_exact)\n    {\n        ComputeAvailableColors(state, have_colors_board")
        TS = sub1(TS, "        if (untap_credit) { cand_untapper.assign(cands.size(), 0); }\n", "")
        TS = sub1(TS, "            if (untap_credit && IsEtbUntapCast(la)) { cand_untapper[j] = 1; }\n", "")
        TS = sub1(TS,
                  "                bool to_payer = false;   // a land Aura, or (MTG_SOLVE_UNTAP_CREDIT) an untapper\n"
                  "                for (int j : sel)\n"
                  "                {\n"
                  "                    const std::size_t uj = static_cast<std::size_t>(j);\n"
                  "                    if (cand_land_aura[uj] || (untap_credit && cand_untapper[uj])) { to_payer = true; break; }\n"
                  "                }\n"
                  "                if (!to_payer || !SubsetPayableWithFilters(state, cands, sel)) { mc_store_reject(); return; }\n",
                  "                bool casts_aura = false;\n"
                  "                for (int j : sel) { if (cand_land_aura[static_cast<std::size_t>(j)]) { casts_aura = true; break; } }\n"
                  "                if (!casts_aura || !SubsetPayableWithFilters(state, cands, sel)) { mc_store_reject(); return; }\n")
        HA = ha_drop(HA, "SOLVE_UNTAP_CREDIT", "MTG_SOLVE_UNTAP_CREDIT")
        CM = cm_drop(CM, NEWTESTS[2])
        T0 = head(FILES["T0"])
    elif k == 1:   # E5
        DP = sub1(DP,
                  "        // `casting` carries one entry per selected action (Solve's consider pushes cands[j].def for\n"
                  "        // every pick), so a non-cast action -- a land drop, an activation -- arrives as nullptr.\n"
                  "        // The two scans above and below null-check through DefIsBlinkOutlet / DefIsUntapPayload;\n"
                  "        // this print did not, and segfaulted every traced EDF game.\n"
                  "        std::string cs;\n"
                  "        for (const CardDefinition* d : casting) { cs += (d ? d->card.m_name.str() : std::string(\"-\")) + \",\"; }\n",
                  "        std::string cs;\n"
                  "        for (const CardDefinition* d : casting) { cs += d->card.m_name.str() + \",\"; }\n")
        DP = sub1(DP, "        if (d == nullptr) { continue; }   // a non-cast pick (see the trace above) is no sink\n", "")
        DP = sub1(DP, "        for (const CardDefinition* d : casting)\n"
                      "        { if (d != nullptr) { avail -= d->card.m_mana_cost.ManaValue(); } }   // nullptr: a non-cast pick\n",
                  "        for (const CardDefinition* d : casting) { avail -= d->card.m_mana_cost.ManaValue(); }\n")
        DP = sub1(DP, "            for (const CardDefinition* d : casting) { cs += (d ? d->card.m_name.str() : std::string(\"-\")) + \",\"; }\n",
                  "            for (const CardDefinition* d : casting) { cs += d->card.m_name.str() + \",\"; }\n")
        DP = sub1(DP, "            if (d == nullptr) { continue; }   // a non-cast pick (see RecogniseFlickerLoopProspective)\n", "")
        CM = cm_drop(CM, NEWTESTS[1])
    f.update(TS=TS, DP=DP, HA=HA, CM=CM, T0=T0)

def state(k):
    f = {key: open(os.path.join(FIN, os.path.basename(p))).read() for key, p in FILES.items()}
    for fix in range(6, k, -1):
        revert(fix, f)
    return f

def main():
    k = int(sys.argv[1])
    if "--check" in sys.argv:
        s0 = state(0)
        for key, p in FILES.items():
            assert s0[key] == head(p), ("state 0 != HEAD", p)
        s6 = state(6)
        for key, p in FILES.items():
            assert s6[key] == open(os.path.join(FIN, os.path.basename(p))).read()
        for kk in range(7):
            state(kk)
        print("check OK: state 0 == HEAD for all files; every state builds from the finals")
        return
    f = state(k)
    for key, p in FILES.items():
        open(os.path.join(W, p), "w").write(f[key])
    for kk, t in NEWTESTS.items():
        dst = os.path.join(W, t)
        src = os.path.join(FIN, os.path.basename(t))
        if kk <= k:
            open(dst, "w").write(open(src).read())
        elif os.path.exists(dst):
            os.rename(dst, os.path.join(FIN, "parked_" + os.path.basename(t)))
    print("wrote state %d" % k)

if __name__ == "__main__":
    main()
