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
    // over games with dynamic self-scheduling (no static-chunk tail).
    // depth=0 (greedy) is fast; higher depth captures look-ahead value of cards like Aether Vial.
    static std::vector<GameRecord> RunForRecords(
        const Decklist& deck, const MulliganProfile& profile,
        int num_games, uint64_t seed, int max_turns,
        int depth = 0, int timeout_ms = 0);

    // Average win turn over records (losses count as max_turns+1).
    static double AverageWinTurn(const std::vector<GameRecord>& records, int max_turns);

private:
    // Computes per-card marginal win-turn scores and a recommended hand threshold.
    // scores[name][k] = improvement from the (k+1)-th copy in opening hand.
    // *threshold_out is set to mean - 1.5*std_dev of hand scores over records.
    // *hs_mean_out / *hs_std_out (optional) receive the hand-score distribution's
    // mean and std so callers can build a set of candidate thresholds to grid over.
    static std::map<std::string, std::vector<double>> ComputeCardScores(
        const std::vector<GameRecord>& records, int max_turns, double* threshold_out,
        double* hs_mean_out = nullptr, double* hs_std_out = nullptr);

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
