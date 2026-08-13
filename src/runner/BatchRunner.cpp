#include "BatchRunner.h"
#include "GoldFishRunner.h"
#include "../core/GameEngine.h"
#include "../core/GameLogger.h"
#include "../core/EnvFlags.h"
#include "../core/HardwareConcurrency.h"
#include "../ai/AIEngine.h"
#include "../ai/MulliganProfile.h"
#include "../ai/Profiler.h"
#include "../ai/MulliganProfileIO.h"
#include "../deck/DeckLoader.h"
#include "../ai/ValueArm.h"
#include "../ai/DecisionProviders.h"   // SelectDecisionProvider + Name(): report which archetype a job runs under
#include <algorithm>
#include <climits>
#include <condition_variable>
#include <deque>
#include <iomanip>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
// One fully-resolved job: deck loaded + run parameters resolved. The (potentially huge -- ~167 MB for a
// big deck's exhaustive keep/bottom sidecar) MulliganProfile is NOT stored here: a manifest can list
// hundreds of jobs, and holding every job's profile by value OOMs. Instead the job keeps the profile's
// PATH and the two scalars the play loop needs off the profile directly (vial_target_mv); the profile
// itself is loaded on demand through a small LRU ProfileCache (see below) and copied into each worker's
// AIEngine. Play settings are resolved once at parse (needs the profile transiently) and stored by value.
struct Job
{
    std::string     name;
    Decklist        deck;
    std::string     profile_path;               // key into ProfileCache; loaded lazily by the worker
    int             vial_target_mv      = 0;     // pulled off the profile at parse (cheap scalar)
    int             games               = 0;
    uint64_t        seed                = 0;
    // GLOBAL index of this job's first game. A job is normally a whole run (base 0), but it can also
    // be a CHUNK of one -- games [game_index, game_index+games) of a longer sequence -- which is what
    // lets a chunked generator (the value-leaf depth matrix) pool every chunk of every cell into ONE
    // process instead of one subprocess per chunk. Game i of the job is then globally game
    // (game_index + i): that index drives the opponent spawn schedule and the logged game number,
    // while the shuffle seed stays `seed + i` (so a chunk sets "seed" to base_seed + game_index,
    // exactly as the single-run `--seed (base+off) --game-index off` form does). Default 0 keeps
    // every existing manifest byte-identical.
    int             game_index          = 0;
    int             depth               = 0;
    int             budget_ms           = 0;
    int             max_turns           = 8;   // goldfish horizon (see main.cpp); per-job overridable
    bool            second_main         = false;  // precomputed DeckUsesSecondMain
    int             sched_weight        = 0;   // optional LPT scheduling priority (higher = run first);
                                               // overrides the depth/budget cost proxy for known-slow
                                               // jobs (e.g. Hinata's combo search). 0 => use the proxy.
    // Per-job VALUE-LEAF ARM (see ai/ValueArm.h). These used to be process environment, which is why
    // the depth matrix had to spawn one `mtg --batch` per arm; as job fields, H and V cells share ONE
    // pooled queue and one tail. Sentinels mean "unset" => the env default => byte-identical for every
    // manifest that omits them.
    valuearm::Arm   arm;
    // Per-job deck numbering (see decknumbering in GoldFishRunner.h). Empty => none => the deck's
    // usual per-deck numbering. Held BY VALUE per job: a comparison's combinations each need their
    // own map, and a pointer into a shared cache would have to outlive the worker's job.
    std::map<std::string, std::vector<int>> numbering;
    // CONDEMNATION grouping key: every job of the same matrix cell (deck+arm+depth+seed) shares one.
    // Empty in the manifest => the job's own name, i.e. each job is its own cell and cross-chunk
    // condemnation is inert. Resolved to a dense index at parse (cell_id) so the hot path touches a
    // vector, not a map.
    std::string     cell;
    int             cell_id             = 0;
    // CONDEMNATION judgment key: every cell of the same matrix ROW (deck+arm+depth, i.e. the cell
    // minus its seed) shares one. Tractability is a property of the DEPTH, and a single seed is only
    // a sample of it: judging each seed's cell separately let one 32-minute game condemn V6/V7/V8 on
    // seed 8008 alone while the other three seeds ran 4-10x under the limit and filled -- which makes
    // rows NON-COMPARABLE, since which seeds a row carries moves its mean further than the effects
    // under study (docs/design/condemnation-row-average.md). The MEAN rule therefore accumulates and
    // judges here; max_game_sec stays per-cell (one pathological game should not take 4 seeds' cells
    // with it). Empty in the manifest => the job's cell key, i.e. per-cell judgment exactly as before
    // -- every existing manifest is byte-identical.
    std::string     row;
    int             row_id              = 0;
};

// Tractability guard, evaluated INSIDE the pooled queue rather than between waves.
//
// The old driver could only condemn at a barrier: it ran every cell to a reference sample, stopped
// the world, decided, then ran the survivors. That barrier is why a long run synchronises on its
// single slowest game. Here the rule is applied continuously -- a worker checks it after each game,
// and once a cell is condemned the remaining items of that cell are skipped as they come off the
// queue -- so no cell ever waits for another and there is exactly one tail for the whole matrix.
//
// Absent from the manifest => disabled => byte-identical (nothing is ever skipped).
struct CondemnRule
{
    bool   enabled             = false;
    double sec_per_game        = 0.0;   // condemn a cell whose MEAN cost exceeds this
    int    reference_games     = 0;     // ...but only after it has this many games (the sample)
    int    never_condemn_depth = 0;     // ...and never at or below this depth (the d<=5 ladder IS
                                        //    the crossover; condemning there leaves a HOLE, not a saving)
    // SEPARATE limit for a SINGLE game, applied to games still running (see the in-flight hook).
    // Deliberately not sec_per_game: these measure different things, and sharing the number would
    // condemn a cell with a 5 s/game mean because one game took 61 s. The mean rule asks "is filling
    // this cell affordable?"; this one asks "is this individual game pathological?" -- which the mean
    // cannot answer at all until the game finishes, and a 21.4-hour game answers far too late.
    // 0 / absent => the in-flight rule is off.
    double max_game_sec        = 0.0;
    // How many games of a single NOT-YET-JUDGED condemnable cell may be in flight at once. This is
    // what stops LPT from putting the whole box on work that is about to be thrown away: the cell is
    // metered in at this rate and refilled on each uncondemned completion, while protected cells use
    // the remaining threads. 1 is deliberate -- a cell needs only a trickle to reach its verdict.
    int    drip                = 1;
};

// Bounded LRU cache of loaded (+sidecar-attached) mulligan profiles, keyed by profile path.
//
// A big deck's exhaustive keep/bottom profile is ~167 MB; a recipe/A-B manifest can reference dozens of
// distinct profiles. Loading them all up front (the old Job-holds-profile design) OOMs. This cache loads
// each profile at most once while it stays resident and evicts the least-recently-used beyond a small cap
// (MTG_BATCH_PROFILE_CACHE, default 3). Because the work list is sorted so a profile's games are
// contiguous (see the stable_sort below), the resident set at any instant is ~1 profile (2 briefly at a
// boundary), so a cap of 3 yields essentially one load per distinct profile with no reload thrash.
//
// get() returns a shared_ptr so a worker that grabbed a handle keeps its profile alive even if the cache
// evicts it meanwhile. Loads are serialised under the cache mutex (see get()): the profile's own big
// exhaustive table is a shared_ptr<const>, so the many AIEngine copies already SHARE it (per-worker copies
// cost ~nothing) -- the only real memory pressure is redundant CONCURRENT loads at a profile boundary, which
// serialising eliminates while keeping steady-state play lock-free (it never loads once a profile is warm).
class ProfileCache
{
public:
    explicit ProfileCache(std::size_t cap) : cap_(std::max<std::size_t>(1, cap)) {}

    // Keyed on (profile path, value-sidecar override) because the override CHANGES the loaded object:
    // an H job asks for "none" (no sidecar) and a V job for a model path, off the same deck profile.
    // Keying on the path alone would hand the first arm's profile to the other -- silently measuring
    // one arm twice, which is exactly the class of bug this whole change exists to remove.
    std::shared_ptr<const MulliganProfile> get(const std::string& path,
                                               const std::string& value_profile = std::string())
    {
        const std::string key = value_profile.empty() ? path : (path + '\x1f' + value_profile);
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = map_.find(key);
        if (it != map_.end())
        {
            lru_.splice(lru_.begin(), lru_, it->second.pos);   // mark most-recently-used
            return it->second.ptr;
        }
        // Miss: load UNDER the lock so at most one profile parses at a time. Loading outside the lock is a
        // thundering-herd trap: work is grouped by profile, so a profile boundary makes ~all workers miss on
        // the SAME next profile at once, each redundantly parsing the same 150 MB JSON (~0.6 GB transient
        // apiece) before one wins the insert -- that spiked RSS to 14 GB+. Serialising loads costs only a
        // brief stall per boundary (a load is ~1 s; steady-state play does no loads at all, and the second
        // worker to want a profile now gets the cache hit instead of re-parsing) and bounds resident memory
        // to ~cap profiles + the one in-flight load.
        std::shared_ptr<const MulliganProfile> loaded = load(path, value_profile);
        lru_.push_front(key);
        map_[key] = Entry{loaded, lru_.begin()};
        while (map_.size() > cap_)
        {
            map_.erase(lru_.back());   // evict LRU (its profile survives while any worker holds a handle)
            lru_.pop_back();
        }
        return loaded;
    }

private:
    static std::shared_ptr<const MulliganProfile> load(const std::string& path,
                                                       const std::string& value_profile)
    {
        // AttachValueSidecar reads the override off the thread's arm (see ai/ValueArm.h). Set it for
        // the duration of THIS load only: the loading thread is a worker that will go on to run other
        // jobs, and the arm it runs under is set separately per job.
        const std::string saved = valuearm::t_arm.value_profile;
        valuearm::t_arm.value_profile = value_profile;
        struct Restore {
            const std::string& s; ~Restore() { valuearm::t_arm.value_profile = s; }
        } restore{saved};
        // Mirror ParseJob's original load exactly: base profile (or the built-in default when the file is
        // absent -- the auto-detect case), then attach the exhaustive keep/bottom, eval, and value sidecars
        // (each a no-op when its sidecar file is missing).
        auto prof = std::filesystem::exists(path)
                        ? std::make_shared<MulliganProfile>(LoadDeckProfile(path))
                        : std::make_shared<MulliganProfile>(MulliganProfile::DefaultProfile());
        AttachExhaustiveSidecar(*prof, path);
        AttachEvalSidecar(*prof, path);
        AttachValueSidecar(*prof, path);
        return prof;
    }

    struct Entry { std::shared_ptr<const MulliganProfile> ptr; std::list<std::string>::iterator pos; };
    std::size_t                              cap_;
    std::mutex                               mtx_;
    std::list<std::string>                   lru_;   // front = MRU, back = LRU
    std::unordered_map<std::string, Entry>   map_;
};

std::size_t ProfileCacheCap()
{
    if (const char* e = std::getenv("MTG_BATCH_PROFILE_CACHE"))
    {
        long v = std::atol(e);
        if (v > 0) { return static_cast<std::size_t>(v); }
    }
    return 3;   // small: a big profile is ~167 MB, and the sort keeps the resident set ~1-2 anyway
}

// HEARTBEAT: periodically write the slowest games to disk, RUNNING ones included.
//
// SLOW-GAME lines only appear when a game FINISHES, so the pathological games -- the ones that
// actually matter -- are invisible for exactly as long as they are a problem. A run measured
// 2026-08-10 had a single game at 21.4 hours and nothing on disk said so until it ended. This writes
// a ranked snapshot every MTG_BATCH_HEARTBEAT_MS (default 60s) to MTG_BATCH_HEARTBEAT, merging games
// still in flight (elapsed so far) with the slowest completed ones, so a run can be diagnosed while
// it is running instead of afterwards.
//
// DEFAULT ON. It was opt-in (unset path => no thread), which meant a bare `mtg --batch` had no
// heartbeat, no slow-game lines AND no in-flight condemnation -- all three mid-run protections are
// gated on this object being alive. `MTG_BATCH_HEARTBEAT=0` (or `off`/`none`) disables; a path
// overrides where the ranked file goes.
//
// Every beat also prints ONE line to stderr, and that line leads with worker UTILISATION, because
// that is the number the whole 2026-08-10 saga turned on: the run was diagnosed as an engine
// regression, then as condemnation, when it was 3 of 20 cores busy the entire time. A run cannot
// hide that from a 10-minute heartbeat.
class Heartbeat
{
public:
    Heartbeat(std::size_t slots, std::size_t keep)
        : running_(slots), keep_(keep)
    {
        const char* p = std::getenv("MTG_BATCH_HEARTBEAT");
        const std::string want = p ? std::string(p) : std::string();
        if (p && (want == "0" || want == "off" || want == "none")) { enabled_ = false; }
        else if (p && !want.empty()) { path_ = want; }
        else
        {
            // Default file, under logs/ per the repo convention. If the directory cannot be made
            // (read-only cwd, sandbox), keep the stderr line and simply skip the file -- the
            // instrument must never be the reason a run fails to start.
            std::error_code ec;
            std::filesystem::create_directories("logs/batch", ec);
            if (!ec) { path_ = "logs/batch/heartbeat.txt"; }
        }
        if (const char* m = std::getenv("MTG_BATCH_HEARTBEAT_MS"))
        { const long v = std::atol(m); if (v > 0) { period_ms_ = v; } }
        if (const char* f = std::getenv("MTG_BATCH_HEARTBEAT_MIN_MS"))
        { const long v = std::atol(f); if (v >= 0) { min_ms_ = v; } }
        for (Slot& s : running_) { s.active.store(0); s.start_ms.store(0); }
    }

    bool on() const { return enabled_; }

    // Called once per period with EVERY in-flight game as (cell_id, elapsed ms). One call with the
    // whole set, not one per game, because the mean rule needs to aggregate per cell before judging.
    using InFlight = std::vector<std::pair<int, long long>>;
    void SetInFlightHook(std::function<void(const InFlight&)> f) { on_inflight_ = std::move(f); }

    void SetRunning(std::size_t slot, const std::string& job, int gi, int cell_id)
    {
        if (!enabled_ || slot >= running_.size()) { return; }
        Slot& s = running_[slot];
        {
            std::lock_guard<std::mutex> lk(s.mtx);
            s.job = job; s.gi = gi; s.cell_id = cell_id;
        }
        s.start_ms.store(NowMs(), std::memory_order_relaxed);
        s.active.store(1, std::memory_order_release);
    }

    void ClearRunning(std::size_t slot)
    {
        if (!enabled_ || slot >= running_.size()) { return; }
        running_[slot].active.store(0, std::memory_order_release);
    }

    // Keep only the slowest `keep_` finished games: a full matrix is >100k games and we want a
    // ranked tail, not a log of everything.
    void Finished(const std::string& job, int gi, long long ms)
    {
        // Floor-checked BEFORE the lock. The interesting games are hours long, so recording every
        // finished game would take a mutex on the hot path (20 workers x ~9 ms V-cell games) to
        // retain rows that could never make the top 100 anyway. Below the floor: two loads, no lock.
        if (!enabled_ || ms < min_ms_) { return; }
        std::lock_guard<std::mutex> lk(fin_mtx_);
        if (finished_.size() < keep_) { finished_.push_back({ms, job, gi}); }
        else
        {
            auto worst = std::min_element(finished_.begin(), finished_.end(),
                [](const Entry& a, const Entry& b) { return a.ms < b.ms; });
            if (worst != finished_.end() && worst->ms < ms) { *worst = Entry{ms, job, gi}; }
        }
    }

    void Start()
    {
        if (!enabled_) { return; }
        stop_ = false;
        thread_ = std::thread([this] {
            // A condition_variable, not a poll loop: the thread sleeps for the WHOLE period and is
            // woken only by Stop(). Zero wakeups in between, so it takes nothing from the workers --
            // which matters because it exists to watch a run that is already starved for cores.
            for (;;)
            {
                std::unique_lock<std::mutex> lk(wake_mtx_);
                if (wake_cv_.wait_for(lk, std::chrono::milliseconds(period_ms_),
                                      [this] { return stop_; }))
                { break; }               // Stop() woke us
                lk.unlock();
                Write();
            }
            Write();   // final snapshot, so the file reflects the end state
        });
    }

    void Stop()
    {
        if (!enabled_) { return; }
        { std::lock_guard<std::mutex> lk(wake_mtx_); stop_ = true; }
        wake_cv_.notify_all();
        if (thread_.joinable()) { thread_.join(); }
    }

private:
    struct Entry { long long ms; std::string job; int gi; };
    struct Slot
    {
        std::atomic<int>       active{0};
        std::atomic<long long> start_ms{0};
        std::mutex             mtx;
        std::string            job;
        int                    gi = 0;
        int                    cell_id = 0;
    };

    static long long NowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void Write()
    {
        std::vector<Entry> rows;
        {
            std::lock_guard<std::mutex> lk(fin_mtx_);
            rows = finished_;
        }
        const std::size_t n_done = rows.size();
        const long long now = NowMs();
        std::size_t n_running = 0;
        InFlight live;
        for (Slot& s : running_)
        {
            if (s.active.load(std::memory_order_acquire) == 0) { continue; }
            const long long st = s.start_ms.load(std::memory_order_relaxed);
            std::lock_guard<std::mutex> lk(s.mtx);
            const long long elapsed = now - st;
            rows.push_back({elapsed, s.job + "  [RUNNING]", s.gi});
            ++n_running;
            // A cell's cost only counts FINISHED games, so a cell whose reference sample contains
            // one catastrophic game looks free while the pool keeps dispatching more of it (a run
            // measured 2026-08-10 had a single game at 21.4 h). This is the only thread that sees
            // in-flight elapsed time, so it is where that gap closes.
            if (on_inflight_) { live.emplace_back(s.cell_id, elapsed); }
        }
        if (on_inflight_ && !live.empty()) { on_inflight_(live); }
        std::sort(rows.begin(), rows.end(),
                  [](const Entry& a, const Entry& b) { return a.ms > b.ms; });
        if (rows.size() > keep_) { rows.resize(keep_); }

        // ONE stderr line per beat, utilisation FIRST. A ranked file nobody thought to open is not
        // observability: this is the line that makes "23 hours at 3 of 20 cores" impossible to miss,
        // and it costs one write every 10 minutes.
        {
            const double busy = running_.empty() ? 0.0
                              : 100.0 * static_cast<double>(n_running) / static_cast<double>(running_.size());
            std::fprintf(stderr, "[batch] heartbeat: %zu/%zu workers busy (%.0f%%)",
                         n_running, running_.size(), busy);
            if (!rows.empty() && rows.front().ms >= min_ms_)
            {
                std::fprintf(stderr, "  slowest %.2fh %s gi=%d",
                             static_cast<double>(rows.front().ms) / 3600000.0,
                             rows.front().job.c_str(), rows.front().gi);
            }
            if (!path_.empty()) { std::fprintf(stderr, "  -> %s", path_.c_str()); }
            std::fprintf(stderr, "\n");
            std::fflush(stderr);
        }
        if (path_.empty()) { return; }

        // Write-then-rename so a reader never sees a half-written file.
        const std::string tmp = path_ + ".tmp";
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) { return; }
        out << "# batch heartbeat -- slowest games (running games included, marked [RUNNING])\n"
            << "# " << n_running << " in flight, " << n_done << " finished games retained (top "
            << keep_ << " by duration)\n";
        for (const Entry& e : rows)
        {
            out << std::fixed << std::setprecision(2) << (static_cast<double>(e.ms) / 3600000.0)
                << " h  gi=" << e.gi << "  " << e.job << "\n";
        }
        out.close();
        std::error_code ec;
        std::filesystem::rename(tmp, path_, ec);
    }

    bool                     enabled_ = true;   // MTG_BATCH_HEARTBEAT=0/off/none turns it off
    std::string              path_;             // may be empty (unwritable cwd) -- stderr line still runs
    // 10 minutes. The games this exists to surface run for HOURS, so a coarse snapshot loses
    // nothing, and a rare wake is the point: never compete with the workers it is watching.
    long                     period_ms_ = 600000;
    long long                min_ms_    = 60000;   // ignore anything under a minute (see Finished)
    std::vector<Slot>        running_;
    std::size_t              keep_;
    std::mutex               fin_mtx_;
    std::vector<Entry>       finished_;
    std::function<void(const InFlight&)> on_inflight_;
    std::mutex               wake_mtx_;
    std::condition_variable  wake_cv_;
    bool                     stop_ = false;
    std::thread              thread_;
};

// Sentinel written into a job's win-turn slot for a game the pool SKIPPED because its cell was
// condemned mid-run. Distinct from -1 ("played, did not win"): a skipped game must be dropped from
// the aggregate, not scored as a loss.
constexpr int kSkipped = INT_MIN;

// A single unit of pooled work: game `game` of job `job`.
struct WorkItem
{
    int job;
    int game;
};

// Report any game at or over this many ms (see the SLOW-GAME emission in the worker). A MILLISECOND
// value, not a boolean, so it is getenv+parse rather than EnvOn -- same convention and same variable
// as GoldFishRunner's, INCLUDING its default: ON at 30 s, `=0` disables.
//
// It used to default OFF here while GoldFishRunner defaulted ON, which is exactly backwards: the
// single-game path defaults to reporting on runs that are short and watched, and `--batch` -- the
// route every long pooled run is REQUIRED to take -- defaulted to silence. That inversion is why a
// 23-hour matrix run at 3 of 20 cores, containing one 21.4-hour game, produced no signal at all.
static long long SlowGameMs()
{
    const char* e = std::getenv("MTG_SLOW_GAME_MS");
    if (!e || !*e) { return 30000LL; }
    char* end = nullptr;
    const long long v = std::strtoll(e, &end, 10);
    return (end && *end == '\0' && v > 0) ? v : 0;
}

Job ParseJob(const json& jspec, ProfileCache& cache)
{
    Job j;
    j.name = jspec.value("name", std::string("<unnamed>"));

    if (!jspec.contains("deck"))  { throw std::runtime_error("manifest job missing \"deck\""); }
    if (!jspec.contains("games")) { throw std::runtime_error("manifest job missing \"games\""); }
    if (!jspec.contains("seed"))  { throw std::runtime_error("manifest job missing \"seed\""); }

    std::filesystem::path deck_path = jspec["deck"].get<std::string>();
    j.deck = DeckLoader::LoadFromFile(deck_path);

    j.games               = jspec["games"].get<int>();
    j.seed                = jspec["seed"].get<uint64_t>();
    j.game_index          = jspec.value("game_index", 0);   // chunk offset; see Job::game_index
    j.depth               = jspec.value("depth", 0);
    j.budget_ms           = jspec.value("budget_ms", 0);
    j.sched_weight        = jspec.value("weight", 0);      // LPT priority override (see Job::sched_weight)
    j.max_turns           = jspec.value("max_turns", 8);   // global goldfish horizon; per-job override

    // Per-job VALUE-LEAF ARM (see ai/ValueArm.h). Every key is optional and every default is the
    // "unset" sentinel, so a manifest that omits them all runs exactly as before.
    if (jspec.contains("value_model"))
    { j.arm.value_model = jspec["value_model"].get<bool>() ? 1 : 0; }
    if (jspec.contains("value_min_depth"))
    { j.arm.value_min_depth = jspec["value_min_depth"].get<int>(); }
    if (jspec.contains("ladder_value_leaf"))
    { j.arm.ladder_value_leaf = jspec["ladder_value_leaf"].get<bool>() ? 1 : 0; }
    if (jspec.contains("value_startgate_alpha"))
    { j.arm.startgate_alpha = jspec["value_startgate_alpha"].get<double>(); }
    j.arm.value_profile   = jspec.value("value_profile", std::string());
    // "deck_numbering": path to {"Card Name": [n1, n2, ...]}. Throws on a bad path/parse -- a
    // mis-specified numbering must fail loudly, never silently fall back to per-deck numbering
    // (that would read as "comparison mode on" while measuring unpaired arms).
    if (jspec.contains("deck_numbering"))
    {
        const std::string np = jspec["deck_numbering"].get<std::string>();
        std::ifstream nf(np);
        if (!nf) { throw std::runtime_error("manifest job \"deck_numbering\": cannot open " + np); }
        nlohmann::json nj; nf >> nj;
        for (auto it = nj.begin(); it != nj.end(); ++it)
        { j.numbering[it.key()] = it.value().get<std::vector<int>>(); }
        if (j.numbering.empty())
        { throw std::runtime_error("manifest job \"deck_numbering\": empty map in " + np); }
    }
    j.cell                = jspec.value("cell", std::string());
    j.row                 = jspec.value("row", std::string());   // mean-rule judgment key; see Job::row
    // Note: lookahead bottoming is no longer a manifest field -- the engine derives it
    // from depth (on iff depth>0). A stale "lookahead_bottoming" key is simply ignored.

    // Profile: explicit "profile" path, else auto-detect deckname.profile.json
    // (mirrors the single-run mtg.exe behaviour exactly). The full profile is NOT stored on the job --
    // only its path (the worker loads it lazily via the shared LRU cache). We load it once here, through
    // the same cache, to resolve play settings and pull the vial_target_mv scalar; that resident copy is
    // released at the end of this function (or evicted later) rather than retained per job.
    std::filesystem::path profile_path;
    if (jspec.contains("profile"))
    {
        profile_path = jspec["profile"].get<std::string>();
    }
    else
    {
        profile_path = deck_path.parent_path()
                     / (deck_path.stem().string() + ".profile.json");
    }
    j.profile_path = profile_path.string();
    std::shared_ptr<const MulliganProfile> prof = cache.get(j.profile_path, j.arm.value_profile);
    j.vial_target_mv = prof->vial_target_mv;

    // Resolve the effective play settings from the manifest's explicit depth/budget + the deck's value_play.
    // A case that OMITS depth falls to value_play, or -- with no enabled model -- the built-in default depth
    // (d5), NEVER greedy depth 0 (so a model-less "d5" case that pins only budget_ms actually searches at 5,
    // not 0). A case that pins depth uses it verbatim (byte-identical). ParseJob throwing aborts loudly.
    {
        const bool depth_given  = jspec.contains("depth");
        const bool budget_given = jspec.contains("budget_ms");
        const bool ignore_play  = jspec.value("ignore_play_profile", false);
        PlaySettings ps = ResolvePlaySettings(*prof,
                                              depth_given  ? j.depth     : -1,
                                              budget_given ? j.budget_ms : -1,
                                              ignore_play);
        j.depth     = ps.depth;
        j.budget_ms = ps.budget_ms;
        std::cerr << "[play] " << j.name << " depth=" << ps.depth << " budget=" << ps.budget_ms
                  << "ms source=" << ps.source
                  // Which archetype's heuristics this job runs under. Detection is by card params, so
                  // an edited decklist can cross a signature and silently change provider between two
                  // arms of a comparison -- the one asymmetry a shared apparatus cannot absorb.
                  // Under MTG_PROVIDER_DECK the effective provider is pinned to the base deck's;
                  // detection still runs on THIS job's list so a crossing is reported, not routed.
                  << " provider=" << SelectDecisionProvider(j.deck).Name();
        const DecisionProvider& det = DetectDecisionProvider(j.deck);
        if (&det != &SelectDecisionProvider(j.deck))
        {
            std::cerr << " provider_detected=" << det.Name() << " (pinned via MTG_PROVIDER_DECK)";
        }
        std::cerr << "\n";
    }

    j.second_main = GoldFishRunner::DeckUsesSecondMain(j.deck);
    return j;
}
} // namespace

std::vector<BatchJobResult> BatchRunner::RunManifest(
    const std::filesystem::path& manifest_path, int num_threads,
    const JobDoneCallback& on_job_done, const std::filesystem::path& trace_dir)
{
    if (!trace_dir.empty()) { std::filesystem::create_directories(trace_dir); }
    std::ifstream in(manifest_path);
    if (!in) { throw std::runtime_error("cannot open manifest: " + manifest_path.string()); }
    json manifest = json::parse(in);
    if (!manifest.contains("jobs") || !manifest["jobs"].is_array())
    {
        throw std::runtime_error("manifest must contain a \"jobs\" array");
    }

    // Shared LRU cache of loaded profiles (see ProfileCache). Used both here at parse (to resolve each
    // job's settings/vial without retaining the profile) and by the workers below (to build each AIEngine).
    ProfileCache profile_cache(ProfileCacheCap());

    std::vector<Job> jobs;
    jobs.reserve(manifest["jobs"].size());
    for (const json& jspec : manifest["jobs"]) { jobs.push_back(ParseJob(jspec, profile_cache)); }

    // Manifest-level tractability guard (see CondemnRule). Absent => disabled => nothing is skipped.
    CondemnRule condemn;
    if (manifest.contains("condemn") && manifest["condemn"].is_object())
    {
        const json& c = manifest["condemn"];
        condemn.sec_per_game        = c.value("sec_per_game", 0.0);
        condemn.reference_games     = c.value("reference_games", 0);
        condemn.never_condemn_depth = c.value("never_condemn_depth", 0);
        condemn.max_game_sec        = c.value("max_game_sec", 0.0);
        condemn.drip                = std::max(1, c.value("drip", 1));
        condemn.enabled             = (condemn.sec_per_game > 0.0 && condemn.reference_games > 0)
                                   || condemn.max_game_sec > 0.0;
    }

    // Dense cell indices, so the per-game hot path indexes a vector instead of hashing a string.
    // A job with no "cell" is its own cell -- cross-job condemnation then cannot trigger for it,
    // which is the right default for every manifest that is not a matrix.
    int n_cells = 0;
    std::vector<std::string> cell_name;
    std::vector<int>         cell_depth;   // for the never-condemn floor, off the first job of the cell
    // ROW indices for the mean rule (see Job::row). A job with no "row" judges on its cell key, so a
    // manifest that never says "row" behaves exactly as before -- per-cell judgment, or per-job when
    // it says neither.
    int n_rows = 0;
    std::vector<std::string> row_name;
    std::vector<int>         row_depth;    // never-condemn floor for the row rule; min over its jobs,
                                           // so a mixed-depth row is protected by its shallowest member
    std::vector<char>        row_is_cell;  // fallback row (no "row" in the manifest): report it with
                                           // the old cell= wording so pre-row log parsers still match
    std::vector<int>         cell_row;     // cell_id -> row_id, for the in-flight hook's aggregation
    {
        std::unordered_map<std::string, int> cell_ix;
        std::unordered_map<std::string, int> row_ix;
        for (Job& j : jobs)
        {
            const std::string key = j.cell.empty() ? ("\x1f" + j.name) : j.cell;
            auto it = cell_ix.find(key);
            if (it == cell_ix.end())
            {
                it = cell_ix.emplace(key, n_cells++).first;
                cell_name.push_back(j.cell.empty() ? j.name : j.cell);
                cell_depth.push_back(j.depth);
                cell_row.push_back(-1);   // filled below, once the job's row is resolved
            }
            j.cell_id = it->second;

            const std::string rkey = j.row.empty() ? key : j.row;
            auto rt = row_ix.find(rkey);
            if (rt == row_ix.end())
            {
                rt = row_ix.emplace(rkey, n_rows++).first;
                row_name.push_back(j.row.empty() ? cell_name[static_cast<std::size_t>(j.cell_id)]
                                                 : j.row);
                row_depth.push_back(j.depth);
                row_is_cell.push_back(j.row.empty() ? 1 : 0);
            }
            j.row_id = rt->second;
            row_depth[static_cast<std::size_t>(j.row_id)] =
                std::min(row_depth[static_cast<std::size_t>(j.row_id)], j.depth);
            cell_row[static_cast<std::size_t>(j.cell_id)] = j.row_id;
        }
    }
    // games/ms accumulate as the pool runs, keyed on the ROW (the mean rule's judgment unit);
    // `condemned` latches once and is only ever set true. Cells keep their own latch because
    // max_game_sec still condemns per cell, and a skip must honour either verdict.
    std::vector<std::atomic<int>>       row_games(static_cast<std::size_t>(n_rows));
    std::vector<std::atomic<long long>> row_ms(static_cast<std::size_t>(n_rows));
    std::vector<std::atomic<int>>       row_condemned(static_cast<std::size_t>(n_rows));
    std::vector<std::atomic<int>>       cell_condemned(static_cast<std::size_t>(n_cells));
    for (int i = 0; i < n_rows; ++i)
    { row_games[i].store(0); row_ms[i].store(0); row_condemned[i].store(0); }
    for (int i = 0; i < n_cells; ++i) { cell_condemned[i].store(0); }

    // Per-job, per-game results. Pre-sized so workers write to disjoint slots with
    // no synchronisation. games[j][gi] = win turn (<=0 means no win).
    std::vector<std::vector<int>> win_turns(jobs.size());
    std::vector<std::vector<uint64_t>> digests(jobs.size());
    // Per-job core-milliseconds (see BatchJobResult::elapsed_ms). Relaxed adds: the value is read
    // only after the job's last game lands, and that read is already ordered by the acq_rel
    // remaining-counter decrement below.
    std::vector<std::atomic<long long>> job_ms(jobs.size());
    for (std::atomic<long long>& a : job_ms) { a.store(0, std::memory_order_relaxed); }
    for (std::size_t j = 0; j < jobs.size(); ++j)
    {
        win_turns[j].assign(jobs[j].games, -1);
        digests[j].assign(jobs[j].games, 0);
    }

    // Flatten every game of every job into one work list.
    std::vector<WorkItem> items;
    {
        std::size_t total = 0;
        for (const Job& j : jobs) { total += static_cast<std::size_t>(std::max(0, j.games)); }
        items.reserve(total);
        for (int j = 0; j < static_cast<int>(jobs.size()); ++j)
        {
            for (int gi = 0; gi < jobs[j].games; ++gi) { items.push_back({j, gi}); }
        }
    }

    // LPT scheduling: run the most expensive games first so cheap games backfill
    // the tail. An explicit per-job `weight` wins first (for jobs whose true cost the
    // depth/budget proxy misjudges -- e.g. Hinata's combo search measured ~40x the other
    // decks at the SAME depth, with a heavy multi-minute tail, so without a weight its d3
    // games sort behind every deck's d5 and become the long tail); then depth, then budget
    // as the proxy. Then, within a cost tier, group by profile PATH so all games sharing a profile run
    // consecutively -- this keeps the ProfileCache's resident set to ~1 profile at a time (a big profile is
    // ~167 MB) so the small cap loads each distinct profile essentially once with no reload thrash. Job
    // index is the final tiebreak (keeps a single job's games contiguous). Reordering is
    // lossless -- each game is seeded by its job's seed + game index, so order cannot
    // change any result -- and a stable sort keeps a job's games contiguous, which
    // maximises the worker's per-job engine reuse below.
    std::stable_sort(items.begin(), items.end(),
        [&](const WorkItem& a, const WorkItem& b)
        {
            const Job& ja = jobs[a.job];
            const Job& jb = jobs[b.job];
            if (ja.sched_weight != jb.sched_weight) { return ja.sched_weight > jb.sched_weight; }
            if (ja.depth     != jb.depth)     { return ja.depth     > jb.depth; }
            if (ja.budget_ms != jb.budget_ms) { return ja.budget_ms > jb.budget_ms; }
            if (ja.profile_path != jb.profile_path) { return ja.profile_path < jb.profile_path; }
            return a.job < b.job;
        });

    // WORK POOL. Every game -- protected and condemnable alike -- sits in the ONE static list in
    // weight order, and the drip is an in-flight CAP enforced at Take, not a separate queue
    // (docs/design/batch-drip-release.md). An item whose NOT-YET-JUDGED condemnable cell already has
    // `drip` games running is PARKED and retried when one of that cell's games lands; an item whose
    // cell has been judged -- condemned (drains as a cheap skip) or proven tractable (its row reached
    // `reference_games` uncondemned) -- is simply taken in its static-order turn.
    //
    // Why the cap and not a sort: a condemnable cell's whole point is that most of its work may be
    // thrown away, so dispatching it by cost (LPT puts it first -- it is the most expensive thing in
    // the queue) puts every thread on work that is about to be discarded, and the verdict cannot
    // arrive until those games finish. Metering caps how much of the box a not-yet-judged cell can
    // occupy, while the protected cells (H5 and friends, which must run in full regardless) keep the
    // rest of the threads busy.
    //
    // Why not the old front/pending split (a high-priority deque fed `drip` games per cell): two
    // measured defects. Metering NEVER ENDED -- a cell judged tractable stayed metered for its whole
    // 400-game target, and because `front` outranked ALL static work, 16 proven-tractable V6-V8
    // cells held 16 of 24 threads ahead of the protected d<=5 ladder the trust decision actually
    // reads (FiveColour, 2026-08-12). And a run whose static work finished first ended with
    // `threads - cells*drip` workers idle. Under the cap both disappear: a judged cell simply stops
    // being skipped, in the weight order the manifest already carries, and nothing is held back at
    // the end.
    struct Pool
    {
        std::mutex                        mtx;
        std::condition_variable           cv;
        const std::vector<WorkItem>*      items = nullptr;
        std::size_t                       cursor = 0;
        // Deferred condemnable work, keyed by ORIGINAL list index so it re-enters in static (weight)
        // order. Every deferred item was popped from an index below `cursor`, so draining `ready`
        // before `items[cursor]` preserves the global order exactly. A std::map (ordered, begin() =
        // lowest index) is plenty: it only ever holds capped cells' games, and pool operations run
        // once per multi-second game.
        std::map<std::size_t, WorkItem>                          ready;
        std::vector<std::deque<std::pair<std::size_t, WorkItem>>> parked;   // per cell, with index
        std::vector<int>                  inflight;           // condemnable games running, per cell
        long long                         outstanding = 0;    // condemnable games not yet finalised
        int                               drip = 1;
        // Verdict for a condemnable item, evaluated under the pool lock at Take time (it reads the
        // judgment atomics, which move under the workers and the heartbeat).
        enum class Verdict { kProtected, kCondemnable, kCapped };
        std::function<Verdict(const WorkItem&)> classify;

        bool Take(WorkItem& out)
        {
            std::unique_lock<std::mutex> lk(mtx);
            for (;;)
            {
                while (!ready.empty() || cursor < items->size())
                {
                    const bool  from_ready = !ready.empty();
                    std::size_t idx;
                    WorkItem    wi;
                    if (from_ready) { idx = ready.begin()->first; wi = ready.begin()->second; }
                    else            { idx = cursor;               wi = (*items)[cursor]; }
                    const Verdict v = classify(wi);
                    if (from_ready) { ready.erase(ready.begin()); } else { ++cursor; }
                    if (v == Verdict::kCapped)
                    {
                        parked[cell_of(wi)].push_back({idx, wi});
                        continue;
                    }
                    if (v == Verdict::kCondemnable) { ++inflight[cell_of(wi)]; }
                    out = wi;
                    return true;
                }
                if (outstanding <= 0) { return false; }
                // Everything left is parked behind a drip cap: wait for one of those cells' games
                // to land rather than exiting, or the run would end with games unplayed. Liveness
                // holds because a cell only ever parks work while it has >= 1 game in flight, and
                // that game's OnFinished promotes the parked items.
                cv.wait(lk);
            }
        }

        // One condemnable game finalised (played or skipped). Frees the cell's in-flight slot and
        // promotes its parked items for re-evaluation -- they may now be under the cap, their row
        // may have crossed reference_games (released for good), or a condemnation may have landed
        // (they drain as skips). Promotion is wholesale rather than one-at-a-time: Take re-parks
        // whatever is still capped, and the churn is bounded by the cell's own game count, once per
        // finished game -- microseconds against multi-second games.
        void OnFinished(int cell_id)
        {
            std::lock_guard<std::mutex> lk(mtx);
            --outstanding;
            --inflight[static_cast<std::size_t>(cell_id)];
            auto& q = parked[static_cast<std::size_t>(cell_id)];
            while (!q.empty()) { ready.emplace(q.front().first, q.front().second); q.pop_front(); }
            cv.notify_all();
        }

        std::function<int(const WorkItem&)> cell_of;   // bound below, once jobs is in scope
    };
    Pool pool;
    pool.items = &items;
    pool.drip  = condemn.drip;
    pool.parked.resize(static_cast<std::size_t>(std::max(1, n_cells)));
    pool.inflight.assign(static_cast<std::size_t>(std::max(1, n_cells)), 0);
    {
        long long n_condemnable = 0;
        for (const WorkItem& wi : items)
        {
            const Job& j = jobs[wi.job];
            if (condemn.enabled && j.depth > condemn.never_condemn_depth) { ++n_condemnable; }
        }
        pool.outstanding = n_condemnable;
        if (pool.outstanding > 0)
        {
            std::cerr << "[batch] metering " << pool.outstanding << " games of condemnable cells at "
                      << condemn.drip << " in flight per cell until judged; "
                      << (items.size() - static_cast<std::size_t>(n_condemnable))
                      << " protected games run alongside\n";
        }
    }
    // The mean rule is active iff both its parameters are set; without it a cell is never "proven
    // tractable" (row_games does not accrue), so metering persists for the whole run -- exactly the
    // old drip-forever semantics, which is right when only max_game_sec is guarding.
    const bool mean_rule_on = condemn.sec_per_game > 0.0 && condemn.reference_games > 0;
    pool.classify = [&](const WorkItem& wi) -> Pool::Verdict
    {
        const Job& j = jobs[wi.job];
        if (!condemn.enabled || j.depth <= condemn.never_condemn_depth)
        { return Pool::Verdict::kProtected; }
        // Condemned (row by the mean rule, cell by max_game_sec): take it -- it drains as a skip.
        if (row_condemned[j.row_id].load(std::memory_order_relaxed) != 0
            || cell_condemned[j.cell_id].load(std::memory_order_relaxed) != 0)
        { return Pool::Verdict::kCondemnable; }
        // Judged tractable: the row reached its reference sample uncondemned. Released from
        // metering -- from here it runs in plain static order.
        if (mean_rule_on
            && row_games[j.row_id].load(std::memory_order_relaxed) >= condemn.reference_games)
        { return Pool::Verdict::kCondemnable; }
        // Unjudged: metered at `drip` in flight per cell.
        return pool.inflight[static_cast<std::size_t>(j.cell_id)] < pool.drip
             ? Pool::Verdict::kCondemnable : Pool::Verdict::kCapped;
    };
    pool.cell_of = [&](const WorkItem& wi) { return jobs[wi.job].cell_id; };

    const long long s_slow_game_ms = SlowGameMs();
    const bool      s_dump_wins    = EnvOn("MTG_DUMP_WINS");   // see the emission in the worker

    int requested = num_threads;
    num_threads = concurrency_util::ResolveWorkerThreads(num_threads);
    num_threads = std::min<int>(num_threads, std::max<std::size_t>(1, items.size()));
    concurrency_util::LogWorkerThreads(std::cerr, "batch", requested, num_threads);

    // Reduce one job's per-game win turns to its aggregate (average over WINS only --
    // matches GoldFishRunner). Used both for the streamed per-job callback and the
    // final returned vector.
    auto reduce_job = [&](std::size_t j) -> BatchJobResult
    {
        BatchJobResult r;
        r.name         = jobs[j].name;
        // A CONDEMNED cell's remaining games are skipped, so a job can finish short. Skipped slots
        // carry kSkipped and are dropped here rather than folded in: ComputeAvgTurns scores anything
        // <=0 as a LOSS, so leaving them in would report a condemned cell as a pile of losses --
        // a silently wrong number, which is worse than a short one.
        std::vector<int>      wt_played;
        std::vector<uint64_t> dg_played;
        wt_played.reserve(win_turns[j].size());
        dg_played.reserve(digests[j].size());
        for (std::size_t i = 0; i < win_turns[j].size(); ++i)
        {
            if (win_turns[j][i] == kSkipped) { continue; }
            wt_played.push_back(win_turns[j][i]);
            dg_played.push_back(digests[j][i]);
        }
        r.games_played = static_cast<int>(wt_played.size());
        r.win_turns    = wt_played;
        r.digests      = dg_played;
        long long sum = 0;
        for (int wt : wt_played) { if (wt > 0) { ++r.games_won; sum += wt; } }
        if (r.games_won > 0) { r.average_win_turn = static_cast<double>(sum) / r.games_won; }
        r.avg_turns = wt_played.empty() ? 0.0 : ComputeAvgTurns(wt_played, jobs[j].max_turns);
        // Case digest: FNV-1a fold of the per-game digests in game (gi) order -- a single
        // fingerprint of the whole case's play. Games in gi order (the vector index), so it is
        // deterministic and order-stable regardless of the pool's execution interleave.
        uint64_t cd = 1469598103934665603ULL;
        for (uint64_t d : dg_played)
        {
            for (int b = 0; b < 8; ++b) { cd ^= static_cast<uint8_t>((d >> (b * 8)) & 0xff); cd *= 1099511628211ULL; }
        }
        r.case_digest = cd;
        r.elapsed_ms  = job_ms[j].load(std::memory_order_relaxed);
        return r;
    };

    // Per-job remaining-game counters drive the streaming callback: the worker that
    // finishes a job's LAST game reduces it and fires on_job_done. acq_rel on the
    // decrement makes that worker observe every other thread's win_turns writes for
    // the job (disjoint slots + this release/acquire chain). A mutex serialises the
    // callback so streamed output never interleaves. Only armed when streaming.
    std::vector<std::atomic<int>> remaining(jobs.size());
    for (std::size_t j = 0; j < jobs.size(); ++j)
    {
        remaining[j].store(jobs[j].games, std::memory_order_relaxed);
    }
    std::mutex cb_mtx;

    // Slowest-game heartbeat (see Heartbeat). One running-slot per worker; inert unless
    // MTG_BATCH_HEARTBEAT names a file.
    Heartbeat hb(static_cast<std::size_t>(num_threads), 100);
    // IN-FLIGHT condemnation. A cell's mean cost only sees FINISHED games, so a cell whose sample
    // contains one catastrophic game reads as free and the pool keeps feeding it. A single game
    // already over the per-game limit is proof enough on its own: condemn the cell so its remaining
    // games are skipped. The game in flight still runs to completion (there is no safe way to abort
    // a search mid-node) -- this stops the OTHER nineteen from being dispatched behind it.
    if (condemn.enabled)
    {
        hb.SetInFlightHook([&](const Heartbeat::InFlight& live) {
            auto condemn_cell = [&](int cid, const char* why, double v, double limit) {
                if (cid < 0 || cid >= n_cells) { return; }
                if (cell_depth[cid] <= condemn.never_condemn_depth) { return; }
                if (cell_condemned[cid].exchange(1, std::memory_order_relaxed) != 0) { return; }
                std::fprintf(stderr,
                    "[goldfish] CONDEMNED cell=%s %s %.1f s (limit %.1f); remaining games of this "
                    "cell are skipped\n", cell_name[cid].c_str(), why, v, limit);
            };
            auto condemn_row = [&](int rid, const char* why, double v, double limit) {
                if (rid < 0 || rid >= n_rows) { return; }
                if (row_depth[static_cast<std::size_t>(rid)] <= condemn.never_condemn_depth) { return; }
                if (row_condemned[rid].exchange(1, std::memory_order_relaxed) != 0) { return; }
                const bool fb = row_is_cell[static_cast<std::size_t>(rid)] != 0;
                std::fprintf(stderr,
                    "[goldfish] CONDEMNED %s=%s %s %.1f s/game (limit %.1f); remaining games of "
                    "%s are skipped\n", fb ? "cell" : "row",
                    row_name[static_cast<std::size_t>(rid)].c_str(), why, v, limit,
                    fb ? "this cell" : "every cell of this row");
            };

            // (1) Single pathological game, judged while it runs. Deliberately still per CELL: one
            // game is one observation, and widening its blast radius to the row would remove four
            // seeds on it instead of one (the abandon-the-game rework is deferred separately --
            // docs/design/condemnation-row-average.md, "Also worth fixing").
            if (condemn.max_game_sec > 0.0)
            {
                for (const auto& [cid, ms] : live)
                {
                    if (static_cast<double>(ms) > condemn.max_game_sec * 1000.0)
                    { condemn_cell(cid, "on an IN-FLIGHT game at", static_cast<double>(ms) / 1000.0,
                                   condemn.max_game_sec); }
                }
            }

            // (2) The MEAN rule, with in-flight games folded in -- aggregated on the ROW, same unit
            // as the finished-game rule. A running game's elapsed time is a LOWER BOUND on its final
            // cost, so this mean can only UNDERSTATE the row -- if it already exceeds the limit, the
            // finished-only mean must exceed it too. That makes condemning here sound (no false
            // positive is possible from the partial term) while catching the case the finished-only
            // rule is blind to: a row whose expensive games are all still running reads as free
            // until the first one lands, which on an explosive deck is hours.
            if (condemn.sec_per_game > 0.0 && condemn.reference_games > 0)
            {
                std::unordered_map<int, std::pair<int, long long>> agg;   // row -> (games, ms)
                for (const auto& [cid, ms] : live)
                {
                    if (cid < 0 || cid >= n_cells) { continue; }
                    auto& a = agg[cell_row[static_cast<std::size_t>(cid)]];
                    ++a.first; a.second += ms;
                }
                for (const auto& [rid, a] : agg)
                {
                    if (rid < 0 || rid >= n_rows) { continue; }
                    const int       n   = row_games[rid].load(std::memory_order_relaxed) + a.first;
                    const long long tot = row_ms[rid].load(std::memory_order_relaxed) + a.second;
                    if (n >= condemn.reference_games
                        && static_cast<double>(tot) / n > condemn.sec_per_game * 1000.0)
                    { condemn_row(rid, "at a running mean (in-flight included) of",
                                  static_cast<double>(tot) / n / 1000.0, condemn.sec_per_game); }
                }
            }
        });
    }
    hb.Start();

    std::atomic<std::size_t> cursor{0};
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([&, slot = static_cast<std::size_t>(t)]()
        {
            // Reuse one engine across consecutive games of the same job (the common
            // case after the stable sort). Rebuild only on a job change. This is the
            // same per-thread engine lifetime GoldFishRunner uses, so per-game
            // results are byte-identical.
            int                        cached_job = -1;
            std::optional<AIEngine>    ai;
            std::optional<GameEngine>  engine;

            for (;;)
            {
                WorkItem wi;
                if (!pool.Take(wi)) { break; }
                const Job& job = jobs[wi.job];
                const bool condemnable = condemn.enabled && job.depth > condemn.never_condemn_depth;

                // CONDEMNED row or cell: drop this game rather than run it. Checked as the item
                // comes off the queue (not at a barrier), so a cell stops consuming cores the moment
                // it is ruled intractable and every other cell keeps running undisturbed. Either
                // verdict skips: the mean rule condemns the ROW, max_game_sec condemns the CELL.
                // `condemnable` (job.depth above the floor) gates the skip so a protected d<=5 job
                // can never be skipped through a shared row/cell key -- the floor is structural, not
                // just a property of who sets the latch.
                if (condemn.enabled && condemnable
                    && (row_condemned[jobs[wi.job].row_id].load(std::memory_order_relaxed) != 0
                        || cell_condemned[jobs[wi.job].cell_id].load(std::memory_order_relaxed) != 0))
                {
                    win_turns[wi.job][wi.game] = kSkipped;
                    // Finalise in the pool as well: a skipped game is still a condemnable game
                    // accounted for, and if `outstanding` never drains the workers wait forever.
                    pool.OnFinished(job.cell_id);
                    if (on_job_done && remaining[wi.job].fetch_sub(1, std::memory_order_acq_rel) == 1)
                    {
                        BatchJobResult r = reduce_job(wi.job);
                        std::lock_guard<std::mutex> lk(cb_mtx);
                        on_job_done(r);
                    }
                    continue;
                }

                if (wi.job != cached_job)
                {
                    // The job's VALUE-LEAF ARM must be installed BEFORE the engine is built: the
                    // profile load resolves the sidecar off it, and the search reads it per node.
                    // Set unconditionally (an omitted block resets to "use env"), so a previous
                    // job's arm can never leak into this one through the reused worker thread.
                    valuearm::t_arm = jobs[wi.job].arm;
                    // Same lifetime rule as the arm: set unconditionally (empty => nullptr => the env
                    // default) so a previous job's numbering cannot leak through the reused thread.
                    decknumbering::t_map =
                        jobs[wi.job].numbering.empty() ? nullptr : &jobs[wi.job].numbering;
                    // Load (or hit) the job's profile through the shared cache and hand a copy to the
                    // AIEngine (which owns its own copy). The shared handle is released at the end of this
                    // block; only the AIEngine's copy persists across the job's games.
                    std::shared_ptr<const MulliganProfile> prof =
                        profile_cache.get(job.profile_path, job.arm.value_profile);
                    ai.emplace(*prof, job.depth, job.budget_ms);
                    ai->SetSearchPostCombat(job.second_main);
                    engine.emplace(*ai);
                    cached_job = wi.job;
                }

                // Global game number = the job's chunk offset + the local index. The shuffle seed
                // stays seed+local (a chunk's "seed" already carries the offset), but the spawn
                // schedule and the logged index are GLOBAL, so a chunk reproduces the corresponding
                // `--seed (base+off) --game-index off` single run exactly. game_index is 0 for a
                // whole-run job, which is every pre-existing manifest.
                const int global_gi = job.game_index + wi.game;
                GameState state = GoldFishRunner::SetupGame(
                    job.deck, job.seed + static_cast<uint64_t>(wi.game));
                state.vial_target_mv = job.vial_target_mv;
                GoldFishRunner::PopulateOpponentSpawns(state, global_gi);

                // Attach a logger for the play fingerprint. Default: DIGEST-ONLY (no structure
                // built, no disk -- cheap). With trace_dir set: a FULL logger that also builds
                // the structural trace and writes it to disk (used to inspect an unpruned run's
                // better line without re-running -- esp. valuable for the pathological deep-tail
                // games you never want to recompute). Fold* runs before the digest_only early-out
                // in every log method, so the digest is identical either way -> win turns and
                // digests are byte-identical whether or not tracing is on. Reset per game via
                // StartGame. Records only the real game's decisions (m_logger is nulled in the
                // search rollouts), so it does not perturb play.
                hb.SetRunning(slot, job.name, global_gi, job.cell_id);
                const bool trace = !trace_dir.empty();
                GameLogger dlog(/*digest_only=*/!trace);
                dlog.StartGame(std::string(), global_gi, job.name, job.seed + wi.game, {});
                engine->SetLogger(&dlog);
                const auto g_t0 = std::chrono::steady_clock::now();
                win_turns[wi.job][wi.game] = engine->RunGame(state, job.max_turns);
                const long long g_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - g_t0).count();
                job_ms[wi.job].fetch_add(g_ms, std::memory_order_relaxed);
                hb.Finished(job.name, global_gi, g_ms);
                hb.ClearRunning(slot);

                // Continuous tractability guard. Evaluated after every game rather than at a wave
                // barrier: the point of the pool is that no cell ever waits on another, and a
                // barrier reintroduces exactly the synchronisation the pool exists to remove.
                //
                // The MEAN accumulates and judges on the ROW (deck+arm+depth), not the cell:
                // tractability is a property of the depth, and judging each seed's cell separately
                // let one outlier game remove a single seed from a row, making rows non-comparable
                // (docs/design/condemnation-row-average.md). With no "row" in the manifest the row
                // IS the cell and this is the old per-cell rule verbatim.
                if (condemn.sec_per_game > 0.0 && condemn.reference_games > 0
                    && job.depth > condemn.never_condemn_depth)
                {
                    const int rid = job.row_id;
                    const int   n  = row_games[rid].fetch_add(1, std::memory_order_relaxed) + 1;
                    const long long tot = row_ms[rid].fetch_add(g_ms, std::memory_order_relaxed) + g_ms;
                    if (n >= condemn.reference_games
                        && static_cast<double>(tot) / n > condemn.sec_per_game * 1000.0
                        && row_condemned[rid].exchange(1, std::memory_order_relaxed) == 0)
                    {
                        const bool fb = row_is_cell[static_cast<std::size_t>(rid)] != 0;
                        std::fprintf(stderr,
                            "[goldfish] CONDEMNED %s=%s after %d games at %.1f s/game "
                            "(limit %.1f); remaining games of %s are skipped\n",
                            fb ? "cell" : "row",
                            row_name[static_cast<std::size_t>(rid)].c_str(),
                            n, static_cast<double>(tot) / n / 1000.0, condemn.sec_per_game,
                            fb ? "this cell" : "every cell of this row");
                    }
                }

                // SLOW-GAME capture. GoldFishRunner emits this from its own game loop, which the
                // batch path does not use -- so before this, `--batch` reported no slow games at
                // all, however pathological the deck. That matters because an expensive deck's cost
                // is concentrated in a handful of games and the repro list is the whole input to an
                // optimization pass; a chunked generator moving onto `--batch` would have lost it
                // silently. Carries job= (the chunk) so a line is attributable to a matrix cell,
                // which the bare GoldFishRunner form is not once many cells share a process.
                // The replay seed is base_seed + LOCAL index (that is what SetupGame shuffled on)
                // while --game-index is the GLOBAL one (what the spawn schedule used).
                if (s_slow_game_ms > 0 && g_ms >= s_slow_game_ms)
                {
                    std::fprintf(stderr,
                        "[goldfish] SLOW-GAME %lldms  job=%s gi=%d wt=%d  repro: --seed %llu "
                        "--game-index %d --games 1\n",
                        g_ms, job.name.c_str(), global_gi, win_turns[wi.job][wi.game],
                        static_cast<unsigned long long>(job.seed + static_cast<uint64_t>(wi.game)),
                        global_gi);
                    std::fflush(stderr);
                }
                // MTG_DUMP_WINS, for the same reason and with the same defect: it lives in
                // GoldFishRunner's game loop, which `--batch` does not use, so the per-game A/B diff
                // its own comment advertises produced NOTHING under the batch path -- an empty diff
                // reads as "no game changed". Same class as the MTG_PROFILE gap (4eb2f04). Carries
                // job= because many chunks share one process, and the repro seed is the LOCAL one
                // (what SetupGame shuffled on), matching the SLOW-GAME line above.
                if (s_dump_wins)
                {
                    std::fprintf(stderr, "[win] job=%s gi=%d wt=%d\n",
                                 job.name.c_str(), global_gi, win_turns[wi.job][wi.game]);
                }
                engine->SetLogger(nullptr);
                digests[wi.job][wi.game] = dlog.Digest();
                if (trace)
                {
                    dlog.EndGame(win_turns[wi.job][wi.game]);
                    dlog.WriteToFile(trace_dir / (job.name + "_gi" + std::to_string(global_gi) + ".json"));
                }

                // Finalise in the pool: frees this cell's in-flight slot and promotes its parked
                // games, which Take re-classifies -- released outright if the row has now been
                // judged (tractable or condemned), metered back in otherwise. Sibling cells of a
                // condemned row drain the same way as their own in-flight games land and their
                // parked work re-enters as cheap dequeue-time skips.
                if (condemnable) { pool.OnFinished(job.cell_id); }

                // Stream this job the instant its last game lands.
                if (on_job_done
                    && remaining[wi.job].fetch_sub(1, std::memory_order_acq_rel) == 1)
                {
                    BatchJobResult r = reduce_job(wi.job);
                    std::lock_guard<std::mutex> lk(cb_mtx);
                    on_job_done(r);
                }
            }
            // Fold this worker's thread_local counters into the global aggregate, exactly as the
            // single-run path does (GoldFishRunner.cpp). The hot path is deliberately lock-free
            // thread_local storage, so a worker that never flushes contributes NOTHING -- and since
            // every game runs on a worker, an unflushed batch reports all-zero counters rather than
            // no counters, which looks like a measured result. No-op unless MTG_PROFILE is defined.
            PROF_FLUSH_THREAD();
        });
    }
    for (std::thread& th : threads) { th.join(); }
    hb.Stop();

    // Final per-job aggregate, in manifest order (regardless of streaming).
    std::vector<BatchJobResult> results(jobs.size());
    for (std::size_t j = 0; j < jobs.size(); ++j) { results[j] = reduce_job(j); }
    return results;
}
