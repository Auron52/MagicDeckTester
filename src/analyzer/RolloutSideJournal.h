#pragma once
// ROLLOUT SIDE JOURNAL for the exhaustive keep generator (src/analyzer/ExhaustiveKeep.cpp).
//
// THE INVARIANT IT EXISTS FOR (USER 2026-10-29): "The point is that the only wasted work that is allowed
// is games in progress" -- "with no exceptions". On a pause/kill at any instant, the only rollouts a
// resumed generation may roll again are the ones that were mid-play at that instant.
//
// The main journal (<raw>.journal) records CELL state -- a cell-side's (sum, sumsq, n) -- and only at
// commit points: a sub batch when its last rollout lands, a size-7 cell every kJournalStride folded
// rollouts or at its floor/freeze. Everything rolled but not yet committed lived only in memory:
// speculative next-wave sub rollouts (the sub memo, kind -2), completed pieces of a split sub batch
// (kind 3), PRECOMPUTE values (kind -1), size-7 lookahead rollouts parked in `slot` above the fold, and
// the up-to-kJournalStride-1 folded rollouts between two progress records. A daily pause threw all of
// them away (Hinata2 2026-10-29: hours of 24-core speculation with the main journal not growing).
//
// THE DESIGN: one record per COMPLETED rollout, appended the moment run_one finishes it, regardless of
// which path asked for it. A rollout's value is a pure function of (seed_base, r, w, pd) -- the property
// every memo in the generator already rests on -- so a record is a FACT, valid under the run's identity
// fingerprint no matter what the main journal says. On resume the records the main journal does not
// already cover are loaded into a read-only BANK that run_one consults before rolling, so every path
// (floor, refine, split, speculative, precompute) consumes them exactly as the killed run would have,
// and the finished table is byte-identical to an uninterrupted run.
//
// This header holds the parts that do not need the generator's state (record format, file reader,
// background writer with compaction and rolling backups, the signal drain), so they are unit-tested in
// isolation (test/unit/test_rollout_side_journal.cpp).
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>
#if defined(__linux__)
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace rollj
{

// One completed rollout. `us` is its measured cost (ORDERING input only -- slowest-first wave dispatch);
// `hits` is the execution-trace touched-card set when the rollout was traced (MTG_KEEP_TRACE).
struct Rec
{
    long long w = 0;
    int       pd = 0;
    long long r = 0;
    double    v = 0.0;
    long long us = 0;
    bool      has_hits = false;
    std::vector<int> hits;
};

// Same packing as the generator's sub_key: r below 2^23, pd one bit, w above.
constexpr long long kMaxR = (1LL << 23) - 1;
inline std::uint64_t Key(long long w, int pd, long long r)
{
    return (static_cast<std::uint64_t>(w) << 24) | (static_cast<std::uint64_t>(pd & 1) << 23)
         | static_cast<std::uint64_t>(r);
}

// "<w> <pd> <r> <v> <us>[ h<i>,<j>,...]\n". The value goes through std::to_chars (shortest exact
// round-trip, locale-independent), so a reloaded value is the same double bit for bit -- which is what
// makes a resume byte-identical rather than merely close. NaN (a wall-abandoned rollout) is written as
// "nan" explicitly so no platform spelling ("-nan(ind)") can reach the parser.
inline void Format(std::string& out, const Rec& x)
{
    char b[128];
    char* p = b;
    char* const e = b + sizeof b;
    p = std::to_chars(p, e, x.w).ptr;  *p++ = ' ';
    p = std::to_chars(p, e, x.pd).ptr; *p++ = ' ';
    p = std::to_chars(p, e, x.r).ptr;  *p++ = ' ';
    if (std::isnan(x.v)) { std::memcpy(p, "nan", 3); p += 3; }
    else                 { p = std::to_chars(p, e, x.v).ptr; }
    *p++ = ' ';
    p = std::to_chars(p, e, x.us).ptr;
    out.append(b, static_cast<std::size_t>(p - b));
    if (x.has_hits)
    {
        out += " h";
        for (std::size_t i = 0; i < x.hits.size(); ++i)
        {
            if (i) { out += ','; }
            char hb[16];
            out.append(hb, static_cast<std::size_t>(std::to_chars(hb, hb + sizeof hb, x.hits[i]).ptr - hb));
        }
    }
    out += '\n';
}

// Parse one record line [s, e) (no trailing newline). Strict: anything malformed is rejected, never
// half-applied -- a record either names a whole rollout or it is skipped.
inline bool Parse(const char* s, const char* e, Rec& x)
{
    auto skip = [&](const char*& p) { while (p < e && *p == ' ') { ++p; } };
    const char* p = s;
    auto num = [&](long long& out) -> bool
    {
        skip(p);
        const auto rr = std::from_chars(p, e, out);
        if (rr.ec != std::errc() || rr.ptr == p) { return false; }
        p = rr.ptr;
        return true;
    };
    long long pd = 0;
    if (!num(x.w) || x.w < 0) { return false; }
    if (!num(pd) || (pd != 0 && pd != 1)) { return false; }
    x.pd = static_cast<int>(pd);
    if (!num(x.r) || x.r < 0 || x.r > kMaxR) { return false; }
    skip(p);
    if (e - p >= 3 && std::memcmp(p, "nan", 3) == 0) { x.v = std::numeric_limits<double>::quiet_NaN(); p += 3; }
    else
    {
        const auto rr = std::from_chars(p, e, x.v);
        if (rr.ec != std::errc() || rr.ptr == p || !std::isfinite(x.v)) { return false; }
        p = rr.ptr;
    }
    if (!num(x.us)) { return false; }
    x.has_hits = false;
    x.hits.clear();
    skip(p);
    if (p < e && *p == 'h')
    {
        x.has_hits = true;
        ++p;
        while (p < e && *p != ' ')
        {
            int v = 0;
            const auto rr = std::from_chars(p, e, v);
            if (rr.ec != std::errc() || rr.ptr == p || v < 0) { return false; }
            x.hits.push_back(v);
            p = rr.ptr;
            if (p < e && *p == ',') { ++p; continue; }
            break;
        }
    }
    skip(p);
    return p == e;
}

struct ReadStats
{
    bool      opened = false;
    bool      header_ok = false;   // line 1 present and newline-terminated
    std::string header;            // line 1, without the newline
    long long records = 0;         // valid records handed to the sink
    long long bad = 0;             // malformed / empty-skipped lines (a cut line plus the recovery "\n")
    bool      torn_tail = false;   // the file ended mid-record (no final newline): that record is dropped
};

// Read <path>: line 1 is the header, every later newline-terminated line a record. A final line with no
// newline is a record the kill cut mid-write -- it is DROPPED (counted in torn_tail), never parsed: a
// prefix of "12 0 3 5 4417" could parse as a valid record with a truncated `us`, and a prefix of a value
// could parse as a different value. Blank lines are skipped silently (the writer emits one after a
// write-failure recovery, exactly as the main journal does).
template <class Sink>
ReadStats ReadFile(const std::string& path, Sink&& sink)
{
    ReadStats st;
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) { return st; }
    st.opened = true;
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::size_t pos = 0;
    const std::size_t nl0 = data.find('\n');
    if (nl0 == std::string::npos) { st.header = data; return st; }   // no complete header: nothing usable
    st.header = data.substr(0, nl0);
    st.header_ok = true;
    pos = nl0 + 1;
    Rec x;
    while (pos < data.size())
    {
        const std::size_t nl = data.find('\n', pos);
        if (nl == std::string::npos) { st.torn_tail = true; break; }
        if (nl > pos)
        {
            if (Parse(data.data() + pos, data.data() + nl, x)) { ++st.records; sink(x); }
            else { ++st.bad; }
        }
        pos = nl + 1;
    }
    return st;
}

// Rename `path` aside to <path>.rejected.<epoch> (never delete, never truncate). Returns the new name,
// or empty on failure.
inline std::string RenameAside(const std::string& path)
{
    std::error_code ec;
    std::string aside = path + ".rejected." + std::to_string(static_cast<long long>(std::time(nullptr)));
    for (int k = 1; std::filesystem::exists(aside, ec) && k < 100; ++k)
    { aside = path + ".rejected." + std::to_string(static_cast<long long>(std::time(nullptr))) + "." + std::to_string(k); }
    std::filesystem::rename(path, aside, ec);
    return ec ? std::string() : aside;
}

// Background writer. Workers call Append() (one short critical section: format into a string); a
// dedicated thread drains the buffer to the file every flush_ms, so file I/O never sits on a worker.
// Durability bound: a completed rollout reaches the OS (kill-safe, the main journal's standard) within
// flush_ms; SIGTERM/SIGINT/SIGHUP drain it immediately (TermSentinel below), so only SIGKILL / a crash
// can lose the last <= flush_ms of completions.
//
// COMPACTION: every compact_bytes appended, the file is rewritten (tmp + atomic rename) keeping only
// the records `keep` still wants -- the generator passes "not yet covered by a record the MAIN journal
// has durably written", so the side file stays the size of the uncommitted frontier instead of growing
// with every rollout of a multi-day run. A failed compaction leaves the old file in place.
//
// ROLLING BACKUPS: <path>.bak1/.bak2 every backup_s, rotated exactly like the main journal's (bak1 ->
// bak2 first, then copy -> bak1.tmp -> rename), always taken from a flushed, whole-line file.
//
// WRITE FAILURE (a dropped mount): records are HELD in memory and the file is reopened in append mode
// at most every 30 s; on recovery a newline terminates any record the failure cut, then every held
// record is written in order. Same contract as the main journal's journal_emit_locked.
class Writer
{
public:
    struct Opts
    {
        std::string path;
        std::string header;          // line 1, WITHOUT the newline
        int         flush_ms = 100;
        long long   backup_s = 900;  // 0 disables
        long long   compact_bytes = 64LL << 20;
        bool        quiet = false;   // unit tests
    };
    using KeepFn = std::function<bool(const Rec&)>;

    Writer(Opts o, KeepFn keep) : o_(std::move(o)), keep_(std::move(keep)) {}
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    ~Writer() { Close(false); }

    // Write header + `initial` to <path>.tmp, rename it over <path> (atomic), open for append and start
    // the flusher. `initial` is the carried frontier (records a resume still needs). On failure nothing
    // at <path> is touched and false is returned.
    bool Open(const std::vector<Rec>& initial, std::string* err)
    {
        if (!RewriteFile(initial, err)) { return false; }
        f_.open(o_.path, std::ios::out | std::ios::app | std::ios::binary);
        if (!f_.is_open()) { if (err) { *err = "cannot reopen " + o_.path + " for append"; } return false; }
        backup_last_ = std::chrono::steady_clock::now();
        open_.store(true);
        th_ = std::thread([this] { Loop(); });
        return true;
    }

    void Append(const Rec& x)
    {
        std::string s;
        Format(s, x);
        std::lock_guard<std::mutex> lk(pm_);
        pending_ += s;
    }

    // Synchronous drain. force_retry ignores the 30 s retry spacing. A no-op once closed.
    void FlushNow(bool force_retry = false)
    {
        std::lock_guard<std::mutex> lk(io_);
        if (open_.load()) { FlushLocked(force_retry); }
    }

    // Signal path: like FlushNow but never blocks for long on a flusher stuck in I/O.
    bool TryDrain(int ms)
    {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (!io_.try_lock())
        {
            if (std::chrono::steady_clock::now() >= until) { return false; }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (open_.load()) { FlushLocked(true); }   // after Close there is nothing left to drain
        const bool ok = !failed_ && held_.empty();
        io_.unlock();
        return ok;
    }

    // Stop the flusher and write everything still buffered. `retry`: on a failing mount keep retrying for
    // up to ~30 s before giving up loudly (used where the file still matters -- a run that did not finish).
    void Close(bool retry)
    {
        if (!open_.load()) { return; }
        { std::lock_guard<std::mutex> lk(cvm_); stop_ = true; }
        cv_.notify_all();
        if (th_.joinable()) { th_.join(); }
        std::lock_guard<std::mutex> lk(io_);
        FlushLocked(true);
        for (int k = 0; retry && (failed_ || !held_.empty()) && k < 10; ++k)
        {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            FlushLocked(true);
        }
        if ((failed_ || !held_.empty()) && !o_.quiet)
        {
            std::cerr << "[keepgen] SPEC-JOURNAL: " << held_.size() << " bytes of completed rollouts could NOT be"
                         " written to " << o_.path << " at close -- those rollouts will be rolled again on resume\n"
                      << std::flush;
        }
        f_.close();
        open_.store(false);
    }

    long long Records()     const { return recs_.load(std::memory_order_relaxed); }
    long long Compactions() const { return compactions_.load(std::memory_order_relaxed); }
    long long FileBytes()   const { return file_bytes_.load(std::memory_order_relaxed); }
    const std::string& Path() const { return o_.path; }

    // Test hook: force a compaction on the next flush.
    void ForceCompactNext() { std::lock_guard<std::mutex> lk(io_); force_compact_ = true; }

private:
    void Loop()
    {
        std::unique_lock<std::mutex> lk(cvm_);
        while (!stop_)
        {
            cv_.wait_for(lk, std::chrono::milliseconds(std::max(1, o_.flush_ms)), [this] { return stop_; });
            if (stop_) { break; }
            lk.unlock();
            {
                std::lock_guard<std::mutex> io(io_);
                FlushLocked(false);
                if (!failed_ && held_.empty())
                {
                    MaybeBackupLocked();
                    MaybeCompactLocked();
                }
            }
            lk.lock();
        }
    }

    // io_ held. Move the worker buffer into `held_` and write it.
    void FlushLocked(bool force_retry)
    {
        { std::lock_guard<std::mutex> lk(pm_); if (!pending_.empty()) { held_ += pending_; pending_.clear(); } }
        if (held_.empty()) { return; }
        const auto now = std::chrono::steady_clock::now();
        if (failed_)
        {
            if (!force_retry && now - retry_last_ < std::chrono::seconds(30)) { return; }
            retry_last_ = now;
            f_.close(); f_.clear();
            f_.open(o_.path, std::ios::out | std::ios::app | std::ios::binary);
            if (!f_.is_open()) { f_.clear(); return; }
            f_ << "\n";   // terminate a record the failure may have cut mid-line (the reader skips blanks)
        }
        f_.write(held_.data(), static_cast<std::streamsize>(held_.size()));
        f_.flush();
        if (!f_.good())
        {
            if (!failed_ && !o_.quiet)
            {
                std::cerr << "[keepgen] SPEC-JOURNAL WRITE FAILED on " << o_.path << " -- holding completed"
                             " rollouts in memory and retrying every 30 s\n" << std::flush;
            }
            if (!failed_) { retry_last_ = now; }
            failed_ = true;
            return;
        }
        if (failed_ && !o_.quiet)
        {
            std::cerr << "[keepgen] spec-journal RECOVERED: wrote " << held_.size() << " held bytes to "
                      << o_.path << "\n" << std::flush;
        }
        failed_ = false;
        recs_.fetch_add(static_cast<long long>(std::count(held_.begin(), held_.end(), '\n')),
                        std::memory_order_relaxed);
        since_compact_ += static_cast<long long>(held_.size());
        file_bytes_.fetch_add(static_cast<long long>(held_.size()), std::memory_order_relaxed);
        held_.clear();
    }

    void MaybeBackupLocked()
    {
        if (o_.backup_s <= 0) { return; }
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - backup_last_).count() < o_.backup_s) { return; }
        backup_last_ = now;
        std::error_code ec;
        const std::string b1 = o_.path + ".bak1", b2 = o_.path + ".bak2", tmp = o_.path + ".bak1.tmp";
        if (std::filesystem::exists(b1, ec)) { std::filesystem::rename(b1, b2, ec); }
        ec.clear();
        std::filesystem::copy_file(o_.path, tmp, std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) { std::filesystem::rename(tmp, b1, ec); }
        if (ec && !o_.quiet)
        {
            std::cerr << "[keepgen]   WARNING: spec-journal auto-backup FAILED (" << ec.message()
                      << ") -- the live side journal is intact but UNPROTECTED\n" << std::flush;
        }
    }

    void MaybeCompactLocked()
    {
        if (!force_compact_ && since_compact_ < o_.compact_bytes) { return; }
        force_compact_ = false;
        f_.flush();
        f_.close();
        std::vector<Rec> kept;
        std::unordered_set<std::uint64_t> seen;
        long long before = 0;
        ReadFile(o_.path, [&](const Rec& x)
        {
            ++before;
            if (!keep_(x)) { return; }
            if (!seen.insert(Key(x.w, x.pd, x.r)).second) { return; }
            kept.push_back(x);
        });
        std::string err;
        const bool ok = RewriteFile(kept, &err);
        f_.clear();
        f_.open(o_.path, std::ios::out | std::ios::app | std::ios::binary);
        if (!f_.is_open())
        {
            f_.clear();
            failed_ = true;   // held_ is empty; the next Append is held and retried like any failed write
            retry_last_ = std::chrono::steady_clock::now();
        }
        since_compact_ = 0;   // on failure too: retry after another threshold's worth, not every flush
        if (!ok)
        {
            if (!o_.quiet)
            {
                std::cerr << "[keepgen]   WARNING: spec-journal compaction FAILED (" << err << ") -- kept the"
                             " uncompacted file, still appending to it\n" << std::flush;
            }
            return;
        }
        compactions_.fetch_add(1, std::memory_order_relaxed);
        if (!o_.quiet)
        {
            std::cerr << "[keepgen]   spec-journal compacted: " << before << " -> " << kept.size()
                      << " records (the rest are covered by the main journal)\n" << std::flush;
        }
    }

    // header + records -> <path>.tmp -> rename over <path>. Leaves <path> untouched on any failure.
    bool RewriteFile(const std::vector<Rec>& recs, std::string* err)
    {
        const std::string tmp = o_.path + ".tmp";
        std::string buf;
        buf.reserve(o_.header.size() + 1 + recs.size() * 28);
        buf += o_.header; buf += '\n';
        for (const Rec& x : recs) { Format(buf, x); }
        {
            std::ofstream t(tmp, std::ios::out | std::ios::trunc | std::ios::binary);
            if (!t.is_open()) { if (err) { *err = "cannot create " + tmp; } return false; }
            t.write(buf.data(), static_cast<std::streamsize>(buf.size()));
            t.flush();
            t.close();
            if (t.fail()) { std::error_code ec; std::filesystem::remove(tmp, ec); if (err) { *err = "write failed on " + tmp; } return false; }
        }
        std::error_code ec;
        std::filesystem::rename(tmp, o_.path, ec);
        if (ec) { std::filesystem::remove(tmp, ec); if (err) { *err = "rename failed: " + ec.message(); } return false; }
        file_bytes_.store(static_cast<long long>(buf.size()), std::memory_order_relaxed);
        return true;
    }

    Opts   o_;
    KeepFn keep_;
    std::mutex  pm_;  std::string pending_;            // worker side
    std::mutex  io_;                                   // file side (flusher, drain, close)
    std::ofstream f_;
    std::string held_;
    bool failed_ = false, force_compact_ = false;
    std::atomic<bool> open_{ false };
    std::chrono::steady_clock::time_point retry_last_{}, backup_last_{};
    long long since_compact_ = 0;
    std::mutex cvm_; std::condition_variable cv_; bool stop_ = false; std::thread th_;
    std::atomic<long long> recs_{ 0 }, compactions_{ 0 }, file_bytes_{ 0 };
};

// ---- TERM SENTINEL: a pause (SIGTERM) must not lose the writer's last flush interval ----------------
// The artifact queue pauses a generation with SIGTERM to its process group (artifact_queue.py `pause`),
// and the default action kills the process with up to flush_ms of completed rollouts still in the
// writer's buffer. This blocks SIGTERM/SIGINT/SIGHUP in the constructing thread (every thread it creates
// afterwards inherits the mask -- so it must be constructed BEFORE the generator starts any thread) and
// waits for them on a dedicated thread. On one: SIGPIPE is ignored (the group's `tee` dies in the same
// instant, and a stderr write must not kill us mid-drain), the registered drain runs, and the signal is
// re-raised with its default action, so the process still dies by that signal -- the exit status every
// caller already handles. Only signals whose disposition is SIG_DFL are taken over: a nohup'ed run's
// SIG_IGN SIGHUP, or a background job's SIG_IGN SIGINT, stays ignored.
// Linux only (sigtimedwait); elsewhere a no-op, which leaves the flush interval as the bound.
class TermSentinel
{
public:
    TermSentinel()
    {
#if defined(__linux__)
        sigemptyset(&set_);
        for (int s : { SIGTERM, SIGINT, SIGHUP })
        {
            struct sigaction cur{};
            if (sigaction(s, nullptr, &cur) == 0 && cur.sa_handler == SIG_DFL && !(cur.sa_flags & SA_SIGINFO))
            { sigaddset(&set_, s); }
        }
        if (pthread_sigmask(SIG_BLOCK, &set_, &old_) != 0) { return; }
        active_ = true;
        th_ = std::thread([this] { Loop(); });
#endif
    }
    ~TermSentinel()
    {
#if defined(__linux__)
        if (!active_) { return; }
        stop_.store(true);
        if (th_.joinable()) { th_.join(); }
        pthread_sigmask(SIG_SETMASK, &old_, nullptr);   // a signal that arrived after the join is delivered now
#endif
    }
    TermSentinel(const TermSentinel&) = delete;
    TermSentinel& operator=(const TermSentinel&) = delete;

    void SetDrain(std::function<void()> f) { std::lock_guard<std::mutex> lk(mx_); drain_ = std::move(f); }
    void ClearDrain()                      { std::lock_guard<std::mutex> lk(mx_); drain_ = nullptr; }

private:
#if defined(__linux__)
    void Loop()
    {
        while (!stop_.load())
        {
            timespec ts{ 0, 200 * 1000 * 1000 };
            const int s = sigtimedwait(&set_, nullptr, &ts);
            if (s <= 0) { continue; }
            ::signal(SIGPIPE, SIG_IGN);
            {
                std::lock_guard<std::mutex> lk(mx_);   // held across the drain: ClearDrain waits for it
                if (drain_) { drain_(); }
            }
            std::fprintf(stderr, "[keepgen] signal %d: completed rollouts drained to the side journal --"
                                 " exiting\n", s);
            std::fflush(stderr);
            struct sigaction dfl{};
            dfl.sa_handler = SIG_DFL;
            sigemptyset(&dfl.sa_mask);
            sigaction(s, &dfl, nullptr);
            sigset_t one; sigemptyset(&one); sigaddset(&one, s);
            ::kill(::getpid(), s);                     // pending (blocked everywhere)...
            pthread_sigmask(SIG_UNBLOCK, &one, nullptr);   // ...delivered here: default action ends the process
            ::_exit(128 + s);                           // unreachable unless the kernel disagrees
        }
    }
    sigset_t set_{}, old_{};
#endif
    bool active_ = false;
    std::atomic<bool> stop_{ false };
    std::thread th_;
    std::mutex mx_;
    std::function<void()> drain_;
};

}  // namespace rollj
