#include "GoldFishRunner.h"
#include "../core/GameEngine.h"
#include "../ai/AIEngine.h"
#include <numeric>

RunResult GoldFishRunner::Run(const Decklist& deck, int numGames, uint64_t baseSeed, int maxTurns)
{
    RunResult result;
    result.seed        = baseSeed;
    result.gamesPlayed = numGames;
    result.winTurns.reserve(numGames);

    AIEngine   ai;
    GameEngine engine(ai);

    for (int i = 0; i < numGames; ++i)
    {
        GameState state = SetupGame(deck, baseSeed + static_cast<uint64_t>(i));
        int winTurn = engine.RunGame(state, maxTurns);
        result.winTurns.push_back(winTurn);
        if (winTurn > 0)
        {
            ++result.gamesWon;
        }
    }

    if (result.gamesWon > 0)
    {
        long long sum = 0;
        int count = 0;
        for (int t : result.winTurns)
        {
            if (t > 0)
            {
                sum += t;
                ++count;
            }
        }
        result.averageWinTurn = static_cast<double>(sum) / count;
    }

    return result;
}

GameState GoldFishRunner::SetupGame(const Decklist& deck, uint64_t seed)
{
    GameState state;

    // Player 0 = goldfishing AI; Player 1 = do-nothing opponent (Phase 1)
    state.players[0].life = 20;
    state.players[1].life = 20;

    state.players[0].library.assign(deck.mainboard.begin(), deck.mainboard.end());
    state.players[0].library.Shuffle(seed);

    // Wire controller/owner pointers for future permanents as they enter the battlefield.
    // (No permanents exist at game start; pointer wiring happens in GameEngine on ETB.)
    state.activePlayerIndex   = 0;
    state.priorityPlayerIndex = 0;
    state.turnNumber          = 0;
    state.gameSeed            = seed;

    return state;
}
