#pragma once
// OUTPUT HEALTH -- a batch whose output files could not be written must not exit 0.
//
// WHY. The repo (and therefore logs/ and every generation queue) is a 9p mount of the Windows host
// folder; it has failed outright twice and returns transient EIO under memory fragmentation
// (docs/design/repo-mount-failures.md). An std::ofstream that hits one failed write sets badbit and
// silently drops EVERY later write, and the process still exits 0. The value-row dump guarded each
// row with `if (v_out.good())`, so after one bad write a phase-A label batch would have banked a
// truncated rows file and marked itself complete; a .wins file cut short would hand the depth matrix
// or a GT accept a game set nobody played. Every pipeline already gates on the batch EXIT STATUS, so
// the fix is to make a failed write reach it: each writer reports here, and `--batch` returns 5
// (after its normal output) when anything failed. Phase A's resume then re-queues only the games
// whose rows are missing, exactly as after a killed batch.
//
// Diagnostic plumbing only: nothing here is read by play, so games are byte-identical.
#include <atomic>
#include <iostream>
#include <ostream>
#include <string>

namespace outhealth
{
inline std::atomic<int>& Failures() { static std::atomic<int> n{0}; return n; }

// Record a failed write. Loud (stderr), bounded so a dead mount cannot flood the log.
inline void Fail(const std::string& what)
{
    if (Failures().fetch_add(1, std::memory_order_relaxed) < 20)
    {
        std::cerr << "[output] WRITE FAILED: " << what
                  << " -- the output is INCOMPLETE; this batch will exit with status 5\n" << std::flush;
    }
}

// Flush `s` and report it if it is not good. Returns s.good().
inline bool Check(std::ostream& s, const std::string& what)
{
    s.flush();
    if (s.good()) { return true; }
    Fail(what);
    return false;
}
}  // namespace outhealth
