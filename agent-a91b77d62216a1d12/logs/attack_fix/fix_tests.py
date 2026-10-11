#!/usr/bin/env python3
"""The enumerator never emits a bare attack-only plan, so the tests pin the cast plans only."""
W = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a91b77d62216a1d12'
p = W + '/test/unit/test_attack_projection.cpp'
s = open(p).read()
reps = [
('''bool Empty(const TurnSolver::Plan& p)
{
    for (const Action& a : p.actions) { if (a.kind != Action::Kind::PlayLand) { return false; } }
    return true;
}

''', ''),
('''// THE MEASUREMENT. `wins_this_turn` is `projected >= opponent life`, so the largest opponent life at
// which a plan still reads as a win IS its projected damage. Threshold() scans the opponent's life
// and returns that number, which pins the projection exactly rather than on one side of a line.''',
'''// THE MEASUREMENT. `wins_this_turn` is `projected >= opponent life`, so the largest opponent life at
// which a plan still reads as a win IS its projected damage. Threshold() scans the opponent's life
// and returns that number, which pins the projection exactly rather than on one side of a line.
// (Neither site scores a bare attack-only plan -- the enumerator does not emit one and Solve's walk
// skips the empty selection -- so each board's pre-plan attack is given in its comment, and the
// tests pin the CAST plans, whose projection is pre-plan attack - what the plan's taps cost + what
// its casts add.) The d0 Solve site is pinned through the plan it picks at the threshold and one
// above it: the cast line reads as a win exactly up to its projected damage.'''),
('''    const int empty = Threshold([](int l) { return FaeBoard(l, 0); }, Empty);
    const int greaves = Threshold([](int l) { return FaeBoard(l, 0); },
                                  [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Lightning Greaves" }); });
    CHECK(empty == 10);              // Faeburrow 5 + Hellkite 5
    CHECK(greaves == empty - 5);     // Faeburrow pays the second mana and stays home
    // The d0 / playout Solve agrees: at 6 the Greaves line is not lethal, so the winning plan it
    // picks does not cast Greaves.
    const TurnSolver::Plan g = GreedySolve(FaeBoard(6, 0));
    CHECK(g.wins_this_turn);
    CHECK_FALSE(Casts(g, "Lightning Greaves"));''',
'''    // Pre-plan attack 10: Faeburrow 5 + Hellkite 5.
    const int greaves = Threshold([](int l) { return FaeBoard(l, 0); },
                                  [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Lightning Greaves" }); });
    CHECK(greaves == 5);             // Faeburrow pays the second mana and stays home
    // The d0 / playout Solve projects it the same way.
    const TurnSolver::Plan g5 = GreedySolve(FaeBoard(5, 0));
    CHECK((g5.wins_this_turn && Casts(g5, "Lightning Greaves")));
    const TurnSolver::Plan g6 = GreedySolve(FaeBoard(6, 0));
    CHECK_FALSE((g6.wins_this_turn && Casts(g6, "Lightning Greaves")));'''),
('''    const int empty = Threshold([](int l) { return FaeBoard(l, 1); }, Empty);
    const int greaves = Threshold([](int l) { return FaeBoard(l, 1); },
                                  [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Lightning Greaves" }); });
    CHECK(empty == 10);
    CHECK(greaves == 10);            // the phantom: Faeburrow both pays and swings
    const TurnSolver::Plan g = GreedySolve(FaeBoard(6, 1));
    CHECK(Casts(g, "Lightning Greaves"));''',
'''    const int greaves = Threshold([](int l) { return FaeBoard(l, 1); },
                                  [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Lightning Greaves" }); });
    CHECK(greaves == 10);            // the phantom: Faeburrow both pays and swings
    const TurnSolver::Plan g = GreedySolve(FaeBoard(10, 1));
    CHECK((g.wins_this_turn && Casts(g, "Lightning Greaves")));'''),
('''    const int empty = Threshold([](int l) { return SnowBoard(l, 0); }, Empty);
    const int heart = Threshold([](int l) { return SnowBoard(l, 0); },
                                [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Coldsteel Heart" }); });
    CHECK(empty == 2);               // Druid 1 + Augur 1
    CHECK(heart == 1);               // the Druid pays, Augur swings alone
    const TurnSolver::Plan g = GreedySolve(SnowBoard(2, 0));
    CHECK(g.wins_this_turn);
    CHECK_FALSE(Casts(g, "Coldsteel Heart"));''',
'''    // Pre-plan attack 2: Druid 1 + Augur 1.
    const int heart = Threshold([](int l) { return SnowBoard(l, 0); },
                                [](const TurnSolver::Plan& p) { return CastsOnly(p, { "Coldsteel Heart" }); });
    CHECK(heart == 1);               // the Druid pays, Augur swings alone
    const TurnSolver::Plan g1 = GreedySolve(SnowBoard(1, 0));
    CHECK((g1.wins_this_turn && Casts(g1, "Coldsteel Heart")));
    const TurnSolver::Plan g2 = GreedySolve(SnowBoard(2, 0));
    CHECK_FALSE((g2.wins_this_turn && Casts(g2, "Coldsteel Heart")));'''),
('''    const TurnSolver::Plan g = GreedySolve(SnowBoard(2, 1));
    CHECK(Casts(g, "Coldsteel Heart"));''',
'''    const TurnSolver::Plan g = GreedySolve(SnowBoard(2, 1));
    CHECK((g.wins_this_turn && Casts(g, "Coldsteel Heart")));'''),
('''    CHECK(Threshold([](int l) { return HoofBoard(l, 0); }, Empty) == 4);
''', '''    // Pre-plan attack 4: two Knights.
'''),
('''    const TurnSolver::Plan g = GreedySolve(HoofBoard(18, 0));
    CHECK(g.wins_this_turn);
    CHECK(Casts(g, "Craterhoof Behemoth"));''',
'''    const TurnSolver::Plan g = GreedySolve(HoofBoard(18, 0));
    CHECK((g.wins_this_turn && Casts(g, "Craterhoof Behemoth")));
    const TurnSolver::Plan g19 = GreedySolve(HoofBoard(19, 0));
    CHECK_FALSE(g19.wins_this_turn);'''),
('''    CHECK(Threshold([](int l) { return BogDorkBoard(l, 0); }, Empty) == 8);
''', '''    // Pre-plan attack 8.
'''),
('''    CHECK(Threshold([](int l) { return WellBoard(l, 0); }, Empty) == 14);   // 3+1+1+1 + 2 x 4
''', '''    // Pre-plan attack 14: 3 + 1 + 1 + 1, then Bogbeast's +2 on four attackers.
'''),
('''    CHECK(Threshold([](int l) { return ParagonBoard(l, 0); }, Empty) == 6);   // 4 + 1 + 1
''', '''    // Pre-plan attack 6: 4 + 1 + 1.
'''),
('''    CHECK(Threshold([](int l) { return SageBoard(l, 0); }, Empty) == 4);
''', '''    // Pre-plan attack 4: Knight 2 + two Pilgrims (the Sage swings for 0).
'''),
('''    CHECK(Threshold([](int l) { return ColossBoard(l, 0); }, Empty) == 2);
''', '''    // Pre-plan attack 2: the Knight.
'''),
]
for a, b in reps:
    assert s.count(a) == 1, a[:80]
    s = s.replace(a, b)
open(p, 'w').write(s)
print('ok')
