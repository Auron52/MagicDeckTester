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
// KeepTableDir() -- MTG_KEEP_TABLE_DIR if set, else the user's cache dir -- named by a hash of the
// sidecar's CANONICAL path (a symlinked sidecar shares the real one's table) and validated against the
// source's (size, mtime) stored in the header. A regenerated sidecar overwrites its own table, so
// nothing accumulates. It is a persistent cache, not a per-run temp: a true temp would re-parse
// ~1.8 GB of JSON on every launch.
//
// Positioned reads, deliberately NOT mmap: a mapping over a file another process is rebuilding turns
// a rejected cache into a SIGBUS, and mmap would need per-platform code. Reads are serialised by a
// mutex on one std::ifstream; at a handful of lookups per game that costs nothing measurable, and it
// keeps a single portable path (the play viewer runs this on Windows).
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace keeptable
{
constexpr uint32_t    kMagic        = 0x54504b4cu;   // 'LKPT'
constexpr uint32_t    kVersion      = 1u;
constexpr std::size_t kPreambleBytes = 8;
constexpr std::size_t kTrailerBytes  = 8 + 8 + 8 + 4 + 4;
constexpr uint32_t    kMaxBottomRows = 32;           // bottom_mask is a uint32 ((max_mull+1)*2 <= 32)

// The policy's scalars + provenance, stored once. Mirrors the non-table members of
// ExhaustiveKeepPolicy (which includes this header, so it cannot be named here).
struct Header
{
    std::string src_path;     // the sidecar this table was built from, as the loader keyed it
    uint64_t    src_size  = 0;
    uint64_t    src_mtime = 0;
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
    inline bool DecodeHeader(MemReader& r, Header& h)
    {
        if (!GetStr(r, h.src_path))     { return false; }
        if (!GetPod(r, h.src_size))     { return false; }
        if (!GetPod(r, h.src_mtime))    { return false; }
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
    // (path + size + mtime), or the file length -- which must equal preamble + N*record + header +
    // trailer EXACTLY. Never throws.
    static std::shared_ptr<const Table> Open(const std::filesystem::path& p, const std::string& src_path,
                                             uint64_t src_size, uint64_t src_mtime, std::string* why = nullptr)
    {
        auto fail = [&](const std::string& s) { if (why) { *why = s; } return std::shared_ptr<const Table>(); };
        std::error_code ec;
        const uint64_t fsz = static_cast<uint64_t>(std::filesystem::file_size(p, ec));
        if (ec) { return fail("absent"); }
        if (fsz < kPreambleBytes + kTrailerBytes) { return fail("too short"); }
        std::shared_ptr<Table> t(new Table());
        t->is_.open(p, std::ios::binary);
        if (!t->is_) { return fail("cannot open"); }
        char pre[kPreambleBytes];
        if (!t->ReadAt(0, pre, sizeof pre)) { return fail("unreadable preamble"); }
        uint32_t magic, version;
        std::memcpy(&magic, pre, 4); std::memcpy(&version, pre + 4, 4);
        if (magic != kMagic || version != kVersion) { return fail("bad magic/version"); }
        char tr[kTrailerBytes];
        if (!t->ReadAt(fsz - kTrailerBytes, tr, sizeof tr)) { return fail("unreadable trailer"); }
        uint64_t header_off, header_len, n;
        std::memcpy(&header_off, tr, 8); std::memcpy(&header_len, tr + 8, 8); std::memcpy(&n, tr + 16, 8);
        std::memcpy(&magic, tr + 24, 4); std::memcpy(&version, tr + 28, 4);
        if (magic != kMagic || version != kVersion) { return fail("bad trailer"); }
        if (header_off + header_len + kTrailerBytes != fsz || header_off < kPreambleBytes || header_len > (64u << 20))
        { return fail("length mismatch"); }
        std::vector<char> hb(static_cast<std::size_t>(header_len));
        if (!t->ReadAt(header_off, hb.data(), hb.size())) { return fail("unreadable header"); }
        detail::MemReader r(hb.data(), hb.size());
        if (!detail::DecodeHeader(r, t->h_)) { return fail("corrupt header"); }
        if (t->h_.src_path != src_path || t->h_.src_size != src_size || t->h_.src_mtime != src_mtime)
        { return fail("source changed"); }
        if (t->h_.N != n || t->h_.K == 0 || t->h_.F_keep == 0 || t->h_.F_bot > kMaxBottomRows) { return fail("corrupt header"); }
        t->rec_ = RecordBytes(t->h_.K, t->h_.F_keep, t->h_.F_bot);
        if (kPreambleBytes + n * t->rec_ != header_off) { return fail("record region mismatch"); }
        return t;
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
    bool ReadAt(uint64_t off, char* dst, std::size_t n) const
    {
        is_.clear();
        is_.seekg(static_cast<std::streamoff>(off));
        is_.read(dst, static_cast<std::streamsize>(n));
        if (!is_ || static_cast<std::size_t>(is_.gcount()) != n) { is_.clear(); return false; }
        return true;
    }
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

// `<sanitised sidecar filename>-<fnv1a64 of the canonical path>.keeptable`: readable in a listing,
// unique per sidecar, and stable across runs so a regeneration overwrites rather than accumulates.
inline std::string TableFileName(const std::string& canonical_src)
{
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : canonical_src) { h ^= c; h *= 1099511628211ull; }
    static const char* hex = "0123456789abcdef";
    std::string hs;
    for (int i = 60; i >= 0; i -= 4) { hs += hex[(h >> i) & 0xf]; }
    std::string stem = std::filesystem::path(canonical_src).filename().string();
    for (char& c : stem) { if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-')) { c = '_'; } }
    if (stem.size() > 64) { stem.resize(64); }
    return stem + "-" + hs + ".keeptable";
}
}   // namespace keeptable
