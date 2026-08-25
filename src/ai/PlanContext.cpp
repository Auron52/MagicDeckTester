#include "PlanContext.h"
#include "TurnSolver.h"   // Action (kept out of the header on purpose)

// thread_local because plan enumeration runs concurrently across the goldfish runner's workers --
// same reason g_real_resolution and g_search_candidate_enum are thread_local.
namespace
{
    thread_local const PlanContext* t_current = nullptr;
    thread_local const PlanTraits*  t_traits  = nullptr;
}

const PlanContext* CurrentPlanContext() { return t_current; }

PlanContextScope::PlanContextScope(const PlanContext* pc) : m_prev(t_current) { t_current = pc; }
PlanContextScope::~PlanContextScope() { t_current = m_prev; }

const PlanTraits* CurrentPlanTraits() { return t_traits; }

PlanTraitsScope::PlanTraitsScope(const PlanTraits* pt) : m_prev(t_traits) { t_traits = pt; }
PlanTraitsScope::~PlanTraitsScope() { t_traits = m_prev; }

std::pair<const Action*, const Action*> PlanContextRest(const PlanContext* pc)
{
    if (pc == nullptr || pc->actions == nullptr || pc->index + 1 > pc->actions->size())
    { return { nullptr, nullptr }; }
    return { pc->actions->data() + pc->index + 1, pc->actions->data() + pc->actions->size() };
}
