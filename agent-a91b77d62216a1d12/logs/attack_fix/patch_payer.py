#!/usr/bin/env python3
"""MTG_ATTACK_TAP_ABILITY_KINDS + MTG_ATTACK_TAP_PAYER_ORDER."""
W = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a91b77d62216a1d12'
p = W + '/src/ai/TurnSolver.cpp'
s = open(p).read()


def rep(old, new, cnt=1):
    global s
    assert s.count(old) == cnt, (s.count(old), old[:90])
    s = s.replace(old, new)


# ---- lever readers + doc points --------------------------------------------------------------------
rep('''static bool AttackTapCreatureOnlyEnabled()
{
    static const bool env_on = EnvOn("MTG_ATTACK_TAP_CREATURE_ONLY", true);   // DEFAULT ON; =0 all-spells need
    return heurarm::Flag(heurarm::ATTACK_TAP_CREATURE_ONLY, env_on);
}
''', '''static bool AttackTapCreatureOnlyEnabled()
{
    static const bool env_on = EnvOn("MTG_ATTACK_TAP_CREATURE_ONLY", true);   // DEFAULT ON; =0 all-spells need
    return heurarm::Flag(heurarm::ATTACK_TAP_CREATURE_ONLY, env_on);
}
// 8. MTG_ATTACK_TAP_ABILITY_KINDS -- point 2 covered ActivatePermAbility only, but five more action
//    kinds pay a {T} on their creature source: Garth One-Eye's conjure (GarthActivate), Krenko's tokens
//    (TapForTokens), Deathrite's exile (GraveyardExileAbility), Stoneforge's put (PutFromHandAbility)
//    and Kaldring's replay (GraveyardPlayAbility). FiveColour d0 gi123 (seed 1210124) T5, opponent at
//    19: {Unite the Coalition, Garth -> Black Lotus} read 10 direct + 10 attack = lethal with Garth's
//    own 5 still swinging; the real line dealt 15. (It surfaced once MTG_ATTACK_DOMAIN_DORK_YIELD made
//    the competing {Unite, Garth -> Regrowth} line read honestly: the false-lethal Lotus line was then
//    the only "win" left.) Same bits, same re-projection.
// 9. MTG_ATTACK_TAP_PAYER_ORDER -- WHICH attacking dorks pay. The discount spent the lowest-power
//    sources first, ties in board order; the executor spends its creature band in the provider's
//    ManaSourceRank order under the plan's traits (DecisionProviders.cpp "SCALER PLAN BIAS": on an
//    attack turn a subtype scaler taps FIRST -- one big tap instead of N bodies -- then the flat dorks,
//    a domain grower after them, Deathrite last), ties by ManaSourceHoldValue. Stompy d0 held-out
//    gi1118 (seed 9301119) T4: Craterhoof's 8 off three Forests left 5 to the four 2-power Elves; the
//    discount tapped Llanowar, Llanowar, Fyndhorn and then Archdruid (projected 10), the executor
//    tapped Archdruid + one Llanowar and swung for 24. The projection now asks the provider the same
//    question the payment asks, with the two traits that rank reads (attack_matters, casts_scaler_food)
//    set for the subset; memoised per board and food flag.
static bool AttackTapAbilityKindsEnabled()
{
    static const bool env_on = EnvOn("MTG_ATTACK_TAP_ABILITY_KINDS", true);   // DEFAULT ON; =0 PermAbility only
    return heurarm::Flag(heurarm::ATTACK_TAP_ABILITY_KINDS, env_on);
}
static bool AttackTapPayerOrderEnabled()
{
    static const bool env_on = EnvOn("MTG_ATTACK_TAP_PAYER_ORDER", true);   // DEFAULT ON; =0 lowest power first
    return heurarm::Flag(heurarm::ATTACK_TAP_PAYER_ORDER, env_on);
}
''')

# ---- ctx fields ------------------------------------------------------------------------------------
rep('''    struct Entry
    {
        std::uint64_t mask = 0;
        int pending = 0, yield = 0;
        std::vector<AttackingManaSource> srcs;
    };
    std::vector<Entry> memo;
    // Stage 2 (see points 4-6 above). cand_walk empty = no candidate starts a walk.''',
'''    struct Entry
    {
        std::uint64_t mask = 0;
        int pending = 0, yield = 0;
        std::vector<AttackingManaSource> srcs;
        std::vector<AttackingManaSource> ord[2];   // srcs in the executor's tap order (no food / food)
        bool ord_built[2] = { false, false };
    };
    std::vector<Entry> memo;
    // Point 9: per candidate, a creature cast that feeds a live subtype scaler (empty = none does), and
    // the pre-plan sources in the executor's tap order (no food / food).
    std::vector<unsigned char> cand_food;
    std::vector<AttackingManaSource> base_ord[2];
    bool base_ord_built[2] = { false, false };
    // Stage 2 (see points 4-6 above). cand_walk empty = no candidate starts a walk.''')

# ---- the order helper (before BuildAttackTapCtx) -----------------------------------------------------
rep('''static void BuildAttackTapCtx(const GameState& state, bool is_pre_combat, int pending_atk,''',
'''// Point 9: `srcs` in the order the executor's payment spends its creature band -- the provider's own
// ManaSourceRank, then ManaSourceHoldValue, under the traits the payment would see (pre-combat with an
// attacker, so attack_matters; `food` = the plan casts a live scaler's food). Lowest power, then the
// original order, break the remaining ties, so a provider that ranks every dork alike keeps the
// lowest-power-first spend exactly.
static void ExecutorPayerOrder(const GameState& state, const std::vector<AttackingManaSource>& srcs,
                               bool food, std::vector<AttackingManaSource>& out)
{
    PlanTraits tr;
    tr.attack_matters    = true;
    tr.casts_scaler_food = food;
    PlanTraitsScope scope(&tr);
    const DecisionProvider& prov = ResolveProvider(state);
    const int active = state.active_player_index;
    struct Key { int rank, hold, power, idx; };
    std::vector<Key> keys;
    keys.reserve(srcs.size());
    for (int i = 0; i < static_cast<int>(srcs.size()); ++i)
    {
        const Permanent* p = nullptr;
        for (const Permanent& q : state.battlefield)
        {
            if (q.controller_index == active && q.card.m_number == srcs[static_cast<std::size_t>(i)].number)
            { p = &q; break; }
        }
        const CardDefinition* d = p ? CardDatabase::Instance().LookupCached(p->card) : nullptr;
        const int rank = d ? prov.ManaSourceRank(state, *d) : std::numeric_limits<int>::max();
        const int hold = d ? prov.ManaSourceHoldValue(state, *p, *d) : 0;
        keys.push_back({ rank, hold, srcs[static_cast<std::size_t>(i)].power, i });
    }
    std::stable_sort(keys.begin(), keys.end(), [](const Key& a, const Key& b)
    {
        if (a.rank != b.rank)   { return a.rank < b.rank; }
        if (a.hold != b.hold)   { return a.hold < b.hold; }
        if (a.power != b.power) { return a.power < b.power; }
        return a.idx < b.idx;
    });
    out.clear();
    for (const Key& k : keys) { out.push_back(srcs[static_cast<std::size_t>(k.idx)]); }
}

static void BuildAttackTapCtx(const GameState& state, bool is_pre_combat, int pending_atk,''')

# ---- ability kinds ---------------------------------------------------------------------------------
rep('''    if (pending_atk > 0 && AttackTapAbilityDiscountEnabled())
    {
        for (int j = 0; j < static_cast<int>(cands.size()); ++j)
        {
            const Action& a = cands[j];
            if (a.kind != Action::Kind::ActivatePermAbility || a.sac_source_id <= 0) { continue; }
            if (!PermAbilityTaps(a.ability_mode)) { continue; }   // SacDraw/Drain/ExileTop: no {T}
            if (!attacker(own_perm(a.sac_source_id))) { continue; }
            set_bit(j, a.sac_source_id);
        }
    }''',
'''    if (pending_atk > 0 && AttackTapAbilityDiscountEnabled())
    {
        const bool more_kinds = AttackTapAbilityKindsEnabled();   // point 8
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
    // Point 9: which candidates feed a live subtype scaler (ComputePlanTraits' casts_scaler_food).
    if (pending_atk > 0 && AttackTapPayerOrderEnabled())
    {
        std::vector<const std::string*> scaler_subs;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != active || p.tapped || p.def_absent) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (d != nullptr && IsScaledManaDork(*d)) { scaler_subs.push_back(&d->params.mana_per_creature_subtype); }
        }
        if (!scaler_subs.empty())
        {
            for (int j = 0; j < static_cast<int>(cands.size()); ++j)
            {
                const Action& a = cands[j];
                if (a.kind != Action::Kind::CastFromHand) { continue; }
                const CardDefinition* d = a.def ? a.def : CardDatabase::Instance().Lookup(a.card_name);
                if (d == nullptr || !d->card.IsCreature()) { continue; }
                for (const std::string* sub : scaler_subs)
                {
                    if (!CardHasSubtype(d->card, *sub)) { continue; }
                    if (ctx.cand_food.empty()) { ctx.cand_food.assign(cands.size(), 0); }
                    ctx.cand_food[static_cast<std::size_t>(j)] = 1;
                    break;
                }
            }
        }
    }''')

# ---- AttackTapLoss: non-const entry, the payer order, canonical spent key ----------------------------
rep('''    const std::vector<AttackingManaSource>* s1 = ctx.srcs;
    int pend1 = ctx.pending_atk, y1 = ctx.src_yield;
    if (mask != 0)
    {
        const AttackTapCtx::Entry* e = nullptr;
        for (const AttackTapCtx::Entry& m : ctx.memo) { if (m.mask == mask) { e = &m; break; } }''',
'''    const std::vector<AttackingManaSource>* s1 = ctx.srcs;
    int pend1 = ctx.pending_atk, y1 = ctx.src_yield;
    AttackTapCtx::Entry* e = nullptr;
    if (mask != 0)
    {
        for (AttackTapCtx::Entry& m : ctx.memo) { if (m.mask == mask) { e = &m; break; } }''')
rep('''    std::uint64_t spent = 0;
    const int lost_base = AttackTapDiscountSplit(*s1, need, need_nc, &spent);''',
'''    // Point 9: the sources in the executor's tap order (memoised per stage-1 board and food flag).
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
    const int lost_base = AttackTapDiscountSplit(*pay, need, need_nc, &spent);''')
# The memo key must name the spent SOURCES, not positions in a food-dependent order: re-key on s1.
rep('''    const auto key = std::make_tuple(mask, reproj ? spent : std::uint64_t{0}, walk_key);''',
'''    std::uint64_t spent_key = 0;   // the spent sources as bits of s1 (the order-independent identity)
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
    const auto key = std::make_tuple(mask, spent_key, walk_key);''')
rep('''        for (int i = 0; i < static_cast<int>(s1->size()) && i < 64; ++i)
        {
            if (!(spent & (std::uint64_t{1} << i))) { continue; }
            for (Permanent& p : t.battlefield)
            {
                if (p.controller_index == t.active_player_index
                    && p.card.m_number == (*s1)[static_cast<std::size_t>(i)].number)
                { p.tapped = true; break; }
            }
        }''',
'''        for (int i = 0; i < static_cast<int>(pay->size()) && i < 64; ++i)
        {
            if (!(spent & (std::uint64_t{1} << i))) { continue; }
            for (Permanent& p : t.battlefield)
            {
                if (p.controller_index == t.active_player_index
                    && p.card.m_number == (*pay)[static_cast<std::size_t>(i)].number)
                { p.tapped = true; break; }
            }
        }''')
open(p, 'w').write(s)
print('ok')
