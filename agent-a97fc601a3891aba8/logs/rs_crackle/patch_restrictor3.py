#!/usr/bin/env python3
"""RestrictorPrePayable gains a SELF mode: the restrictor's own cost, payable from the board plus every OTHER
producer in the subset (any order)? If not, no order can cast it -- reject whatever `after` says."""
p = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8/src/ai/TurnSolver.cpp'
s = open(p).read()

old_sig = '''static bool RestrictorPrePayable(const GameState& state, const std::vector<Action>& cands,
                                 const std::vector<int>& sel, int j, int r_rank,
                                 const ManaPool& pool, const ManaCost& combined,
                                 const ColorFeasibility& cf, const ColorDemandIndex* demand)
{'''
new_sig = '''//
// `self_only`: the restrictor ALONE against the board plus every OTHER producer in the subset, in any
// order. If even that cannot pay it, no order casts it at all (2HG gi 254 again: no follower count is
// needed to see that {R}{R}{R} cannot come from two red sources) -- the subset is rejected whatever the
// casts-after count says, so a lethal-projected phantom cannot be committed turn after turn either.
static bool RestrictorPrePayable(const GameState& state, const std::vector<Action>& cands,
                                 const std::vector<int>& sel, int j, int r_rank,
                                 const ManaPool& pool, const ManaCost& combined,
                                 const ColorFeasibility& cf, const ColorDemandIndex* demand,
                                 bool self_only = false)
{'''
assert s.count(old_sig) == 1
s = s.replace(old_sig, new_sig)

old_in = '''        bool in_pre = (k == j);
        if (!in_pre && a.kind == Action::Kind::CastFromHand && a.def != nullptr)
        { in_pre = prov.CastOrderRank(state, *a.def) <= r_rank; }
        if (a.kind == Action::Kind::SacForMana)
        {
            // A pre-pass crack (Lotus Bloom, Dwarven Ruins): its mana precedes every cast.
            if (!CreditFixedColorSac(sup, a)) { sup.wild += a.ritual_float; }
        }
        if (in_pre)
        {
            pre.push_back(k);
            if (k != j) { sup.wild += a.ritual_float; sup.AddPool(a.rock_mana); }
            continue;
        }'''
new_in = '''        bool in_pre = (k == j);
        if (!in_pre && !self_only && a.kind == Action::Kind::CastFromHand && a.def != nullptr)
        { in_pre = prov.CastOrderRank(state, *a.def) <= r_rank; }
        if (a.kind == Action::Kind::SacForMana)
        {
            // A pre-pass crack (Lotus Bloom, Dwarven Ruins): its mana precedes every cast.
            if (!CreditFixedColorSac(sup, a)) { sup.wild += a.ritual_float; }
        }
        else if (self_only && k != j)
        {
            // SELF mode: any other producer might be ordered ahead of the restrictor.
            sup.wild += a.ritual_float; sup.AddPool(a.rock_mana);
        }
        if (in_pre)
        {
            pre.push_back(k);
            if (k != j) { sup.wild += a.ritual_float; sup.AddPool(a.rock_mana); }
            continue;
        }'''
assert s.count(old_in) == 1
s = s.replace(old_in, new_in)

old1 = '''            if (after + 1 > cands[j].max_casts_after && RestrictorOrderExactOn() && !OrderingSearchEnabled(state)
                && !RestrictorPrePayable(state, cands, sel, j, r_rank, pool, combined, colour_feas, &colour_demand))
            { return; }
'''
new1 = '''            if (RestrictorOrderExactOn() && !OrderingSearchEnabled(state)
                && ((after + 1 > cands[j].max_casts_after
                     && !RestrictorPrePayable(state, cands, sel, j, r_rank, pool, combined, colour_feas, &colour_demand))
                    || !RestrictorPrePayable(state, cands, sel, j, r_rank, pool, combined, colour_feas, &colour_demand,
                                             /*self_only=*/true)))
            { return; }
'''
assert s.count(old1) == 1
s = s.replace(old1, new1)
old2 = '''            if (after + 1 > cands[j].max_casts_after && RestrictorOrderExactOn() && !OrderingSearchEnabled(state)
                && !RestrictorPrePayable(state, cands, sel, j, r_rank, pool, combined, colour_feas, &colour_demand))
            { _ct.label = "irencrag-order"; return; }
'''
new2 = '''            if (RestrictorOrderExactOn() && !OrderingSearchEnabled(state)
                && ((after + 1 > cands[j].max_casts_after
                     && !RestrictorPrePayable(state, cands, sel, j, r_rank, pool, combined, colour_feas, &colour_demand))
                    || !RestrictorPrePayable(state, cands, sel, j, r_rank, pool, combined, colour_feas, &colour_demand,
                                             /*self_only=*/true)))
            { _ct.label = "irencrag-order"; return; }
'''
assert s.count(old2) == 1
s = s.replace(old2, new2)
open(p, 'w').write(s)
print('patched')
