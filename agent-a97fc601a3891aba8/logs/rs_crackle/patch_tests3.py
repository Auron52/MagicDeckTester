#!/usr/bin/env python3
"""Move the CASTABLE_MANA unit test into test_hinata_crackle_count.cpp (lever M's commit), leaving
test_restrictor_order.cpp with the RESTRICTOR_ORDER_EXACT cases only (lever R's commit)."""
ro = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8/test/unit/test_restrictor_order.cpp'
hc = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8/test/unit/test_hinata_crackle_count.cpp'
t = open(ro).read()
start = t.index('TEST_CASE("HINATA_CASTABLE_MANA:')
end = t.index('TEST_CASE("RESTRICTOR_ORDER_EXACT: Irencrag + Ornithopter')
t = t[:start] + t[end:]
old_head = t[:t.index('#include <doctest/doctest.h>')]
new_head = ('// Unit cover for MTG_RESTRICTOR_ORDER_EXACT, the d0-runner stall behind Hinata2\'s 2HG gi 254 and d0 gi 195 /\n'
            '// 865 / 1773 (docs/design/reality-spasm-crackle-classification.md, "The d0-runner stall"): the subset walks\'\n'
            '// Irencrag Feat test ("you can cast only one more spell this turn") also requires the casts ranked before it,\n'
            '// and Irencrag itself, to be payable without its own float -- otherwise every payable order casts it earlier\n'
            '// and one more cast follows -- and rejects a subset in which no order can pay Irencrag at all.\n')
t = new_head + t[len(old_head):]
t = t.replace('const char* kHin   = "Hinata, Dawn-Crowned";\n', '')
open(ro, 'w').write(t)

h = open(hc).read()
h += r'''
// ---- MTG_HINATA_CASTABLE_MANA: "castable from hand" is a question about mana, not about sources ------------
namespace
{
// `board` (untapped, ours, not summoning sick), `hand` in hand -- no Hinata on the battlefield.
GameState CastableBoard(const std::vector<std::string>& board, const std::vector<std::string>& hand)
{
    EnsureCardsHc();
    GameState s;
    s.active_player_index = 0;
    s.turn_number         = 3;
    int num = 1;
    for (const std::string& n : board)
    {
        Permanent p;
        p.card              = DefHc(n.c_str()).card;
        p.card.m_number     = num++;
        p.controller_index  = 0;
        p.owner_index       = 0;
        p.entered_this_turn = false;
        s.battlefield.push_back(p);
    }
    for (const std::string& n : hand) { s.players[0].hand.push_back(PhHc(n, num++)); }
    return s;
}
}   // namespace

TEST_CASE("HINATA_CASTABLE_MANA: the board's real mana, not its source count, decides 'castable from hand'")
{
    const char* kHin = "Hinata, Dawn-Crowned";
    // 2HG gi 254's turn-3 board: three sources, five mana ({C}{C} + {U}{R} + any) -- {1}{U}{R}{W} pays.
    const GameState rich = CastableBoard({ "Sol Ring", "Izzet Boilerworks", "Forbidden Orchard" }, { kHin });
    // Four Islands: four sources reach her mana value, but nothing makes {R} or {W}.
    const GameState blue = CastableBoard({ "Island", "Island", "Island", "Island" }, { kHin });
    {
        Pin on(heurarm::HINATA_CASTABLE_MANA, true);
        CHECK(HinataCastableFromHand(rich));
        CHECK_FALSE(HinataCastableFromHand(blue));
    }
    {
        Pin off(heurarm::HINATA_CASTABLE_MANA, false);   // the legacy source count
        CHECK_FALSE(HinataCastableFromHand(rich));
        CHECK(HinataCastableFromHand(blue));
    }
    // No Hinata in hand: never castable.
    CHECK_FALSE(HinataCastableFromHand(CastableBoard({ "Sol Ring", "Izzet Boilerworks", "Forbidden Orchard" },
                                                     { kCrackle })));
    // ...and it is what the provider's untap-ritual gate reads under the same-subset credit: Reality Spasm
    // is offered on the five-mana board (she resolves first), not on the four-Islands one.
    const HinataProvider hp;
    Pin credit(heurarm::HINATA_SUBSET_CREDIT, true);
    Pin on(heurarm::HINATA_CASTABLE_MANA, true);
    CHECK(hp.ShouldEmitUntapRitual(rich));
    CHECK_FALSE(hp.ShouldEmitUntapRitual(blue));
}
'''
open(hc, 'w').write(h)
print('tests restructured')
