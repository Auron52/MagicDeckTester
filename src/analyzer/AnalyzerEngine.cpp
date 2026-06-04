#include "AnalyzerEngine.h"
#include "../core/GameEngine.h"
#include "../ai/AIEngine.h"
#include "../ai/MulliganProfile.h"
#include "../ai/MulliganProfileIO.h"
#include "../runner/GoldFishRunner.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

using json = nlohmann::json;

// ============================================================
// Internal helpers
// ============================================================

double AnalyzerEngine::AverageWinTurn(const std::vector<GameRecord>& records, int max_turns)
{
    double sum = 0.0;
    for (const GameRecord& r : records)
    {
        sum += (r.win_turn > 0) ? static_cast<double>(r.win_turn)
                                : static_cast<double>(max_turns + 1);
    }
    return records.empty() ? 0.0 : sum / static_cast<double>(records.size());
}

std::vector<AnalyzerEngine::GameRecord> AnalyzerEngine::RunForRecords(
    const Decklist& deck, const MulliganProfile& profile,
    int num_games, uint64_t seed, int max_turns)
{
    std::vector<GameRecord> records;
    records.reserve(num_games);

    AIEngine   ai(profile, /*depth=*/0, /*timeout_ms=*/0);
    GameEngine engine(ai);

    for (int i = 0; i < num_games; ++i)
    {
        GameState state = GoldFishRunner::SetupGame(deck, seed + static_cast<uint64_t>(i));
        int win_turn    = engine.RunGame(state, max_turns);

        GameRecord rec;
        rec.win_turn    = win_turn;
        rec.opening_hand = ai.GetKeptOpeningHand();
        records.push_back(std::move(rec));
    }
    return records;
}

std::map<std::string, double> AnalyzerEngine::ScanCardImpacts(
    const std::vector<GameRecord>& records, int max_turns)
{
    // Collect all distinct non-land card names seen in opening hands.
    std::set<std::string> all_cards;
    for (const GameRecord& r : records)
    {
        for (const std::string& c : r.opening_hand) { all_cards.insert(c); }
    }

    std::map<std::string, double> impact;
    for (const std::string& card : all_cards)
    {
        double sum_with = 0.0, sum_without = 0.0;
        int    cnt_with = 0,   cnt_without  = 0;

        for (const GameRecord& r : records)
        {
            double wt = (r.win_turn > 0) ? static_cast<double>(r.win_turn)
                                         : static_cast<double>(max_turns + 1);
            bool in_hand = false;
            for (const std::string& c : r.opening_hand)
            {
                if (c == card) { in_hand = true; break; }
            }
            if (in_hand) { sum_with += wt;    ++cnt_with;    }
            else         { sum_without += wt; ++cnt_without; }
        }

        if (cnt_with > 0 && cnt_without > 0)
        {
            double avg_with    = sum_with    / cnt_with;
            double avg_without = sum_without / cnt_without;
            impact[card] = avg_without - avg_with;  // positive = card helps when in hand
        }
    }
    return impact;
}

MulliganProfile AnalyzerEngine::GridSearchLands(
    const Decklist& deck, const MulliganProfile& base_profile,
    int games_per_config, uint64_t seed, int max_turns,
    double& best_win_turn)
{
    MulliganProfile best      = base_profile;
    best_win_turn             = std::numeric_limits<double>::max();
    uint64_t        seed_step = static_cast<uint64_t>(games_per_config);
    uint64_t        offset    = 0;

    static const CurveCheck  CURVE_VALUES[] = {
        CurveCheck::None, CurveCheck::TwoDrop,
        CurveCheck::OneDrop, CurveCheck::OneAndTwo
    };
    static const BottomOrder BOTTOM_VALUES[] = {
        BottomOrder::CountFirst, BottomOrder::TotalFirst
    };

    for (int min_l = 1; min_l <= 3; ++min_l)
    for (int max_l = min_l; max_l <= 5; ++max_l)
    for (int stop  = 3; stop  <= 5; ++stop)
    for (CurveCheck  cc : CURVE_VALUES)
    for (BottomOrder bo : BOTTOM_VALUES)
    {
        MulliganProfile profile  = base_profile;
        profile.min_lands        = min_l;
        profile.max_lands        = max_l;
        profile.stop_at          = stop;
        profile.curve_check      = cc;
        profile.bottom_order     = bo;

        std::vector<GameRecord> records = RunForRecords(
            deck, profile, games_per_config, seed + offset * seed_step, max_turns);

        double avg = AverageWinTurn(records, max_turns);
        if (avg < best_win_turn)
        {
            best_win_turn = avg;
            best          = profile;
        }
        ++offset;
    }
    return best;
}

// ============================================================
// Color source analysis helpers
// ============================================================

// Returns the maximum number of pips of each color across all card mana costs in the deck.
// Only colors with at least one pip are included in the result.
static std::map<Color, int> FindMaxPipsPerColor(const Decklist& deck)
{
    std::map<Color, int> max_pips;
    for (const Card& c : deck.mainboard)
    {
        std::optional<CardDefinition> def = CardDatabase::Instance().Lookup(c.m_name);
        if (!def) { continue; }
        const ManaCost& cost = def->card.m_mana_cost;
        if (cost.white > 0) { max_pips[Color::White] = std::max(max_pips[Color::White], cost.white); }
        if (cost.blue  > 0) { max_pips[Color::Blue]  = std::max(max_pips[Color::Blue],  cost.blue);  }
        if (cost.black > 0) { max_pips[Color::Black] = std::max(max_pips[Color::Black], cost.black); }
        if (cost.red   > 0) { max_pips[Color::Red]   = std::max(max_pips[Color::Red],   cost.red);   }
        if (cost.green > 0) { max_pips[Color::Green] = std::max(max_pips[Color::Green], cost.green); }
    }
    return max_pips;
}

// Tests min_count values from max_count down to 1 for the given color.
// Returns the highest confirmed minimum (0 if requiring any minimum is not helpful).
static int ConfirmColorSourceMin(
    const Decklist& deck, const MulliganProfile& base_profile,
    Color color, int max_count,
    double baseline_avg, uint64_t& confirm_seed, int max_turns)
{
    constexpr int    CONFIRM1_GAMES      = 3000;
    constexpr int    CONFIRM2_GAMES      = 5000;
    constexpr double CONFIRM1_THRESHOLD  = 0.10;
    constexpr double CONFIRM2_THRESHOLD  = 0.05;

    for (int min_count = max_count; min_count >= 1; --min_count)
    {
        MulliganProfile test_profile = base_profile;
        test_profile.min_color_sources[color] = min_count;

        std::cerr << "  Color source {" << ColorToChar(color) << "} >= "
                  << min_count << " — confirming...\n";

        std::vector<AnalyzerEngine::GameRecord> c1 = AnalyzerEngine::RunForRecords(
            deck, test_profile, CONFIRM1_GAMES, confirm_seed, max_turns);
        double avg1     = AnalyzerEngine::AverageWinTurn(c1, max_turns);
        double improve1 = baseline_avg - avg1;
        confirm_seed   += CONFIRM1_GAMES;

        if (improve1 < CONFIRM1_THRESHOLD)
        {
            std::cerr << "    Round 1 failed (improvement " << improve1
                      << " < " << CONFIRM1_THRESHOLD << ") — skipping.\n";
            continue;
        }

        std::vector<AnalyzerEngine::GameRecord> c2 = AnalyzerEngine::RunForRecords(
            deck, test_profile, CONFIRM2_GAMES, confirm_seed, max_turns);
        double avg2     = AnalyzerEngine::AverageWinTurn(c2, max_turns);
        double improve2 = baseline_avg - avg2;
        confirm_seed   += CONFIRM2_GAMES;

        if (improve2 < CONFIRM2_THRESHOLD)
        {
            std::cerr << "    Round 2 failed (improvement " << improve2
                      << " < " << CONFIRM2_THRESHOLD << ") — skipping.\n";
            continue;
        }

        std::cerr << "    Confirmed min=" << min_count
                  << " (round1 +" << improve1 << ", round2 +" << improve2 << " turns).\n";
        return min_count;
    }

    return 0;
}

// ============================================================
// Mulligan optimiser
// ============================================================

AnalyzerEngine::OptResult AnalyzerEngine::OptimizeMulligan(
    const Decklist& deck, uint64_t seed, int max_turns)
{
    // Use a seed far from the user's analysis seed to avoid overfitting
    // to the same shuffle sequences.
    constexpr uint64_t OPT_SEED_OFFSET = 1'000'000ULL;
    uint64_t opt_seed = seed + OPT_SEED_OFFSET;

    // Phase parameters.
    // All optimisation phases run at depth=0 (greedy), so even large game counts
    // are fast (~0.1ms/game).  Counts are sized so that the standard error
    // (~1.5/sqrt(N)) is well below each acceptance threshold.
    constexpr int    SCAN_GAMES        = 2000;  // std_err ≈ 0.034 turns
    constexpr int    CONFIRM1_GAMES    = 3000;  // std_err ≈ 0.027 turns; threshold 0.10
    constexpr int    CONFIRM2_GAMES    = 5000;  // std_err ≈ 0.021 turns; threshold 0.05
    constexpr int    GRID_GAMES        = 1000;  // std_err ≈ 0.047 turns per config
    constexpr int    MAX_CANDIDATES    = 5;
    constexpr double SCAN_THRESHOLD    = 0.30;  // min correlation (turns) to enter pipeline
    constexpr double CONFIRM1_THRESHOLD = 0.10; // min improvement to pass round 1
    constexpr double CONFIRM2_THRESHOLD = 0.05; // min improvement to bake in

    std::cerr << "Optimising mulligan profile...\n";

    // ---- Phase 1: baseline scan ------------------------------------------------
    MulliganProfile             baseline_profile;
    std::vector<GameRecord>     scan_records = RunForRecords(
        deck, baseline_profile, SCAN_GAMES, opt_seed, max_turns);
    double baseline_avg = AverageWinTurn(scan_records, max_turns);

    std::map<std::string, double> impacts = ScanCardImpacts(scan_records, max_turns);

    // Sort candidates by correlation strength (descending).
    std::vector<std::pair<std::string, double>> sorted;
    sorted.reserve(impacts.size());
    for (const std::pair<const std::string, double>& kv : impacts) { sorted.push_back(kv); }
    std::sort(sorted.begin(), sorted.end(),
        [](const std::pair<std::string, double>& a, const std::pair<std::string, double>& b)
        {
            return a.second > b.second;
        });

    // ---- Phase 2: multi-round confirmation of candidates -----------------------
    MulliganProfile              working_profile;
    std::vector<RequiredPieceFlag> flags;
    int                          confirmed = 0;
    uint64_t confirm_seed = opt_seed + SCAN_GAMES;

    for (const std::pair<std::string, double>& kv : sorted)
    {
        if (kv.second < SCAN_THRESHOLD) { break; }
        if (confirmed >= MAX_CANDIDATES) { break; }

        const std::string& card = kv.first;

        // Skip lands — they're controlled by the grid search, not required_pieces.
        std::optional<CardDefinition> opt = CardDatabase::Instance().Lookup(card);
        if (opt && opt->card.IsLand()) { continue; }

        std::cerr << "  Candidate: " << card
                  << " (correlation " << kv.second << " turns) — confirming...\n";

        // Build test profile that also requires this card.
        MulliganProfile test_profile = working_profile;
        test_profile.required_pieces.push_back(card);

        // Round 1
        std::vector<GameRecord> c1 = RunForRecords(
            deck, test_profile, CONFIRM1_GAMES, confirm_seed, max_turns);
        double avg1       = AverageWinTurn(c1, max_turns);
        double improve1   = baseline_avg - avg1;
        confirm_seed     += CONFIRM1_GAMES;

        if (improve1 < CONFIRM1_THRESHOLD)
        {
            std::cerr << "    Round 1 failed (improvement " << improve1 << " < "
                      << CONFIRM1_THRESHOLD << ") — skipping.\n";
            continue;
        }

        // Round 2: deeper confirmation with a fresh seed
        std::vector<GameRecord> c2 = RunForRecords(
            deck, test_profile, CONFIRM2_GAMES, confirm_seed, max_turns);
        double avg2      = AverageWinTurn(c2, max_turns);
        double improve2  = baseline_avg - avg2;
        confirm_seed    += CONFIRM2_GAMES;

        if (improve2 < CONFIRM2_THRESHOLD)
        {
            std::cerr << "    Round 2 failed (improvement " << improve2 << " < "
                      << CONFIRM2_THRESHOLD << ") — skipping.\n";
            continue;
        }

        std::cerr << "    Confirmed (round1 +" << improve1
                  << ", round2 +" << improve2 << " turns).\n";

        working_profile.required_pieces.push_back(card);
        RequiredPieceFlag flag;
        flag.card_name        = card;
        flag.correlation      = kv.second;
        flag.baseline_win_turn = baseline_avg;
        flag.win_turn_required = avg2;
        flag.improvement      = improve2;
        flags.push_back(flag);
        ++confirmed;
    }

    // ---- Phase 2b: color source requirements -----------------------------------
    std::cerr << "  Analysing color source requirements...\n";
    std::map<Color, int> max_pips = FindMaxPipsPerColor(deck);
    for (const std::pair<const Color, int>& kv : max_pips)
    {
        if (kv.second == 0) { continue; }
        int confirmed = ConfirmColorSourceMin(
            deck, working_profile, kv.first, kv.second,
            baseline_avg, confirm_seed, max_turns);
        if (confirmed > 0)
        {
            working_profile.min_color_sources[kv.first] = confirmed;
        }
    }

    // ---- Phase 3: land parameter grid search -----------------------------------
    std::cerr << "  Grid-searching land parameters...\n";
    double best_win_turn = 0.0;
    MulliganProfile optimal = GridSearchLands(
        deck, working_profile, GRID_GAMES, confirm_seed, max_turns, best_win_turn);

    std::cerr << "  Done. Optimal profile: min_lands=" << optimal.min_lands
              << " max_lands=" << optimal.max_lands
              << " stop_at=" << optimal.stop_at
              << " curve_check=" << CurveCheckToString(optimal.curve_check)
              << " bottom_order=" << BottomOrderToString(optimal.bottom_order) << "\n";

    return {optimal, flags};
}

// ============================================================
// Public API
// ============================================================

AnalysisResult AnalyzerEngine::Run(const Decklist& deck, int num_games,
                                    uint64_t base_seed, int max_turns,
                                    int depth, int timeout_ms)
{
    OptResult opt = OptimizeMulligan(deck, base_seed, max_turns);
    AnalysisResult result = RunMonteCarlo(
        deck, num_games, base_seed, max_turns, depth, timeout_ms, opt.profile);
    result.mulligan_profile = opt.profile;
    result.mulligan_flags   = opt.flags;
    return result;
}

AnalysisResult AnalyzerEngine::RunMonteCarlo(
    const Decklist& deck, int num_games,
    uint64_t base_seed, int max_turns,
    int depth, int timeout_ms,
    const MulliganProfile& profile)
{
    AIEngine   ai(profile, depth, timeout_ms);
    GameEngine engine(ai);

    RunResult run;
    run.seed         = base_seed;
    run.games_played = num_games;
    run.win_turns.reserve(num_games);

    for (int i = 0; i < num_games; ++i)
    {
        GameState state = GoldFishRunner::SetupGame(deck, base_seed + static_cast<uint64_t>(i));
        int win_turn = engine.RunGame(state, max_turns);
        run.win_turns.push_back(win_turn);
        if (win_turn > 0) { ++run.games_won; }
    }

    if (run.games_won > 0)
    {
        long long sum = 0;
        int count = 0;
        for (int t : run.win_turns) { if (t > 0) { sum += t; ++count; } }
        run.average_win_turn = static_cast<double>(sum) / count;
    }

    AnalysisResult result;
    result.deck_name        = "deck";
    result.seed             = run.seed;
    result.games_played     = run.games_played;
    result.games_won        = run.games_won;
    result.average_win_turn = run.average_win_turn;
    return result;
}

// ============================================================
// JSON serialisation
// ============================================================

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

    j["mulligan_profile"] = MulliganProfileToJsonObj(result.mulligan_profile);

    // Flags for auto-added required pieces
    json flags = json::array();
    for (const RequiredPieceFlag& f : result.mulligan_flags)
    {
        json flag;
        flag["card"]              = f.card_name;
        flag["correlation_turns"] = f.correlation;
        flag["baseline_win_turn"] = f.baseline_win_turn;
        flag["win_turn_required"] = f.win_turn_required;
        flag["improvement"]       = f.improvement;
        flag["message"]           = "Automatically added to required_pieces — "
                                    "verify this is correct for your deck";
        flags.push_back(flag);
    }
    j["mulligan_flags"] = flags;

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
