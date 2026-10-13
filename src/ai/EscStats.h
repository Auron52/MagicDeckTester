#pragma once
// Per-thread tallies of value-leaf ESCALATIONS (diagnostic only; never read by play). TurnSolver adds to them
// at the escalation's take decision; the batch runner snapshots them around each game and credits the deltas
// to that game's job, so ONE pooled batch reports per-job (per-deck) numbers. Printed per job under
// MTG_ESC_JOB_STATS=1. "Discarded" = the heuristic line the escalation computed was then REJECTED by the
// take rule (hcommitted < take_at[committed]) and the value-leaf line was kept: its units were dead work.
namespace escstats
{
inline thread_local long long t_take_n = 0;   // escalations whose heuristic line was TAKEN
inline thread_local long long t_take_u = 0;   // ... and the work units they spent
inline thread_local long long t_disc_n = 0;   // escalations DISCARDED (value-leaf line kept)
inline thread_local long long t_disc_u = 0;   // ... and the work units they spent
// Discarded escalations whose heuristic line was a VERIFIED win (lethal inside its own searched horizon,
// decided by simulation, not a leaf estimate) -- and of those, how many were EARLIER than / EQUAL to the
// kept value-leaf line's (unverified, estimated) win turn.
inline thread_local long long t_disc_ver_n = 0;
inline thread_local long long t_disc_ver_earlier = 0;
inline thread_local long long t_disc_ver_equal = 0;
// Set when the escalation's line came from an ANYTIME rescue: a deeper pass the budget ABORTED had nonetheless
// proved a better win (MTG_ID_ANYTIME). Its depth is NOT credited to hcommitted, so the take rule cannot see it.
inline thread_local bool      t_esc_rescued = false;
inline thread_local long long t_disc_rescued_n = 0;   // discarded escalations holding such a partial-pass win (climb)
// The same rescue inside the main iterative-deepening loop (FullSearchLine), which DOES credit the aborted pass's
// depth (committed_depth = pass_depth) -- so there the take rule sees it.
inline thread_local bool      t_esc_rescued_main = false;
inline thread_local long long t_disc_rescued_main_n = 0;
// UP-FRONT ESCALATION SKIP prototype (MTG_ESC_SKIP_LOG / MTG_ESC_SKIP_PREDICT). Per-pass structure of the
// OUTERMOST probe ladder (t_rec == 1) and of the escalation's own heuristic ladder (t_rec == 2), recorded by
// FullSearchLine at each completed pass. Separate from g_probe_leaves on purpose: that array is armed only on
// the predictor / cap paths and is READ by play (path-to-trust), so arming it on the ladder decks would move
// play. These arrays are read only by the skip prototype and its log line.
inline thread_local int       t_rec = 0;
inline thread_local long long t_pl[16] = {0};   // probe leaf evals at pass d (incremental)
inline thread_local long long t_pc[16] = {0};   // probe budget units at pass d (incremental)
inline thread_local long long t_hl[16] = {0};   // escalation heuristic leaf evals at pass d
inline thread_local long long t_hc[16] = {0};   // escalation heuristic budget units at pass d
inline thread_local const char* t_job = nullptr;   // batch job name (set by BatchRunner around each game)
inline thread_local int         t_gi  = -1;        // batch game index within the job
struct RecGuard
{
    int prev;
    explicit RecGuard(int mode) : prev(t_rec) { t_rec = mode; }
    ~RecGuard() { t_rec = prev; }
};
}
