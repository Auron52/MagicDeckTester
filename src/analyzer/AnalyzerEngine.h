#pragma once
#include "../deck/DeckLoader.h"
#include <cstdint>
#include <string>
#include <vector>

// Per-turn statistics gathered during the analysis run.
struct TurnStats
{
    int turn            = 0;
    double avg_damage   = 0.0;  // average damage dealt to opponent this turn across games
    double avg_hand     = 0.0;  // average hand size entering this turn
    double avg_board    = 0.0;  // average number of permanents on battlefield
};

// Aggregate results from a full analysis run, written to stdout as JSON.
struct AnalysisResult
{
    std::string deck_name;
    uint64_t    seed            = 0;
    int         games_played    = 0;
    int         games_won       = 0;
    double      average_win_turn = 0.0;
    std::vector<TurnStats> turn_stats;
    // TODO: decision statistics (which spells were cast when, mull patterns, etc.)
    // added in Phase 1.2 once CardDatabase is populated and TakeTurn is implemented.
};

class AnalyzerEngine
{
public:
    // Runs num_games games with the given deck and base seed.
    // Outputs the AnalysisResult as JSON to stdout.
    AnalysisResult Run(const Decklist& deck, int num_games, uint64_t base_seed,
                       int max_turns = 20);

private:
    // TODO: Replace with proper search (MCTS or exhaustive per-turn enumeration).
    // Current approach: Monte Carlo — play greedily and collect statistics.
    AnalysisResult RunMonteCarlo(const Decklist& deck, int num_games,
                                 uint64_t base_seed, int max_turns);
};

// Serialise an AnalysisResult to a JSON string.
std::string AnalysisResultToJson(const AnalysisResult& result);
