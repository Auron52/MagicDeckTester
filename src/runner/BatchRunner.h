#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// Per-job aggregate, in manifest order. Byte-identical to running that job as a
// standalone `mtg.exe <deck> --games N --seed S ...` invocation.
struct BatchJobResult
{
    std::string name;
    int    games_played     = 0;
    int    games_won        = 0;
    double average_win_turn = 0.0;   // mean over WINS only (internal / future 1v1)
    double avg_turns        = 0.0;   // THE goldfish metric: mean turn-to-win, unwon = max_turns+1
    std::vector<int>      win_turns;   // per-game; <=0 = no win within max_turns
    std::vector<uint64_t> digests;     // per-game play digest (GameLogger::Digest), 0 if unavailable
    // GLOBAL game index of each entry above, i.e. game_index + its position in the job. Parallel to
    // win_turns, and NOT simply 0..n-1: a job can finish short (a condemned cell's games are skipped
    // at dequeue, an abandoned game is voided), and those entries are dropped, so position in the
    // vector stops matching game identity. Carrying the index makes a short result still alignable
    // -- which is what lets a per-game consumer tell "game 380 was voided" from "the file is stale".
    std::vector<int>      game_indices;
    // Per-game SEARCH WORK in units (ai/GameWorkMeter.h), parallel to win_turns. Populated only
    // under MTG_DUMP_UNITS, because it exists to CALIBRATE the per-game ceiling: the useful
    // threshold is relative to a cell's own typical cost (cells span 11 ms to 700 s per game), so
    // the distribution has to be measurable before a multiplier can be chosen. Units rather than
    // milliseconds for the same reason the ceiling is in units -- a wall-clock calibration would
    // pick a different threshold on every machine and under every load.
    std::vector<long long> units;
    uint64_t              case_digest = 0;  // fold of per-game digests in game order (a case fingerprint)
    // SUM of this job's per-game wall times, in ms -- core-milliseconds, not elapsed span. Games of
    // one job interleave with other jobs across the pool, so an elapsed first-to-last span would
    // measure the pool, not the job. The sum is the same quantity a serial per-chunk subprocess used
    // to report as its wall clock, which is what a cost/tractability model consumes.
    long long             elapsed_ms  = 0;
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
//
// Per-job manifest fields: name, deck, profile, games, seed, game_index, depth,
// budget_ms, max_turns, weight, ignore_play_profile.
//
// `game_index` makes a job a CHUNK of a longer run -- games [game_index,
// game_index+games) -- rather than a whole one. That is what lets a chunked
// generator pool every chunk of every cell into ONE process: without it, a chunk
// had to be its own `mtg` invocation, and the batch size then WAS the tail
// granularity (measured 2026-08-08: two 25-game chunks still running at 54,004 s
// after everything else had drained, three of twenty-four cores busy for fifteen
// hours). With it the whole run has ONE tail, of one game. Game i of a chunk is
// globally game (game_index + i) for the spawn schedule and the log, while its
// shuffle seed stays `seed + i` -- so a chunk reproduces the single-run
// `--seed (base+off) --game-index off` form exactly. Default 0 = a whole run.
class BatchRunner
{
public:
    // Invoked once per job AS SOON AS that job's last game finishes (not at the very
    // end), so a caller can stream per-job results while the rest of the batch is
    // still running. Called from a worker thread under an internal mutex, so the
    // callback itself need not lock; jobs arrive in completion order, not manifest
    // order. Optional (default no-op).
    using JobDoneCallback = std::function<void(const BatchJobResult&)>;

    // Parses the manifest JSON and runs it. num_threads: 0 = hardware_concurrency.
    // on_job_done (optional) streams each job's result the moment it completes; the
    // returned vector still holds every job's result in manifest order at the end.
    // trace_dir (optional): when non-empty, each game writes a FULL decision-log JSON to
    //   <trace_dir>/<jobname>_gi<game>.json (a real GameLogger, not digest-only). This is
    //   the "log unpruned runs so we can see WHAT better line it found, without a rerun"
    //   path -- expensive (structure + disk per game), so leave empty for normal batches.
    //   The play digest is folded identically in both logger modes, so enabling traces does
    //   NOT change win turns or digests (comparisons vs a digest-only baseline stay valid).
    // Throws std::runtime_error on a malformed manifest or unreadable deck/profile.
    static std::vector<BatchJobResult> RunManifest(
        const std::filesystem::path& manifest_path, int num_threads = 0,
        const JobDoneCallback& on_job_done = {},
        const std::filesystem::path& trace_dir = {});
};
