p = '/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/src/ai/TurnSolver.cpp'
s = open(p).read()
old = "    for (const Action& ra : cands) { if (ra.ritual_float > 0) { any_ritual = true; break; } }\n"
new = old + ("    // MTG_RITUAL_FLOAT_COLOR_EXACT: does any candidate float a FIXED colour? (one scan; false -> the\n"
             "    // colour-exact credit below never runs)\n"
             "    bool any_colour_ritual = false;\n"
             "    if (any_ritual && RitualFloatColorExactOn())\n"
             "    { for (const Action& ra : cands) { if (RitualFloatColorIndex(ra) >= 0) { any_colour_ritual = true; break; } } }\n")
assert s.count(old) == 2
s = s.replace(old, new)
open(p, 'w').write(s)
print('ok')
