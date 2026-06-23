#include "BatchRunner.h"
#include "GoldFishRunner.h"
#include "../core/GameEngine.h"
#include "../core/HardwareConcurrency.h"
#include "../ai/AIEngine.h"
#include "../ai/MulliganProfile.h"
#include "../ai/MulliganProfileIO.h"
#include "../deck/DeckLoader.h"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <iostream>
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

    j.second_main = GoldFishRunner::DeckUsesSecondMain(j.deck);
    return j;
}
} // namespace

std::vector<BatchJobResult> BatchRunner::RunManifest(
    const std::filesystem::path& manifest_path, int num_threads)
{
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
    for (std::size_t j = 0; j < jobs.size(); ++j)
    {
        win_turns[j].assign(jobs[j].games, -1);
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
    // the tail. Depth then budget is the cost proxy (a deeper / higher-budget
    // search dominates wall time). Reordering is lossless -- each game is seeded by
    // its job's seed + game index, so order cannot change any result -- and a
    // stable sort keeps a job's games contiguous, which maximises the worker's
    // per-job engine reuse below.
    std::stable_sort(items.begin(), items.end(),
        [&](const WorkItem& a, const WorkItem& b)
        {
            const Job& ja = jobs[a.job];
            const Job& jb = jobs[b.job];
            if (ja.depth     != jb.depth)     { return ja.depth     > jb.depth; }
            if (ja.budget_ms != jb.budget_ms) { return ja.budget_ms > jb.budget_ms; }
            return a.job < b.job;
        });

    int requested = num_threads;
    num_threads = concurrency_util::ResolveWorkerThreads(num_threads);
    num_threads = std::min<int>(num_threads, std::max<std::size_t>(1, items.size()));
    concurrency_util::LogWorkerThreads(std::cerr, "batch", requested, num_threads);

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

                win_turns[wi.job][wi.game] = engine->RunGame(state, job.max_turns);
            }
        });
    }
    for (std::thread& th : threads) { th.join(); }

    // Reduce per job (matches GoldFishRunner: average over winning games only).
    std::vector<BatchJobResult> results(jobs.size());
    for (std::size_t j = 0; j < jobs.size(); ++j)
    {
        BatchJobResult& r = results[j];
        r.name         = jobs[j].name;
        r.games_played = jobs[j].games;
        r.win_turns    = win_turns[j];

        long long sum = 0;
        for (int wt : win_turns[j])
        {
            if (wt > 0) { ++r.games_won; sum += wt; }
        }
        if (r.games_won > 0)
        {
            r.average_win_turn = static_cast<double>(sum) / r.games_won;
        }
    }
    return results;
}
