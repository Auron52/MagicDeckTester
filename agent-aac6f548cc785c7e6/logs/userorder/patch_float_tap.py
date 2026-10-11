"""Fix (3): MTG_LINE_FLOAT_TAP_FIRST -- the autonomous payer taps a spare source for a generic pip before it
spends floating colour the rest of the line still owes."""
import os
WT = os.environ.get('PATCH_ROOT', '/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6')


def sub(path, old, new, count=1):
    s = open(path).read()
    assert s.count(old) == count, (path, old[:80], s.count(old))
    s = s.replace(old, new)
    open(path, 'w').write(s)


# 1. heurarm slot, appended after FB_HOLD_BANKED (fix 2 lands first)
h = WT + '/src/ai/HeuristicArm.h'
t = open(h).read()
i = t.index('    FB_HOLD_BANKED,')
j = t.index('\n', i) + 1
t = t[:j] + ('    LINE_FLOAT_TAP_FIRST,     // MTG_LINE_FLOAT_TAP_FIRST (autonomous twin of the human-only MTG_PAY_LINE_FLOAT_HOLD) a payment whose GENERIC pips would spend floating colour the rest of the applying line still owes first tries tapping a SPARE source for them -- one whose mana for this payment no later pip wants ({C}, e.g. an Unclaimed Territory under a noncreature spell), never a depletion / storage / pay-sac source or a dork, and not a creature-only-colour land that is the only source of a colour the line still owes -- with a plain-payment fallback. dragonstorm d3 s7100081 T2: Medallion\'s {2} took the floating {R}{R} and left Pyretic\'s {R} to a Territory that cannot pay it. Executor + rollout + payability walks (shared payer). Not in human play. Default ON; =0 -> float first\n') + t[j:]
k = t.index('        "MTG_FB_HOLD_BANKED",\n') + len('        "MTG_FB_HOLD_BANKED",\n')
t = t[:k] + '        "MTG_LINE_FLOAT_TAP_FIRST",\n' + t[k:]
open(h, 'w').write(t)

# 2. reader
sub(WT + '/src/ai/EngineFlags.h',
    '''inline bool FbHoldBankedOn()
{
    static const bool env_on = EnvOn("MTG_FB_HOLD_BANKED", true);
    return heurarm::Flag(heurarm::FB_HOLD_BANKED, env_on);
}
''',
    '''inline bool FbHoldBankedOn()
{
    static const bool env_on = EnvOn("MTG_FB_HOLD_BANKED", true);
    return heurarm::Flag(heurarm::FB_HOLD_BANKED, env_on);
}

// MTG_LINE_FLOAT_TAP_FIRST (DEFAULT ON; =0 disables) -- the shared payer's line-aware float rung
// (TapForCostSharedImpl, ManaPayment.cpp). One payer for executor, rollout and the payability walks.
inline bool LineFloatTapFirstOn()
{
    static const bool env_on = EnvOn("MTG_LINE_FLOAT_TAP_FIRST", true);
    return heurarm::Flag(heurarm::LINE_FLOAT_TAP_FIRST, env_on);
}
''')

# 3. the per-attempt generic hold on the reserve drain
m = WT + '/src/ai/ManaPayment.cpp'
s = open(m).read()
anchor = 'static bool TapForCostSharedOnceImpl(GameState& state, const ManaCost& cost_in, bool for_creature,'
assert anchor in s
s = s.replace(anchor, '''// MTG_LINE_FLOAT_TAP_FIRST: generic pips the turn-scoped reserve must NOT pay in this attempt (they go to
// the tap loop instead). Set only around the rung's held attempt in TapForCostSharedImpl; 0 everywhere else.
static thread_local int t_float_generic_hold = 0;
struct FloatGenericHoldScope
{
    int prev;
    explicit FloatGenericHoldScope(int k) : prev(t_float_generic_hold) { t_float_generic_hold = k; }
    ~FloatGenericHoldScope() { t_float_generic_hold = prev; }
    FloatGenericHoldScope(const FloatGenericHoldScope&)            = delete;
    FloatGenericHoldScope& operator=(const FloatGenericHoldScope&) = delete;
};

''' + anchor, 1)
open(m, 'w').write(s)
sub(m,
    '''    SpendFloatingTowardCost(state.floating_mana, cost);
    // Publish this payment's coloured need''',
    '''    {
        // MTG_LINE_FLOAT_TAP_FIRST: the rung's held generic pips skip the reserve (0 -> the plain drain).
        const int fgh = std::min(t_float_generic_hold, cost.generic);
        cost.generic -= fgh;
        SpendFloatingTowardCost(state.floating_mana, cost);
        cost.generic += fgh;
    }
    // Publish this payment's coloured need''')

# 4. the planner + the rung
impl_def = '''static bool TapForCostSharedImpl(GameState& state, const ManaCost& cost_in, bool for_creature,
                                 ManaPool* available, bool honor_legacy_cco)
{
    if (tapstats::Enabled()) { tapstats::g_pay_impl.fetch_add(1, std::memory_order_relaxed); }'''
sub(m, impl_def, '''// ---- MTG_LINE_FLOAT_TAP_FIRST (the autonomous line-aware float rung) -------------------------------------
//
// The payer spends the turn-scoped reserve before it taps anything (SpendFloatingTowardCost), generic
// pips included. When the reserve's colours are what the REST of the applying line still owes, and an
// untapped source sits there whose mana this payment can use but no later pip wants, the reserve should
// pay the line and the source the generic. dragonstorm d3 s7100081 T2: Mountain + Unclaimed Territory;
// Rite of Flame floats {R}{R}; Ruby Medallion's {2} spent BOTH, and Pyretic Ritual's {R} was left to the
// Territory, which makes only {C} for a noncreature spell -- the line "Rite, Medallion, Pyretic, Seething,
// Lathliss" (which the exact walk proves payable: {C} + {R} on the Medallion) lost two casts. The human
// play twin (MTG_PAY_LINE_FLOAT_HOLD) covers this payment's OWN float; this covers the turn's reserve.
//
// SPARE source, for this payment: a land or rock (no dork -- attack -- and no depletion / storage /
// pay-sac source -- banked or one-shot mana), whose payment-legal colours (ProducesForPayment) include no
// colour a later pip owes. A creature-only-colour land (Territory, Cavern) is spare for a noncreature
// payment only if every colour the line still owes that it could make for a later CREATURE is also on
// offer from the reserve or from another unrestricted untapped source -- otherwise it may be that
// colour's only provider (s7100151 T5: the Territory was Kolaghan's only {B}). The rung reserves every
// NON-spare source for one held attempt with `k` generic pips kept off the reserve, k = min(the generic
// the reserve would pay out of colours the line owes, the spare yield); any failure falls through to
// the unchanged sequence below. A later {C} pip in the line (lu.colorless) disables it.
static bool LineFloatTapFirstPlan(const GameState& state, const ManaCost& cost_in, bool for_creature,
                                  int& hold_k, std::uint64_t& hold_mask)
{
    hold_k = 0;
    hold_mask = 0;
    const ManaPool& f = state.floating_mana;
    if (cost_in.generic <= 0 || f.white + f.blue + f.black + f.red + f.green == 0) { return false; }
    const ManaCost& lu = g_line_unpaid_cost;
    if (lu.colorless > 0) { return false; }
    // Later demand: the line total minus this payment's own coloured pips (LineOwedAfterOwnPips' rule;
    // its own-pip scope is published only inside the attempt, so it is subtracted here directly).
    const int later[5] = { std::max(0, lu.white - cost_in.white), std::max(0, lu.blue - cost_in.blue),
                           std::max(0, lu.black - cost_in.black), std::max(0, lu.red - cost_in.red),
                           std::max(0, lu.green - cost_in.green) };
    if (later[0] + later[1] + later[2] + later[3] + later[4] == 0) { return false; }
    // What the reserve would pay toward the GENERIC, by colour: the drain with and without the generic.
    ManaPool r_pips = f; ManaCost c_pips = cost_in; c_pips.generic = 0;
    SpendFloatingTowardCost(r_pips, c_pips);
    ManaPool r_all = f;  ManaCost c_all = cost_in;
    SpendFloatingTowardCost(r_all, c_all);
    const int rp[5] = { r_pips.white, r_pips.blue, r_pips.black, r_pips.red, r_pips.green };
    const int ra[5] = { r_all.white, r_all.blue, r_all.black, r_all.red, r_all.green };
    int owed_spend = 0;
    for (int c = 0; c < 5; ++c)
    { owed_spend += std::max(0, (rp[c] - ra[c]) - std::max(0, rp[c] - later[c])); }
    if (owed_spend <= 0) { return false; }
    auto colour_idx = [](Color c) -> int
    {
        switch (c)
        {
            case Color::White: return 0; case Color::Blue: return 1; case Color::Black: return 2;
            case Color::Red:   return 3; case Color::Green: return 4; default: return -1;
        }
    };
    const int active = state.active_player_index;
    auto is_src = [&](const Permanent& p, const CardDefinition& d) -> bool
    {
        return d.tmpl == CardTemplate::BasicLand || d.params.mana_rock
            || (d.tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield));
    };
    // Colours an unrestricted untapped source other than `self` can make (for a later creature pip).
    auto other_makes = [&](int self, int ci) -> bool
    {
        if (rp[ci] > 0 || r_pips.wild > 0) { return true; }
        for (int q = 0; q < static_cast<int>(state.battlefield.size()); ++q)
        {
            if (q == self) { continue; }
            const Permanent& o = state.battlefield[static_cast<std::size_t>(q)];
            if (o.controller_index != active || o.tapped) { continue; }
            const CardDefinition* od = CardDatabase::Instance().LookupCached(o.card);
            if (od == nullptr || od->params.colored_creature_only || !is_src(o, *od)) { continue; }
            for (Color c : ProducesForPayment(state, active, *od, /*for_creature=*/true, &o))
            { if (colour_idx(c) == ci) { return true; } }
        }
        return false;
    };
    int spare = 0;
    const int n = static_cast<int>(state.battlefield.size());
    for (int i = 0; i < n; ++i)
    {
        const Permanent& p = state.battlefield[static_cast<std::size_t>(i)];
        if (p.controller_index != active || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d == nullptr || !is_src(p, *d)) { continue; }
        bool ok = d->tmpl != CardTemplate::ManaDork && !d->params.storage_land
               && d->params.enters_tapped_with_depletion == 0 && !d->params.creature_mana_only
               && !PaySacSpendableNow(state, p, *d);
        if (ok) { for (const Counter& c : p.counters) { if (c.type == Counter::Type::Depletion) { ok = false; break; } } }
        if (ok)
        {
            for (Color c : ProducesForPayment(state, active, *d, for_creature, &p))
            { const int ci = colour_idx(c); if (ci >= 0 && later[ci] > 0) { ok = false; break; } }
        }
        if (ok && d->params.colored_creature_only && !for_creature)
        {
            for (Color c : EffectiveProducesFor(state, active, *d, &p))
            { const int ci = colour_idx(c); if (ci >= 0 && later[ci] > 0 && !other_makes(i, ci)) { ok = false; break; } }
        }
        if (ok)
        {
            const int y = PermanentManaYield(state, p, *d);
            spare += y > 0 ? y : 0;
        }
        else if (i < 64) { hold_mask |= (1ull << i); }
    }
    hold_k = std::min(owed_spend, spare);
    return hold_k > 0;
}

''' + impl_def)
sub(m,
    '''    if (rmask != 0)
    {
        if (held_attempt(rmask, "impl.rmask")) { return true; }''',
    '''    // MTG_LINE_FLOAT_TAP_FIRST (see LineFloatTapFirstPlan): one held attempt that keeps the line's owed
    // float for the line and pays the generic off a spare source. Autonomous only (human play has its own
    // hold); inert without a live line, a floating colour and a spare source.
    if (LineFloatTapFirstOn() && !HumanPlayActive())
    {
        int fk = 0;
        std::uint64_t fmask = 0;
        if (LineFloatTapFirstPlan(state, cost_in, for_creature, fk, fmask))
        {
            FloatGenericHoldScope _fgh(fk);
            if (held_attempt(rmask | fmask, "impl.floattap")) { return true; }
        }
    }
    if (rmask != 0)
    {
        if (held_attempt(rmask, "impl.rmask")) { return true; }''')
print('ok')
