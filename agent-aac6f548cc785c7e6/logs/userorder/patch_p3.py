WT='/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6'
h=WT+'/src/ai/HeuristicArm.h'
t=open(h).read()
anchor='    PAYABLE_ORDER_EXACT,'
i=t.index(anchor); j=t.index('\n',i)+1
slots=('    PAYABLE_ORDER_DYNAMIC_DECLINE, // MTG_PAYABLE_ORDER_DYNAMIC_DECLINE MTG_PAYABLE_ORDER keeps an order holding a cast with a live DYNAMIC per-target discount (Hinata, Dawn-Crowned; Soulfire) as given -- its stamped cost is only right at the position the enumeration gave it -- the rule LadderProjectable already applies. Both sites. Default ON; =0 -> walked like any other order\n')
t=t[:j]+slots+t[j:]
a2='        "MTG_PAYABLE_ORDER_EXACT",\n'
k=t.index(a2)+len(a2)
t=t[:k]+'        "MTG_PAYABLE_ORDER_DYNAMIC_DECLINE",\n'+t[k:]
open(h,'w').write(t)

p=WT+'/src/ai/ManaPayment.cpp'
s=open(p).read()
old_fn='''static bool PoCreditedProducer(const GameState& state, const std::vector<Action>& acts, const Action& a)
{'''
new_pre='''// MTG_PAYABLE_ORDER_DYNAMIC_DECLINE (DEFAULT ON; =0 -> such an order is walked like any other). A cast
// whose cost carries a live DYNAMIC per-target discount (Hinata, Dawn-Crowned's {1} per target, the
// Soulfire own-target discount) was priced for the board at the position the enumeration gave it; the
// board changes as the line resolves, so walking it at another position judges a cost that is about to
// change underneath it. LadderProjectable has always declined exactly this class; MTG_PAYABLE_ORDER did
// not. Declining keeps the order as given, at both sites -- the same as an order outside the domain.
static bool PayableOrderDynamicDeclineOn()
{
    static const bool env_on = EnvOn("MTG_PAYABLE_ORDER_DYNAMIC_DECLINE", true);   // DEFAULT ON; =0 disables
    return heurarm::Flag(heurarm::PAYABLE_ORDER_DYNAMIC_DECLINE, env_on);
}

// LadderProjectable's dynamic-cost test, alone (its spectacle and missing-definition declines are not
// part of this lever).
static bool PoDynamicDiscount(const GameState& state, const std::vector<Action>& acts,
                              const std::vector<int>& order)
{
    const int active = state.active_player_index;
    for (int i : order)
    {
        const Action& a = acts[i];
        if (a.alt_cost) { continue; }
        const CardDefinition* d = OrderDefOf(a);
        if (d == nullptr) { continue; }
        if (SoulfireOwnTargetDiscount(*d, state, active, a.soulfire_own_targets) > 0) { return true; }
        if (HinataGenericDiscount(*d, state, a.chosen_x) > 0) { return true; }
    }
    return false;
}

'''
assert old_fn in s
s=s.replace(old_fn,new_pre+old_fn)
old_dom='''    if (!any) { return; }
    g_po_stats.domain[site].fetch_add(1, std::memory_order_relaxed);
'''
new_dom='''    if (!any) { return; }
    if (PayableOrderDynamicDeclineOn() && PoDynamicDiscount(state, acts, order)) { return; }
    g_po_stats.domain[site].fetch_add(1, std::memory_order_relaxed);
'''
assert s.count(old_dom)==1
s=s.replace(old_dom,new_dom)
open(p,'w').write(s)
print('ok')
