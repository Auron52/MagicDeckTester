#!/usr/bin/env python3
"""Fix the two unit-test fixtures the new levers changed."""
p = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8/test/unit/test_hinata_crackle_count.cpp'
s = open(p).read()
a = 'GameState HinataBoard(int lands, const std::vector<std::string>& hand, bool with_hinata = true)'
assert s.count(a) == 1
s = s.replace(a, 'GameState HinataBoard(int lands, const std::vector<std::string>& hand, bool with_hinata = true,\n'
                 '                      const char* land = "Island")')
b = '    for (int i = 0; i < lands; ++i) { put("Island"); }'
assert s.count(b) == 1
s = s.replace(b, '    for (int i = 0; i < lands; ++i) { put(land); }')
c = ('    // ...Hinata in HAND with 4 sources (her mana value): castable this turn -> the combo is live.\n'
     '    const GameState castable = HinataBoard(4, { kCrackle, "Hinata, Dawn-Crowned" }, /*with_hinata=*/false);')
assert s.count(c) == 1
s = s.replace(c, '    // ...Hinata in HAND with four any-colour lands: {1}{U}{R}{W} pays -> castable, the combo is live.\n'
                 '    // (Four Islands would NOT do since MTG_HINATA_CASTABLE_MANA: no {R} or {W} -- see\n'
                 '    // test_restrictor_order.cpp.)\n'
                 '    const GameState castable = HinataBoard(4, { kCrackle, "Hinata, Dawn-Crowned" }, /*with_hinata=*/false,\n'
                 '                                           "Forbidden Orchard");')
open(p, 'w').write(s)

p2 = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8/test/unit/test_restrictor_order.cpp'
t = open(p2).read()
d = ('    // Six Mountains: the Ornithopter is paid before Irencrag from the board -- legal under both arms.\n'
     '    for (bool arm : { true, false })\n'
     '    {\n'
     '        PinRo pin(heurarm::RESTRICTOR_ORDER_EXACT, arm);\n'
     '        GameState s = BoardRo({ "Mountain", "Mountain", "Mountain", "Mountain", "Mountain", "Mountain" },\n')
assert t.count(d) == 1
t = t.replace(d, '    // Seven Mountains: the Ornithopter ({2}) and Irencrag ({1}{R}{R}{R}) are both paid from the board in\n'
                 '    // rank order, and the float funds the Crackle (X=2, the provider\'s max X) -- legal under both arms.\n'
                 '    for (bool arm : { true, false })\n'
                 '    {\n'
                 '        PinRo pin(heurarm::RESTRICTOR_ORDER_EXACT, arm);\n'
                 '        GameState s = BoardRo({ "Mountain", "Mountain", "Mountain", "Mountain", "Mountain", "Mountain",\n'
                 '                                "Mountain" },\n')
open(p2, 'w').write(t)
print('patched')
