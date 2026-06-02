#pragma once
#include "../deck/DeckLoader.h"
#include "../core/GameState.h"
#include <vector>
#include <cstdint>

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
    RunResult Run(const Decklist& deck, int num_games, uint64_t base_seed, int max_turns = 20);

private:
    GameState SetupGame(const Decklist& deck, uint64_t seed);
};
