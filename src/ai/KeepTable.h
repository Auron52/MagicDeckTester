#pragma once
// The exhaustive keep/bottom table as an ON-DISK, SEEKABLE file.
//
// WHY. A deck's exhaustive table is consulted a handful of times per GAME (each mulligan step and each
// bottoming decision), never per search node -- yet the runtime held the whole thing resident as two
// std::maps of int-vectors: ~5.3 GB for FiveColour's 1.98M compositions x 27 buckets, on a 10.7 GB
// box whose RSS watchdog trips at 8 GB. That footprint scales with the ARTIFACT, not the machine, and
// was the real ceiling on which decks could run here at all (the streaming bincache fix before this
// only bounded the load-time transient; the policy itself stayed resident). A composition's record is
// a pure function of its sorted position, so the table can live on disk as fixed-width records in key
// order and be binary-searched with ~21 small positioned reads per lookup (2M records) -- free at that
// call rate -- and the resident cost of a policy drops to its header (bucket names) plus one open
// file. USER design (2026-09-15): "we want an uncompressed version of the .gz on disk at least ...
// we need to be able to seek."
//
// LAYOUT (native-endian: this is a machine-local derived cache, never shipped or pooled):
//   [magic u32][version u32]                                          preamble, 8 bytes
//   [record 0][record 1] ... [record N-1]                             fixed width, STRICTLY ASCENDING
//   [header block]                                                    source fingerprint + policy scalars
//   [header_off u64][header_len u64][N u64][magic u32][version u32]   trailer, 32 bytes
//   record = int32 comp[K] | uint8 flags[F_keep] | uint32 bottom_mask | int32 rows[F_bot][K]
// Bit i of bottom_mask set => row i is present with length K. A clear bit is exactly the in-memory
// policy's "no / empty / deferred row" (DecideBottom returns false for both), so the two backings
// decide identically by construction. The header is written LAST because the record count is only
// known when the streaming build finishes; the trailer's lengths make the file self-validating --
// Open() requires file size == preamble + N*record + header + trailer EXACTLY, the same length-
// exactness rule the old cache used to reject a short or torn file (a zero tail parses as legitimate
// empty rows, which is a silently wrong policy, not an error).
//
// WHERE IT LIVES: not beside the sidecar. This checkout is a 9p/drvfs share (whole-second mtimes,
// rename not atomic against an open fd, slow small reads), so the table goes on local disk under
// KeepTableDir() -- MTG_KEEP_TABLE_DIR if set, else the user's cache dir. It is a persistent cache, not
// a per-run temp: a true temp would re-parse ~1.8 GB of JSON on every launch.
//
// KEYED BY CONTENT, AND BOUNDED (format v2, 2026-11-03). v1 named a table after the sidecar's canonical
// PATH, so every checkout of the repo (each worktree holds its own copy of every sidecar) built its own
// full set -- ~13 GB each, FiveColour alone 3.2 GB -- and nothing was ever deleted: 800 GB on
// 2026-11-03, when the disk filled and runs died with ENOSPC (docs/design/keeptable-cache-accumulation.md).
//   * A table is `<sidecar filename>-<fnv1a64 of the sidecar's BYTES>.keeptable`, validated by the
//     content hash + size in its header, so identical sidecars at N paths share ONE table.
//   * Hashing a sidecar (<= ~60 MB) happens once per (canonical path, size, mtime); the answer is kept
//     in a tiny `<sidecar filename>-<fnv1a64 of the path>.keepref` beside the tables, so a launch costs
//     one small extra read, never a re-hash (the harness and the play server launch many processes).
//   * Opening a table refreshes its mtime, so mtime means "last used" (atime is unreliable under
//     relatime), and Collect() evicts: refs whose sidecar is gone or changed, tables no live ref names,
//     stale build temps, then least-recently-used tables over a size cap (MTG_KEEP_TABLE_CACHE_GB).
// The content key is sound because the table is a pure function of the sidecar's bytes and of the
// format version (which the magic/version check covers). The old (path, size, mtime) key was a proxy
// for exactly that; the ref inherits its one weakness (a same-size rewrite within one mtime tick).
//
// Positioned reads, deliberately NOT mmap: a mapping over a file another process is rebuilding turns
// a rejected cache into a SIGBUS, and mmap would need per-platform code. Reads are serialised by a
// mutex on one std::ifstream; at a handful of lookups per game that costs nothing measurable, and it
// keeps a single portable path (the play viewer runs this on Windows).
#include "../core/EnvFlags.h"   // EnvInt (MTG_KEEP_TABLE_CACHE_GB)
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace keeptable
{
constexpr uint32_t    kMagic        = 0x54504b4cu;   // 'LKPT'
constexpr uint32_t    kVersion      = 2u;            // 2: content-keyed (Header::content_hash)
constexpr std::size_t kPreambleBytes = 8;
constexpr std::size_t kTrailerBytes  = 8 + 8 + 8 + 4 + 4;
constexpr uint32_t    kMaxBottomRows = 32;           // bottom_mask is a uint32 ((max_mull+1)*2 <= 32)

// The policy's scalars + provenance, stored once. Mirrors the non-table members of
// ExhaustiveKeepPolicy (which includes this header, so it cannot be named here).
struct Header
{
    std::string src_path;        // the sidecar this table was FIRST built from. Provenance only since v2:
                                 // a content-keyed table is shared by every path holding those bytes.
    uint64_t    src_size  = 0;   // validated, with content_hash
    uint64_t    src_mtime = 0;   // provenance only since v2
    uint64_t    content_hash = 0;   // v2+: ContentHash() of the sidecar's bytes -- the table's identity
    uint32_t    K = 0, F_keep = 0, F_bot = 0;
    uint64_t    N = 0;
    std::vector<std::vector<std::string>> buckets;
    int32_t     max_mull = 0;
    bool        bottoming_enabled = false;
    std::string commit, play_digest;
    int32_t     effective_R = 0;
};

inline std::size_t RecordBytes(uint32_t K, uint32_t F_keep, uint32_t F_bot)
{
    return 4u * static_cast<std::size_t>(K) + F_keep + 4u + 4u * static_cast<std::size_t>(F_bot) * K;
}

namespace detail
{
    // Sinks: the header block is assembled in a string and the records stream to the file through a
    // bounded buffer, so a 3 GB build costs ~8 MB of transient RAM.
    struct StringSink
    {
        std::string b;
        void append(const char* p, std::size_t n) { b.append(p, n); }
    };
    struct FileSink
    {
        std::ofstream os;
        std::string   buf;
        bool          ok = true;
        explicit FileSink(const std::filesystem::path& p, std::size_t cap = (8u << 20))
            : os(p, std::ios::binary), cap_(cap) { buf.reserve(cap); }
        void append(const char* p, std::size_t n)
        {
            if (buf.size() + n > cap_) { Flush(); }
            if (n >= cap_) { os.write(p, static_cast<std::streamsize>(n)); ok = ok && bool(os); }
            else           { buf.append(p, n); }
        }
        void Flush()
        {
            if (!buf.empty()) { os.write(buf.data(), static_cast<std::streamsize>(buf.size())); ok = ok && bool(os); buf.clear(); }
        }
        bool Close() { Flush(); os.flush(); ok = ok && bool(os); os.close(); return ok; }
    private:
        std::size_t cap_;
    };
    template <class S, class T> inline void PutPod(S& b, T v)
    { b.append(reinterpret_cast<const char*>(&v), sizeof(T)); }
    template <class S> inline void PutStr(S& b, const std::string& s)
    { PutPod<S, uint32_t>(b, static_cast<uint32_t>(s.size())); b.append(s.data(), s.size()); }

    struct MemReader
    {
        const char* p; const char* end;
        MemReader(const char* d, std::size_t n) : p(d), end(d + n) {}
        const char* Need(std::size_t n)
        { if (static_cast<std::size_t>(end - p) < n) { return nullptr; } const char* r = p; p += n; return r; }
        bool AtEnd() const { return p == end; }
    };
    template <class T> inline bool GetPod(MemReader& r, T& v)
    { const char* p = r.Need(sizeof(T)); if (!p) { return false; } std::memcpy(&v, p, sizeof(T)); return true; }
    inline bool GetStr(MemReader& r, std::string& s)
    { uint32_t n; if (!GetPod(r, n)) { return false; } const char* p = r.Need(n); if (!p) { return false; } s.assign(p, n); return true; }

    template <class S> inline void EncodeHeader(S& b, const Header& h)
    {
        PutStr(b, h.src_path);
        PutPod<S, uint64_t>(b, h.src_size);
        PutPod<S, uint64_t>(b, h.src_mtime);
        PutPod<S, uint64_t>(b, h.content_hash);
        PutPod<S, uint32_t>(b, h.K);
        PutPod<S, uint32_t>(b, h.F_keep);
        PutPod<S, uint32_t>(b, h.F_bot);
        PutPod<S, uint64_t>(b, h.N);
        PutPod<S, uint32_t>(b, static_cast<uint32_t>(h.buckets.size()));
        for (const auto& bk : h.buckets)
        { PutPod<S, uint32_t>(b, static_cast<uint32_t>(bk.size())); for (const auto& n : bk) { PutStr(b, n); } }
        PutPod<S, int32_t>(b, h.max_mull);
        PutPod<S, uint8_t>(b, static_cast<uint8_t>(h.bottoming_enabled ? 1 : 0));
        PutStr(b, h.commit);
        PutStr(b, h.play_digest);
        PutPod<S, int32_t>(b, h.effective_R);
    }
    // Version 1 (path-keyed, no content_hash) is still DECODED so the cache sweep can recognise a legacy
    // table whose source path is gone; Table::Open only ever accepts kVersion.
    inline bool DecodeHeader(MemReader& r, Header& h, uint32_t version = kVersion)
    {
        if (!GetStr(r, h.src_path))     { return false; }
        if (!GetPod(r, h.src_size))     { return false; }
        if (!GetPod(r, h.src_mtime))    { return false; }
        if (version >= 2 && !GetPod(r, h.content_hash)) { return false; }
        if (!GetPod(r, h.K) || !GetPod(r, h.F_keep) || !GetPod(r, h.F_bot)) { return false; }
        if (!GetPod(r, h.N))            { return false; }
        uint32_t nb; if (!GetPod(r, nb)) { return false; }
        h.buckets.assign(nb, {});
        for (auto& bk : h.buckets)
        {
            uint32_t nn; if (!GetPod(r, nn)) { return false; }
            bk.resize(nn);
            for (auto& n : bk) { if (!GetStr(r, n)) { return false; } }
        }
        if (!GetPod(r, h.max_mull))     { return false; }
        uint8_t be; if (!GetPod(r, be)) { return false; } h.bottoming_enabled = (be != 0);
        if (!GetStr(r, h.commit))       { return false; }
        if (!GetStr(r, h.play_digest))  { return false; }
        if (!GetPod(r, h.effective_R))  { return false; }
        return r.AtEnd();
    }

    // -1 / 0 / +1, std::map's ordering of two equal-length composition keys.
    inline int CompareKey(const int32_t* a, const int32_t* b, uint32_t K)
    {
        for (uint32_t i = 0; i < K; ++i) { if (a[i] != b[i]) { return a[i] < b[i] ? -1 : 1; } }
        return 0;
    }
}   // namespace detail

// ---- Writer -----------------------------------------------------------------------------------
// Streams records to a file in the order given. The FIRST entry fixes the record shape (K, F_keep,
// F_bot); every later entry must match it (rows may be absent entirely: that is a zero mask). Keys
// must arrive STRICTLY ASCENDING -- the JSON emitters walk a std::map, so a shipped sidecar always
// satisfies this; anything else fails the build and the loader falls back to the in-memory policy,
// which is slow but never wrong.
class Writer
{
public:
    explicit Writer(const std::filesystem::path& tmp) : fs_(tmp)
    {
        if (!fs_.os) { Fail("cannot create " + tmp.string()); return; }
        detail::PutPod<detail::FileSink, uint32_t>(fs_, kMagic);
        detail::PutPod<detail::FileSink, uint32_t>(fs_, kVersion);
    }
    bool               ok()    const { return ok_; }
    const std::string& error() const { return err_; }
    uint64_t           count() const { return n_; }

    bool Add(const std::vector<int>& comp, const std::vector<char>& flags,
             const std::vector<std::vector<int>>* rows)
    {
        if (!ok_) { return false; }
        const uint32_t nrows = rows ? static_cast<uint32_t>(rows->size()) : 0u;
        if (n_ == 0)
        {
            K_ = static_cast<uint32_t>(comp.size()); F_keep_ = static_cast<uint32_t>(flags.size()); F_bot_ = nrows;
            if (K_ == 0 || F_keep_ == 0 || F_bot_ > kMaxBottomRows)
            { return Fail("unsupported table shape K=" + std::to_string(K_) + " F_keep=" + std::to_string(F_keep_) + " F_bot=" + std::to_string(F_bot_)); }
            rec_.assign(RecordBytes(K_, F_keep_, F_bot_), 0);
        }
        else
        {
            if (comp.size() != K_ || flags.size() != F_keep_ || (nrows != 0 && nrows != F_bot_))
            { return Fail("entry " + std::to_string(n_) + " does not match the table's shape"); }
            if (!(prev_ < comp))
            { return Fail("entry " + std::to_string(n_) + " is not in ascending composition order"); }
        }
        static_assert(sizeof(int) == sizeof(int32_t), "records store ints as raw int32_t");
        char* p = rec_.data();
        std::memcpy(p, comp.data(), 4u * K_);          p += 4u * K_;
        std::memcpy(p, flags.data(), F_keep_);          p += F_keep_;
        char* mask_p = p;                               p += 4;
        std::memset(p, 0, 4u * F_bot_ * K_);
        uint32_t mask = 0;
        for (uint32_t i = 0; i < nrows; ++i)
        {
            const std::vector<int>& row = (*rows)[i];
            if (row.size() != K_) { continue; }       // absent/deferred/malformed: DecideBottom rejects these alike
            mask |= (1u << i);
            std::memcpy(p + 4u * K_ * i, row.data(), 4u * K_);
        }
        std::memcpy(mask_p, &mask, 4);
        fs_.append(rec_.data(), rec_.size());
        prev_ = comp; ++n_;
        return fs_.ok || Fail("write error");
    }

    // Writes the header block + trailer and closes. `h`'s shape/count fields are filled in here.
    bool Finish(Header h)
    {
        if (!ok_)     { return false; }
        if (n_ == 0)  { return Fail("no entries"); }
        h.K = K_; h.F_keep = F_keep_; h.F_bot = F_bot_; h.N = n_;
        detail::StringSink hb;
        detail::EncodeHeader(hb, h);
        const uint64_t header_off = kPreambleBytes + n_ * rec_.size();
        fs_.append(hb.b.data(), hb.b.size());
        detail::PutPod<detail::FileSink, uint64_t>(fs_, header_off);
        detail::PutPod<detail::FileSink, uint64_t>(fs_, static_cast<uint64_t>(hb.b.size()));
        detail::PutPod<detail::FileSink, uint64_t>(fs_, n_);
        detail::PutPod<detail::FileSink, uint32_t>(fs_, kMagic);
        detail::PutPod<detail::FileSink, uint32_t>(fs_, kVersion);
        return fs_.Close() || Fail("flush/close error");
    }

private:
    bool Fail(const std::string& why) { ok_ = false; if (err_.empty()) { err_ = why; } return false; }
    detail::FileSink  fs_;
    bool              ok_ = true;
    std::string       err_;
    uint32_t          K_ = 0, F_keep_ = 0, F_bot_ = 0;
    uint64_t          n_ = 0;
    std::vector<char> rec_;
    std::vector<int>  prev_;
};

// ---- Table (reader) ---------------------------------------------------------------------------
class Table
{
public:
    // Opens and validates. Null (with `why`) on any mismatch: magic/version, the source fingerprint
    // (content hash + size), or the file length -- which must equal preamble + N*record + header +
    // trailer EXACTLY. The TABLE's own mtime is never consulted (it is the cache's last-use stamp, see
    // MarkUsed), so refreshing it cannot invalidate anything. Never throws.
    static std::shared_ptr<const Table> Open(const std::filesystem::path& p, uint64_t content_hash,
                                             uint64_t src_size, std::string* why = nullptr)
    {
        auto fail = [&](const std::string& s) { if (why) { *why = s; } return std::shared_ptr<const Table>(); };
        std::shared_ptr<Table> t(new Table());
        uint32_t version = 0;
        uint64_t header_off = 0, n = 0;
        std::string err;
        if (!ReadHeader(p, t->is_, t->h_, version, header_off, n, err)) { return fail(err); }
        if (version != kVersion) { return fail("bad magic/version"); }
        if (t->h_.content_hash != content_hash || t->h_.src_size != src_size) { return fail("source changed"); }
        if (t->h_.N != n || t->h_.K == 0 || t->h_.F_keep == 0 || t->h_.F_bot > kMaxBottomRows) { return fail("corrupt header"); }
        t->rec_ = RecordBytes(t->h_.K, t->h_.F_keep, t->h_.F_bot);
        if (kPreambleBytes + n * t->rec_ != header_off) { return fail("record region mismatch"); }
        return t;
    }

    // Reads and decodes a table file's header, any version from 1 to kVersion (the cache sweep reads
    // legacy v1 files too). Validates the preamble/trailer framing and the exact header extent; leaves
    // the record region and the source fingerprint to the caller. `is` is left open on success.
    static bool ReadHeader(const std::filesystem::path& p, std::ifstream& is, Header& h, uint32_t& version,
                           uint64_t& header_off, uint64_t& n, std::string& why)
    {
        auto fail = [&](const char* s) { why = s; return false; };
        std::error_code ec;
        const uint64_t fsz = static_cast<uint64_t>(std::filesystem::file_size(p, ec));
        if (ec) { return fail("absent"); }
        if (fsz < kPreambleBytes + kTrailerBytes) { return fail("too short"); }
        is.open(p, std::ios::binary);
        if (!is) { return fail("cannot open"); }
        char pre[kPreambleBytes];
        if (!ReadAt(is, 0, pre, sizeof pre)) { return fail("unreadable preamble"); }
        uint32_t magic;
        std::memcpy(&magic, pre, 4); std::memcpy(&version, pre + 4, 4);
        if (magic != kMagic || version < 1 || version > kVersion) { return fail("bad magic/version"); }
        char tr[kTrailerBytes];
        if (!ReadAt(is, fsz - kTrailerBytes, tr, sizeof tr)) { return fail("unreadable trailer"); }
        uint64_t header_len;
        uint32_t tmagic, tversion;
        std::memcpy(&header_off, tr, 8); std::memcpy(&header_len, tr + 8, 8); std::memcpy(&n, tr + 16, 8);
        std::memcpy(&tmagic, tr + 24, 4); std::memcpy(&tversion, tr + 28, 4);
        if (tmagic != kMagic || tversion != version) { return fail("bad trailer"); }
        if (header_off + header_len + kTrailerBytes != fsz || header_off < kPreambleBytes || header_len > (64u << 20))
        { return fail("length mismatch"); }
        std::vector<char> hb(static_cast<std::size_t>(header_len));
        if (!ReadAt(is, header_off, hb.data(), hb.size())) { return fail("unreadable header"); }
        detail::MemReader r(hb.data(), hb.size());
        if (!detail::DecodeHeader(r, h, version)) { return fail("corrupt header"); }
        return true;
    }

    const Header& header() const { return h_; }
    uint64_t      size()   const { return h_.N; }

    // Binary search over the record region. On a hit copies the whole record into `rec`. The most
    // recent hit is remembered: a keep decision and the bottoming decision that follows it look up
    // the same hand, so the second costs no I/O. Thread-safe (one reader, mutex-serialised).
    bool Find(const std::vector<int>& comp, std::vector<char>& rec) const
    {
        if (comp.size() != h_.K || h_.N == 0) { return false; }
        std::lock_guard<std::mutex> lk(mu_);
        if (last_ok_ && last_comp_ == comp) { rec = last_rec_; return true; }
        key_.resize(h_.K);
        uint64_t lo = 0, hi = h_.N;
        while (lo < hi)
        {
            const uint64_t mid = lo + (hi - lo) / 2;
            const uint64_t off = kPreambleBytes + mid * rec_;
            if (!ReadAt(off, reinterpret_cast<char*>(key_.data()), 4u * h_.K)) { return false; }
            const int c = detail::CompareKey(key_.data(), comp.data(), h_.K);
            if (c < 0)      { lo = mid + 1; }
            else if (c > 0) { hi = mid; }
            else
            {
                rec.resize(rec_);
                if (!ReadAt(off, rec.data(), rec_)) { return false; }
                last_comp_ = comp; last_rec_ = rec; last_ok_ = true;
                return true;
            }
        }
        return false;
    }

    // Field accessors over a record returned by Find (index bounds are the CALLER's check, mirroring
    // the in-memory policy's own `idx < size()` tests).
    bool KeepFlag(const std::vector<char>& rec, int idx) const
    { return rec[4u * h_.K + static_cast<uint32_t>(idx)] != 0; }
    bool HasRow(const std::vector<char>& rec, int idx) const
    {
        uint32_t mask; std::memcpy(&mask, rec.data() + 4u * h_.K + h_.F_keep, 4);
        return (mask >> idx) & 1u;
    }
    void Row(const std::vector<char>& rec, int idx, std::vector<int>& out) const
    {
        out.resize(h_.K);
        std::memcpy(out.data(), rec.data() + 4u * h_.K + h_.F_keep + 4 + 4u * h_.K * static_cast<uint32_t>(idx), 4u * h_.K);
    }

private:
    Table() = default;
    static bool ReadAt(std::ifstream& is, uint64_t off, char* dst, std::size_t n)
    {
        is.clear();
        is.seekg(static_cast<std::streamoff>(off));
        is.read(dst, static_cast<std::streamsize>(n));
        if (!is || static_cast<std::size_t>(is.gcount()) != n) { is.clear(); return false; }
        return true;
    }
    bool ReadAt(uint64_t off, char* dst, std::size_t n) const { return ReadAt(is_, off, dst, n); }
    Header                     h_;
    std::size_t                rec_ = 0;
    mutable std::mutex         mu_;
    mutable std::ifstream      is_;
    mutable std::vector<int>   key_;
    mutable std::vector<int>   last_comp_;
    mutable std::vector<char>  last_rec_;
    mutable bool               last_ok_ = false;
};

// ---- Placement --------------------------------------------------------------------------------
// MTG_KEEP_TABLE_DIR overrides; else the platform's per-user cache dir; else the temp dir. Never the
// repo checkout (see the header comment: it is a 9p share here).
inline std::filesystem::path KeepTableDir()
{
    auto env = [](const char* k) -> const char* { const char* v = std::getenv(k); return (v && *v) ? v : nullptr; };
    if (const char* d = env("MTG_KEEP_TABLE_DIR")) { return std::filesystem::path(d); }
#ifdef _WIN32
    if (const char* d = env("LOCALAPPDATA")) { return std::filesystem::path(d) / "mtg" / "keeptable"; }
#else
    if (const char* d = env("XDG_CACHE_HOME")) { return std::filesystem::path(d) / "mtg" / "keeptable"; }
    if (const char* d = env("HOME"))           { return std::filesystem::path(d) / ".cache" / "mtg" / "keeptable"; }
#endif
    std::error_code ec;
    const std::filesystem::path t = std::filesystem::temp_directory_path(ec);
    return (ec ? std::filesystem::path(".") : t) / "mtg-keeptable";
}

namespace detail
{
    constexpr uint64_t kFnvBasis = 1469598103934665603ull;
    inline uint64_t Fnv1a(const char* p, std::size_t n, uint64_t h = kFnvBasis)
    {
        for (std::size_t i = 0; i < n; ++i) { h ^= static_cast<unsigned char>(p[i]); h *= 1099511628211ull; }
        return h;
    }
    inline std::string Hex16(uint64_t h)
    {
        static const char* hex = "0123456789abcdef";
        std::string hs;
        for (int i = 60; i >= 0; i -= 4) { hs += hex[(h >> i) & 0xf]; }
        return hs;
    }
    // The sidecar's filename, sanitised: makes a cache listing readable. Never part of the identity.
    inline std::string Stem(const std::string& src)
    {
        std::string stem = std::filesystem::path(src).filename().string();
        for (char& c : stem) { if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-')) { c = '_'; } }
        if (stem.size() > 64) { stem.resize(64); }
        return stem;
    }
    // The mtime representation the header and the refs store (unchanged from v1).
    inline uint64_t MtimeKey(std::filesystem::file_time_type t)
    { return static_cast<uint64_t>(t.time_since_epoch().count()); }
}   // namespace detail

// `<sanitised sidecar filename>-<content hash>.keeptable`: every path holding the same bytes names the
// same file, and a regenerated sidecar names a NEW one (the old one ages out through Collect()).
inline std::string TableFileName(const std::string& src, uint64_t content_hash)
{
    return detail::Stem(src) + "-" + detail::Hex16(content_hash) + ".keeptable";
}

// `<sanitised sidecar filename>-<fnv1a64 of the canonical path>.keepref`: one per sidecar PATH.
inline std::string RefFileName(const std::string& canonical_src)
{
    return detail::Stem(canonical_src) + "-" + detail::Hex16(detail::Fnv1a(canonical_src.data(), canonical_src.size()))
         + ".keepref";
}

// FNV-1a 64 of a file's bytes. Byte-wise FNV runs at ~1 GB/s, far above the read rate of the 9p checkout
// the sidecars live on, and it is paid once per (path, size, mtime) -- the ref remembers the answer.
// 64 bits against accidental collision among a few thousand sidecar versions: ~1e-13.
inline bool ContentHash(const std::filesystem::path& p, uint64_t& out)
{
    std::ifstream is(p, std::ios::binary);
    if (!is) { return false; }
    std::vector<char> buf(1u << 20);
    uint64_t h = detail::kFnvBasis;
    while (is)
    {
        is.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize got = is.gcount();
        if (got > 0) { h = detail::Fnv1a(buf.data(), static_cast<std::size_t>(got), h); }
    }
    if (is.bad() || !is.eof()) { return false; }
    out = h;
    return true;
}

// ---- Content refs: (canonical path, size, mtime) -> content hash ------------------------------------
// [magic u32][version u32][src_size u64][src_mtime u64][content_hash u64][path_len u32][path bytes],
// length-exact. Written to a unique temp and renamed into place, so a reader sees a whole ref or none.
constexpr uint32_t kRefMagic   = 0x52504b4cu;   // 'LKPR'
constexpr uint32_t kRefVersion = 1u;
struct Ref
{
    std::string src_path;
    uint64_t    src_size = 0, src_mtime = 0, content_hash = 0;
};

inline bool ReadRef(const std::filesystem::path& p, Ref& r)
{
    std::ifstream is(p, std::ios::binary);
    if (!is) { return false; }
    std::string b((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    if (b.size() > (1u << 16)) { return false; }
    detail::MemReader m(b.data(), b.size());
    uint32_t magic = 0, version = 0;
    return detail::GetPod(m, magic) && magic == kRefMagic && detail::GetPod(m, version) && version == kRefVersion
        && detail::GetPod(m, r.src_size) && detail::GetPod(m, r.src_mtime) && detail::GetPod(m, r.content_hash)
        && detail::GetStr(m, r.src_path) && m.AtEnd();
}

inline void WriteRef(const std::filesystem::path& p, const Ref& r, uint64_t nonce)
{
    detail::StringSink b;
    detail::PutPod<detail::StringSink, uint32_t>(b, kRefMagic);
    detail::PutPod<detail::StringSink, uint32_t>(b, kRefVersion);
    detail::PutPod<detail::StringSink, uint64_t>(b, r.src_size);
    detail::PutPod<detail::StringSink, uint64_t>(b, r.src_mtime);
    detail::PutPod<detail::StringSink, uint64_t>(b, r.content_hash);
    detail::PutStr(b, r.src_path);
    const std::filesystem::path tmp = p.string() + "." + std::to_string(nonce) + ".tmp";
    std::error_code ec;
    {
        std::ofstream os(tmp, std::ios::binary);
        os.write(b.b.data(), static_cast<std::streamsize>(b.b.size()));
        os.close();
        if (!os) { std::filesystem::remove(tmp, ec); return; }
    }
    std::filesystem::rename(tmp, p, ec);
    if (ec) { std::filesystem::remove(tmp, ec); }   // a racing writer won (Windows); its ref is as good
}

// The content hash of `sidecar` (stat'd by the caller as size/mtime, keyed as `canonical`): from its ref
// when the ref still matches, else by hashing the file and (re)writing the ref. False if the file cannot
// be read or changed while being hashed -- the caller then skips the table, as for any FS error.
inline bool ResolveContentHash(const std::filesystem::path& dir, const std::filesystem::path& sidecar,
                               const std::string& canonical, uint64_t size, uint64_t mtime,
                               uint64_t nonce, uint64_t& hash)
{
    const std::filesystem::path rp = dir / RefFileName(canonical);
    Ref r;
    if (ReadRef(rp, r) && r.src_path == canonical && r.src_size == size && r.src_mtime == mtime)
    { hash = r.content_hash; return true; }
    if (!ContentHash(sidecar, hash)) { return false; }
    std::error_code e1, e2;
    const uint64_t size2  = static_cast<uint64_t>(std::filesystem::file_size(sidecar, e1));
    const uint64_t mtime2 = detail::MtimeKey(std::filesystem::last_write_time(sidecar, e2));
    if (e1 || e2 || size2 != size || mtime2 != mtime) { return false; }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    r.src_path = canonical; r.src_size = size; r.src_mtime = mtime; r.content_hash = hash;
    WriteRef(rp, r, nonce);
    return true;
}

// ---- Use marking + eviction -------------------------------------------------------------------------
// Refresh a table's mtime: the cache's "last used" stamp (atime is unreliable under relatime/noatime).
// Safe for validation -- the header holds the SOURCE's fingerprint and Open never reads the table's own
// times. Failure is ignored (it only makes the table look older to the sweep).
inline void MarkUsed(const std::filesystem::path& p)
{
    std::error_code ec;
    std::filesystem::last_write_time(p, std::filesystem::file_time_type::clock::now(), ec);
}

struct CollectOptions
{
    uint64_t cap_bytes = 0;            // size cap on the directory; 0 = unbounded (no LRU eviction)
    double   space_fraction = 0.25;    // the cap is also <= this share of (cache + free space); 0 = off
    std::chrono::seconds tmp_max_age{ 24 * 3600 };   // a build temp this old is a dead process's partial
    std::chrono::seconds orphan_grace{ 3600 };       // an unreferenced table must have been idle this long
    std::chrono::seconds lru_min_idle{ 600 };        // the cap never evicts a table used this recently
};
struct CollectStats
{
    int      removed_tmp = 0, removed_ref = 0, removed_orphan = 0, removed_lru = 0;
    uint64_t freed = 0, total_after = 0, cap = 0;
    int removed() const { return removed_tmp + removed_ref + removed_orphan + removed_lru; }
};

// One sweep over `dir`. Never touches a file named in `protect` (this process's open tables), and every
// removal is best-effort: a file another process holds open cannot be deleted on Windows, and another
// process may remove or replace a file mid-sweep. Removing a table another process is still READING is
// harmless on POSIX (its descriptor keeps the inode) and refused on Windows; either way the worst case
// is a later rebuild, never a wrong policy, because Open() validates every table it reads.
//   1. refs whose sidecar is gone or changed (size/mtime) -> removed; the rest form the LIVE hash set;
//   2. `*.tmp` older than tmp_max_age -> removed (a live build rewrites its temp continuously);
//   3. a v2 table no live ref names, idle for orphan_grace -> removed (the grace covers a process that
//      wrote its ref and table between this sweep's two passes); a legacy v1 table whose header
//      src_path is gone -> removed (v1 is path-keyed, so nothing can ever validate it again);
//   4. if the remainder exceeds the cap, least-recently-used tables idle for lru_min_idle -> removed
//      until it fits. The cap is SOFT: tables in active use are never evicted for it, because on POSIX
//      unlinking an open file frees nothing and the next launch would only rebuild it.
inline CollectStats Collect(const std::filesystem::path& dir, const std::set<std::string>& protect,
                            const CollectOptions& o,
                            std::filesystem::file_time_type now = std::filesystem::file_time_type::clock::now())
{
    namespace fs = std::filesystem;
    CollectStats st;
    struct Entry { fs::path p; std::string name; uint64_t size; fs::file_time_type mtime; };
    std::vector<Entry> refs, rest;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        std::error_code e1, e2, e3;
        if (!it->is_regular_file(e1) || e1) { continue; }
        Entry e{ it->path(), it->path().filename().string(), 0, {} };
        e.size  = static_cast<uint64_t>(fs::file_size(e.p, e2));
        e.mtime = fs::last_write_time(e.p, e3);
        if (e2 || e3) { continue; }
        auto ends = [&](const char* suf) { const std::size_t n = std::strlen(suf);
                                           return e.name.size() >= n && e.name.compare(e.name.size() - n, n, suf) == 0; };
        if (ends(".keepref"))                          { refs.push_back(e); }
        else if (ends(".keeptable") || ends(".tmp"))   { rest.push_back(e); }
    }
    auto remove = [&](const Entry& e) { std::error_code rec; return fs::remove(e.p, rec) && !rec; };

    std::set<uint64_t> live;
    for (const Entry& e : refs)
    {
        Ref r;
        bool ok = ReadRef(e.p, r);
        if (ok)
        {
            std::error_code e1, e2;
            const uint64_t sz = static_cast<uint64_t>(fs::file_size(r.src_path, e1));
            const uint64_t mt = detail::MtimeKey(fs::last_write_time(r.src_path, e2));
            ok = !e1 && !e2 && sz == r.src_size && mt == r.src_mtime;
        }
        if (ok) { live.insert(r.content_hash); }
        else if (remove(e)) { ++st.removed_ref; st.freed += e.size; }
    }

    std::vector<Entry> tables;
    uint64_t total = 0;
    for (const Entry& e : rest)
    {
        const auto idle = now - e.mtime;
        if (e.name.size() >= 4 && e.name.compare(e.name.size() - 4, 4, ".tmp") == 0)
        {
            if (idle > o.tmp_max_age && remove(e)) { ++st.removed_tmp; st.freed += e.size; }
            else { total += e.size; }
            continue;
        }
        if (protect.count(e.name)) { total += e.size; continue; }
        bool orphan = false;
        {
            std::ifstream is;
            Header h;
            uint32_t version = 0;
            uint64_t off = 0, n = 0;
            std::string why;
            if (Table::ReadHeader(e.p, is, h, version, off, n, why))
            {
                if (version >= 2) { orphan = !live.count(h.content_hash) && idle > o.orphan_grace; }
                else { std::error_code xe; orphan = !fs::exists(h.src_path, xe) && !xe; }
            }
        }   // closed before any removal (Windows cannot delete a file this process holds open)
        if (orphan && remove(e)) { ++st.removed_orphan; st.freed += e.size; continue; }
        total += e.size;
        tables.push_back(e);
    }

    uint64_t cap = o.cap_bytes;
    if (cap > 0 && o.space_fraction > 0)
    {
        const fs::space_info si = fs::space(dir, ec);
        if (!ec)
        {
            const uint64_t share = static_cast<uint64_t>(static_cast<double>(total + si.available) * o.space_fraction);
            cap = std::min(cap, share);
        }
    }
    st.cap = cap;
    if (cap > 0 && total > cap)
    {
        std::sort(tables.begin(), tables.end(), [](const Entry& a, const Entry& b) { return a.mtime < b.mtime; });
        for (const Entry& e : tables)
        {
            if (total <= cap) { break; }
            if (now - e.mtime < o.lru_min_idle) { continue; }
            if (remove(e)) { ++st.removed_lru; st.freed += e.size; total -= e.size; }
        }
    }
    st.total_after = total;
    return st;
}

// MTG_KEEP_TABLE_CACHE_GB: size cap on KeepTableDir() in whole GB; 0 (or negative) = unbounded. DEFAULT 64.
// Why 64: the content-keyed working set is ~13 GB per generation of the whole deck set (FiveColour 3.2 GB
// is the largest table), so 64 GB holds ~5 distinct generations at once -- every branch a busy machine
// plausibly plays in a day -- while staying ~6% of this 1 TB disk. Collect() also clamps the cap to a
// quarter of (cache + free space), so a smaller disk (the viewer's Windows machine) or a disk filling for
// other reasons shrinks the cache instead of running out: the 2026-11-03 failure was ENOSPC.
inline uint64_t CacheCapBytes()
{
    static const int gb = EnvInt("MTG_KEEP_TABLE_CACHE_GB", 64);
    return gb > 0 ? static_cast<uint64_t>(gb) << 30 : 0;
}

// The names (in KeepTableDir) of every table this process has opened or built -- never swept by it.
inline std::set<std::string> ProtectedTables(const std::string* add = nullptr)
{
    static std::mutex mu;
    static std::set<std::string> s;
    std::lock_guard<std::mutex> lk(mu);
    if (add) { s.insert(*add); }
    return s;
}

// Sweep `dir`, at most once an hour across all processes (a `.gc-stamp` file's mtime), or always when
// `force` (after a build, the moment the cache just grew by a whole table). The stamp keeps a launch's
// cost at one stat: the harness and the viewer start many processes.
inline void MaybeCollect(const std::filesystem::path& dir, bool force)
{
    namespace fs = std::filesystem;
    const fs::path stamp = dir / ".gc-stamp";
    const auto now = fs::file_time_type::clock::now();
    std::error_code ec;
    if (!force)
    {
        const auto t = fs::last_write_time(stamp, ec);
        if (!ec && t <= now && now - t < std::chrono::hours(1)) { return; }
    }
    { std::ofstream touch(stamp, std::ios::app); }
    fs::last_write_time(stamp, now, ec);
    CollectOptions o;
    o.cap_bytes = CacheCapBytes();
    const CollectStats st = Collect(dir, ProtectedTables(), o, now);
    if (st.removed() > 0)
    {
        auto gb = [](uint64_t b) { return static_cast<double>(b) / 1073741824.0; };
        std::cerr << "[keeptable] cache sweep: removed " << st.removed() << " files (" << gb(st.freed) << " GB: "
                  << st.removed_lru << " least-recently-used, " << st.removed_orphan << " unreferenced, "
                  << st.removed_ref << " stale refs, " << st.removed_tmp << " stale temps); cache now "
                  << gb(st.total_after) << " GB, cap " << (st.cap ? gb(st.cap) : 0.0) << " GB\n";
    }
}
}   // namespace keeptable
