#include "BatchRunner.h"
#include "GoldFishRunner.h"
#include "../core/GameEngine.h"
#include "../ai/GameWorkMeter.h"
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
    // Per-GAME work ceiling in search units (see ai/GameWorkMeter.h). 0 = disarmed = byte-identical
    // to a manifest that omits it. A game that hits it is VOIDED -- recorded kSkipped, dropped from
    // the job's average, and reported on stderr so the caller can build a skip list.
    long long       abandon_units       = 0;
    // ...or, instead of an absolute number, a ceiling RELATIVE to this cell's own cost: k x the
    // median of the cell's first `abandon_calib` games. An absolute ceiling cannot serve a matrix
    // whose cells span 11 ms to 700 s per game -- measured on burn, six cells alone spanned 150x in
    // median units (V1 50, H3 7,506) -- so one number is either inert at the top of the ladder or
    // shreds the bottom of it.
    //
    // THE CALIBRATION SAMPLE IS A FIXED SET OF GAMES, NOT A RUNNING MEDIAN. A running median depends
    // on which games happen to have finished when the next one starts, i.e. on thread interleave, so
    // the abandoned set would differ run to run and machine to machine -- destroying the one property
    // the unit currency was chosen for (ai/GameWorkMeter.h). Instead the sample is exactly the games
    // with GLOBAL index < abandon_calib; they run with no relative ceiling, everything above that
    // index is HELD BACK until the cell's median is known, and the resulting ceiling is FROZEN and
    // reported on stderr so the driver can store it and hand it straight back on the next resume
    // (where those calibration games no longer appear). A cell with no calibration games in the
    // manifest and no absolute ceiling therefore runs unbounded -- the pre-existing behaviour.
    //
    // abandon_units still applies DURING calibration, so a caller can put a generous absolute safety
    // cap under the calibration window without letting it decide the ceiling.
    double          abandon_k           = 0.0;   // 0 = no relative ceiling
    int             abandon_calib       = 0;     // games forming the sample (by GLOBAL index)
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
    // under study (docs/design/condemnation-row-average.md). The MEDIAN rule therefore accumulates and
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
    // Condemn a row whose MEDIAN cost per game exceeds this. The question the matrix actually needs
    // answered is "can we escalate to this depth SOME of the time, at a cost that is not insane?" --
    // because that is the only situation the derived policy is ever applied in. Shipped play is
    // budgeted, so it never processes a pathological position at depth anyway; a cell is therefore
    // usable if a typical game is affordable, however ugly its tail, and unusable only when the
    // majority of its games are impractical (which is precisely "the median exceeds the limit").
    //
    // WHY NOT THE MEAN, which this replaced. Two reasons, and the second one is fatal:
    //   * it answers the wrong question. A mean conflates "many moderately slow games" (condemn --
    //     there is nothing to salvage) with "a few catastrophic ones" (abandon those and keep the
    //     cell), which call for opposite responses. One 32-minute game put V6/V7/V8@8008 1-3% over a
    //     60 s/game limit and condemned three healthy cells whose other 49 games ran ~19.5 s/game.
    //   * with the per-game ceiling armed it is not even well defined. A ceiling'd game contributes
    //     its TRUNCATED cost, so a cell with 10% abandoned games reads 0.9m + 0.1*(k*m) -- at k=25
    //     that is 3.4x the median with 74% of it contributed by games we discard, and at k=10 it is
    //     1.9x. The verdict would follow an arbitrary constant. A median does not move at all.
    double median_sec_per_game = 0.0;
    int    reference_games     = 0;     // ...but only after it has this many games (the sample)
    int    never_condemn_depth = 0;     // ...and never at or below this depth (the d<=5 ladder IS
                                        //    the crossover; condemning there leaves a HOLE, not a saving)
    // SEPARATE limit for a SINGLE game, applied to games still running (see the in-flight hook).
    // Deliberately not sec_per_game: these measure different things, and sharing the number would
    // condemn a cell whose typical game is 5 s because one game took 61 s. The median rule asks "can
    // we escalate to this depth some of the time, affordably?"; this one asks "is this individual
    // game pathological?" -- which a per-row statistic cannot answer at all until the game finishes,
    // and a 21.4-hour game answers far too late.
    //
    // A LIVENESS BACKSTOP ONLY, and it yields: a cell whose per-game WORK ceiling is armed
    // (ai/GameWorkMeter.h) is never condemned by this rule, because that game is already bounded and
    // by something DETERMINISTIC. See the in-flight hook, rule (1).
    // 0 / absent => the in-flight rule is off.
    double max_game_sec        = 0.0;
    // Path the DRIVER may write row names into, one per line, to condemn them mid-run. The pool
    // re-reads it on every heartbeat tick.
    //
    // This exists for the one verdict the engine cannot reach on its own: QUALITY. "Does depth d buy
    // anything over d-1?" is a paired comparison against games this process may never have seen --
    // on a resume the earlier ones live only in the driver's state file -- so the driver owns the
    // test and needs a way to say so before the run ends. Without a channel the rule could only fire
    // at the NEXT resume, which for a single long phase-C invocation means never, i.e. it would save
    // nothing on the run that is actually paying for the dead rung.
    //
    // Empty => no polling, no file, byte-identical.
    std::string control_file;
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
    // whole set, not one per game, because the row rule needs to aggregate per cell before judging.
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
    j.abandon_units       = jspec.value("abandon_units", 0LL);   // per-game ceiling; see Job::abandon_units
    j.abandon_k           = jspec.value("abandon_k", 0.0);       // ...or k x the cell's own median
    j.abandon_calib       = jspec.value("abandon_calib", 0);     //    over its first N games

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
    // PATH-TO-TRUST arm (see ValueArm.h): both are process-wide statics, so carrying them per job is
    // what lets ONE pooled batch run the A/B instead of one invocation per arm.
    if (jspec.contains("esc_to_trust")) { j.arm.esc_to_trust = jspec["esc_to_trust"].get<bool>() ? 1 : 0; }
    if (jspec.contains("trust_slack"))  { j.arm.trust_slack  = jspec["trust_slack"].get<double>(); }
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
        // REFUSED, not silently reinterpreted. `sec_per_game` was a MEAN limit; the rule is now a
        // MEDIAN one (see CondemnRule), and the same number means something different under it --
        // the mean carried tail inflation the median does not, so a limit sized for one is wrong for
        // the other. Accepting the old key would hand back a table condemned on a threshold nobody
        // chose. Same convention as valueleaf.sh's RETIRED_KNOBS: a retired setting errors.
        if (c.contains("sec_per_game"))
        {
            throw std::runtime_error(
                "manifest condemn.sec_per_game is RETIRED: the tractability rule now judges the "
                "MEDIAN cost per game, not the mean, so the limit means something different and "
                "must be re-chosen. Use \"median_sec_per_game\".");
        }
        condemn.median_sec_per_game = c.value("median_sec_per_game", 0.0);
        condemn.reference_games     = c.value("reference_games", 0);
        condemn.never_condemn_depth = c.value("never_condemn_depth", 0);
        condemn.max_game_sec        = c.value("max_game_sec", 0.0);
        condemn.control_file        = c.value("control_file", std::string());
        condemn.drip                = std::max(1, c.value("drip", 1));
        condemn.enabled             = (condemn.median_sec_per_game > 0.0 && condemn.reference_games > 0)
                                   || condemn.max_game_sec > 0.0
                                   || !condemn.control_file.empty();
    }

    // Dense cell indices, so the per-game hot path indexes a vector instead of hashing a string.
    // A job with no "cell" is its own cell -- cross-job condemnation then cannot trigger for it,
    // which is the right default for every manifest that is not a matrix.
    int n_cells = 0;
    std::vector<std::string> cell_name;
    std::vector<int>         cell_depth;   // for the never-condemn floor, off the first job of the cell
    // ROW indices for the median rule (see Job::row). A job with no "row" judges on its cell key, so a
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
    // Per-game costs accumulate as the pool runs, keyed on the ROW (the rule's judgment unit);
    // `condemned` latches once and is only ever set true. Cells keep their own latch because
    // max_game_sec still condemns per cell, and a skip must honour either verdict.
    //
    // The median needs the SAMPLE, not a running sum, so this is a vector per row behind one mutex
    // rather than an atomic. That is affordable precisely because it is touched once per GAME: a
    // sort of at most a few thousand longs against a game that took seconds to minutes. row_games
    // stays atomic because `classify` reads it on the hot dequeue path to decide when a row is
    // released from metering, and that must not take a lock.
    std::vector<std::atomic<int>>       row_games(static_cast<std::size_t>(n_rows));
    std::vector<std::vector<long long>> row_sample(static_cast<std::size_t>(n_rows));
    std::mutex                          row_sample_mtx;
    std::vector<std::atomic<int>>       row_condemned(static_cast<std::size_t>(n_rows));
    std::vector<std::atomic<int>>       cell_condemned(static_cast<std::size_t>(n_cells));
    for (int i = 0; i < n_rows; ++i) { row_games[i].store(0); row_condemned[i].store(0); }
    for (int i = 0; i < n_cells; ++i) { cell_condemned[i].store(0); }
    // Median of a copy (the caller's vector is shared state and may be a partial sample with
    // in-flight games appended). Lower median on an even count: deterministic, and it errs toward
    // keeping a row rather than condemning it.
    auto median_ms = [](std::vector<long long> v) -> double
    {
        if (v.empty()) { return 0.0; }
        const std::size_t mid = v.size() / 2;
        std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid), v.end());
        return static_cast<double>(v[mid]);
    };

    // Per-job, per-game results. Pre-sized so workers write to disjoint slots with
    // no synchronisation. games[j][gi] = win turn (<=0 means no win).
    std::vector<std::vector<int>> win_turns(jobs.size());
    std::vector<std::vector<uint64_t>> digests(jobs.size());
    // Which of the kSkipped slots were ABANDONED at the work ceiling, as opposed to skipped at
    // dequeue with a condemned cell. Both write kSkipped, but they mean opposite things downstream
    // (see BatchJobResult::abandoned), and a short chunk cannot tell them apart on its own.
    std::vector<std::vector<char>> abandoned_at(jobs.size());
    // Per-game search work. ALWAYS recorded now, not just under MTG_DUMP_UNITS: reduce_job uses it
    // to decide, at reduce time, whether a CALIBRATION game exceeded the ceiling its own sample went
    // on to produce (see the calibration rule there). MTG_DUMP_UNITS still controls whether it is
    // written to disk. One long long per game -- 166 KB for a 20,800-game matrix.
    static const bool s_dump_units = EnvOn("MTG_DUMP_UNITS");
    std::vector<std::vector<long long>> units(jobs.size());
    // Per-job core-milliseconds (see BatchJobResult::elapsed_ms). Relaxed adds: the value is read
    // only after the job's last game lands, and that read is already ordered by the acq_rel
    // remaining-counter decrement below.
    std::vector<std::atomic<long long>> job_ms(jobs.size());
    for (std::atomic<long long>& a : job_ms) { a.store(0, std::memory_order_relaxed); }
    for (std::size_t j = 0; j < jobs.size(); ++j)
    {
        win_turns[j].assign(jobs[j].games, -1);
        digests[j].assign(jobs[j].games, 0);
        abandoned_at[j].assign(jobs[j].games, 0);
        units[j].assign(jobs[j].games, 0);
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

    // ---------------------------------------------------- RELATIVE PER-GAME CEILING (see Job::abandon_k)
    // One of these per cell, holding the calibration sample and the ceiling frozen from it. The
    // sample is the cell's games with GLOBAL index < abandon_calib, counted HERE from the work list
    // -- so `need` is a property of the manifest, not of completion order, and the freeze happens at
    // the same point on every machine. `frozen` is the only field the hot path reads, so it is the
    // only one that is atomic; the sample is built under the mutex, once per calibration game.
    struct CellCeiling
    {
        // 0 = still calibrating (the cell's non-calibration games are HELD); >0 = the frozen ceiling;
        // -1 = resolved with no ceiling. The -1 state is not cosmetic: a cell condemned partway
        // through its calibration never completes the sample, so without an explicit release its
        // held games would sit in `parked` with nothing left to promote them -- every worker
        // waiting on the condition variable, the run hung. Any non-zero value releases.
        std::atomic<long long> frozen{0};
        std::mutex             m;
        std::vector<long long> sample;
        int                    need  = 0;      // calibration games present in THIS manifest
        int                    calib = 0;
        double                 k     = 0.0;
        // Worker slots currently running one of this cell's calibration games. The freeze needs to
        // know that every unfinished sample game already sits above the middle value, and a running
        // game's progress is only readable through the slot it publishes to (ai/GameWorkMeter.h).
        std::vector<int>       running_slots;
    };
    std::vector<std::unique_ptr<CellCeiling>> cell_ceiling(static_cast<std::size_t>(std::max(1, n_cells)));
    for (std::unique_ptr<CellCeiling>& p : cell_ceiling) { p = std::make_unique<CellCeiling>(); }
    bool any_relative = false;
    for (const WorkItem& wi : items)
    {
        const Job& j = jobs[wi.job];
        if (j.abandon_k <= 0.0 || j.abandon_calib <= 0) { continue; }
        if (j.game_index + wi.game >= j.abandon_calib) { continue; }
        CellCeiling& cc = *cell_ceiling[static_cast<std::size_t>(j.cell_id)];
        ++cc.need;
        cc.calib = j.abandon_calib;
        cc.k     = j.abandon_k;
        any_relative = true;
    }
    if (any_relative)
    {
        std::cerr << "[batch] relative per-game ceiling armed: each cell runs its first "
                  << "abandon_calib games unbounded, then freezes k x their median for the rest\n";
    }
    // ABSOLUTE per-game ceiling per cell (max over its jobs; they agree in every manifest the driver
    // writes). Read by the in-flight max_game_sec rule to tell a cell whose games are already
    // deterministically bounded from one where nothing is guarding them -- see rule (1).
    std::vector<long long> cell_abandon_units(static_cast<std::size_t>(std::max(1, n_cells)), 0);
    for (const Job& j : jobs)
    {
        long long& u = cell_abandon_units[static_cast<std::size_t>(j.cell_id)];
        u = std::max(u, j.abandon_units);
    }
    // One "over max_game_sec but bounded" report per cell. The hook re-fires every heartbeat tick
    // while the game is still running, and the point of the line is that a human should look at k,
    // not that they should read it forty times.
    std::vector<std::atomic<int>> cell_overrun_noted(static_cast<std::size_t>(std::max(1, n_cells)));
    for (int i = 0; i < std::max(1, n_cells); ++i) { cell_overrun_noted[i].store(0); }

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
        std::vector<int>                  inflight;           // games running, per cell
        long long                         outstanding = 0;    // items taken but not yet finalised
        long long                         parked_total = 0;   // items sitting in `parked`
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
                        ++parked_total;
                        continue;
                    }
                    // Every taken item counts against its cell, not just condemnable ones: a cell
                    // can now also park work behind its CALIBRATION (see CellCeiling), and that
                    // includes protected d<=5 cells -- which is the point, since the protected H
                    // rungs are exactly where the degenerate games live.
                    ++inflight[cell_of(wi)];
                    out = wi;
                    return true;
                }
                // Nothing parked and nothing left to hand out: this worker is done, regardless of how
                // many games are still RUNNING elsewhere. Waiting on `outstanding` alone would hold
                // every idle worker on the condition variable until the very last game of the run
                // landed -- correct, but it turns a drained queue into a thread-join that only
                // happens at the end.
                if (parked_total <= 0 || outstanding <= 0) { return false; }
                // Everything left is parked behind a drip cap or a pending calibration: wait for one
                // of those cells' games to land rather than exiting, or the run would end with games
                // unplayed. Liveness holds because a cell only ever parks work while it has >= 1 game
                // in flight, and that game's OnFinished promotes the parked items.
                cv.wait(lk);
            }
        }

        // One game finalised (played or skipped). Frees the cell's in-flight slot and promotes its
        // parked items for re-evaluation -- they may now be under the drip cap, their row may have
        // crossed reference_games (released for good), their cell's ceiling may have just frozen, or
        // a condemnation may have landed (they drain as skips). Promotion is wholesale rather than
        // one-at-a-time: Take re-parks whatever is still capped, and the churn is bounded by the
        // cell's own game count, once per finished game -- microseconds against multi-second games.
        void OnFinished(int cell_id)
        {
            std::lock_guard<std::mutex> lk(mtx);
            --outstanding;
            --inflight[static_cast<std::size_t>(cell_id)];
            auto& q = parked[static_cast<std::size_t>(cell_id)];
            while (!q.empty())
            {
                ready.emplace(q.front().first, q.front().second);
                q.pop_front();
                --parked_total;
            }
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
        // EVERY item, not just the condemnable ones. This is the counter Take waits on when all
        // remaining work is parked, and calibration now parks work in PROTECTED cells too -- so
        // counting only condemnable games would let Take return false with a protected cell's games
        // still sitting in `parked`, silently dropping them from the run.
        pool.outstanding = static_cast<long long>(items.size());
        if (n_condemnable > 0)
        {
            std::cerr << "[batch] metering " << n_condemnable << " games of condemnable cells at "
                      << condemn.drip << " in flight per cell until judged; "
                      << (items.size() - static_cast<std::size_t>(n_condemnable))
                      << " protected games run alongside\n";
        }
    }
    // The median rule is active iff both its parameters are set; without it a cell is never "proven
    // tractable" (row_games does not accrue), so metering persists for the whole run -- exactly the
    // old drip-forever semantics, which is right when only max_game_sec is guarding.
    const bool median_rule_on = condemn.median_sec_per_game > 0.0 && condemn.reference_games > 0;
    pool.classify = [&](const WorkItem& wi) -> Pool::Verdict
    {
        const Job& j = jobs[wi.job];
        // CALIBRATION GATE, ahead of everything else -- including the d<=5 protection, which is about
        // never CONDEMNING a cell the crossover is read from, not about letting its individual games
        // run unbounded. A cell whose relative ceiling is not frozen yet runs only its calibration
        // games; the rest wait. Held work is released by the freeze in the worker, which happens
        // before that game's OnFinished promotes the cell's parked items.
        {
            const CellCeiling& cc = *cell_ceiling[static_cast<std::size_t>(j.cell_id)];
            if (cc.need > 0 && cc.frozen.load(std::memory_order_acquire) == 0
                && j.game_index + wi.game >= cc.calib)
            { return Pool::Verdict::kCapped; }
        }
        if (!condemn.enabled || j.depth <= condemn.never_condemn_depth)
        { return Pool::Verdict::kProtected; }
        // Condemned (row by the median rule, cell by max_game_sec): take it -- it drains as a skip.
        if (row_condemned[j.row_id].load(std::memory_order_relaxed) != 0
            || cell_condemned[j.cell_id].load(std::memory_order_relaxed) != 0)
        { return Pool::Verdict::kCondemnable; }
        // Judged tractable: the row reached its reference sample uncondemned. Released from
        // metering -- from here it runs in plain static order.
        if (median_rule_on
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

    // Progress of the game each worker slot is running, published by the meter but ONLY while that
    // game belongs to a cell's calibration sample (ai/GameWorkMeter.h). The freeze reads it to prove
    // an unfinished sample game already sits above the sample's middle value.
    std::vector<std::atomic<long long>> slot_units(static_cast<std::size_t>(std::max(1, num_threads)));
    for (std::atomic<long long>& a : slot_units) { a.store(0, std::memory_order_relaxed); }

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
        std::vector<int>      gi_played;
        wt_played.reserve(win_turns[j].size());
        dg_played.reserve(digests[j].size());
        gi_played.reserve(win_turns[j].size());
        // THE CALIBRATION RULE, applied here rather than during the run, and this is what makes the
        // abandoned set a function of the DATA instead of the schedule. A calibration game runs
        // without the relative ceiling (it is one of the games that defines it), and once the ceiling
        // freezes the running ones are stopped -- but whether a given sample game was still running
        // at that instant is a race. So the verdict is re-derived here from cost alone: a calibration
        // game is abandoned iff its work reached the ceiling its own sample produced, whether it was
        // stopped mid-flight or finished first. The set is then exactly {g : cost(g) >= ceiling},
        // identical on every machine, and the mid-flight abort is a pure cost saving with no bearing
        // on which games the table keeps.
        //
        // Safe against the reduce racing the freeze: a job holding calibration games also holds the
        // cell's HELD games (the driver's chunk is larger than the calibration window), and those
        // cannot run until the freeze -- so the job cannot complete before the ceiling exists.
        const CellCeiling& jcc = *cell_ceiling[static_cast<std::size_t>(jobs[j].cell_id)];
        const long long    jceil = jcc.frozen.load(std::memory_order_acquire);
        for (std::size_t i = 0; i < win_turns[j].size(); ++i)
        {
            const int gidx = jobs[j].game_index + static_cast<int>(i);
            if (win_turns[j][i] == kSkipped)
            {
                if (abandoned_at[j][i]) { r.abandoned.push_back(gidx); }
                continue;
            }
            if (jcc.need > 0 && jceil > 0 && gidx < jcc.calib && units[j][i] >= jceil)
            {
                r.abandoned.push_back(gidx);
                continue;
            }
            wt_played.push_back(win_turns[j][i]);
            dg_played.push_back(digests[j][i]);
            gi_played.push_back(gidx);
            if (s_dump_units) { r.units.push_back(units[j][i]); }
        }
        r.games_played = static_cast<int>(wt_played.size());
        r.win_turns    = wt_played;
        r.digests      = dg_played;
        r.game_indices = gi_played;
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

            // (0) DRIVER-SUPPLIED verdicts (see CondemnRule::control_file). One row name per line.
            //
            // These deliberately IGNORE never_condemn_depth, and that is the whole point of keeping
            // them on a separate path. The d<=5 floor protects the crossover rungs from the COST
            // guards, which are wall-clock and can fire because the box was busy -- an accident that
            // would leave a hole in the answer. A driver verdict is not an accident: it arrives only
            // after a paired equivalence test has MEASURED that the extra depth buys nothing, on a
            // sample large enough to bound the difference, and the rung keeps the games that proved
            // it. Capping there removes cost, not answer. Applied to H4->H5 -- dead on every deck
            // measured -- it is the only place the rule could ever pay off, and the floor would
            // otherwise make it unreachable.
            if (!condemn.control_file.empty())
            {
                std::ifstream cf(condemn.control_file);
                std::string   line;
                while (std::getline(cf, line))
                {
                    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                    { line.pop_back(); }
                    if (line.empty()) { continue; }
                    for (int rid = 0; rid < n_rows; ++rid)
                    {
                        if (row_name[static_cast<std::size_t>(rid)] != line) { continue; }
                        if (row_condemned[rid].exchange(1, std::memory_order_relaxed) != 0) { break; }
                        std::fprintf(stderr,
                            "[goldfish] CONDEMNED row=%s on the DRIVER's verdict (extra depth "
                            "measured equivalent); remaining games of every cell of this row are "
                            "skipped\n", line.c_str());
                        std::fflush(stderr);
                        break;
                    }
                }
            }

            // (1) Single pathological game, judged while it runs -- a LIVENESS backstop, and only
            // where nothing else is guarding the game.
            //
            // The right response to one pathological game is to abandon THAT GAME, not the cell
            // ("Also worth fixing", docs/design/condemnation-row-average.md). That response now
            // EXISTS and is the per-game work ceiling (ai/GameWorkMeter.h): it stops the game
            // itself, in work units, so the set it drops is a deterministic function of the data and
            // can be shared as a skip list. This rule cannot do the same thing -- it is keyed on
            // WALL CLOCK, so an abort here would drop a different game on a different box, or on the
            // same box under different load, and a wall-clock entry in a table-wide skip list
            // silently makes the matrix unreproducible. So the fix is not to teach this rule to
            // abandon; it is to make it YIELD wherever the deterministic ceiling is already in force.
            //
            // Where it is in force, condemning is not merely disproportionate (one observation
            // against 350 games, and the row loses a whole seed -- which moves a row mean further
            // than the effects the matrix is built to measure) but WRONG ON ITS OWN TERMS: the game
            // is already bounded, so its overrun says the box is loaded or k is too high, neither of
            // which is a property of this depth's tractability. Report and leave the cell alone.
            //
            // Where it is NOT in force -- a legacy manifest with no ceiling at all, or the
            // calibration window of a cell whose ceiling has yet to freeze -- nothing else can stop
            // the game, so the old cell condemnation stands as the backstop it was always meant to
            // be. Deliberately still per CELL and never per row: one game is one observation, and
            // widening the blast radius would take four seeds on it instead of one.
            if (condemn.max_game_sec > 0.0)
            {
                for (const auto& [cid, ms] : live)
                {
                    if (static_cast<double>(ms) <= condemn.max_game_sec * 1000.0) { continue; }
                    if (cid < 0 || cid >= n_cells) { continue; }
                    const bool bounded =
                        cell_ceiling[static_cast<std::size_t>(cid)]->frozen.load(
                            std::memory_order_acquire) > 0
                        || cell_abandon_units[static_cast<std::size_t>(cid)] > 0;
                    if (!bounded)
                    {
                        condemn_cell(cid, "on an IN-FLIGHT game at",
                                     static_cast<double>(ms) / 1000.0, condemn.max_game_sec);
                        continue;
                    }
                    if (cell_overrun_noted[cid].exchange(1, std::memory_order_relaxed) == 0)
                    {
                        std::fprintf(stderr,
                            "[batch] OVER max_game_sec cell=%s: a game has been running %.1f s "
                            "(limit %.1f) but the cell's per-game WORK ceiling is armed, so the "
                            "game is bounded and the cell is NOT condemned. If this repeats, the "
                            "ceiling (abandon_k) is too loose for this cell or the box is loaded.\n",
                            cell_name[static_cast<std::size_t>(cid)].c_str(),
                            static_cast<double>(ms) / 1000.0, condemn.max_game_sec);
                        std::fflush(stderr);
                    }
                }
            }

            // (2) The MEDIAN rule, with in-flight games folded in -- aggregated on the ROW, same
            // unit as the finished-game rule. A running game's elapsed time is a LOWER BOUND on its
            // final cost, and a median is MONOTONE in every element, so raising any of those partial
            // values can only raise the median: this median can therefore only UNDERSTATE the row's
            // final one. If it already exceeds the limit the finished sample must too, so condemning
            // here cannot be a false positive from the partial term -- the same soundness argument
            // the mean version had, and it survives the change of statistic intact. What it catches
            // is the case the finished-only rule is blind to: a row whose expensive games are all
            // still running reads as free until the first one lands, which on an explosive deck is
            // hours.
            if (condemn.median_sec_per_game > 0.0 && condemn.reference_games > 0)
            {
                std::unordered_map<int, std::vector<long long>> agg;   // row -> in-flight elapsed
                for (const auto& [cid, ms] : live)
                {
                    if (cid < 0 || cid >= n_cells) { continue; }
                    agg[cell_row[static_cast<std::size_t>(cid)]].push_back(ms);
                }
                for (const auto& [rid, partial] : agg)
                {
                    if (rid < 0 || rid >= n_rows) { continue; }
                    const int n = row_games[rid].load(std::memory_order_relaxed)
                                + static_cast<int>(partial.size());
                    if (n < condemn.reference_games) { continue; }
                    std::vector<long long> s;
                    {
                        std::lock_guard<std::mutex> lk(row_sample_mtx);
                        s = row_sample[static_cast<std::size_t>(rid)];
                    }
                    s.insert(s.end(), partial.begin(), partial.end());
                    const double med = median_ms(std::move(s));
                    if (med > condemn.median_sec_per_game * 1000.0)
                    { condemn_row(rid, "at a running MEDIAN (in-flight included) of",
                                  med / 1000.0, condemn.median_sec_per_game); }
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
                // verdict skips: the median rule condemns the ROW, max_game_sec condemns the CELL.
                // `condemnable` (job.depth above the floor) gates the skip so a protected d<=5 job
                // can never be skipped through a shared row/cell key -- the floor is structural, not
                // just a property of who sets the latch.
                if (condemn.enabled && condemnable
                    && (row_condemned[jobs[wi.job].row_id].load(std::memory_order_relaxed) != 0
                        || cell_condemned[jobs[wi.job].cell_id].load(std::memory_order_relaxed) != 0))
                {
                    win_turns[wi.job][wi.game] = kSkipped;
                    // A condemned cell will never finish its calibration sample, so RELEASE the
                    // games it is holding (see CellCeiling::frozen) -- they are about to drain as
                    // skips too. Without this the cell's held games have nothing left that could
                    // promote them and the whole run blocks on the condition variable.
                    {
                        CellCeiling& cc = *cell_ceiling[static_cast<std::size_t>(job.cell_id)];
                        long long expect = 0;
                        if (cc.need > 0)
                        { cc.frozen.compare_exchange_strong(expect, -1, std::memory_order_acq_rel); }
                    }
                    // Finalise in the pool as well: a skipped game is still a game accounted for,
                    // and if `outstanding` never drains the workers wait forever.
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
                // Arm the per-game work ceiling (see ai/GameWorkMeter.h). Paired with End() below so
                // a pooled worker thread can never carry one game's meter into the next; Begin also
                // resets, so the pairing is belt-and-braces rather than load-bearing.
                //
                // The FROZEN relative ceiling wins when the cell has one; the absolute number is the
                // fallback and is also what guards the calibration window itself (Job::abandon_k).
                // A calibration game is identified by GLOBAL index, so which games are exempt does
                // not depend on the order the pool happened to run them in.
                CellCeiling& cc = *cell_ceiling[static_cast<std::size_t>(job.cell_id)];
                const bool is_calib = cc.need > 0 && global_gi < cc.calib;
                const long long frozen = cc.frozen.load(std::memory_order_acquire);
                const long long ceiling =
                    (!is_calib && frozen > 0) ? frozen : job.abandon_units;
                gamework::Begin(ceiling);
                // A calibration game PUBLISHES its progress and watches the cell's ceiling, so the
                // freeze on another thread can (a) prove this game is above the sample's middle
                // value and stop waiting for it, and (b) stop it the moment the ceiling exists.
                // Registered here rather than inside Begin so the slot bookkeeping and the sample
                // stay under the same lock.
                if (is_calib && frozen == 0)
                {
                    {
                        std::lock_guard<std::mutex> lk(cc.m);
                        cc.running_slots.push_back(static_cast<int>(slot));
                    }
                    gamework::PublishTo(&slot_units[slot], &cc.frozen);
                }
                int wt = engine->RunGame(state, job.max_turns);
                const long long g_units = gamework::Used();
                gamework::End();
                if (wt == GameEngine::kAbandoned)
                {
                    // VOID, not a loss. kSkipped is dropped by reduce_job, so an abandoned game does
                    // not drag the cell's average toward max_turns+1 -- which is the whole point:
                    // scoring it as a loss would make skipping CHANGE the number we are measuring.
                    wt = kSkipped;
                    abandoned_at[wi.job][wi.game] = 1;
                    // The caller needs to know WHICH game, in the same self-contained form as
                    // SLOW-GAME, because the skip list is per (deck, seed, offset) and is what every
                    // other cell filters on. Units, not ms: the decision was deterministic and the
                    // reader must be able to reproduce it.
                    std::fprintf(stderr,
                                 "[goldfish] ABANDONED job=%s gi=%d units=%lld limit=%lld  repro: "
                                 "--seed %llu --game-index %d --games 1\n",
                                 job.name.c_str(), global_gi, g_units, ceiling,
                                 static_cast<unsigned long long>(job.seed
                                                                 + static_cast<uint64_t>(wi.game)),
                                 global_gi);
                    std::fflush(stderr);
                }
                // CALIBRATION. Fold this game into its cell's sample and, on the last one, FREEZE the
                // ceiling. Done before OnFinished below, so the games this cell has parked are
                // promoted into a pool that already knows the ceiling. Reported on stderr because the
                // driver has to store it: the calibration games do not appear in the manifest of a
                // later resume, so without the recorded number that resume would run unbounded --
                // and a ceiling recomputed from a different sample would abandon a different set,
                // which is exactly the reproducibility this whole mechanism is built on.
                if (is_calib)
                {
                    std::lock_guard<std::mutex> lk(cc.m);
                    cc.sample.push_back(g_units);
                    cc.running_slots.erase(std::remove(cc.running_slots.begin(),
                                                       cc.running_slots.end(),
                                                       static_cast<int>(slot)),
                                           cc.running_slots.end());
                    if (cc.frozen.load(std::memory_order_relaxed) == 0)
                    {
                        // FREEZE AS SOON AS THE MEDIAN IS DETERMINED, not when the last sample game
                        // lands. The median of `need` values only depends on the order statistics up
                        // to index hi = need/2, so it is already pinned once
                        //   (a) every sample game has been dispatched -- completed + running == need,
                        //       or an unstarted one could still come in below and move it;
                        //   (b) hi+1 of them have COMPLETED, giving s[0..hi]; and
                        //   (c) every still-running one has already consumed >= s[hi], so its final
                        //       value can only land at or above hi and cannot displace s[0..hi].
                        // Under (a)-(c) the final sorted sample agrees with `s` on every index the
                        // median reads, so freezing now yields exactly the number that waiting would.
                        //
                        // This is what makes the ceiling reach INTO its own calibration window: the
                        // frozen value is published to the running games (t_late_limit), and they are
                        // provably above the median, so stopping them cannot change it. Measured
                        // motivation: FiveColour V5's game 6 was 82.3% of the cell's total work and
                        // sat inside the window, so waiting for it cost more than every other game
                        // in the cell put together.
                        std::vector<long long> s = cc.sample;
                        std::sort(s.begin(), s.end());
                        const std::size_t hi = static_cast<std::size_t>(cc.need) / 2;
                        const bool all_dispatched =
                            static_cast<int>(cc.sample.size() + cc.running_slots.size()) == cc.need;
                        bool determined = all_dispatched && s.size() > hi;
                        if (determined)
                        {
                            for (int rs : cc.running_slots)
                            {
                                if (slot_units[static_cast<std::size_t>(rs)]
                                        .load(std::memory_order_relaxed) < s[hi])
                                { determined = false; break; }
                            }
                        }
                        if (determined)
                        {
                            const std::size_t n = static_cast<std::size_t>(cc.need);
                            const long long med = (n % 2 == 1) ? s[n / 2]
                                                               : (s[n / 2 - 1] + s[n / 2]) / 2;
                            const long long lim =
                                std::max<long long>(1, static_cast<long long>(cc.k * med));
                            cc.frozen.store(lim, std::memory_order_release);
                            std::fprintf(stderr,
                                         "[batch] CEILING cell=%s calib=%d median=%lld k=%.1f "
                                         "units=%lld  (from %d completed, %d still running above "
                                         "the median)\n",
                                         job.cell.c_str(), cc.need, med, cc.k, lim,
                                         static_cast<int>(cc.sample.size()),
                                         static_cast<int>(cc.running_slots.size()));
                            std::fflush(stderr);
                        }
                    }
                }
                win_turns[wi.job][wi.game] = wt;
                units[wi.job][wi.game] = g_units;
                const long long g_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - g_t0).count();
                job_ms[wi.job].fetch_add(g_ms, std::memory_order_relaxed);
                hb.Finished(job.name, global_gi, g_ms);
                hb.ClearRunning(slot);

                // Continuous tractability guard. Evaluated after every game rather than at a wave
                // barrier: the point of the pool is that no cell ever waits on another, and a
                // barrier reintroduces exactly the synchronisation the pool exists to remove.
                //
                // The MEDIAN accumulates and judges on the ROW (deck+arm+depth), not the cell:
                // tractability is a property of the depth, and judging each seed's cell separately
                // let one outlier game remove a single seed from a row, making rows non-comparable
                // (docs/design/condemnation-row-average.md). With no "row" in the manifest the row
                // IS the cell and this is the old per-cell rule verbatim.
                if (condemn.median_sec_per_game > 0.0 && condemn.reference_games > 0
                    && job.depth > condemn.never_condemn_depth)
                {
                    const int rid = job.row_id;
                    const int n   = row_games[rid].fetch_add(1, std::memory_order_relaxed) + 1;
                    // An ABANDONED game is folded in at its (truncated) cost rather than dropped.
                    // It is impractical by definition -- that is why it was abandoned -- and
                    // excluding it would let a row where most games are pathological look healthy
                    // from the few that survived, which is exactly the verdict this rule exists to
                    // reach.
                    double med = 0.0;
                    {
                        std::lock_guard<std::mutex> lk(row_sample_mtx);
                        row_sample[static_cast<std::size_t>(rid)].push_back(g_ms);
                        if (n >= condemn.reference_games)
                        { med = median_ms(row_sample[static_cast<std::size_t>(rid)]); }
                    }
                    if (n >= condemn.reference_games
                        && med > condemn.median_sec_per_game * 1000.0
                        && row_condemned[rid].exchange(1, std::memory_order_relaxed) == 0)
                    {
                        const bool fb = row_is_cell[static_cast<std::size_t>(rid)] != 0;
                        std::fprintf(stderr,
                            "[goldfish] CONDEMNED %s=%s after %d games at a MEDIAN of %.1f s/game "
                            "(limit %.1f); remaining games of %s are skipped\n",
                            fb ? "cell" : "row",
                            row_name[static_cast<std::size_t>(rid)].c_str(),
                            n, med / 1000.0, condemn.median_sec_per_game,
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
                // judged (tractable or condemned) or the cell's ceiling has just frozen, metered
                // back in otherwise. Sibling cells of a condemned row drain the same way as their
                // own in-flight games land and their parked work re-enters as cheap dequeue-time
                // skips. Called for EVERY game, not only condemnable ones: `outstanding` now counts
                // every item (a protected cell can park work behind its calibration), and a
                // protected game that never finalised would leave that counter permanently above
                // zero -- Take would block on the condition variable at the end of the run.
                pool.OnFinished(job.cell_id);

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
