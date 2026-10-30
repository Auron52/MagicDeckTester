#pragma once
// PER-GAME RESUME LOG for `mtg --batch` (MTG_BATCH_RESUME=<file>, opt-in per run).
//
// THE INVARIANT (USER 2026-10-29): after a pause or kill "the only wasted work that is allowed is games
// in progress". A batch used to keep its results only in memory until a job's LAST game landed, so a
// pause of the mulligan driver's validation A/B (64k games) or of the deck's regression replayed every
// game it had finished.
//
// With MTG_BATCH_RESUME set, every game that finishes appends ONE record -- its job, local index, win
// turn, play digest, work units, wall ms and abandoned flag -- with a single write() before the worker
// moves on, so a process kill of any kind loses no finished game (the file is fsynced at most once a
// second and at the end, which bounds a MACHINE crash). A later run of the SAME batch loads the
// records, does not enqueue those games, and folds the stored results into each job exactly where the
// game would have written them, so every per-job line (played/avg/digest/units), every .wins file and
// every downstream number is byte-identical to an uninterrupted run (ms is wall time, so it is the
// recorded wall time, not a re-measurement).
//
// "THE SAME BATCH" is a KEY, compared exactly; a mismatched log is renamed aside, never deleted:
//   caller key (MTG_BATCH_RESUME_KEY: the caller hashes the inputs only it knows -- decklists, profiles,
//   sidecars, card data) + the build's engine fingerprint (every C++ source) + the manifest's bytes +
//   every MTG_* variable in the environment (they are the engine's levers).
// The game itself is a pure function of those (seed + index -> shuffle; the engine is deterministic),
// which is the property every pooled batch, the regression GT and the index-carry skip list rest on.
#include "../core/DurableFile.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

namespace batchresume
{

struct GameRec
{
    int           job = 0;      // manifest index
    int           gi = 0;       // LOCAL game index within the job
    int           wt = 0;       // win_turns slot value exactly as the pool wrote it (kSkipped for abandoned)
    std::uint64_t dg = 0;       // per-game play digest
    long long     units = 0;
    long long     ms = 0;
    bool          ab = false;   // abandoned at the work ceiling
    std::string   name;         // the job's name, cross-checked on load
};

inline std::string Fnv64Hex(const std::string& s)
{
    std::uint64_t h = 1469598103934665603ULL;
    for (const unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    char b[17];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
    return b;
}

inline void Format(std::string& out, const GameRec& r)
{
    char b[160];
    const int n = std::snprintf(b, sizeof b, "%d %d %d %016llx %lld %lld %d ", r.job, r.gi, r.wt,
                                static_cast<unsigned long long>(r.dg), r.units, r.ms, r.ab ? 1 : 0);
    out.append(b, static_cast<std::size_t>(n));
    out += r.name;
    out += '\n';
}

// Strict: a line either names a whole game or is skipped.
inline bool Parse(const std::string& line, GameRec& r)
{
    unsigned long long dg = 0;
    int ab = 0, used = 0;
    if (std::sscanf(line.c_str(), "%d %d %d %16llx %lld %lld %d %n", &r.job, &r.gi, &r.wt, &dg, &r.units,
                    &r.ms, &ab, &used) != 7 || used <= 0)
    { return false; }
    if (r.job < 0 || r.gi < 0 || (ab != 0 && ab != 1)) { return false; }
    r.dg = dg;
    r.ab = ab == 1;
    r.name = line.substr(static_cast<std::size_t>(used));
    return !r.name.empty() && r.name.find(' ') == std::string::npos;
}

inline std::string Header(const std::string& key) { return "{\"batchresume\":1,\"key\":\"" + key + "\"}"; }

struct Loaded
{
    bool        exists = false;
    bool        header_ok = false;
    std::string header;
    std::vector<GameRec> recs;
    long long   bad = 0;
    bool        torn = false;     // ended mid-record: that record is dropped, and an append must start a new line
};

inline Loaded Load(const std::string& path)
{
    Loaded L;
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) { return L; }
    L.exists = true;
    const std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const std::size_t nl0 = data.find('\n');
    if (nl0 == std::string::npos) { L.header = data; L.torn = !data.empty(); return L; }
    L.header = data.substr(0, nl0);
    L.header_ok = true;
    std::size_t pos = nl0 + 1;
    GameRec r;
    while (pos < data.size())
    {
        const std::size_t nl = data.find('\n', pos);
        if (nl == std::string::npos) { L.torn = true; break; }
        if (nl > pos)
        {
            if (Parse(data.substr(pos, nl - pos), r)) { L.recs.push_back(r); }
            else { ++L.bad; }
        }
        pos = nl + 1;
    }
    return L;
}

// Append-only writer: one write() per game under a mutex; fsync at most once a second and at Close.
class Log
{
public:
    // fresh: truncate and write the header. Otherwise append (after a newline if the file ended torn).
    bool Open(const std::string& path, const std::string& key, bool fresh, bool torn_tail)
    {
        path_ = path;
        if (fresh)
        {
            fd_ = durable::OpenTrunc(path);
            if (fd_ < 0) { return false; }
            const std::string h = Header(key) + "\n";
            if (!durable::WriteAll(fd_, h.data(), h.size())) { return false; }
            durable::Sync(fd_);
            durable::SyncDir(path);
        }
        else
        {
            fd_ = durable::OpenAppend(path);
            if (fd_ < 0) { return false; }
            if (torn_tail && !durable::WriteAll(fd_, "\n", 1)) { return false; }
        }
        last_sync_ = std::chrono::steady_clock::now();
        return true;
    }
    // A failed write (a dropped mount) HOLDS the record and retries every 30 s, ending any record the
    // failure cut with a newline first -- the same contract as the keep generator's journals.
    void Append(const GameRec& r)
    {
        std::string s;
        Format(s, r);
        std::lock_guard<std::mutex> lk(mx_);
        const auto now = std::chrono::steady_clock::now();
        if (failed_)
        {
            held_ += s;
            if (now - retry_last_ < std::chrono::seconds(30)) { return; }
            retry_last_ = now;
            durable::Close(fd_);
            fd_ = durable::OpenAppend(path_);
            if (fd_ < 0 || !durable::WriteAll(fd_, "\n", 1) || !durable::WriteAll(fd_, held_.data(), held_.size()))
            { return; }
            std::fprintf(stderr, "[batch] resume log RECOVERED on %s\n", path_.c_str());
            held_.clear();
            failed_ = false;
            return;
        }
        if (fd_ < 0) { return; }
        if (!durable::WriteAll(fd_, s.data(), s.size()))
        {
            failed_ = true;
            retry_last_ = now;
            held_ += s;
            std::fprintf(stderr, "[batch] RESUME LOG WRITE FAILED on %s -- holding finished games in memory and"
                                 " retrying every 30 s\n", path_.c_str());
            return;
        }
        if (now - last_sync_ >= std::chrono::seconds(1)) { durable::Sync(fd_); last_sync_ = now; }
    }
    void Close()
    {
        std::lock_guard<std::mutex> lk(mx_);
        if (fd_ >= 0) { durable::Sync(fd_); durable::Close(fd_); fd_ = -1; }
    }
    ~Log() { Close(); }

private:
    std::mutex  mx_;
    int         fd_ = -1;
    bool        failed_ = false;
    std::string path_, held_;
    std::chrono::steady_clock::time_point last_sync_{}, retry_last_{};
};

}  // namespace batchresume
