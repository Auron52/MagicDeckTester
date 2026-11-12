#pragma once
// PER-GAME PARTIAL RESULTS for `mtg --batch` (docs/design/uncharged-enumeration-backstop-hole.md,
// "2026-11-12 05:09 UTC").
//
// Why this exists. A batch reports a job only when the job's LAST game lands: `<name>.wins` and the
// streamed `played= avg= digest= ms= units=` line are both written by the job-done callback. So a
// killed, crashed or abandoned process lost every game its unfinished jobs had already played -- up to
// ~24 per value-leaf matrix chunk -- and a resume had to replay them. The USER (2026-11-12): "we should
// make sure that this doesn't lose more games than the ones that were still running."
//
// The shape. Each finished game appends ONE self-checking line to `<game-log-dir>/<name>.partial`,
// flushed before the game is reported finished. The job's normal outputs are written exactly as before
// when it completes, and once the WHOLE batch has finished cleanly every partial is removed, so a run
// that is never interrupted leaves the directory as it always did. (Not per job: a driver that re-runs
// a whole manifest needs the completed jobs' games too.) `mtg --batch --resume` reads every partial in the directory
// and PREFILLS the matching games' slots instead of replaying them; the job's reduction (avg, the
// in-order case digest, units, played) then runs over exactly the per-game values an uninterrupted run
// would have held, so its .wins and its result line are byte-identical. `ms` is the one aggregate that
// is not reproducible run to run anyway; a prefilled game contributes the ms it was recorded with.
//
// Records are keyed by (job identity, GLOBAL game index, per-game shuffle seed), never by job name, so
// a driver that re-splits its chunks around a skip list on resume still finds every game it banked.
#include <cstdint>
#include <map>
#include <optional>
#include <tuple>
#include <string>
#include <string_view>
#include <vector>

namespace batchpartial
{

// One finished game. `limit`/`rel` record the per-game work ceiling the game ran under, because an
// ABANDONED verdict (and, in principle, a played one) is only valid for that ceiling: a resumed job may
// reuse the record only when it would apply the same one (see BatchRunner's prefill rule).
struct Record
{
    int         gi        = 0;    // GLOBAL game index (job.game_index + local)
    uint64_t    seed      = 0;    // the game's shuffle seed (job.seed + local)
    bool        abandoned = false;
    int         win_turn  = 0;    // meaningful only when !abandoned; <= 0 = no win within max_turns
    uint64_t    digest    = 0;    // GameLogger::Digest of the game's decision stream
    long long   units     = 0;    // GameWorkMeter units (feeds units_total / .units)
    long long   ms        = 0;    // core-ms (feeds the job's ms=)
    long long   limit     = 0;    // effective per-game work ceiling applied (0 = none)
    std::string rel       = "-";  // relative-ceiling signature when `limit` came from a frozen cell
                                  // ceiling, else "-" (no spaces, by construction)
};

// FNV-1a 64, the hash every identity piece here is built from.
constexpr uint64_t kFnvBasis = 1469598103934665603ULL;
uint64_t Fnv1a(const void* data, std::size_t n, uint64_t h = kFnvBasis);
inline uint64_t Fnv1a(std::string_view s, uint64_t h = kFnvBasis) { return Fnv1a(s.data(), s.size(), h); }

// "<16 hex>"
std::string Hex16(uint64_t v);

// The header line that opens every partial file (newline included).
std::string FormatHeader(uint64_t id, const std::string& job_name);

// One record as a complete line, '\n' included. The line ends in ` *<8 hex>`, a checksum of the
// bytes before it, so a TORN line (a kill mid-write, or a crash that leaves a zero-filled tail) is
// recognised and dropped rather than parsed as a result.
std::string FormatRecord(const Record& r);

// Parse ONE line (without its '\n'). nullopt for anything malformed or failing its checksum.
std::optional<Record> ParseRecord(std::string_view line);

struct FileContents
{
    uint64_t            id = 0;
    std::string         job_name;
    std::vector<Record> records;   // in file order
    int                 dropped = 0;   // lines rejected (torn / bad checksum / malformed)
};

// Parse a whole file. nullopt if the header is missing or malformed (the file then holds nothing
// usable). A final line with no terminating '\n' is ALWAYS dropped, even if it happens to parse: the
// writer emits each record as one complete line, so an unterminated tail is by definition a write that
// did not finish.
std::optional<FileContents> ParseFile(std::string_view bytes);

// Signature of a relative per-game ceiling (Job::abandon_k / abandon_calib / abandon_floor_units). The
// frozen value is a deterministic function of the cell's calibration sample, so two runs with the same
// signature freeze the same ceiling.
std::string RelSignature(double k, int calib, long long floor_units);

// The job's CASE DIGEST: FNV-1a over the per-game digests' bytes, in game (gi) order. The one fold
// both an uninterrupted job and a resumed one go through, so they cannot drift apart.
uint64_t FoldCaseDigest(const std::vector<uint64_t>& per_game_in_gi_order);

// Every record of every partial file read at resume, keyed (job identity, global gi, game seed), with
// one entry per CEILING (limit, rel) the game was recorded under: the same game legitimately appears
// under two ceilings when two jobs of one identity differ only in abandon_units. The same game under the
// same ceiling can also appear twice (a resume re-persists the games it reused into the new job's file
// before removing the old one, so a kill between the two leaves both). Identical copies fold into one;
// copies that DISAGREE are a determinism failure and that entry is dropped, so the game is replayed
// rather than trusted.
class Index
{
public:
    void Add(uint64_t id, const Record& r, int file);
    // The first non-conflicted entry for the game that `accept` takes (the caller's ceiling rule), or
    // nullptr.
    template <typename Pred>
    const Record* Find(uint64_t id, int gi, uint64_t seed, Pred accept) const
    {
        auto it = map_.find(std::make_tuple(id, gi, seed));
        if (it == map_.end()) { return nullptr; }
        for (const Entry& e : it->second) { if (!e.conflict && accept(e.rec)) { return &e.rec; } }
        return nullptr;
    }
    const Record* Find(uint64_t id, int gi, uint64_t seed) const
    { return Find(id, gi, seed, [](const Record&) { return true; }); }
    int Conflicts() const { return conflicts_; }

private:
    struct Entry
    {
        Record           rec;
        bool             conflict = false;
    };
    std::map<std::tuple<uint64_t, int, uint64_t>, std::vector<Entry>> map_;
    int conflicts_ = 0;
};

}  // namespace batchpartial
