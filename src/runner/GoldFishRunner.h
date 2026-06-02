#pragma once
#include "../deck/DeckLoader.h"
#include "../core/GameState.h"
#include <vector>
#include <cstdint>

struct RunResult
{
    uint64_t seed          = 0;     // base seed used; pass to --seed to reproduce this run
    double averageWinTurn  = 0.0;
    int    gamesWon        = 0;
    int    gamesPlayed     = 0;
    std::vector<int> winTurns;  // per-game result; -1 = did not win within maxTurns
};

class GoldFishRunner
{
public:
    // baseSeed + gameIndex is the seed for each individual game (seeding contract).
    // Caller is responsible for generating baseSeed — use std::random_device for a
    // non-reproducible run, or a stored seed value to replay a previous run.
    RunResult run(const Decklist& deck, int numGames, uint64_t baseSeed, int maxTurns = 20);

private:
    GameState setupGame(const Decklist& deck, uint64_t seed);
};
