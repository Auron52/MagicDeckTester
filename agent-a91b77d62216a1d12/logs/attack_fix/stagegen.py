#!/usr/bin/env python3
"""Build the per-commit trees by DOWNGRADING the final tree one commit at a time:
   H (final) -> G -> F -> E -> D -> C -> B -> A -> 0, and check stage 0 == ccacda4e byte-for-byte.
   Commits: A F1+F6 | B F2 | C F4a+F3+F4b | D F5.1 | E F5.4 | F F5.3+F5.2 | G KINDS+PAYER | H AOW.
   Usage: stagegen.py <outdir>   (writes <outdir>/<stage>/<relpath> for every stage)"""
import os, sys
W = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a91b77d62216a1d12'
ORIG = W + '/logs/attack_fix/stage/orig'
FILES = {'arm': 'src/ai/HeuristicArm.h', 'ts': 'src/ai/TurnSolver.cpp', 'dph': 'src/ai/DecisionProviders.h',
         'dpc': 'src/ai/DecisionProviders.cpp', 'land': 'src/ai/LandPlay.cpp', 'cmake': 'CMakeLists.txt',
         'tap': 'test/unit/test_attack_projection.cpp', 'bsw': 'test/unit/test_bruna_sweep.cpp'}
ORIGNAME = {'arm': 'HeuristicArm.h', 'ts': 'TurnSolver.cpp', 'dph': 'DecisionProviders.h',
            'dpc': 'DecisionProviders.cpp', 'land': 'LandPlay.cpp', 'cmake': 'CMakeLists.txt',
            'bsw': 'test_bruna_sweep.cpp'}


def rep(s, old, new, cnt=1, tag=''):
    n = s.count(old)
    assert n == cnt, 'rep %s: found %d (want %d): %r' % (tag, n, cnt, old[:140])
    return s.replace(old, new)


def cut(s, start, end, tag=''):
    """Remove s[start_anchor : end_anchor) (end anchor kept)."""
    assert s.count(start) == 1, 'cut start %s: %d %r' % (tag, s.count(start), start[:100])
    a = s.index(start)
    b = s.index(end, a)
    return s[:a] + s[b:]


def between(s, start, end):
    a = s.index(start)
    b = s.index(end, a)
    return s[a:b]


def drop_slot(arm, slot):
    lines = arm.split('\n')
    e = [i for i, l in enumerate(lines) if l.startswith('    %s,' % slot)]
    n = [i for i, l in enumerate(lines) if l.strip() == '"MTG_%s",' % slot]
    assert len(e) == 1 and len(n) == 1, (slot, e, n)
    del lines[n[0]]
    del lines[e[0]]
    return '\n'.join(lines)


def drop_test(tap, header):
    """Remove the test section that starts at `header` (up to the next '\n// ---- ' or EOF)."""
    assert tap.count(header) == 1, header
    a = tap.index(header)
    b = tap.find('\n// ---- ', a + 1)
    if b < 0:
        return tap[:a].rstrip('\n') + '\n'
    return tap[:a] + tap[b + 1:]


def load(base, names):
    return {k: open(os.path.join(base, v)).read() for k, v in names.items()}


final = load(W, FILES)
orig = load(ORIG, ORIGNAME)
orig['tap'] = None

o = orig['ts']
ORIG_STRUCT_CTX = between(o, 'struct AttackTapCtx\n{', 'static int SumYield(')
ORIG_BUILD = between(o, 'static void BuildAttackTapCtx(', '// The attack damage the subset loses to its own taps.')
ORIG_LOSS = between(o, '// The attack damage the subset loses to its own taps.', '// Pre-computed per-permanent on-cast trigger')
STAGEA_LOSS = rep(ORIG_LOSS, '''// `supply_total` the pool it was judged payable from (the credited `eff` when credited), and''',
                  '''// `supply` the pool it was judged payable from (the credited `eff` when credited), and''', tag='A1')
STAGEA_LOSS = rep(STAGEA_LOSS, '''                         int cost_mv, int supply_total, int legacy_nc_total)
{
    const bool need_exact = AttackTapNeedExactEnabled();
    if (need_exact && !ctx.pre_combat) { return 0; }          // no attack left to lose
''', '''                         int cost_mv, const ManaPool& supply, int legacy_nc_total)
{
    const bool need_exact = AttackTapNeedExactEnabled();
    if (need_exact && !ctx.pre_combat) { return 0; }          // no attack left to lose
    // MTG_ATTACK_TAP_ASTROLABE_CREDIT: a phantom conversion unit is not supply (see the lever).
    const int supply_total = supply.Total()
        - (AttackTapAstrolabeCreditEnabled() ? std::min(supply.wild_phantom, supply.wild) : 0);
''', tag='A2')

STAGEC_DISCOUNT = '''// `spent` receives the bitmask of the sources the discount spent (indices into the sorted `srcs`, a
// prefix) -- what a re-projection taps (MTG_ATTACK_TAP_REPROJECT). Past 64 sources (never seen) only
// the part of the prefix that fits the mask is recorded.
static int AttackTapDiscount(const std::vector<AttackingManaSource>& srcs, int need, std::uint64_t* spent)
{
    *spent = 0;
    if (need <= 0 || srcs.empty() || !AttackTapDiscountEnabled()) { return 0; }
    int covered = 0, lost = 0;
    for (int i = 0; i < static_cast<int>(srcs.size()); ++i)
    {
        if (covered >= need) { break; }
        covered += srcs[static_cast<std::size_t>(i)].yield;
        lost    += srcs[static_cast<std::size_t>(i)].power;
        if (i < 64) { *spent |= std::uint64_t{1} << i; }
    }
    return lost;
}
'''
ORIG_DISCOUNT = between(o, 'static int AttackTapDiscount(', '\n// Projection twin of Bruna')


def down_H(f):     # -> G: drop the attack-only seeding
    f = dict(f)
    f['ts'] = cut(f['ts'], '\n\n    // ---- The attack-only plan competes (MTG_SOLVE_ATTACK_ONLY_WIN',
                  '\n\n    // The default enumeration replaces the 2^m action powerset', tag='H1')
    f['arm'] = drop_slot(f['arm'], 'SOLVE_ATTACK_ONLY_WIN')
    f['tap'] = drop_test(f['tap'], '// ---- The attack-only plan competes in Solve (MTG_SOLVE_ATTACK_ONLY_WIN)')
    return f


def down_G(f):     # -> F: drop KINDS + PAYER
    f = dict(f)
    s = f['ts']
    s = cut(s, '// 8. MTG_ATTACK_TAP_ABILITY_KINDS -- point 2 covered', '\n// Per-candidate walk flags', tag='G1')
    s = rep(s, '''        std::vector<AttackingManaSource> srcs;
        std::vector<AttackingManaSource> ord[2];   // srcs in the executor's tap order (no food / food)
        bool ord_built[2] = { false, false };
    };
    std::vector<Entry> memo;
    // Point 9: per candidate, a creature cast that feeds a live subtype scaler (empty = none does), and
    // the pre-plan sources in the executor's tap order (no food / food).
    std::vector<unsigned char> cand_food;
    std::vector<AttackingManaSource> base_ord[2];
    bool base_ord_built[2] = { false, false };
''', '''        std::vector<AttackingManaSource> srcs;
    };
    std::vector<Entry> memo;
''', tag='G2')
    s = cut(s, "// Point 9: `srcs` in the order the executor's payment spends", 'static void BuildAttackTapCtx(', tag='G3')
    s = rep(s, '''        const bool more_kinds = AttackTapAbilityKindsEnabled();   // point 8
        for (int j = 0; j < static_cast<int>(cands.size()); ++j)
        {
            const Action& a = cands[j];
            if (a.sac_source_id <= 0) { continue; }
            bool taps = false;
            if (a.kind == Action::Kind::ActivatePermAbility)
            { taps = PermAbilityTaps(a.ability_mode); }           // SacDraw/Drain/ExileTop: no {T}
            else if (more_kinds)
            {
                taps = a.kind == Action::Kind::GarthActivate || a.kind == Action::Kind::TapForTokens
                    || a.kind == Action::Kind::GraveyardExileAbility
                    || a.kind == Action::Kind::PutFromHandAbility
                    || a.kind == Action::Kind::GraveyardPlayAbility;
            }
            if (!taps) { continue; }
            if (!attacker(own_perm(a.sac_source_id))) { continue; }
            set_bit(j, a.sac_source_id);
        }
    }
''', '''        for (int j = 0; j < static_cast<int>(cands.size()); ++j)
        {
            const Action& a = cands[j];
            if (a.kind != Action::Kind::ActivatePermAbility || a.sac_source_id <= 0) { continue; }
            if (!PermAbilityTaps(a.ability_mode)) { continue; }   // SacDraw/Drain/ExileTop: no {T}
            if (!attacker(own_perm(a.sac_source_id))) { continue; }
            set_bit(j, a.sac_source_id);
        }
    }
''', tag='G4')
    s = cut(s, "    // Point 9: which candidates feed a live subtype scaler (ComputePlanTraits' casts_scaler_food).",
            '    if (pending_atk > 0 && AttackAuraHostTapEnabled())', tag='G5')
    s = rep(s, '''    AttackTapCtx::Entry* e = nullptr;
    if (mask != 0)
    {
        for (AttackTapCtx::Entry& m : ctx.memo) { if (m.mask == mask) { e = &m; break; } }''',
            '''    if (mask != 0)
    {
        const AttackTapCtx::Entry* e = nullptr;
        for (const AttackTapCtx::Entry& m : ctx.memo) { if (m.mask == mask) { e = &m; break; } }''', tag='G6')
    s = rep(s, '''    // Point 9: the sources in the executor's tap order (memoised per stage-1 board and food flag).
    const std::vector<AttackingManaSource>* pay = s1;
    if ((need > 0 || need_nc > 0) && s1->size() > 1 && AttackTapPayerOrderEnabled())
    {
        bool food = false;
        if (!ctx.cand_food.empty())
        { for (int j : sel) { if (ctx.cand_food[static_cast<std::size_t>(j)]) { food = true; break; } } }
        std::vector<AttackingManaSource>& ord = e ? e->ord[food ? 1 : 0] : ctx.base_ord[food ? 1 : 0];
        bool& built = e ? e->ord_built[food ? 1 : 0] : ctx.base_ord_built[food ? 1 : 0];
        if (!built) { ExecutorPayerOrder(state, *s1, food, ord); built = true; }
        pay = &ord;
    }
    std::uint64_t spent = 0;
    const int lost_base = AttackTapDiscountSplit(*pay, need, need_nc, &spent);''',
            '''    std::uint64_t spent = 0;
    const int lost_base = AttackTapDiscountSplit(*s1, need, need_nc, &spent);''', tag='G7')
    s = rep(s, '''    std::uint64_t spent_key = 0;   // the spent sources as bits of s1 (the order-independent identity)
    if (reproj)
    {
        for (int i = 0; i < static_cast<int>(pay->size()) && i < 64; ++i)
        {
            if (!(spent & (std::uint64_t{1} << i))) { continue; }
            const int num = (*pay)[static_cast<std::size_t>(i)].number;
            for (int k = 0; k < static_cast<int>(s1->size()) && k < 64; ++k)
            { if ((*s1)[static_cast<std::size_t>(k)].number == num) { spent_key |= std::uint64_t{1} << k; break; } }
        }
    }
    const auto key = std::make_tuple(mask, spent_key, walk_key);''',
            '''    const auto key = std::make_tuple(mask, reproj ? spent : std::uint64_t{0}, walk_key);''', tag='G8')
    s = rep(s, '''        for (int i = 0; i < static_cast<int>(pay->size()) && i < 64; ++i)
        {
            if (!(spent & (std::uint64_t{1} << i))) { continue; }
            for (Permanent& p : t.battlefield)
            {
                if (p.controller_index == t.active_player_index
                    && p.card.m_number == (*pay)[static_cast<std::size_t>(i)].number)
                { p.tapped = true; break; }
            }
        }''', '''        for (int i = 0; i < static_cast<int>(s1->size()) && i < 64; ++i)
        {
            if (!(spent & (std::uint64_t{1} << i))) { continue; }
            for (Permanent& p : t.battlefield)
            {
                if (p.controller_index == t.active_player_index
                    && p.card.m_number == (*s1)[static_cast<std::size_t>(i)].number)
                { p.tapped = true; break; }
            }
        }''', tag='G9')
    f['ts'] = s
    f['arm'] = drop_slot(drop_slot(f['arm'], 'ATTACK_TAP_PAYER_ORDER'), 'ATTACK_TAP_ABILITY_KINDS')
    f['tap'] = drop_test(drop_test(f['tap'], '// ---- Point 9: the executor'), '// ---- Point 8: Garth')
    return f


def down_F(f):     # -> E: drop AURA_HOST + CREATURE_ONLY
    f = dict(f)
    s = f['ts']
    s = rep(s, '''// `number` = the source's card number, so a re-projection (MTG_ATTACK_TAP_REPROJECT) can tap the very
// sources the discount spent; `nc_ok` = its mana can pay a NONCREATURE spell (MTG_ATTACK_TAP_CREATURE_ONLY).
struct AttackingManaSource { int power; int yield; int number = 0; bool nc_ok = true; };''',
            '''// `number` = the source's card number, so a re-projection (MTG_ATTACK_TAP_REPROJECT) can tap the very
// sources the discount spent.
struct AttackingManaSource { int power; int yield; int number = 0; };''', tag='F1')
    s = rep(s, '''        out.push_back({ power, y >= 0 ? y : ManaProducedPerTap(*def), p.card.m_number,
                        !def->params.creature_mana_only });''',
            '''        out.push_back({ power, y >= 0 ? y : ManaProducedPerTap(*def), p.card.m_number });''', tag='F2')
    a = s.index('// The split cover (MTG_ATTACK_TAP_CREATURE_ONLY)')
    b = s.index('\n// Projection twin of Bruna', a)
    s = s[:a] + STAGEC_DISCOUNT + s[b:]
    s = rep(s, '''//    attack came from), memoised by which sources are spent -- a prefix of the power-sorted list
//    except under the split cover below, so about #sources + 1 re-projections per enumeration.''',
            '''//    attack came from), memoised by which sources are spent -- a prefix of the power-sorted list,
//    so at most #sources + 1 re-projections per enumeration.''', tag='F3')
    s = cut(s, '// 6. MTG_ATTACK_AURA_HOST_TAP -- an Aura that taps its host', 'static bool AttackTapReprojectEnabled()', tag='F4')
    s = cut(s, 'static bool AttackAuraHostTapEnabled()', '\n// Per-candidate walk flags', tag='F5')
    s = rep(s, '''    // Per candidate: the bit of the attacking creature its {T} ability (or its tap-on-entry Aura)
    // takes out of the attack, or -1.''', '''    // Per candidate: the bit of the attacking creature its {T} ability taps, or -1.''', tag='F6')
    s = cut(s, '    if (pending_atk > 0 && AttackAuraHostTapEnabled())', '    // Stage-2 walk flags.', tag='F7')
    s = rep(s, '''// value and `supply` the pool it was judged payable from (the credited `eff` when credited);
// `nc_cost_mv` / `nc_supply` the same for its noncreature share (MTG_ATTACK_TAP_CREATURE_ONLY), and
// `legacy_nc_total` the pool_noncreature total the legacy need was computed against.''',
            '''// value and `supply` the pool it was judged payable from (the credited `eff` when credited), and
// `legacy_nc_total` the pool_noncreature total the legacy need was computed against.''', tag='F8')
    s = rep(s, '''                         const std::vector<int>& sel, int cost_mv, const ManaPool& supply,
                         int legacy_nc_total, int nc_cost_mv, const ManaPool& nc_supply)''',
            '''                         const std::vector<int>& sel, int cost_mv, const ManaPool& supply,
                         int legacy_nc_total)''', tag='F9')
    s = rep(s, '''    // MTG_ATTACK_TAP_CREATURE_ONLY: the noncreature share is paid from noncreature-spendable supply
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
    const int lost_base = AttackTapDiscountSplit(*s1, need, need_nc, &spent);''',
            '''    std::uint64_t spent = 0;
    const int lost_base = AttackTapDiscount(*s1, need, &spent);''', tag='F10')
    s = rep(s, '''                                               (credited ? eff : pool), pool_noncreature.Total(),
                                               noncreature_combined.ManaValue(),
                                               (credited ? eff_nc : pool_noncreature));''',
            '''                                               (credited ? eff : pool), pool_noncreature.Total());''',
            cnt=2, tag='F11')
    f['ts'] = s
    f['arm'] = drop_slot(drop_slot(f['arm'], 'ATTACK_TAP_CREATURE_ONLY'), 'ATTACK_AURA_HOST_TAP')
    f['tap'] = drop_test(drop_test(f['tap'], '// ---- F5.2: creature-only mana'), '// ---- F5.3: an Aura that taps')
    return f


def down_E(f):     # -> D: drop the bounce guard
    f = dict(f)
    s = f['land']
    s = cut(s, '    // MTG_LAND_BOUNCE_GUARD (default ON; =0 -> any other land suffices).', '\n    // Tutor-top combo hold', tag='E1')
    s = rep(s, '''            if (def->params.etb_bounce_land && (!has_other_land || karoo_returns_karoo())) { continue; }''',
            '''            if (def->params.etb_bounce_land && !has_other_land) { continue; }''', tag='E2')
    f['land'] = s
    f['arm'] = drop_slot(f['arm'], 'LAND_BOUNCE_GUARD')
    f['tap'] = drop_test(f['tap'], '// ---- F5.4: no Karoo-for-Karoo land drop')
    return f


def down_D(f):     # -> C: drop the Bruna short-circuit
    f = dict(f)
    f['dph'] = cut(f['dph'], '    // Board-lethal short-circuit (MTG_BRUNA_LETHAL_SHORTCIRCUIT, default ON)', '};\n\n// Breaching Dragonstorm', tag='D1')
    f['dpc'] = cut(f['dpc'], '// ---- BrunaProvider::UseLethalShortCircuit (MTG_BRUNA_LETHAL_SHORTCIRCUIT, default ON)',
                   '// ---- BrunaProvider::CastOrderRank (MTG_BRUNA_ORDER, PROPOSED, default OFF)', tag='D2')
    f['bsw'] = cut(f['bsw'], "    // Out of attack-only range: Bruna's gather puts the HAND Colossification",
                   '    CHECK(OnlyHostOf(TurnSolver::EnumerateMainPlans(b.s, true), "Colossification") == pilgrim);\n    CHECK(SolveHostOf(b.s, "Colossification") == pilgrim);\n    (void)bruna;', tag='D3')
    f['arm'] = drop_slot(f['arm'], 'BRUNA_LETHAL_SHORTCIRCUIT')
    f['tap'] = drop_test(f['tap'], '// ---- F5.1: Bruna opts into the board-lethal short-circuit')
    return f


def down_C(f):     # -> B: drop REPROJECT + TEAM_PUMP_ETB + PLAN_LIFEGAIN
    f = dict(f)
    s = f['ts']
    s = rep(s, '''// `number` = the source's card number, so a re-projection (MTG_ATTACK_TAP_REPROJECT) can tap the very
// sources the discount spent.
struct AttackingManaSource { int power; int yield; int number = 0; };''',
            '''struct AttackingManaSource { int power; int yield; };''', tag='C1')
    s = rep(s, '''        out.push_back({ power, y >= 0 ? y : ManaProducedPerTap(*def), p.card.m_number });''',
            '''        out.push_back({ power, y >= 0 ? y : ManaProducedPerTap(*def) });''', tag='C2')
    s = rep(s, STAGEC_DISCOUNT, ORIG_DISCOUNT, tag='C3')
    s = cut(s, '// 4. MTG_ATTACK_TAP_REPROJECT -- the MANA half above', 'struct AttackTapCtx\n{', tag='C4')
    s = rep(s, between(s, 'struct AttackTapCtx\n{', 'static int SumYield('), ORIG_STRUCT_CTX, tag='C5')
    s = cut(s, '// A card that turns a life-gain EVENT into attack', "// The plan's CASTS in the order", tag='C6')
    s = cut(s, "// The attack after the plan's casts and life gains", 'static void BuildAttackTapCtx(', tag='C7')
    s = rep(s, between(s, 'static void BuildAttackTapCtx(', '// The attack the subset changes by its own taps'),
            ORIG_BUILD, tag='C8')
    s = rep(s, between(s, '// The attack the subset changes by its own taps', '// ---- MTG_ATTACK_CANNONS_TRIGGER'),
            STAGEA_LOSS, tag='C9')
    s = rep(s, '''        // {T} activations (MTG_ATTACK_TAP_ABILITY_DISCOUNT); both off == the legacy line. It also
        // carries this round's re-projection terms -- see the numbered points that follow
        // AttackTapNeedExactEnabled. Lockstep twin in EnumeratePlans.
        const int atk_tap_lost = AttackTapLoss(state, atk_tap_ctx, cands, sel, combined.ManaValue(),''',
            '''        // {T} activations (MTG_ATTACK_TAP_ABILITY_DISCOUNT); both off == the legacy line.
        const int atk_tap_lost = AttackTapLoss(state, atk_tap_ctx, sel, combined.ManaValue(),''', tag='C10')
    s = rep(s, '''        // {T} activations (MTG_ATTACK_TAP_ABILITY_DISCOUNT); both off == the legacy line. Twin of Solve
        // (re-projection terms included).
        const int atk_tap_lost = AttackTapLoss(state, atk_tap_ctx, cands, sel, combined.ManaValue(),''',
            '''        // {T} activations (MTG_ATTACK_TAP_ABILITY_DISCOUNT); both off == the legacy line. Twin of Solve.
        const int atk_tap_lost = AttackTapLoss(state, atk_tap_ctx, sel, combined.ManaValue(),''', tag='C11')
    f['ts'] = s
    for slot in ('ATTACK_PLAN_LIFEGAIN', 'ATTACK_TEAM_PUMP_ETB', 'ATTACK_TAP_REPROJECT'):
        f['arm'] = drop_slot(f['arm'], slot)
    t = f['tap']
    for h in ('// ---- F4b: the plan', '// ---- F4a: a spent dork', '// ---- F3: a hard-cast Craterhoof'):
        t = drop_test(t, h)
    f['tap'] = t
    return f


def down_B(f):     # -> A: drop the Cannons term
    f = dict(f)
    s = f['ts']
    s = cut(s, "// The plan's CASTS in the order", 'static void BuildAttackTapCtx(', tag='B1')
    s = cut(s, "// ---- MTG_ATTACK_CANNONS_TRIGGER", '// Pre-computed per-permanent on-cast trigger', tag='B2')
    s = rep(s, '''        // Mana Cannons' cast triggers (MTG_ATTACK_CANNONS_TRIGGER; 0 unless a Cannons is live).
        const int cannons_dmg = CannonsTriggerDamage(state, cannons_ctx, cands, sel);
        bool wins = (projected_atk + direct_dmg + extra_lethal + cannons_dmg) >= state.Opponent().life;''',
            '''        bool wins = (projected_atk + direct_dmg + extra_lethal) >= state.Opponent().life;''', tag='B3')
    s = rep(s, '''        // Mana Cannons' cast triggers (MTG_ATTACK_CANNONS_TRIGGER) -- twin of Solve's.
        const int cannons_dmg = CannonsTriggerDamage(state, cannons_ctx, cands, sel);
        bool wins = (projected_atk + direct_dmg + cannons_dmg) >= state.Opponent().life;''',
            '''        bool wins = (projected_atk + direct_dmg) >= state.Opponent().life;''', tag='B4')
    s = rep(s, '''    // Mana Cannons on the battlefield / castable, for the cast-trigger damage term. Lockstep twin.
    const CannonsCtx cannons_ctx = BuildCannonsCtx(state, cands);
''', '', cnt=2, tag='B5')
    f['ts'] = s
    f['arm'] = drop_slot(f['arm'], 'ATTACK_CANNONS_TRIGGER')
    f['tap'] = drop_test(f['tap'], "// ---- F2: Mana Cannons' cast triggers")
    return f


def down_A(f):     # -> 0: drop DOMAIN + ASTRO, the test file and its registration
    f = dict(f)
    s = f['ts']
    s = cut(s, "\n\n// MTG_ATTACK_DOMAIN_DORK_YIELD -- a DOMAIN dork's yield is the pool's", '\nstatic std::vector<AttackingManaSource> CollectAttackingManaSources', tag='A1')
    s = rep(s, '''        int y = PermanentManaYield(state, p, *def);
        if (def->params.domain_mana && AttackDomainDorkYieldEnabled())
        { y = static_cast<int>(DomainColors(state, active).size()) + LandAuraBonus(state, p); }''',
            '''        const int y = PermanentManaYield(state, p, *def);''', tag='A2')
    s = cut(s, '// 3. MTG_ATTACK_TAP_ASTROLABE_CREDIT', '\nstruct AttackTapCtx\n{', tag='A3')
    s = rep(s, STAGEA_LOSS, ORIG_LOSS, tag='A4')
    s = rep(s, '''(credited ? eff : pool), pool_noncreature.Total());''',
            '''(credited ? eff : pool).Total(), pool_noncreature.Total());''', cnt=2, tag='A5')
    f['ts'] = s
    f['arm'] = drop_slot(drop_slot(f['arm'], 'ATTACK_TAP_ASTROLABE_CREDIT'), 'ATTACK_DOMAIN_DORK_YIELD')
    f['cmake'] = rep(f['cmake'], '    test/unit/test_attack_projection.cpp\n', '', tag='A6')
    f['tap'] = None
    return f


def main():
    out = sys.argv[1]
    stages = {'H': final}
    order = [('H', 'G', down_H), ('G', 'F', down_G), ('F', 'E', down_F), ('E', 'D', down_E),
             ('D', 'C', down_D), ('C', 'B', down_C), ('B', 'A', down_B), ('A', '0', down_A)]
    for frm, to, fn in order:
        stages[to] = fn(stages[frm])
    bad = [k for k in orig if stages['0'][k] != orig[k]]
    os.makedirs(out, exist_ok=True)
    if bad:
        for k in bad:
            open(os.path.join(out, 'stage0_' + k), 'w').write(stages['0'][k] or '')
        raise SystemExit('stage 0 differs from ccacda4e in: %s (written to %s/stage0_*)' % (bad, out))
    print('stage 0 == ccacda4e: OK')
    for st, f in stages.items():
        for k, rel in FILES.items():
            p = os.path.join(out, st, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            if f[k] is None:
                continue
            open(p, 'w').write(f[k])
    print('wrote stages to', out)


main()
