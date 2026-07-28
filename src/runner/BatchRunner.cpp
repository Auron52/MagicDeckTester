#include "BatchRunner.h"
#include "GoldFishRunner.h"
#include "../core/GameEngine.h"
#include "../core/GameLogger.h"
#include "../core/HardwareConcurrency.h"
#include "../ai/AIEngine.h"
#include "../ai/MulliganProfile.h"
#include "../ai/MulliganProfileIO.h"
#include "../deck/DeckLoader.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
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
    int             depth               = 0;
    int             budget_ms           = 0;
    int             max_turns           = 8;   // goldfish horizon (see main.cpp); per-job overridable
    bool            second_main         = false;  // precomputed DeckUsesSecondMain
    int             sched_weight        = 0;   // optional LPT scheduling priority (higher = run first);
                                               // overrides the depth/budget cost proxy for known-slow
                                               // jobs (e.g. Hinata's combo search). 0 => use the proxy.
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

    std::shared_ptr<const MulliganProfile> get(const std::string& path)
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = map_.find(path);
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
        std::shared_ptr<const MulliganProfile> loaded = load(path);
        lru_.push_front(path);
        map_[path] = Entry{loaded, lru_.begin()};
        while (map_.size() > cap_)
        {
            map_.erase(lru_.back());   // evict LRU (its profile survives while any worker holds a handle)
            lru_.pop_back();
        }
        return loaded;
    }

private:
    static std::shared_ptr<const MulliganProfile> load(const std::string& path)
    {
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

// A single unit of pooled work: game `game` of job `job`.
struct WorkItem
{
    int job;
    int game;
};

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
    j.depth               = jspec.value("depth", 0);
    j.budget_ms           = jspec.value("budget_ms", 0);
    j.sched_weight        = jspec.value("weight", 0);      // LPT priority override (see Job::sched_weight)
    j.max_turns           = jspec.value("max_turns", 8);   // global goldfish horizon; per-job override
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
    std::shared_ptr<const MulliganProfile> prof = cache.get(j.profile_path);
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
                  << "ms source=" << ps.source << "\n";
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

    // Per-job, per-game results. Pre-sized so workers write to disjoint slots with
    // no synchronisation. games[j][gi] = win turn (<=0 means no win).
    std::vector<std::vector<int>> win_turns(jobs.size());
    std::vector<std::vector<uint64_t>> digests(jobs.size());
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
        r.games_played = jobs[j].games;
        r.win_turns    = win_turns[j];
        r.digests      = digests[j];
        long long sum = 0;
        for (int wt : win_turns[j]) { if (wt > 0) { ++r.games_won; sum += wt; } }
        if (r.games_won > 0) { r.average_win_turn = static_cast<double>(sum) / r.games_won; }
        r.avg_turns = ComputeAvgTurns(win_turns[j], jobs[j].max_turns);
        // Case digest: FNV-1a fold of the per-game digests in game (gi) order -- a single
        // fingerprint of the whole case's play. Games in gi order (the vector index), so it is
        // deterministic and order-stable regardless of the pool's execution interleave.
        uint64_t cd = 1469598103934665603ULL;
        for (uint64_t d : digests[j])
        {
            for (int b = 0; b < 8; ++b) { cd ^= static_cast<uint8_t>((d >> (b * 8)) & 0xff); cd *= 1099511628211ULL; }
        }
        r.case_digest = cd;
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

    std::atomic<std::size_t> cursor{0};
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([&]()
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
                std::size_t k = cursor.fetch_add(1, std::memory_order_relaxed);
                if (k >= items.size()) { break; }

                const WorkItem& wi = items[k];
                const Job&      job = jobs[wi.job];

                if (wi.job != cached_job)
                {
                    // Load (or hit) the job's profile through the shared cache and hand a copy to the
                    // AIEngine (which owns its own copy). The shared handle is released at the end of this
                    // block; only the AIEngine's copy persists across the job's games.
                    std::shared_ptr<const MulliganProfile> prof = profile_cache.get(job.profile_path);
                    ai.emplace(*prof, job.depth, job.budget_ms);
                    ai->SetSearchPostCombat(job.second_main);
                    engine.emplace(*ai);
                    cached_job = wi.job;
                }

                GameState state = GoldFishRunner::SetupGame(
                    job.deck, job.seed + static_cast<uint64_t>(wi.game));
                state.vial_target_mv = job.vial_target_mv;
                GoldFishRunner::PopulateOpponentSpawns(state, wi.game);

                // Attach a logger for the play fingerprint. Default: DIGEST-ONLY (no structure
                // built, no disk -- cheap). With trace_dir set: a FULL logger that also builds
                // the structural trace and writes it to disk (used to inspect an unpruned run's
                // better line without re-running -- esp. valuable for the pathological deep-tail
                // games you never want to recompute). Fold* runs before the digest_only early-out
                // in every log method, so the digest is identical either way -> win turns and
                // digests are byte-identical whether or not tracing is on. Reset per game via
                // StartGame. Records only the real game's decisions (m_logger is nulled in the
                // search rollouts), so it does not perturb play.
                const bool trace = !trace_dir.empty();
                GameLogger dlog(/*digest_only=*/!trace);
                dlog.StartGame(std::string(), wi.game, job.name, job.seed + wi.game, {});
                engine->SetLogger(&dlog);
                win_turns[wi.job][wi.game] = engine->RunGame(state, job.max_turns);
                engine->SetLogger(nullptr);
                digests[wi.job][wi.game] = dlog.Digest();
                if (trace)
                {
                    dlog.EndGame(win_turns[wi.job][wi.game]);
                    dlog.WriteToFile(trace_dir / (job.name + "_gi" + std::to_string(wi.game) + ".json"));
                }

                // Stream this job the instant its last game lands.
                if (on_job_done
                    && remaining[wi.job].fetch_sub(1, std::memory_order_acq_rel) == 1)
                {
                    BatchJobResult r = reduce_job(wi.job);
                    std::lock_guard<std::mutex> lk(cb_mtx);
                    on_job_done(r);
                }
            }
        });
    }
    for (std::thread& th : threads) { th.join(); }

    // Final per-job aggregate, in manifest order (regardless of streaming).
    std::vector<BatchJobResult> results(jobs.size());
    for (std::size_t j = 0; j < jobs.size(); ++j) { results[j] = reduce_job(j); }
    return results;
}
