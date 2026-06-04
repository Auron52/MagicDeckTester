#pragma once
#include "../ai/MulliganProfile.h"
#include "../deck/DeckLoader.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct TurnStats
{
    int turn            = 0;
    double avg_damage   = 0.0;
    double avg_hand     = 0.0;
    double avg_board    = 0.0;
};

// A card found to significantly affect win rate when present in the opening hand.
// Automatically added to required_pieces after multi-round confirmation; flagged
// so the user can verify the heuristic is correct for their deck.
struct RequiredPieceFlag
{
    std::string card_name;
    double      correlation;       // avg win-turn improvement when card is in hand (scan phase)
    double      baseline_win_turn; // avg win turn with default profile
    double      win_turn_required; // avg win turn when this card is required
    double      improvement;       // baseline_win_turn - win_turn_required (positive = better)
};

struct AnalysisResult
{
    std::string deck_name;
    uint64_t    seed             = 0;
    int         games_played     = 0;
    int         games_won        = 0;
    double      average_win_turn = 0.0;
    std::vector<TurnStats>         turn_stats;
    MulliganProfile                mulligan_profile;
    std::vector<RequiredPieceFlag> mulligan_flags;
};

class AnalyzerEngine
{
public:
    AnalysisResult Run(const Decklist& deck, int num_games, uint64_t base_seed,
                       int max_turns = 20, int depth = 2, int timeout_ms = 0);

    // Per-game record: win turn + the hand that was kept after mulliganing.
    struct GameRecord
    {
        int                      win_turn;
        std::vector<std::string> opening_hand;
    };

    // Runs games at depth=0 and captures the kept opening hand for each game.
    static std::vector<GameRecord> RunForRecords(
        const Decklist& deck, const MulliganProfile& profile,
        int num_games, uint64_t seed, int max_turns);

    // Average win turn over records (losses count as max_turns+1).
    static double AverageWinTurn(const std::vector<GameRecord>& records, int max_turns);

private:
    // Computes per-card impact from a set of game records.
    // Returns card_name -> (avg_win_turn_without - avg_win_turn_with): positive = card helps.
    static std::map<std::string, double> ScanCardImpacts(
        const std::vector<GameRecord>& records, int max_turns);

    // Grid-searches min_lands / max_lands / stop_at / skip_curve_check.
    // Returns the best profile found and writes its avg win turn to best_win_turn.
    static MulliganProfile GridSearchLands(
        const Decklist& deck, const MulliganProfile& base_profile,
        int games_per_config, uint64_t seed, int max_turns,
        double& best_win_turn);

    struct OptResult
    {
        MulliganProfile              profile;
        std::vector<RequiredPieceFlag> flags;
    };

    // Full mulligan optimisation: card-importance analysis + land grid search.
    // Uses a seed offset separate from the user's analysis seed to avoid overfitting.
    static OptResult OptimizeMulligan(
        const Decklist& deck, uint64_t seed, int max_turns);

    AnalysisResult RunMonteCarlo(
        const Decklist& deck, int num_games, uint64_t base_seed,
        int max_turns, int depth, int timeout_ms,
        const MulliganProfile& profile);
};

std::string AnalysisResultToJson(const AnalysisResult& result);
