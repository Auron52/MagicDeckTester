#pragma once
#include <climits>

// Deterministic replacement for the wall-clock search deadline.
//
// The lookahead search used to stop on a steady_clock deadline, which made the
// result depend on real machine timing — the only non-RNG source of
// nondeterminism in the engine, and the cause of the spurious "depth-4 is
// worse" signal chased at length (see project-deterministic-budget memory).
//
// SearchBudget instead counts atomic rollout work units — one per simulated
// turn-step — and the search stops when the count is spent. Identical seed +
// budget therefore does an identical amount of work, and reaches an identical
// result, on every machine and every run. The "a deeper pass is never worse"
// property becomes exact rather than statistical.
//
// User-facing budgets are expressed in "virtual milliseconds": a fixed,
// calibrated NODES_PER_VIRTUAL_MS constant maps the familiar ms-style knob onto
// a work-unit count, so the --timeout-ms / --budget-ms flags keep their old
// feel while becoming reproducible. The constant is calibrated so a ~200
// virtual-ms budget is comfortably adequate on the reference deck (the search
// converges, matching the old 200 wall-ms result) without being wasteful — at
// an adequate budget the exact value barely matters, which is the whole point.
class SearchBudget
{
public:
    // Calibrated work-units per virtual millisecond. One unit == one simulated
    // turn-step in a rollout. See project-deterministic-budget memory for the
    // calibration procedure.
    static constexpr long long NODES_PER_VIRTUAL_MS = 900;

    SearchBudget() = default;
    explicit SearchBudget(long long limit_units) : m_limit(limit_units) {}

    // Build a budget from a virtual-ms knob; <= 0 means unlimited.
    static SearchBudget FromVirtualMs(int virtual_ms)
    {
        if (virtual_ms <= 0) { return SearchBudget(0); }
        return SearchBudget(static_cast<long long>(virtual_ms) * NODES_PER_VIRTUAL_MS);
    }

    bool      Unlimited() const { return m_limit <= 0; }
    void      Consume(long long n = 1) { m_used += n; }
    long long Used() const { return m_used; }
    long long Limit() const { return m_limit; }

    bool Exhausted() const { return !Unlimited() && m_used >= m_limit; }

    // Mid-pass OVERRUN guard. The iterative-deepening start gate only decides whether to
    // BEGIN a pass (from an estimate); a pass whose real cost explodes far past the estimate
    // would otherwise run to completion unbounded (a single flooded turn deep in a line can
    // branch orders of magnitude beyond the ~6x growth assumption -> a multi-minute / hung
    // search). SetOverrunLimit arms an ABSOLUTE used-unit ceiling for the running pass; the
    // recursion polls Overrun() and bails out so the caller can roll back to the last pass
    // that completed. Set to 0 to disarm (the default -> no guard, so unset callers and
    // unlimited budgets are byte-identical to before). Normal passes finish far under any
    // sane ceiling, so the guard never fires for them (parity preserved).
    void SetOverrunLimit(long long abs_units) { m_overrun_limit = abs_units; }
    bool Overrun() const { return m_overrun_limit > 0 && m_used >= m_overrun_limit; }

    // Units left before exhaustion, clamped to >= 0 (LLONG_MAX if unlimited).
    // Clamping keeps the start-gate / overrun arithmetic well-behaved once the
    // budget has been overrun by a pass running to completion.
    long long Remaining() const
    {
        if (Unlimited()) { return LLONG_MAX; }
        long long r = m_limit - m_used;
        return r > 0 ? r : 0;
    }

private:
    long long m_limit         = 0;   // 0 (or negative) == unlimited
    long long m_used          = 0;
    long long m_overrun_limit = 0;   // 0 == disarmed (no mid-pass abort)
};
