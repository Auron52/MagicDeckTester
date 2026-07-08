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
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
// One fully-resolved job: deck + profile loaded, all run parameters by value.
struct Job
{
    std::string     name;
    Decklist        deck;
    MulliganProfile profile;
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

// A single unit of pooled work: game `game` of job `job`.
struct WorkItem
{
    int job;
    int game;
};

Job ParseJob(const json& jspec)
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
    // (mirrors the single-run mtg.exe behaviour exactly).
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
    if (std::filesystem::exists(profile_path))
    {
        j.profile = LoadDeckProfile(profile_path);
    }
    // Play always uses a deck's exhaustive keep/bottom sidecar when it ships alongside the base
    // profile (keep on presence; bottoming per the sidecar's bottoming_enabled).
    AttachExhaustiveSidecar(j.profile, profile_path);
    AttachEvalSidecar(j.profile, profile_path);   // ... and its learned mid-game eval sidecar if present

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

    std::vector<Job> jobs;
    jobs.reserve(manifest["jobs"].size());
    for (const json& jspec : manifest["jobs"]) { jobs.push_back(ParseJob(jspec)); }

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
    // as the proxy. Reordering is
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
                    ai.emplace(job.profile, job.depth, job.budget_ms);
                    ai->SetSearchPostCombat(job.second_main);
                    engine.emplace(*ai);
                    cached_job = wi.job;
                }

                GameState state = GoldFishRunner::SetupGame(
                    job.deck, job.seed + static_cast<uint64_t>(wi.game));
                state.vial_target_mv = job.profile.vial_target_mv;
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
