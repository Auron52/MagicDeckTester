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
