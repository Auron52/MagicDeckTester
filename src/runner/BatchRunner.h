#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Per-job aggregate, in manifest order. Byte-identical to running that job as a
// standalone `mtg.exe <deck> --games N --seed S ...` invocation.
struct BatchJobResult
{
    std::string name;
    int    games_played     = 0;
    int    games_won        = 0;
    double average_win_turn = 0.0;
    std::vector<int> win_turns;   // per-game; <=0 = no win within max_turns
};

// Runs a whole manifest of goldfish jobs (different decks / seeds / depths /
// budgets) by pooling EVERY game from EVERY job into one shared work queue driven
// by a single atomic cursor. Because each game is seeded purely by its job's seed
// + game index, execution order cannot change any result, so the pool is free to
// interleave jobs and front-load the slow ones: a slow game in one job is then
// backfilled by another job's games instead of stranding a core at the tail. This
// collapses the per-invocation load-imbalance tails the regression suite pays
// across its many separate runs into a single tail for the whole batch.
//
// Lossless: with cards.json loaded once up front (read-only Lookup thereafter) and
// every per-job input passed by value into the worker, there is no shared mutable
// state on the hot path, so no locking is needed and each job's output matches its
// standalone run exactly.
class BatchRunner
{
public:
    // Parses the manifest JSON and runs it. num_threads: 0 = hardware_concurrency.
    // Throws std::runtime_error on a malformed manifest or unreadable deck/profile.
    static std::vector<BatchJobResult> RunManifest(
        const std::filesystem::path& manifest_path, int num_threads = 0);
};
