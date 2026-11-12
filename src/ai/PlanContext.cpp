#include "PlanContext.h"
#include "TurnSolver.h"   // Action (kept out of the header on purpose)

// thread_local because plan enumeration runs concurrently across the goldfish runner's workers --
// same reason g_real_resolution and g_search_candidate_enum are thread_local.
namespace
{
    thread_local const PlanContext* t_current = nullptr;
    thread_local const PlanTraits*  t_traits  = nullptr;
    thread_local const PlanTraitsScope* t_traits_scope = nullptr;
}

const PlanContext* CurrentPlanContext() { return t_current; }

PlanContextScope::PlanContextScope(const PlanContext* pc) : m_prev(t_current) { t_current = pc; }
PlanContextScope::~PlanContextScope() { t_current = m_prev; }

const PlanTraits* CurrentPlanTraits() { return t_traits; }

PlanTraitsScope::PlanTraitsScope(const PlanTraits* pt)
    : m_prev(t_traits), m_current(pt), m_prev_scope(t_traits_scope)
{
    t_traits = pt;
    t_traits_scope = this;
}
PlanTraitsScope::~PlanTraitsScope()
{
    t_traits = m_prev;
    t_traits_scope = m_prev_scope;
}

std::uint64_t PlanTraitsContentHash(const PlanTraits& t)
{
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](std::uint64_t v) { h = (h ^ v) * 1099511628211ull; h ^= h >> 29; };
    auto mix_def = [&mix](const CardDefinition* d)
    { mix(d != nullptr ? static_cast<std::uint64_t>(d->card.m_name_hash) : 0x5EEDull); };
    mix(static_cast<std::uint64_t>(t.main2) | (static_cast<std::uint64_t>(t.plan_has_own_pump) << 1)
        | (static_cast<std::uint64_t>(t.copy_magnet_live) << 2) | (static_cast<std::uint64_t>(t.bodies_are_multipliers) << 3)
        | (static_cast<std::uint64_t>(t.casts_scaler_food) << 4) | (static_cast<std::uint64_t>(t.attack_matters) << 5)
        | (static_cast<std::uint64_t>(t.restr_creature_up) << 6) | (static_cast<std::uint64_t>(t.restr_inelig_overflow) << 7)
        | (static_cast<std::uint64_t>(t.mid_turn_casts) << 8) | (static_cast<std::uint64_t>(t.own_board_hazard) << 9));
    mix(static_cast<std::uint64_t>(t.pump_target_card));
    mix(static_cast<std::uint64_t>(t.mana_casts));
    mix(static_cast<std::uint64_t>(t.cast_color_mask));
    mix(static_cast<std::uint64_t>(t.cast_mv_total));
    mix(static_cast<std::uint64_t>(t.cast_name_count));
    for (int i = 0; i < PlanTraits::kMaxCastNames; ++i)
    { mix(t.cast_names[i] != nullptr ? std::hash<std::string>{}(*t.cast_names[i]) : 0x0CA5ull); }
    for (int c = 0; c < 5; ++c)
    {
        mix(static_cast<std::uint64_t>(t.cast_pips[c]));
        mix(static_cast<std::uint64_t>(t.act_pips[c]));
        mix(static_cast<std::uint64_t>(t.act_pips_cab[c]));
        mix(static_cast<std::uint64_t>(t.cycle_seed_pips[c]));
    }
    mix(static_cast<std::uint64_t>(t.act_c_pips));
    mix(static_cast<std::uint64_t>(t.fade_exhaust_count));
    for (int i = 0; i < PlanTraits::kMaxFadeExhaust; ++i) { mix(static_cast<std::uint64_t>(t.fade_exhaust_src[i])); }
    mix(static_cast<std::uint64_t>(t.act_src_count));
    for (int i = 0; i < PlanTraits::kMaxActSrcs; ++i) { mix(static_cast<std::uint64_t>(t.act_src_nums[i])); }
    mix(static_cast<std::uint64_t>(t.act_mana_total));
    mix_def(t.cycle_seed_def);
    mix(static_cast<std::uint64_t>(t.cycle_seed_hand_floor));
    // restr_pp is a CardParams inside a process-lifetime CardDefinition: its ADDRESS is its identity. The
    // key VALUE then differs run to run, the partition (which states share an entry) does not.
    mix(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(t.restr_pp)));
    mix(static_cast<std::uint64_t>(t.restr_inelig_count));
    for (int i = 0; i < PlanTraits::kMaxRestrCasts; ++i)
    {
        mix_def(t.restr_inelig_def[i]);
        mix(static_cast<std::uint64_t>(t.restr_inelig_plan_n[i]));
        mix(static_cast<std::uint64_t>(t.restr_inelig_hand0[i]));
        mix(static_cast<std::uint64_t>(t.restr_inelig_mv[i]));
    }
    mix(static_cast<std::uint64_t>(t.act_restr_elig_mana));
    mix(static_cast<std::uint64_t>(t.act_restr_inelig_mana));
    // Prevent Damage deferral inputs: mixed only when present, so every other deck's hash is unchanged.
    if (t.pd_amp_count > 0 || t.pd_victim_count > 0 || t.pd_hazard_other)
    {
        mix(static_cast<std::uint64_t>(t.pd_amp_count) | (static_cast<std::uint64_t>(t.pd_victim_count) << 8)
            | (static_cast<std::uint64_t>(t.pd_hazard_other) << 16));
        for (int i = 0; i < t.pd_amp_count; ++i)
        {
            mix_def(t.pd_amp_key[i]); mix_def(t.pd_amp_def[i]);
            mix(static_cast<std::uint64_t>(t.pd_amp_key_plan_n[i]) | (static_cast<std::uint64_t>(t.pd_amp_key_hand0[i]) << 16));
        }
        for (int i = 0; i < t.pd_victim_count; ++i) { mix(static_cast<std::uint64_t>(t.pd_victims[i])); }
    }
    return h | 1ull;   // never 0: 0 is "no traits installed"
}

std::uint64_t CurrentPlanTraitsHash()
{
    if (t_traits == nullptr) { return 0; }
    const PlanTraitsScope* sc = t_traits_scope;
    if (sc == nullptr || sc->m_current != t_traits) { return PlanTraitsContentHash(*t_traits); }
    if (!sc->m_hash_valid) { sc->m_hash = PlanTraitsContentHash(*t_traits); sc->m_hash_valid = true; }
    return sc->m_hash;
}

std::shared_ptr<const PlanTraits> CapturePlanTraitsForRecord(const PlanTraits* pt)
{
    if (pt == nullptr) { return {}; }
    // PlanTraits are immutable while installed. In particular, paying a cycle
    // installs a separate cleared copy, so its saved pre-payment pointer still
    // identifies the outer owner here rather than the current cleared scope.
    for (const PlanTraitsScope* scope = t_traits_scope; scope != nullptr; scope = scope->m_prev_scope)
    {
        if (scope->m_current != pt) { continue; }
        if (!scope->m_record_snapshot)
        { scope->m_record_snapshot = std::make_shared<const PlanTraits>(*pt); }
        return scope->m_record_snapshot;
    }
    return std::make_shared<const PlanTraits>(*pt);
}

std::pair<const Action*, const Action*> PlanContextRest(const PlanContext* pc)
{
    if (pc == nullptr || pc->actions == nullptr || pc->index + 1 > pc->actions->size())
    { return { nullptr, nullptr }; }
    return { pc->actions->data() + pc->index + 1, pc->actions->data() + pc->actions->size() };
}
