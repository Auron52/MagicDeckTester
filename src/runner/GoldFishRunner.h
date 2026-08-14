#pragma once
#include "../deck/DeckLoader.h"
#include "../core/GameState.h"
#include "../ai/MulliganProfile.h"
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct RunResult
{
    uint64_t seed          = 0;     // base seed used; pass to --seed to reproduce this run
    double average_win_turn = 0.0;  // mean over WINS only (internal / future 1v1)
    double avg_turns        = 0.0;  // THE goldfish metric: mean turn-to-win, unwon = max_turns+1
    int    games_won        = 0;
    int    games_played     = 0;
    std::vector<int> win_turns;  // per-game result; -1 = did not win within max_turns
};

// avg (turns): the goldfish success metric -- mean turn-to-win, where an unwon game (no lethal by
// max_turns) is scored as max_turns+1. Win/loss is never reported on its own: a goldfishing loss is
// an ARBITRARY horizon threshold, and reporting it makes readers treat it as the priority metric.
// Folding unwon games in at the horizon (max_turns+1) keeps it on the turn scale. Lower is better.
// This matches the long-standing convention (PlayOutWinTurn returns max_turns+1 for an unwon game).
inline double ComputeAvgTurns(const std::vector<int>& win_turns, int max_turns)
{
    if (win_turns.empty()) { return 0.0; }
    const double loss_turns = static_cast<double>(max_turns + 1);
    double sum = 0.0;
    for (int wt : win_turns) { sum += (wt > 0) ? static_cast<double>(wt) : loss_turns; }
    return sum / static_cast<double>(win_turns.size());
}

// PER-JOB deck numbering, so ONE pooled batch can run every combination of a comparison.
//
// The supplied-numbering mode (MTG_DECK_NUMBERING, see GoldFishRunner.cpp) is a process-wide static,
// which means one process can only ever BE one combination -- and each combination needs a DIFFERENT
// map. That would force one `mtg --batch` per combination, i.e. exactly the per-item loop CLAUDE.md
// forbids: every invocation pays its own load-imbalance tail and separate pools never share threads.
// Same problem, and the same fix, as valuearm::Arm (see ai/ValueArm.h).
//
// thread_local because a batch worker owns its thread for a whole job and the search does not spawn
// threads. nullptr (the default, and what Clear() restores) means "use the env static", so single
// runs, the regression harness and every pre-existing manifest stay byte-identical.
namespace decknumbering
{
inline thread_local const std::map<std::string, std::vector<int>>* t_map = nullptr;
inline void Clear() { t_map = nullptr; }
}

class GoldFishRunner
{
public:
    // base_seed + gameIndex is the seed for each individual game (seeding contract).
    // Caller is responsible for generating base_seed — use std::random_device for a
    // non-reproducible run, or a stored seed value to replay a previous run.
    // log_dir: if non-empty, write one JSON game log per game into that directory.
    // base_game_index: first game's index for spawn-pattern selection (game i uses
    //   pattern (base_game_index + i) % 10). Pass a non-zero value to replay a
    //   specific game from a larger run with the correct opponent board.
    RunResult Run(const Decklist& deck, int num_games, uint64_t base_seed, int max_turns = 8,
                  const MulliganProfile& profile = MulliganProfile::DefaultProfile(),
                  const std::filesystem::path& log_dir = {},
                  int base_game_index = 0,
                  int lookahead_depth = 0,
                  int timeout_ms = 0,
                  int num_threads = 0,    // 0 = use hardware_concurrency
                  // (bottoming is derived from lookahead_depth: on iff depth>0)
                  // Forced-mulligan replay (isolates PLAY from mulligan/bottoming): when
                  // forced_mull_count >= 0, every game keeps at exactly that many mulligans and
                  // bottoms exactly forced_bottom (by card m_number), reconstructing a recorded
                  // opening hand and letting the autonomous search play it out. Inert (<0) by
                  // default -- goldfish GT byte-identical when unset. Meaningful only for a single
                  // game (one spec), so pair with num_games==1 + --seed/--game-index.
                  int forced_mull_count = -1,
                  std::vector<int> forced_bottom = {});

    // Build the initial GameState for a single game with the given seed.
    // Shared by the runner and the analyzer.
    static GameState SetupGame(const Decklist& deck, uint64_t seed);

    // Adds the opponent's blocker pattern for this game index (10-game cycle).
    // Exposed so diagnostics can reproduce the runner's per-game setup exactly.
    static void PopulateOpponentSpawns(GameState& state, int game_index);

    // Card numbering utilities — exposed for diagnostics.
    static std::map<std::string, std::vector<int>> BuildCardNumbering(const Decklist& deck);
    static void AssignCardNumbers(GameState& state,
                                   const std::map<std::string, std::vector<int>>& numbering);

    // True if the deck has any card whose value depends on the post-combat (second)
    // main phase — currently spectacle cards (combat damage unlocks the cheaper cost).
    // Such decks enable AIEngine::SetSearchPostCombat; everything else skips the
    // second main. Shared so the runner and analyzer enable it identically.
    static bool DeckUsesSecondMain(const Decklist& deck);
    // Deck-level input to the main-phase classifier: any attack-feeding card in the deck?
    // Stamped onto GameState::deck_feeds_combat by SetupGame (see the .cpp note).
    static bool DeckFeedsCombat(const Decklist& deck);
};
