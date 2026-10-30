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
// This header holds the parts that do not need the generator's state (record format, file reader, the
// writer with its sync/compaction/backup thread, the small portable fd layer), so they are unit-tested
// in isolation (test/unit/test_rollout_side_journal.cpp).
#include <algorithm>
#include <atomic>
#include <cerrno>
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
#include "../core/DurableFile.h"

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
ReadStats ParseBuffer(const std::string& data, Sink&& sink)
{
    ReadStats st;
    st.opened = true;
    const std::size_t nl0 = data.find('\n');
    if (nl0 == std::string::npos) { st.header = data; return st; }   // no complete header: nothing usable
    st.header = data.substr(0, nl0);
    st.header_ok = true;
    std::size_t pos = nl0 + 1;
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

// Read the first `limit` bytes of <path> (all of it when limit < 0).
inline bool ReadBytes(const std::string& path, std::string& out, long long limit = -1)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) { return false; }
    if (limit < 0) { out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }
    else
    {
        out.resize(static_cast<std::size_t>(limit));
        f.read(out.data(), static_cast<std::streamsize>(limit));
        out.resize(static_cast<std::size_t>(f.gcount()));
    }
    return true;
}

template <class Sink>
ReadStats ReadFile(const std::string& path, Sink&& sink)
{
    std::string data;
    if (!ReadBytes(path, data)) { return ReadStats{}; }
    return ParseBuffer(data, std::forward<Sink>(sink));
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


// The fd layer (one write() per record, fsync, dir sync) lives in core/DurableFile.h, shared with the
// batch runner's per-game resume log.
namespace fdio = durable;

// ---- The writer ----------------------------------------------------------------------------------
// DURABILITY (USER 2026-10-29, "no exceptions"):
//   * PROCESS death of any kind -- a pause's SIGTERM, SIGKILL, the OOM killer, a crash -- loses ZERO
//     completed rollouts: Append() hands the record to the kernel with one write() before it returns,
//     i.e. before run_one returns the value to anything that could act on it, and the page cache
//     outlives the process.
//   * MACHINE death (power loss, a kernel panic, a VM/container host going away with dirty pages):
//     at most the last sync_ms of completions (default 1 s). The sync thread fsyncs the file every
//     sync_ms, and only AFTER that runs the caller's on_sync hook -- which is where the generator
//     fsyncs the MAIN journal and only then lets the side journal forget rollouts the main journal
//     covers. So compaction can never drop a rollout whose covering main-journal record is not on
//     stable storage.
// A worker's cost is one formatted ~25-byte write() under a mutex per rollout -- against a rollout
// that is a whole searched game (~20 ms to minutes), noise.
//
// COMPACTION every compact_bytes appended: the file is rewritten (tmp + fsync + atomic rename + dir
// sync) keeping only what `keep` still wants. Workers are blocked only for the last step (copying
// the few bytes appended while the prefix was being filtered, and the rename); the read/filter/write
// of the prefix runs unlocked. A failed compaction leaves the old file in place.
//
// ROLLING BACKUPS: <path>.bak1/.bak2 every backup_s, rotated like the main journal's (bak1 -> bak2,
// then copy -> bak1.tmp -> rename). Taken under the lock, so a backup is always whole records.
//
// WRITE FAILURE (a dropped mount): the record is HELD in memory and the sync thread reopens the file
// every 30 s; on recovery a newline ends any record the failure cut, then every held record follows in
// order. Same contract as the main journal's journal_emit_locked. (Held records are, by necessity, the
// one thing a process death during an outage loses: there is no disk to put them on.)
class Writer
{
public:
    struct Opts
    {
        std::string path;
        std::string header;              // line 1, WITHOUT the newline
        int         sync_ms = 1000;      // fsync cadence = the machine-crash bound
        long long   backup_s = 900;      // 0 disables
        long long   compact_bytes = 64LL << 20;
        bool        quiet = false;       // unit tests
    };
    using KeepFn = std::function<bool(const Rec&)>;
    using HookFn = std::function<void()>;

    Writer(Opts o, KeepFn keep, HookFn on_sync = nullptr)
        : o_(std::move(o)), keep_(std::move(keep)), on_sync_(std::move(on_sync)) {}
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    ~Writer() { Close(false); }

    // Write header + `initial` to <path>.tmp, fsync, rename it over <path>, open for append and start the
    // sync thread. `initial` is the carried frontier. On failure <path> is untouched and false returned.
    bool Open(const std::vector<Rec>& initial, std::string* err)
    {
        long long sz = 0;
        if (!RewriteFile(initial, std::string(), &sz, err)) { return false; }
        fd_ = fdio::OpenAppend(o_.path);
        if (fd_ < 0) { if (err) { *err = "cannot reopen " + o_.path + " for append"; } return false; }
        file_bytes_ = sz;
        backup_last_ = std::chrono::steady_clock::now();
        open_.store(true);
        th_ = std::thread([this] { Loop(); });
        return true;
    }

    // One completed rollout -> the kernel, now.
    void Append(const Rec& x)
    {
        std::string s;
        Format(s, x);
        std::lock_guard<std::mutex> lk(io_);
        if (!open_.load(std::memory_order_relaxed)) { return; }
        if (!failed_ && fdio::WriteAll(fd_, s.data(), s.size()))
        {
            file_bytes_ += static_cast<long long>(s.size());
            since_compact_ += static_cast<long long>(s.size());
            recs_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (!failed_)
        {
            failed_ = true;
            retry_last_ = std::chrono::steady_clock::now();
            if (!o_.quiet)
            {
                std::cerr << "[keepgen] SPEC-JOURNAL WRITE FAILED on " << o_.path << " -- holding completed"
                             " rollouts in memory and retrying every 30 s\n" << std::flush;
            }
        }
        held_ += s;
    }

    // Stop the sync thread, write anything held, fsync, close. `retry`: on a failing mount keep retrying
    // for ~30 s (used where the file still matters -- a run that did not finish).
    void Close(bool retry)
    {
        if (!open_.load()) { return; }
        { std::lock_guard<std::mutex> lk(cvm_); stop_ = true; }
        cv_.notify_all();
        if (th_.joinable()) { th_.join(); }
        std::lock_guard<std::mutex> lk(io_);
        for (int k = 0; (failed_ || !held_.empty()) && k < (retry ? 10 : 1); ++k)
        {
            if (k > 0) { std::this_thread::sleep_for(std::chrono::seconds(3)); }
            RetryLocked(true);
        }
        if ((failed_ || !held_.empty()) && !o_.quiet)
        {
            std::cerr << "[keepgen] SPEC-JOURNAL: " << held_.size() << " bytes of completed rollouts could NOT be"
                         " written to " << o_.path << " at close -- those rollouts will be rolled again on resume\n"
                      << std::flush;
        }
        if (fd_ >= 0) { fdio::Sync(fd_); }
        fdio::Close(fd_);
        fd_ = -1;
        open_.store(false);
    }

    // One sync cycle now (fsync + on_sync), as the thread does every sync_ms. Tests and shutdown paths.
    void SyncNow() { DoSync(); }

    long long Records()     const { return recs_.load(std::memory_order_relaxed); }
    long long Compactions() const { return compactions_.load(std::memory_order_relaxed); }
    const std::string& Path() const { return o_.path; }

    // Test hook: force a compaction on the next cycle.
    void ForceCompactNext() { std::lock_guard<std::mutex> lk(io_); force_compact_ = true; }

private:
    void Loop()
    {
        std::unique_lock<std::mutex> lk(cvm_);
        while (!stop_)
        {
            cv_.wait_for(lk, std::chrono::milliseconds(std::max(1, o_.sync_ms)), [this] { return stop_; });
            if (stop_) { break; }
            lk.unlock();
            { std::lock_guard<std::mutex> io(io_); RetryLocked(false); }
            DoSync();
            MaybeBackup();
            MaybeCompact();
            lk.lock();
        }
    }

    // fsync our file (through a dup, so workers are not blocked behind the disk), THEN the caller's hook.
    void DoSync()
    {
        int d = -1;
        { std::lock_guard<std::mutex> io(io_); if (fd_ >= 0 && !failed_) { d = fdio::Dup(fd_); } }
        if (d >= 0) { fdio::Sync(d); fdio::Close(d); }
        if (on_sync_) { on_sync_(); }
    }

    // io_ held. After a failure: reopen (at most every 30 s unless forced), end any cut record, write held.
    void RetryLocked(bool force)
    {
        if (!failed_ && held_.empty()) { return; }
        const auto now = std::chrono::steady_clock::now();
        if (failed_)
        {
            if (!force && now - retry_last_ < std::chrono::seconds(30)) { return; }
            retry_last_ = now;
            fdio::Close(fd_);
            fd_ = fdio::OpenAppend(o_.path);
            if (fd_ < 0) { return; }
            if (!fdio::WriteAll(fd_, "\n", 1)) { return; }   // the reader skips the blank line
            file_bytes_ += 1;
        }
        if (!fdio::WriteAll(fd_, held_.data(), held_.size())) { failed_ = true; return; }
        if (failed_ && !o_.quiet)
        {
            std::cerr << "[keepgen] spec-journal RECOVERED: wrote " << held_.size() << " held bytes to "
                      << o_.path << "\n" << std::flush;
        }
        recs_.fetch_add(static_cast<long long>(std::count(held_.begin(), held_.end(), '\n')),
                        std::memory_order_relaxed);
        file_bytes_ += static_cast<long long>(held_.size());
        since_compact_ += static_cast<long long>(held_.size());
        failed_ = false;
        held_.clear();
    }

    void MaybeBackup()
    {
        if (o_.backup_s <= 0) { return; }
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - backup_last_).count() < o_.backup_s) { return; }
        backup_last_ = now;
        std::lock_guard<std::mutex> io(io_);   // whole records only
        if (failed_) { return; }
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

    void MaybeCompact()
    {
        long long prefix = 0;
        {
            std::lock_guard<std::mutex> io(io_);
            if (failed_ || !held_.empty()) { return; }
            if (!force_compact_ && since_compact_ < o_.compact_bytes) { return; }
            force_compact_ = false;
            since_compact_ = 0;   // on failure too: retry after another threshold's worth
            prefix = file_bytes_;
        }
        // Unlocked: filter the prefix [0, prefix) -- whole records, since every append is one record.
        std::string data;
        if (!ReadBytes(o_.path, data, prefix)) { Warn("cannot read " + o_.path); return; }
        if (static_cast<long long>(data.size()) != prefix) { Warn("short read of the prefix"); return; }
        std::vector<Rec> kept;
        std::unordered_set<std::uint64_t> seen;
        long long before = 0;
        ParseBuffer(data, [&](const Rec& x)
        {
            ++before;
            if (!keep_(x)) { return; }
            if (!seen.insert(Key(x.w, x.pd, x.r)).second) { return; }
            kept.push_back(x);
        });
        std::string body;
        for (const Rec& x : kept) { Format(body, x); }
        const std::string tmp = o_.path + ".tmp";
        {
            const int t = fdio::OpenTrunc(tmp);
            const std::string hdr = o_.header + "\n";
            const bool ok = t >= 0 && fdio::WriteAll(t, hdr.data(), hdr.size())
                         && fdio::WriteAll(t, body.data(), body.size());
            fdio::Close(t);
            if (!ok) { std::error_code ec; std::filesystem::remove(tmp, ec); Warn("cannot write " + tmp); return; }
        }
        long long new_size = static_cast<long long>(o_.header.size() + 1 + body.size());
        // Locked: append whatever landed after the prefix, swap the files, reopen.
        std::lock_guard<std::mutex> io(io_);
        std::string tail;
        if (file_bytes_ > prefix)
        {
            std::string all;
            if (!ReadBytes(o_.path, all, file_bytes_) || static_cast<long long>(all.size()) != file_bytes_)
            { std::error_code ec; std::filesystem::remove(tmp, ec); Warn("short read of the tail"); return; }
            tail = all.substr(static_cast<std::size_t>(prefix));
        }
        const int t = fdio::OpenAppend(tmp);
        const bool ok = t >= 0 && fdio::WriteAll(t, tail.data(), tail.size()) && fdio::Sync(t);
        fdio::Close(t);
        std::error_code ec;
        if (!ok) { std::filesystem::remove(tmp, ec); Warn("cannot finish the rewrite"); return; }
        // Close BEFORE the rename: Windows refuses to replace a file that is open without share-delete.
        fdio::Close(fd_);
        std::filesystem::rename(tmp, o_.path, ec);
        const bool swapped = !ec;
        if (swapped) { fdio::SyncDir(o_.path); } else { std::filesystem::remove(tmp, ec); }
        fd_ = fdio::OpenAppend(o_.path);
        if (fd_ < 0) { failed_ = true; retry_last_ = std::chrono::steady_clock::now(); }
        if (!swapped) { Warn("rename failed"); return; }
        file_bytes_ = new_size + static_cast<long long>(tail.size());
        compactions_.fetch_add(1, std::memory_order_relaxed);
        if (!o_.quiet)
        {
            std::cerr << "[keepgen]   spec-journal compacted: " << before << " -> " << kept.size()
                      << " records (+" << std::count(tail.begin(), tail.end(), '\n')
                      << " appended meanwhile; the rest are covered by the main journal)\n" << std::flush;
        }
    }

    void Warn(const std::string& what)
    {
        if (!o_.quiet)
        {
            std::cerr << "[keepgen]   WARNING: spec-journal compaction FAILED (" << what << ") -- kept the"
                         " uncompacted file, still appending to it\n" << std::flush;
        }
    }

    // header + recs + extra -> <path>.tmp (fsynced) -> rename over <path> (+ dir sync). <path> untouched
    // on any failure. *size = the new file's byte count.
    bool RewriteFile(const std::vector<Rec>& recs, const std::string& extra, long long* size, std::string* err)
    {
        const std::string tmp = o_.path + ".tmp";
        std::string buf;
        buf.reserve(o_.header.size() + 1 + recs.size() * 28 + extra.size());
        buf += o_.header; buf += '\n';
        for (const Rec& x : recs) { Format(buf, x); }
        buf += extra;
        const int t = fdio::OpenTrunc(tmp);
        const bool ok = t >= 0 && fdio::WriteAll(t, buf.data(), buf.size()) && fdio::Sync(t);
        fdio::Close(t);
        std::error_code ec;
        if (!ok) { std::filesystem::remove(tmp, ec); if (err) { *err = "write failed on " + tmp; } return false; }
        std::filesystem::rename(tmp, o_.path, ec);
        if (ec) { std::filesystem::remove(tmp, ec); if (err) { *err = "rename failed: " + ec.message(); } return false; }
        fdio::SyncDir(o_.path);
        if (size) { *size = static_cast<long long>(buf.size()); }
        return true;
    }

    Opts   o_;
    KeepFn keep_;
    HookFn on_sync_;
    std::mutex  io_;                     // the fd, held_, failed_, the byte counters
    int         fd_ = -1;
    std::string held_;
    bool        failed_ = false, force_compact_ = false;
    long long   file_bytes_ = 0, since_compact_ = 0;
    std::atomic<bool> open_{ false };
    std::chrono::steady_clock::time_point retry_last_{}, backup_last_{};
    std::mutex cvm_; std::condition_variable cv_; bool stop_ = false; std::thread th_;
    std::atomic<long long> recs_{ 0 }, compactions_{ 0 };
};

}  // namespace rollj
