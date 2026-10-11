#!/usr/bin/env python3
"""RESTRICTOR_ORDER_EXACT must only REJECT: keep the rank-order `after` for the Irencrag waste gate, and use
the realised count for the legality test alone."""
p = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8/src/ai/TurnSolver.cpp'
s = open(p).read()
old1 = '''            // MTG_RESTRICTOR_ORDER_EXACT: the rank count above is the realised count only when the casts
            // ranked before the restrictor can be paid without its float (RestrictorPrePayable).
            // Rank-ordered decks only -- the same scope as the waste gate below.
            if (after <= cands[j].max_casts_after && RestrictorOrderExactOn() && !OrderingSearchEnabled(state)
                && !RestrictorPrePayable(state, cands, sel, j, r_rank, pool, combined, colour_feas, &colour_demand))
            { ++after; }
            if (after > cands[j].max_casts_after) { return; }   // illegal: too many spells after it
'''
new1 = '''            if (after > cands[j].max_casts_after) { return; }   // illegal: too many spells after it
            // MTG_RESTRICTOR_ORDER_EXACT: the rank count above is the realised count only when the casts
            // ranked before the restrictor can be paid without its float (RestrictorPrePayable); otherwise
            // one more cast follows it. A REJECT-ONLY test: `after` itself is left alone, so the waste gate
            // below keeps judging the rank order it was ruled on (USER 2026-09-01) and no subset it drops
            // is readmitted. Rank-ordered decks only -- the same scope as that gate.
            if (after + 1 > cands[j].max_casts_after && RestrictorOrderExactOn() && !OrderingSearchEnabled(state)
                && !RestrictorPrePayable(state, cands, sel, j, r_rank, pool, combined, colour_feas, &colour_demand))
            { return; }
'''
assert s.count(old1) == 1
s = s.replace(old1, new1)
old2 = '''            // MTG_RESTRICTOR_ORDER_EXACT (lockstep twin of Solve::consider -- see RestrictorPrePayable).
            if (after <= cands[j].max_casts_after && RestrictorOrderExactOn() && !OrderingSearchEnabled(state)
                && !RestrictorPrePayable(state, cands, sel, j, r_rank, pool, combined, colour_feas, &colour_demand))
            { ++after; }
            if (after > cands[j].max_casts_after) { _ct.label = "irencrag"; return; }
'''
new2 = '''            if (after > cands[j].max_casts_after) { _ct.label = "irencrag"; return; }
            // MTG_RESTRICTOR_ORDER_EXACT (lockstep twin of Solve::consider -- reject-only, `after` untouched).
            if (after + 1 > cands[j].max_casts_after && RestrictorOrderExactOn() && !OrderingSearchEnabled(state)
                && !RestrictorPrePayable(state, cands, sel, j, r_rank, pool, combined, colour_feas, &colour_demand))
            { _ct.label = "irencrag-order"; return; }
'''
assert s.count(old2) == 1
s = s.replace(old2, new2)
open(p, 'w').write(s)
print('patched')
