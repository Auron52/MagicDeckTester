#!/usr/bin/env python3
"""MTG_RESTRICTOR_ORDER_EXACT: the restrictor pre-set test, wired into both subset walks."""
p = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8/src/ai/TurnSolver.cpp'
s = open(p).read()

anchor = ("// rescued_by_exception (out, optional): set when exception (c) is what kept the plan -- i.e. the\n"
          "// (a)+(b) rule alone would have dropped it. Used only by MTG_SV_LETHAL_AUDIT.\n")
assert s.count(anchor) == 1
helper = r'''// MTG_RESTRICTOR_ORDER_EXACT (heurarm RESTRICTOR_ORDER_EXACT). DEFAULT ON; =0 -> the rank-only count.
static bool RestrictorOrderExactOn()
{
    static const bool on = EnvOn("MTG_RESTRICTOR_ORDER_EXACT", true);
    return heurarm::Flag(heurarm::RESTRICTOR_ORDER_EXACT, on);
}

// THE CAST RESTRICTOR'S PRE-SET (MTG_RESTRICTOR_ORDER_EXACT). Both subset walks count the casts AFTER a
// cast restrictor (Irencrag Feat: "you can cast only one more spell this turn") by CastOrderRank and
// judge payability ORDER-FREE, with the restrictor's float credited to the whole subset. The two
// models can disagree: a subset whose rank order is legal may be payable only in an order that casts
// the restrictor EARLIER -- its float funding a cast ranked before it, or its own cost. The executor
// then realises that payable order (ApplyPayableCastOrder) or fails the rank order's payment; either
// way the restriction bites and the payoff is refused. Hinata d0 gi 865 / 1773: {Irencrag, Ornithopter,
// Crackle} on four mana is payable only as Irencrag -> Ornithopter -> Crackle, and the Ornithopter took
// the one remaining cast. 2HG gi 254: {Irencrag, Crackle}, whose {R}{R}{R} only Irencrag's own seven red
// could pay (Izzet Boilerworks + Forbidden Orchard make two red), was committed and failed four turns
// running -- the d0 runner cast nothing T3-T6.
//
// The test: can the casts ranked at or before restrictor `j`, together with `j`, be paid WITHOUT j's
// own float? If not, every payable order casts j ahead of one of them, so at least one MORE cast follows
// j than the rank count says. Sound in the no-false-reject direction: `cost` is the subset's bill with
// every subset-level credit already folded in and every non-pre action's cost removed, and the supply
// is the board plus every pre-pass / pre-ranked producer's mana (other rituals, rocks, sac-for-mana)
// regardless of order -- both optimistic, so a pre-set this calls unpayable genuinely is. The colour
// half is the walks' own colour-exact gate (ColorFeasibility, which knows a bounce land makes ONE of
// each of its colours), run on the pre-set alone. Returns true (no change) for anything it does not
// model.
static bool RestrictorPrePayable(const GameState& state, const std::vector<Action>& cands,
                                 const std::vector<int>& sel, int j, int r_rank,
                                 const ManaPool& pool, const ManaCost& combined,
                                 const ColorFeasibility& cf, const ColorDemandIndex* demand)
{
    if (combined.hybrid_count > 0) { return true; }   // unmodelled
    thread_local std::vector<int> pre;
    pre.clear();
    ManaCost cost = combined;
    ManaPool sup  = pool;
    const DecisionProvider& prov = ResolveProvider(state);
    auto sub = [](int& f, int v) { f = std::max(0, f - v); };
    for (int k : sel)
    {
        const Action& a = cands[k];
        if (a.cost.hybrid_count > 0) { return true; }   // unmodelled
        bool in_pre = (k == j);
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
        }
        sub(cost.white, a.cost.white); sub(cost.blue, a.cost.blue);   sub(cost.black, a.cost.black);
        sub(cost.red, a.cost.red);     sub(cost.green, a.cost.green); sub(cost.colorless, a.cost.colorless);
        sub(cost.generic, a.cost.generic);
    }
    if (!sup.CanPay(cost)) { return false; }
    if (cf.usable && !cf.Payable(cands, pre, PoolCredit(pool, sup), /*noncreature_only=*/false, demand))
    { return false; }
    return true;
}

'''
s = s.replace(anchor, helper + anchor)

# Solve's consider: before the legality comparison.
g1 = "            if (after > cands[j].max_casts_after) { return; }   // illegal: too many spells after it\n"
assert s.count(g1) == 1
s = s.replace(g1, '''            // MTG_RESTRICTOR_ORDER_EXACT: the rank count above is the realised count only when the casts
            // ranked before the restrictor can be paid without its float (RestrictorPrePayable).
            // Rank-ordered decks only -- the same scope as the waste gate below.
            if (after <= cands[j].max_casts_after && RestrictorOrderExactOn() && !OrderingSearchEnabled(state)
                && !RestrictorPrePayable(state, cands, sel, j, r_rank, pool, combined, colour_feas, &colour_demand))
            { ++after; }
''' + g1)

# EnumeratePlans' consider: same, lockstep.
g2 = '            if (after > cands[j].max_casts_after) { _ct.label = "irencrag"; return; }\n'
assert s.count(g2) == 1
s = s.replace(g2, '''            // MTG_RESTRICTOR_ORDER_EXACT (lockstep twin of Solve::consider -- see RestrictorPrePayable).
            if (after <= cands[j].max_casts_after && RestrictorOrderExactOn() && !OrderingSearchEnabled(state)
                && !RestrictorPrePayable(state, cands, sel, j, r_rank, pool, combined, colour_feas, &colour_demand))
            { ++after; }
''' + g2)
open(p, 'w').write(s)
print('patched')
