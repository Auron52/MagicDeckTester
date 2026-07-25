#include "AnalyzerEngine.h"
#include "KeepModelTrainer.h"
#include "../core/GameEngine.h"
#include "../ai/AIEngine.h"
#include "../ai/MulliganProfile.h"
#include "../ai/MulliganProfileIO.h"
#include "../cards/CardDatabase.h"
#include "../core/HardwareConcurrency.h"
#include "../runner/GoldFishRunner.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <numeric>
#include <set>
#include <sstream>
#include <thread>

using json = nlohmann::json;

namespace
{
// Search depth/budget the analyzer evaluates at -- BOTH the mulligan-profile
// optimisation (scan / confirm / land grid) and the card scoring. It must match the
// depth the deck is actually PLAYED at, or the mulligan profile and card values are
// calibrated for a different game (a combo hand weak under greedy d0 but strong under
// d5 search would be wrongly mulliganed). Optimisation historically ran at d0 for
// speed; now unified at d5 for accuracy (perf cost accepted -- the land grid is the
// dominant ~288k games/deck). Change both together.
// Default d5; overridable via MTG_ANALYZE_DEPTH for decks too expensive to profile at d5 in a
// reasonable time (e.g. wide-hand combo decks whose per-node subset enumeration explodes). A lower
// depth gives a COARSER profile (calibrated for that depth's play) but is far faster; land/mulligan
// choices are largely depth-insensitive, and a one-turn combo is already found by Solve at d0.
// UNSET -> 5 -> byte-identical to every existing committed profile. Read once at startup.
const int ANALYSIS_DEPTH = []{
    const char* e = std::getenv("MTG_ANALYZE_DEPTH");
    return (e && *e) ? std::max(0, std::atoi(e)) : 5;
}();
// MTG_ANALYZE_SCALE divides every phase's game count, trading profile precision (higher per-cell
// variance) for speed. DEFAULT 2: the full-resolution (scale-1) ~1.15M-game grid overran a single
// overnight window even for burn, so a fresh profile now defaults to half-resolution to fit the
// night. Set MTG_ANALYZE_SCALE=1 to restore full resolution (and the resolution the committed
// decks/*.profile.json were generated at). Floored so a phase never drops below a usable sample.
// Read once at startup. Does NOT affect play or the regression suite -- only analyzer output.
const int ANALYZE_SCALE = []{
    const char* e = std::getenv("MTG_ANALYZE_SCALE");
    int s = (e && *e) ? std::atoi(e) : 2;
    return s < 1 ? 1 : s;
}();
inline int Scaled(int games, int floor_games = 200)
{
    return std::max(floor_games, games / ANALYZE_SCALE);
}
// MTG_KEEP_MODEL=1 turns ON the analyzer-generated mulligan KEEP model (Phase 2): after the
// static-profile optimisation, fit an interpretable keep decision tree and emit it in the profile,
// REPLACING the static-filter keep path at runtime (see KeepModelTrainer / KeepModel). DEFAULT OFF
// so regenerating a deck's profile is byte-identical to today until the model is explicitly opted
// into and validated. Read once at startup.
const bool BUILD_KEEP_MODEL = []{
    const char* e = std::getenv("MTG_KEEP_MODEL");
    return e && *e && std::string(e) != "0";
}();
constexpr int ANALYSIS_BUDGET = 20;    // deterministic virtual-ms node budget; matches the
                                       // regression suite's proven-sufficient d5 budget (the
                                       // node budget is iterative-deepening refinement WITHIN
                                       // d5, not depth). ~10x faster than the old scoring's 200.

int HardwareThreads()
{
    // Affinity-aware (see HardwareConcurrency.h) -- avoids a transiently-low
    // online-CPU reading at launch under-parallelizing the analyzer.
    return concurrency_util::AffinityCpuCount();
}

// Run fn(i) for every i in [0, n) across worker threads that each pull the next
// index from a shared atomic counter (dynamic self-scheduling, like
// GoldFishRunner). All cores stay busy until fewer items remain than threads, so
// a single slow item can only tail by one core rather than stranding a whole
// static chunk. fn must write only to slot i (no shared mutable state), and index
// i must fully determine slot i's work, so completion order cannot change any
// result -- the caller's reduction must likewise be order-independent (or done in
// a fixed order) to stay deterministic.
template <class Fn>
void ParallelFor(int n, Fn fn)
{
    if (n <= 0) { return; }
    int nth = std::min(HardwareThreads(), n);
    std::atomic<int> next{0};
    std::vector<std::thread> threads;
    threads.reserve(nth);
    for (int t = 0; t < nth; ++t)
    {
        threads.emplace_back([&]()
        {
            for (;;)
            {
                int i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= n) { break; }
                fn(i);
            }
        });
    }
    for (std::thread& th : threads) { th.join(); }
}
} // namespace

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
    int num_games, uint64_t seed, int max_turns,
    int depth, int timeout_ms)
{
    // Dynamic self-scheduling over games (see ParallelFor / GoldFishRunner): each
    // worker keeps its own AIEngine and pulls the next game index from a shared
    // atomic counter, so the deep scoring pass no longer strands one thread on a
    // slow game at the tail of a static chunk. Lossless and deterministic: game gi
    // is seeded by gi alone (seed + gi) and written to records[gi], so which worker
    // runs it cannot change the result. The budget is a deterministic node count
    // (virtual ms), thread-invariant by construction, so no per-thread scaling.
    std::vector<GameRecord> records(num_games);
    if (num_games <= 0) { return records; }

    const bool needs_second_main = GoldFishRunner::DeckUsesSecondMain(deck);
    int nth = std::min(HardwareThreads(), num_games);
    std::atomic<int> next_game{0};
    std::vector<std::thread> threads;
    threads.reserve(nth);

    for (int t = 0; t < nth; ++t)
    {
        threads.emplace_back([&]()
        {
            AIEngine   ai(profile, depth, timeout_ms);
            ai.SetSearchPostCombat(needs_second_main);
            GameEngine engine(ai);
            for (;;)
            {
                int gi = next_game.fetch_add(1, std::memory_order_relaxed);
                if (gi >= num_games) { break; }
                GameState state = GoldFishRunner::SetupGame(deck, seed + static_cast<uint64_t>(gi));
                state.vial_target_mv     = profile.vial_target_mv;
                records[gi].win_turn     = engine.RunGame(state, max_turns);
                records[gi].opening_hand = ai.GetKeptOpeningHand();
            }
        });
    }
    for (std::thread& th : threads) { th.join(); }
    return records;
}


std::map<std::string, std::vector<double>> AnalyzerEngine::ComputeCardScores(
    const std::vector<GameRecord>& records, int max_turns, double* threshold_out,
    double* hs_mean_out, double* hs_std_out)
{
    const int N           = static_cast<int>(records.size());
    const int MIN_SAMPLES = 30;   // minimum games per (card, count) bucket for a reliable estimate
    const int MAX_COPIES  = 4;    // marginals beyond 4 copies in opening hand are noise

    // Precompute win turns once (losses count as max_turns + 1).
    std::vector<double> win_turns(N);
    for (int i = 0; i < N; ++i)
    {
        win_turns[i] = (records[i].win_turn > 0)
                       ? static_cast<double>(records[i].win_turn)
                       : static_cast<double>(max_turns + 1);
    }

    // Collect all card names seen across all opening hands.
    std::set<std::string> all_cards;
    for (const GameRecord& r : records)
        for (const std::string& c : r.opening_hand) { all_cards.insert(c); }

    std::map<std::string, std::vector<double>> result;

    for (const std::string& card : all_cards)
    {
        // Group win turns by how many copies of this card were in the opening hand.
        std::map<int, std::vector<double>> groups;  // count -> [win_turns]
        for (int i = 0; i < N; ++i)
        {
            int count = 0;
            for (const std::string& c : records[i].opening_hand)
                if (c == card) { ++count; }
            groups[count].push_back(win_turns[i]);
        }

        // Compute marginals: marginal[k] = avg_wt(k-1 copies) - avg_wt(k copies).
        // Stop at the first copy count with too few samples.
        std::vector<double> marginals;
        for (int k = 1; k <= MAX_COPIES; ++k)
        {
            auto it_k   = groups.find(k);
            auto it_km1 = groups.find(k - 1);
            if (it_k   == groups.end() || static_cast<int>(it_k->second.size())   < MIN_SAMPLES) { break; }
            if (it_km1 == groups.end() || static_cast<int>(it_km1->second.size()) < MIN_SAMPLES) { break; }

            double avg_k   = std::accumulate(it_k->second.begin(),   it_k->second.end(),   0.0) / it_k->second.size();
            double avg_km1 = std::accumulate(it_km1->second.begin(), it_km1->second.end(), 0.0) / it_km1->second.size();
            marginals.push_back(avg_km1 - avg_k);
        }

        if (!marginals.empty()) { result[card] = std::move(marginals); }
    }

    // Compute hand scores over all records and derive the threshold.
    if (threshold_out && !result.empty())
    {
        std::vector<double> hand_scores;
        hand_scores.reserve(N);
        for (int i = 0; i < N; ++i)
        {
            std::map<std::string, int> counts;
            for (const std::string& c : records[i].opening_hand) { ++counts[c]; }

            double score = 0.0;
            for (const auto& kv : counts)
            {
                auto it = result.find(kv.first);
                if (it == result.end()) { continue; }
                int copies = std::min(kv.second, static_cast<int>(it->second.size()));
                for (int k = 0; k < copies; ++k) { score += std::max(0.0, it->second[k]); }
            }
            hand_scores.push_back(score);
        }

        double mean = std::accumulate(hand_scores.begin(), hand_scores.end(), 0.0) / hand_scores.size();
        double variance = 0.0;
        for (double s : hand_scores) { variance += (s - mean) * (s - mean); }
        variance /= hand_scores.size();
        double stddev = std::sqrt(variance);
        *threshold_out = mean - 1.5 * stddev;
        if (hs_mean_out) { *hs_mean_out = mean; }
        if (hs_std_out)  { *hs_std_out  = stddev; }
    }

    return result;
}


// ============================================================
// Color source analysis helpers
// ============================================================



// ============================================================
// Mulligan optimiser
// ============================================================

int AnalyzerEngine::ComputeVialTargetMv(const Decklist& deck)
{
    bool has_vial = false;
    for (const Card& c : deck.mainboard)
        if (c.m_name == "Aether Vial") { has_vial = true; break; }
    if (!has_vial) { return 0; }

    std::map<int, int> mv_count;
    for (const Card& c : deck.mainboard)
    {
        const CardDefinition* cdef = CardDatabase::Instance().LookupCached(c);
        if (!cdef || !cdef->card.IsCreature()) { continue; }
        int mv = cdef->card.m_mana_cost.ManaValue();
        if (mv > 0) { ++mv_count[mv]; }
    }
    if (mv_count.empty()) { return 0; }
    int best_mv = 0, best_cnt = 0;
    for (const auto& kv : mv_count)
    {
        if (kv.second > best_cnt || (kv.second == best_cnt && kv.first > best_mv))
        { best_cnt = kv.second; best_mv = kv.first; }
    }
    return best_mv;
}

AnalyzerEngine::OptResult AnalyzerEngine::OptimizeMulligan(
    const Decklist& deck, uint64_t seed, int max_turns, int vial_target_mv)
{
    // Use a seed far from the user's analysis seed to avoid overfitting
    // to the same shuffle sequences.
    constexpr uint64_t OPT_SEED_OFFSET = 1'000'000ULL;
    uint64_t opt_seed = seed + OPT_SEED_OFFSET;

    // Analyze produces a CARD-SCORES-ONLY baseline profile (user policy 2026-07-22; analyze-deck
    // skill Stage 4): per-card scores on the default keep, the hand-score gate DECLINED (NO_GATE), and
    // the default land window. The expensive mulligan OPTIMISATION that used to run here -- the baseline
    // scan, required-piece confirmation, colour-source confirmation, and the joint land x hand-score-gate
    // grid (hundreds of thousands of d5 games) -- was DELETED. That work is the separate, commit-bound,
    // USER-INITIATED exhaustive-mulligan stage (mulligan-profile.md / ExhaustiveKeep), never part of
    // analyze. See git history for the removed helpers (ScanCardImpacts / ConfirmColorSourceMin /
    // GridSearchLands / FindMaxPipsPerColor) and the MTG_CARD_SCORES_ONLY / MTG_SKIP_GRID gates.
    constexpr double NO_GATE = -1e18;   // ComputeHandScore is >= 0, so every hand clears it; serialises to JSON.

    MulliganProfile profile;
    profile.vial_target_mv = vial_target_mv;

    std::cerr << "Computing card scores...\n";
    constexpr uint64_t SCORING_OFFSET = 3'000'000ULL;
    const int          SCORING_GAMES  = Scaled(2000);
    std::cerr << "  Card scores (" << SCORING_GAMES << " games, depth=" << ANALYSIS_DEPTH << ")...\n";
    std::vector<GameRecord> scoring_records = RunForRecords(
        deck, profile, SCORING_GAMES, seed + SCORING_OFFSET, max_turns,
        ANALYSIS_DEPTH, ANALYSIS_BUDGET);
    double recommended_thr = 0.0, hs_mean = 0.0, hs_std = 0.0;
    profile.card_scores          = ComputeCardScores(scoring_records, max_turns,
                                                     &recommended_thr, &hs_mean, &hs_std);
    profile.hand_score_threshold = NO_GATE;

    std::cerr << "  Done. Card-scores-only profile: " << profile.card_scores.size()
              << " cards, no hand-score gate, default land window (min_lands=" << profile.min_lands
              << " max_lands=" << profile.max_lands << ").\n";

    return {profile, {}};
}

// ============================================================
// Public API
// ============================================================

AnalysisResult AnalyzerEngine::Run(const Decklist& deck, uint64_t base_seed, int max_turns)
{
    // The analyzer's sole job is PREPARATION: produce the deck's profile (optimised
    // mulligan + per-card scores). It deliberately does NOT run a headline win-rate
    // simulation -- that EVALUATION belongs to the regression suite (mtg.exe), which
    // schedules games far better. So there are no --games/--depth/--budget knobs:
    // the scoring methodology below is fixed.
    AnalysisResult result;
    result.seed = base_seed;

    int vial_target_mv = ComputeVialTargetMv(deck);

    // OptimizeMulligan now computes the per-card scores and chooses the hand-score
    // threshold JOINTLY with the land params (the threshold is a grid axis), so the
    // returned profile already carries card_scores + hand_score_threshold. There is no
    // separate bolt-on scoring pass -- that ordering (land grid first, gate derived and
    // attached afterward) is exactly what let the two over-mulligan in combination.
    OptResult opt = OptimizeMulligan(deck, base_seed, max_turns, vial_target_mv);
    opt.profile.vial_target_mv = vial_target_mv;
    result.mulligan_profile     = opt.profile;
    result.mulligan_flags       = opt.flags;
    result.card_scores          = opt.profile.card_scores;
    result.hand_score_threshold = opt.profile.hand_score_threshold;
    std::cerr << "  Final hand-score threshold: " << result.hand_score_threshold << "\n";

    // Phase 2 (opt-in): fit the interpretable keep model on the clairvoyant rollout oracle and
    // attach it to the profile. When present it REPLACES the static keep path at runtime; the
    // separately-loaded human constraints still wrap it as a hard guard. Off by default so a
    // plain regeneration is byte-identical to today (see BUILD_KEEP_MODEL).
    if (BUILD_KEEP_MODEL)
    {
        // MTG_KEEP_GAMES overrides the keep-model sample size (distinct opening hands), decoupled
        // from the land grid's scale -- a robust keep policy wants grid-comparable hand counts.
        const int keep_games = []{ const char* s = std::getenv("MTG_KEEP_GAMES");
                                   return (s && *s) ? std::max(200, std::atoi(s)) : 0; }();
        KeepModelTrainConfig kc;
        kc.depth     = ANALYSIS_DEPTH;
        kc.budget_ms = ANALYSIS_BUDGET;
        kc.max_turns = max_turns;
        kc.games     = keep_games ? keep_games : Scaled(2000);
        kc.seed      = base_seed;
        result.mulligan_profile.keep_model =
            BuildKeepModel(deck, result.mulligan_profile, result.card_scores, kc);
    }

    return result;
}

// ============================================================
// JSON serialisation
// ============================================================

std::string AnalysisResultToJson(const AnalysisResult& result)
{
    // The analyzer is a profile generator, not an evaluator: it emits the deck
    // identity, the optimised mulligan profile, the auto-required-piece flags, and
    // the per-card scores. Win-rate/avg-win-turn metrics now come from the
    // regression suite (mtg.exe), so they are deliberately not reported here.
    json j;
    j["deck_name"]        = result.deck_name;
    j["seed"]             = result.seed;
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

    if (!result.card_scores.empty())
    {
        json cs = json::object();
        for (const auto& kv : result.card_scores)
        {
            json arr = json::array();
            for (double v : kv.second) { arr.push_back(v); }
            cs[kv.first] = arr;
        }
        j["card_scores"]          = cs;
        j["hand_score_threshold"] = result.hand_score_threshold;
    }

    return j.dump(2);
}
