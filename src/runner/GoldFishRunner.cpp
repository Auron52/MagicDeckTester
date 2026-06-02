#include "GoldFishRunner.h"
#include "../core/GameEngine.h"
#include "../ai/AIEngine.h"
#include <numeric>

RunResult GoldFishRunner::Run(const Decklist& deck, int num_games, uint64_t base_seed, int max_turns)
{
    RunResult result;
    result.seed        = base_seed;
    result.gamesPlayed = num_games;
    result.winTurns.reserve(num_games);

    AIEngine   ai;
    GameEngine engine(ai);

    for (int i = 0; i < num_games; ++i)
    {
        GameState state = SetupGame(deck, base_seed + static_cast<uint64_t>(i));
        int win_turn = engine.RunGame(state, max_turns);
        result.winTurns.push_back(win_turn);
        if (win_turn > 0)
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
