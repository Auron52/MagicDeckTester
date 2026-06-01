#pragma once
#include "../deck/DeckLoader.h"
#include <vector>
#include <cstdint>

struct RunResult {
    double averageWinTurn = 0.0;
    int    gamesWon       = 0;
    int    gamesPlayed    = 0;
    std::vector<int> winTurns;  // per-game result; -1 = did not win within maxTurns
};

class GoldFishRunner {
public:
    // baseSeed + gameIndex is the seed for each individual game (CR seeding contract).
    RunResult run(const Decklist& deck, int numGames = 1000,
                  uint64_t baseSeed = 42, int maxTurns = 20);

private:
    GameState setupGame(const Decklist& deck, uint64_t seed);
    void shuffleLibrary(std::vector<Card>& library, uint64_t seed);
};
