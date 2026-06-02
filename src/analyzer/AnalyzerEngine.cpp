#include "AnalyzerEngine.h"
#include "../core/GameEngine.h"
#include "../ai/AIEngine.h"
#include "../runner/GoldFishRunner.h"
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

AnalysisResult AnalyzerEngine::Run(const Decklist& deck, int num_games,
                                    uint64_t base_seed, int max_turns)
{
    return RunMonteCarlo(deck, num_games, base_seed, max_turns);
}

AnalysisResult AnalyzerEngine::RunMonteCarlo(const Decklist& deck, int num_games,
                                              uint64_t base_seed, int max_turns)
{
    // Reuse GoldFishRunner for the simulation loop; analysis layers on top.
    // TODO: replace with a search-based runner once TakeTurn is implemented.
    GoldFishRunner runner;
    RunResult run = runner.Run(deck, num_games, base_seed, max_turns);

    AnalysisResult result;
    result.deck_name        = "deck";  // TODO: derive from file name
    result.seed             = run.seed;
    result.games_played     = run.games_played;
    result.games_won        = run.games_won;
    result.average_win_turn = run.average_win_turn;

    // TODO: populate turn_stats once the game engine emits per-turn events.

    return result;
}

std::string AnalysisResultToJson(const AnalysisResult& result)
{
    json j;
    j["deck_name"]         = result.deck_name;
    j["seed"]              = result.seed;
    j["games_played"]      = result.games_played;
    j["games_won"]         = result.games_won;
    j["average_win_turn"]  = result.average_win_turn;
    j["win_rate"]          = result.games_played > 0
                             ? static_cast<double>(result.games_won) / result.games_played
                             : 0.0;

    json turns = json::array();
    for (const TurnStats& ts : result.turn_stats)
    {
        json t;
        t["turn"]       = ts.turn;
        t["avg_damage"] = ts.avg_damage;
        t["avg_hand"]   = ts.avg_hand;
        t["avg_board"]  = ts.avg_board;
        turns.push_back(t);
    }
    j["turn_stats"] = turns;

    return j.dump(2);
}
