#!/usr/bin/env python3
"""AttackTapLoss: split cover (creature-only) + spent bitmask."""
W = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a91b77d62216a1d12'
p = W + '/src/ai/TurnSolver.cpp'
s = open(p).read()
old = '''    int need = need_exact ? cost_mv - (supply_total - y1) : cost_mv - legacy_nc_total;
    if (need_exact && nc_cost_mv > 0 && AttackTapCreatureOnlyEnabled())
    {
        bool all_nc = true;
        int  y_nc   = 0;
        for (const AttackingManaSource& sx : *s1) { if (!sx.nc_ok) { all_nc = false; break; } y_nc += sx.yield; }
        if (all_nc)   // an attacking creature-only source would need a split cover; leave that board alone
        {
            const int nc_total = nc_supply.Total()
                - (astro ? std::min(nc_supply.wild_phantom, nc_supply.wild) : 0);
            need = std::max(need, nc_cost_mv - (nc_total - y_nc));
        }
    }
    int spent = 0;
    const int lost_base = AttackTapDiscount(*s1, need, &spent);
'''
new = '''    const int need = need_exact ? cost_mv - (supply_total - y1) : cost_mv - legacy_nc_total;
    // MTG_ATTACK_TAP_CREATURE_ONLY: the noncreature share is paid from noncreature-spendable supply
    // only (pool_noncreature drops creature_mana_only sources), so its need is the noncreature cost
    // beyond the non-attacker part of THAT pool, covered by attacking sources whose mana it can spend.
    int need_nc = 0;
    if (need_exact && nc_cost_mv > 0 && AttackTapCreatureOnlyEnabled())
    {
        int y_nc = 0;
        for (const AttackingManaSource& sx : *s1) { if (sx.nc_ok) { y_nc += sx.yield; } }
        const int nc_total = nc_supply.Total()
            - (astro ? std::min(nc_supply.wild_phantom, nc_supply.wild) : 0);
        need_nc = nc_cost_mv - (nc_total - y_nc);
    }
    std::uint64_t spent = 0;
    const int lost_base = AttackTapDiscountSplit(*s1, need, need_nc, &spent);
'''
assert s.count(old) == 1
s = s.replace(old, new)
old = '''    const bool reproj = spent > 0 && (walk || AttackTapReprojectEnabled());'''
new = '''    const bool reproj = spent != 0 && (walk || AttackTapReprojectEnabled());'''
assert s.count(old) == 1
s = s.replace(old, new)
old = '''    // Memo key: the stage-1 taps, the spent prefix, and the subset's walk-relevant members (casts and'''
new = '''    // Memo key: the stage-1 taps, the spent sources, and the subset's walk-relevant members (casts and'''
assert s.count(old) == 1
s = s.replace(old, new)
old = '''    const auto key = std::make_tuple(mask, reproj ? spent : 0, walk_key);'''
new = '''    const auto key = std::make_tuple(mask, reproj ? spent : std::uint64_t{0}, walk_key);'''
assert s.count(old) == 1
s = s.replace(old, new)
old = '''        for (int i = 0; i < spent && i < static_cast<int>(s1->size()); ++i)
        {
            for (Permanent& p : t.battlefield)'''
new = '''        for (int i = 0; i < static_cast<int>(s1->size()) && i < 64; ++i)
        {
            if (!(spent & (std::uint64_t{1} << i))) { continue; }
            for (Permanent& p : t.battlefield)'''
assert s.count(old) == 1
s = s.replace(old, new)
old = '''    std::map<std::tuple<std::uint64_t, int, std::uint64_t>, int> memo2;   // (mask, spent, walk) -> attack'''
new = '''    std::map<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>, int> memo2;   // (mask, spent, walk) -> attack'''
assert s.count(old) == 1
s = s.replace(old, new)
old = '''//    re-projected with the spent sources TAPPED (PendingAttackDamage, the same function the pre-plan
//    attack came from), memoised by how many of the sorted sources are spent: at most #sources + 1
//    re-projections per enumeration.'''
new = '''//    re-projected with the spent sources TAPPED (PendingAttackDamage, the same function the pre-plan
//    attack came from), memoised by which sources are spent -- a prefix of the power-sorted list
//    except under the split cover below, so about #sources + 1 re-projections per enumeration.'''
assert s.count(old) == 1
s = s.replace(old, new)
old = '''// 7. MTG_ATTACK_TAP_CREATURE_ONLY -- creature-only mana (Somberwald Sage) sits in `supply` but cannot pay
//    the plan's NONCREATURE share, so an attacking dork has to: need = max(need, the noncreature cost
//    beyond the noncreature-spendable non-attacker supply).'''
new = '''// 7. MTG_ATTACK_TAP_CREATURE_ONLY -- creature-only mana (Somberwald Sage) sits in `supply` but cannot pay
//    the plan's NONCREATURE share, so an attacking dork has to: the noncreature share gets its own need
//    (its cost beyond the noncreature-spendable non-attacker supply), covered first by the attacking
//    sources whose mana a noncreature spell can spend (AttackTapDiscountSplit).'''
assert s.count(old) == 1
s = s.replace(old, new)
open(p, 'w').write(s)
print('ok')
