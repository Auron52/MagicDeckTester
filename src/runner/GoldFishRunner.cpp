#include "GoldFishRunner.h"
#include "../core/GameEngine.h"
#include "../ai/AIEngine.h"
#include <numeric>

RunResult GoldFishRunner::Run(const Decklist& deck, int num_games, uint64_t base_seed, int max_turns)
{
    RunResult result;
    result.seed         = base_seed;
    result.games_played = num_games;
    result.win_turns.reserve(num_games);

    AIEngine   ai;
    GameEngine engine(ai);

    for (int i = 0; i < num_games; ++i)
    {
        GameState state = SetupGame(deck, base_seed + static_cast<uint64_t>(i));
        int win_turn = engine.RunGame(state, max_turns);
        result.win_turns.push_back(win_turn);
        if (win_turn > 0)
        {
            ++result.games_won;
        }
    }

    if (result.games_won > 0)
    {
        long long sum = 0;
        int count = 0;
        for (int t : result.win_turns)
        {
            if (t > 0)
            {
                sum += t;
                ++count;
            }
        }
        result.average_win_turn = static_cast<double>(sum) / count;
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
    state.active_player_index   = 0;
    state.priority_player_index = 0;
    state.turn_number           = 0;
    state.game_seed             = seed;

    return state;
}
