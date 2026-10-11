#!/usr/bin/env python3
"""Wire DecisionProvider::HoldCastUnlessLethal into BOTH subset walks (Solve's consider, EnumeratePlans')."""
p = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8/src/ai/TurnSolver.cpp'
s = open(p).read()

DECL = '''    // HoldCastUnlessLethal (provider; Hinata's lone Crackle under MTG_HINATA_HOLD_LETHAL_BURN): a cast
    // the deck holds unless the subset WINS this turn. Flagged once per candidate here; the drop sits at
    // the subset's `wins` projection below (lockstep twin in %s). false for every other deck.
    std::vector<unsigned char> cand_held(cands.size(), 0);
    bool any_held = false;
    const DecisionProvider& held_provider = ResolveProvider(state);
'''
FLAG = '''        if (dd && fa.kind == Action::Kind::CastFromHand && held_provider.HoldCastUnlessLethal(state, *dd))
        { cand_held[j] = 1; any_held = true; }
'''

# --- Solve's precompute ---
a_solve = "    std::vector<unsigned char> fill_face_ok(cands.size(), 0), fill_x_ok(cands.size(), 0),\n                               cand_draws(cands.size(), 0);\n    for (int j = 0; j < m; ++j)\n"
assert s.count(a_solve) == 1, s.count(a_solve)
s = s.replace(a_solve, (DECL % "EnumeratePlans") + a_solve)
e_solve = "        { cand_draws[j] = 1; }\n    }\n\n    bool have_colors[5];"
assert s.count(e_solve) == 1, s.count(e_solve)
s = s.replace(e_solve, "        { cand_draws[j] = 1; }\n" + FLAG + "    }\n\n    bool have_colors[5];")

# --- EnumeratePlans' precompute ---
a_enum = "    std::vector<unsigned char> fill_face_ok(cands.size(), 0), fill_x_ok(cands.size(), 0),\n                               cand_draws(cands.size(), 0);\n    for (int j = 0; j < static_cast<int>(cands.size()); ++j)\n"
assert s.count(a_enum) == 1, s.count(a_enum)
s = s.replace(a_enum, (DECL % "SolveUncached") + a_enum)
e_enum = "        { cand_draws[j] = 1; }\n    }\n\n    // SATURATED HUMAN-PLAY COLLAPSE"
assert s.count(e_enum) == 1, s.count(e_enum)
s = s.replace(e_enum, "        { cand_draws[j] = 1; }\n" + FLAG + "    }\n\n    // SATURATED HUMAN-PLAY COLLAPSE")

# --- Solve's consider gate ---
g_solve = "        if (sacrifice_count > 0 && !wins && HoldSacLandBurn(state, cands, sel)) { return; }\n"
assert s.count(g_solve) == 1, s.count(g_solve)
s = s.replace(g_solve, g_solve + '''
        // HoldCastUnlessLethal (see the precompute): a held cast -- Hinata's lone Crackle -- only in a
        // subset that wins this turn by THIS projection (every cast's damage + the attack, payable).
        if (any_held && !wins) { for (int j : sel) { if (cand_held[j]) { return; } } }
''')

# --- EnumeratePlans' consider gate ---
g_enum = '            if (drop) { _ct.label = "sac-burn-hold"; return; }\n        }\n        _ct.label = "OFFERED";\n'
assert s.count(g_enum) == 1, s.count(g_enum)
s = s.replace(g_enum, '''            if (drop) { _ct.label = "sac-burn-hold"; return; }
        }
        // HoldCastUnlessLethal (mirrors Solve::consider): a held cast only in a subset that wins now.
        if (any_held && !wins)
        { for (int j : sel) { if (cand_held[j]) { _ct.label = "hold-unless-lethal"; return; } } }
        _ct.label = "OFFERED";
''')
open(p, 'w').write(s)
print('patched')
