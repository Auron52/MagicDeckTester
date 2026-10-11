#!/usr/bin/env python3
"""Update both consider() sites: the new AttackTapLoss args + the Mana Cannons damage term."""
import os
W = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a91b77d62216a1d12'
p = os.path.join(W, 'src/ai/TurnSolver.cpp')
s = open(p).read()

# ---- Solve -------------------------------------------------------------------------------------
old = '''        // AttackTapLoss: the exact need (MTG_ATTACK_TAP_NEED_EXACT) and the subset's own non-mana
        // {T} activations (MTG_ATTACK_TAP_ABILITY_DISCOUNT); both off == the legacy line.
        const int atk_tap_lost = AttackTapLoss(state, atk_tap_ctx, sel, combined.ManaValue(),
                                               (credited ? eff : pool), pool_noncreature.Total());
        int projected_atk = pending_atk - atk_tap_lost + vial_haste_atk + haste_cast_atk
                          + noncreature_count * (prowess_attackers + haste_cast_prowess);
        // Deck-specific extra reach toward lethal (Land's Edge ammo + clairvoyant Treasure Hunt),
        // provider-owned. Built only when the deck has such a model (byte-identical otherwise).
        int extra_lethal = 0;
        if (has_extra_lethal)
        {
            casting.clear();
            for (int j : sel) { casting.push_back(cands[j].def); }
            extra_lethal = provider.ExtraLethalDamage(state, casting);
        }
        bool wins = (projected_atk + direct_dmg + extra_lethal) >= state.Opponent().life;
'''
new = '''        // AttackTapLoss: the exact need (MTG_ATTACK_TAP_NEED_EXACT) and the subset's own non-mana
        // {T} activations (MTG_ATTACK_TAP_ABILITY_DISCOUNT); both off == the legacy line. It also
        // carries the re-projection terms (the spent dorks' pump share, a cast Craterhoof's +X, the
        // plan's life gain into the per-life pumps, a tap-on-entry Aura's host) -- see points 3-7 at
        // AttackTapNeedExactEnabled. Lockstep twin in EnumeratePlans.
        const int atk_tap_lost = AttackTapLoss(state, atk_tap_ctx, cands, sel, combined.ManaValue(),
                                               (credited ? eff : pool), pool_noncreature.Total(),
                                               noncreature_combined.ManaValue(),
                                               (credited ? eff_nc : pool_noncreature));
        int projected_atk = pending_atk - atk_tap_lost + vial_haste_atk + haste_cast_atk
                          + noncreature_count * (prowess_attackers + haste_cast_prowess);
        // Deck-specific extra reach toward lethal (Land's Edge ammo + clairvoyant Treasure Hunt),
        // provider-owned. Built only when the deck has such a model (byte-identical otherwise).
        int extra_lethal = 0;
        if (has_extra_lethal)
        {
            casting.clear();
            for (int j : sel) { casting.push_back(cands[j].def); }
            extra_lethal = provider.ExtraLethalDamage(state, casting);
        }
        // Mana Cannons' cast triggers (MTG_ATTACK_CANNONS_TRIGGER; 0 unless a Cannons is live).
        const int cannons_dmg = CannonsTriggerDamage(state, cannons_ctx, cands, sel);
        bool wins = (projected_atk + direct_dmg + extra_lethal + cannons_dmg) >= state.Opponent().life;
'''
assert s.count(old) == 1, 'solve site'
s = s.replace(old, new)

# ---- EnumeratePlans ----------------------------------------------------------------------------
old = '''        // AttackTapLoss: the exact need (MTG_ATTACK_TAP_NEED_EXACT) and the subset's own non-mana
        // {T} activations (MTG_ATTACK_TAP_ABILITY_DISCOUNT); both off == the legacy line. Twin of Solve.
        const int atk_tap_lost = AttackTapLoss(state, atk_tap_ctx, sel, combined.ManaValue(),
                                               (credited ? eff : pool), pool_noncreature.Total());
        int projected_atk = pending_atk - atk_tap_lost + vial_haste_atk + haste_cast_atk
                          + noncreature_count * (prowess_attackers + haste_cast_prowess);
        bool wins = (projected_atk + direct_dmg) >= state.Opponent().life;
'''
new = '''        // AttackTapLoss: the exact need (MTG_ATTACK_TAP_NEED_EXACT) and the subset's own non-mana
        // {T} activations (MTG_ATTACK_TAP_ABILITY_DISCOUNT); both off == the legacy line. Twin of Solve
        // (re-projection terms included).
        const int atk_tap_lost = AttackTapLoss(state, atk_tap_ctx, cands, sel, combined.ManaValue(),
                                               (credited ? eff : pool), pool_noncreature.Total(),
                                               noncreature_combined.ManaValue(),
                                               (credited ? eff_nc : pool_noncreature));
        int projected_atk = pending_atk - atk_tap_lost + vial_haste_atk + haste_cast_atk
                          + noncreature_count * (prowess_attackers + haste_cast_prowess);
        // Mana Cannons' cast triggers (MTG_ATTACK_CANNONS_TRIGGER) -- twin of Solve's.
        const int cannons_dmg = CannonsTriggerDamage(state, cannons_ctx, cands, sel);
        bool wins = (projected_atk + direct_dmg + cannons_dmg) >= state.Opponent().life;
'''
assert s.count(old) == 1, 'enum site'
s = s.replace(old, new)

# ---- the per-enumeration contexts --------------------------------------------------------------
old = '''    AttackTapCtx atk_tap_ctx;
    BuildAttackTapCtx(state, is_pre_combat, pending_atk, atk_mana_srcs, cands, atk_tap_ctx);
'''
new = '''    AttackTapCtx atk_tap_ctx;
    BuildAttackTapCtx(state, is_pre_combat, pending_atk, atk_mana_srcs, cands, atk_tap_ctx);
    // Mana Cannons on the battlefield / castable, for the cast-trigger damage term. Lockstep twin.
    const CannonsCtx cannons_ctx = BuildCannonsCtx(state, cands);
'''
assert s.count(old) == 2, 'ctx sites %d' % s.count(old)
s = s.replace(old, new)
open(p, 'w').write(s)
print('ok')
