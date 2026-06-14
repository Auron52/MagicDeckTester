#pragma once
#include "../ai/MulliganProfile.h"
#include "../deck/DeckLoader.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

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

// The analyzer's product: the deck's prepared profile. It carries no win-rate
// metrics by design -- evaluation (win%, avg win turn) is the regression suite's
// job, not the analyzer's.
struct AnalysisResult
{
    std::string deck_name;
    uint64_t    seed = 0;
    MulliganProfile                mulligan_profile;
    std::vector<RequiredPieceFlag> mulligan_flags;
    std::map<std::string, std::vector<double>> card_scores;
    double      hand_score_threshold = 0.0;
};

class AnalyzerEngine
{
public:
    // Produces the deck's profile (optimised mulligan + per-card scores). Does NOT
    // run a headline win-rate simulation -- that is the regression suite's job --
    // so there are no game-count/depth/budget knobs; the scoring methodology is
    // fixed internally.
    AnalysisResult Run(const Decklist& deck, uint64_t base_seed, int max_turns = 20);

    // Per-game record: win turn + the hand that was kept after mulliganing.
    struct GameRecord
    {
        int                      win_turn;
        std::vector<std::string> opening_hand;
    };

    // Runs games and captures the kept opening hand for each game. Parallelises
    // over games with dynamic self-scheduling (no static-chunk tail). Callers that
    // are already parallel over their own units must use RunForRecordsSerial.
    // depth=0 (greedy) is fast; higher depth captures look-ahead value of cards like Aether Vial.
    static std::vector<GameRecord> RunForRecords(
        const Decklist& deck, const MulliganProfile& profile,
        int num_games, uint64_t seed, int max_turns,
        int depth = 0, int timeout_ms = 0);

    // Single-threaded variant: runs the games on the calling thread. Used inside
    // already-parallel loops (e.g. GridSearchLands over configs) to avoid nesting
    // a second layer of threads.
    static std::vector<GameRecord> RunForRecordsSerial(
        const Decklist& deck, const MulliganProfile& profile,
        int num_games, uint64_t seed, int max_turns,
        int depth = 0, int timeout_ms = 0);

    // Average win turn over records (losses count as max_turns+1).
    static double AverageWinTurn(const std::vector<GameRecord>& records, int max_turns);

private:
    // Computes per-card impact from a set of game records.
    // Returns card_name -> (avg_win_turn_without - avg_win_turn_with): positive = card helps.
    static std::map<std::string, double> ScanCardImpacts(
        const std::vector<GameRecord>& records, int max_turns);

    // Computes per-card marginal win-turn scores and a recommended hand threshold.
    // scores[name][k] = improvement from the (k+1)-th copy in opening hand.
    // *threshold_out is set to mean - 0.5*std_dev of hand scores over records.
    static std::map<std::string, std::vector<double>> ComputeCardScores(
        const std::vector<GameRecord>& records, int max_turns, double* threshold_out);

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
    // vial_target_mv is injected into every profile used so Aether Vial ticks correctly.
    static OptResult OptimizeMulligan(
        const Decklist& deck, uint64_t seed, int max_turns, int vial_target_mv = 0);

    // Returns the most common creature MV if the deck contains Aether Vial, else 0.
    static int ComputeVialTargetMv(const Decklist& deck);
};

std::string AnalysisResultToJson(const AnalysisResult& result);
