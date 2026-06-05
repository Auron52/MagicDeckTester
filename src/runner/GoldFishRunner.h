#pragma once
#include "../deck/DeckLoader.h"
#include "../core/GameState.h"
#include "../ai/MulliganProfile.h"
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct RunResult
{
    uint64_t seed          = 0;     // base seed used; pass to --seed to reproduce this run
    double average_win_turn = 0.0;
    int    games_won        = 0;
    int    games_played     = 0;
    std::vector<int> win_turns;  // per-game result; -1 = did not win within max_turns
};

class GoldFishRunner
{
public:
    // base_seed + gameIndex is the seed for each individual game (seeding contract).
    // Caller is responsible for generating base_seed — use std::random_device for a
    // non-reproducible run, or a stored seed value to replay a previous run.
    // log_dir: if non-empty, write one JSON game log per game into that directory.
    // base_game_index: first game's index for spawn-pattern selection (game i uses
    //   pattern (base_game_index + i) % 10). Pass a non-zero value to replay a
    //   specific game from a larger run with the correct opponent board.
    RunResult Run(const Decklist& deck, int num_games, uint64_t base_seed, int max_turns = 20,
                  const MulliganProfile& profile = MulliganProfile::DefaultProfile(),
                  const std::filesystem::path& log_dir = {},
                  int base_game_index = 0,
                  int lookahead_depth = 0,
                  int timeout_ms = 0,
                  int num_threads = 0,    // 0 = use hardware_concurrency
                  bool lookahead_bottoming = false);

    // Build the initial GameState for a single game with the given seed.
    // Shared by the runner and the analyzer.
    static GameState SetupGame(const Decklist& deck, uint64_t seed);
};
