#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <random>
#include <algorithm>
#include <vector>
#include "deck/DeckLoader.h"
#include "cards/CardDatabase.h"
#include "runner/GoldFishRunner.h"
#include "ai/AIEngine.h"
#include "core/GameEngine.h"
#include "ai/MulliganProfileIO.h"

static void PrintUsage(const char* prog)
{
    std::cerr << "Usage: " << prog
              << " <deckfile> [--games N] [--seed S] [--max-turns T]"
                 " [--depth D] [--budget-ms M] [--profile path] [--log-dir path] [--cards-json path]\n"
              << "  <deckfile>      Plain text (.txt) or Cockatrice (.cod) decklist\n"
              << "  --games N       Number of games to simulate (default: 10000)\n"
              << "  --seed S        Base RNG seed (omit to generate randomly)\n"
              << "  --max-turns T   Maximum turns before declaring no-win (default: 20)\n"
              << "  --depth D       Lookahead depth (default: 0; higher = stronger but slower)\n"
              << "  --budget-ms M   Per-decision search budget in deterministic 'virtual ms';\n"
              << "                  0 = unlimited (default: 0). Alias: --timeout-ms\n"
              << "  --threads N     Worker threads (default: 0 = hardware_concurrency)\n"
              << "  --profile P     Path to a .profile.json file (default: auto-detect deckname.profile.json)\n"
              << "  --log-dir P     Write one JSON game log per game into this directory\n"
              << "  --cards-json P  Path to card definitions JSON (default: src/cards/data/cards.json)\n";
}

static std::vector<std::string> SortedHandNames(GameState& state)
{
    std::vector<std::string> names;
    for (const Card& c : state.ActivePlayer().hand) { names.push_back(c.m_name); }
    std::sort(names.begin(), names.end());
    return names;
}

// Plays a (post-mulligan) state to a win turn at the given lookahead depth.
// Takes state by value so the caller's copy is preserved for reuse.
static int PlayOutWinTurn(GameState state, const MulliganProfile& profile,
                          int depth, int timeout_ms, int max_turns)
{
    AIEngine   ai(profile, depth, timeout_ms);
    GameEngine engine(ai);
    int win_turn = engine.PlayOut(state, max_turns);
    return win_turn > 0 ? win_turn : max_turns + 1;
}

// Diagnostic: attribute the depth-4-worse-than-depth-3 (with --lookahead-bottoming)
// regression to its locus. For each game, run bottoming at depth 3 and depth 4
// (the keep decision is depth-independent, so both reach the same pre-bottom hand
// and identical library order — they differ only in which card bottoming chose).
// Then play the resulting state out at each depth, forming a 2x2:
//   W33 = bottom@3, play@3   W34 = bottom@3, play@4 (isolates main-phase depth)
//   W43 = bottom@4, play@3   W44 = bottom@4, play@4 (W43 isolates bottoming choice)
static void RunDepthDivergenceDiagnostic(const Decklist& deck, const MulliganProfile& profile,
                                         int num_games, uint64_t base_seed,
                                         int max_turns, int timeout_ms)
{
    int    bottom_diff       = 0;
    int    mainphase_diff    = 0;
    int    mulliganed        = 0;
    double sum33 = 0.0, sum34 = 0.0, sum43 = 0.0, sum44 = 0.0;
    int    examples_shown    = 0;
    int    mainphase_shown   = 0;
    const int MAX_EXAMPLES   = 10;

    for (int i = 0; i < num_games; ++i)
    {
        uint64_t seed = base_seed + static_cast<uint64_t>(i);

        GameState s3 = GoldFishRunner::SetupGame(deck, seed);
        s3.vial_target_mv = profile.vial_target_mv;   // match the runner's per-game setup
        GoldFishRunner::PopulateOpponentSpawns(s3, i);
        AIEngine  bot3(profile, 3, timeout_ms);
        bot3.SetLookaheadBottoming(true);
        bot3.HandleMulligan(s3, max_turns);
        std::vector<std::string> hand3 = SortedHandNames(s3);

        GameState s4 = GoldFishRunner::SetupGame(deck, seed);
        s4.vial_target_mv = profile.vial_target_mv;
        GoldFishRunner::PopulateOpponentSpawns(s4, i);
        AIEngine  bot4(profile, 4, timeout_ms);
        bot4.SetLookaheadBottoming(true);
        bot4.HandleMulligan(s4, max_turns);
        std::vector<std::string> hand4 = SortedHandNames(s4);

        bool differs = (hand3 != hand4);
        if (differs)                                  { ++bottom_diff; }
        if (static_cast<int>(hand3.size()) < 7)       { ++mulliganed; }

        int W33 = PlayOutWinTurn(s3, profile, 3, timeout_ms, max_turns);
        int W34 = PlayOutWinTurn(s3, profile, 4, timeout_ms, max_turns);
        int W43 = PlayOutWinTurn(s4, profile, 3, timeout_ms, max_turns);
        int W44 = PlayOutWinTurn(s4, profile, 4, timeout_ms, max_turns);

        sum33 += W33; sum34 += W34; sum43 += W43; sum44 += W44;

        // Main-phase divergence: same kept hand (s3), but play depth changes the
        // win turn -> the depth-3 and depth-4 main-phase decisions differed.
        if (W34 != W33)
        {
            ++mainphase_diff;
            if (mainphase_shown < MAX_EXAMPLES)
            {
                ++mainphase_shown;
                std::cout << "[mainphase] seed " << seed << "  W33=" << W33
                          << " W34=" << W34 << " (spawn pattern " << (i % 10) << ")  hand: ";
                for (const std::string& n : hand3) { std::cout << n << " | "; }
                std::cout << "\n";
            }
        }

        if (differs && examples_shown < MAX_EXAMPLES)
        {
            ++examples_shown;
            std::cout << "[bottoming] seed " << seed << " (W33=" << W33
                      << " W34=" << W34 << " W43=" << W43 << " W44=" << W44 << ")\n";
            std::cout << "  bottom@3 keeps: ";
            for (const std::string& n : hand3) { std::cout << n << " | "; }
            std::cout << "\n  bottom@4 keeps: ";
            for (const std::string& n : hand4) { std::cout << n << " | "; }
            std::cout << "\n";
        }
    }

    double n = static_cast<double>(num_games);
    std::cout << "\n=== DEPTH DIVERGENCE (" << num_games << " games, timeout "
              << timeout_ms << "ms, --lookahead-bottoming) ===\n";
    std::cout << "bottoming differs (d3 vs d4 kept hand): " << bottom_diff
              << " (" << (100.0 * bottom_diff / n) << "%)\n";
    std::cout << "main-phase differs (W34 != W33):        " << mainphase_diff
              << " (" << (100.0 * mainphase_diff / n) << "%)\n";
    std::cout << "mulliganed (kept < 7):                  " << mulliganed
              << " (" << (100.0 * mulliganed / n) << "%)\n";
    std::cout << "mean W33 (bottom@3 play@3): " << (sum33 / n) << "\n";
    std::cout << "mean W34 (bottom@3 play@4): " << (sum34 / n)
              << "   [main-phase effect (W34-W33): " << ((sum34 - sum33) / n) << "]\n";
    std::cout << "mean W43 (bottom@4 play@3): " << (sum43 / n)
              << "   [bottoming effect  (W43-W33): " << ((sum43 - sum33) / n) << "]\n";
    std::cout << "mean W44 (bottom@4 play@4): " << (sum44 / n)
              << "   [total             (W44-W33): " << ((sum44 - sum33) / n) << "]\n";
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        PrintUsage(argv[0]);
        return 1;
    }

    std::filesystem::path deck_path    = argv[1];
    std::filesystem::path cards_json   = "src/cards/data/cards.json";
    std::filesystem::path profile_path;
    std::filesystem::path log_dir;
    int      num_games      = 10000;
    int      max_turns      = 20;
    int      base_game_index = 0;
    int      lookahead_depth = 0;
    int      timeout_ms     = 0;
    int      num_threads    = 0;
    uint64_t seed           = 0;
    bool     seed_provided  = false;
    bool     lookahead_bottoming = false;
    bool     diag_depth     = false;

    for (int i = 2; i < argc; ++i)
    {
        std::string flag = argv[i];
        if (flag == "--lookahead-bottoming") { lookahead_bottoming = true; continue; }
        if (flag == "--diag-depth")          { diag_depth = true; continue; }
        try
        {
            if (i + 1 < argc)
            {
                if (flag == "--games")
                {
                    num_games = std::stoi(argv[++i]);
                }
                else if (flag == "--seed")
                {
                    seed          = std::stoull(argv[++i]);
                    seed_provided = true;
                }
                else if (flag == "--max-turns")
                {
                    max_turns = std::stoi(argv[++i]);
                }
                else if (flag == "--profile")
                {
                    profile_path = argv[++i];
                }
                else if (flag == "--log-dir")
                {
                    log_dir = argv[++i];
                }
                else if (flag == "--game-index")
                {
                    base_game_index = std::stoi(argv[++i]);
                }
                else if (flag == "--depth")
                {
                    lookahead_depth = std::stoi(argv[++i]);
                }
                else if (flag == "--timeout-ms" || flag == "--budget-ms")
                {
                    // Deterministic search budget in "virtual ms" (see SearchBudget).
                    // --timeout-ms kept as a back-compat alias for the same knob.
                    timeout_ms = std::stoi(argv[++i]);
                }
                else if (flag == "--threads")
                {
                    num_threads = std::stoi(argv[++i]);
                }
                else if (flag == "--cards-json")
                {
                    cards_json = argv[++i];
                }
            }
        }
        catch (...)
        {
            std::cerr << "Invalid value for " << flag << ": " << argv[i] << "\n";
            return 1;
        }
    }

    if (!seed_provided)
    {
        std::random_device rd;
        seed = (static_cast<uint64_t>(rd()) << 32) | rd();
    }

    try
    {
        if (std::filesystem::exists(cards_json))
        {
            CardDatabase::Instance().LoadFromJson(cards_json);
        }

        Decklist deck = DeckLoader::LoadFromFile(deck_path);
        std::cout << "Loaded " << deck.mainboard.size() << " mainboard card(s)";
        if (!deck.sideboard.empty())
        {
            std::cout << " + " << deck.sideboard.size() << " sideboard card(s)";
        }
        std::cout << "\n";

        // Auto-detect deckname.profile.json if no explicit --profile was given.
        if (profile_path.empty())
        {
            profile_path = deck_path.parent_path()
                         / (deck_path.stem().string() + ".profile.json");
        }

        MulliganProfile profile;
        if (std::filesystem::exists(profile_path))
        {
            profile = LoadDeckProfile(profile_path);
            std::cerr << "Loaded profile from " << profile_path.string() << "\n";
        }

        if (diag_depth)
        {
            RunDepthDivergenceDiagnostic(deck, profile, num_games, seed, max_turns, timeout_ms);
            return 0;
        }

        GoldFishRunner runner;
        RunResult result = runner.Run(deck, num_games, seed, max_turns, profile, log_dir,
                                       base_game_index, lookahead_depth, timeout_ms, num_threads,
                                       lookahead_bottoming);

        std::cout << "Seed         : " << result.seed << "\n";
        std::cout << "Games played : " << result.games_played << "\n";
        std::cout << "Games won    : " << result.games_won
                  << " (" << (100.0 * result.games_won / result.games_played) << "%)\n";
        if (result.games_won > 0)
        {
            std::cout << "Avg win turn : " << result.average_win_turn << "\n";
        }
        else
        {
            std::cout << "No wins recorded.\n";
        }

        int losses = result.games_played - result.games_won;
        if (losses > 0)
        {
            std::cout << "Losses (" << losses << "):\n";
            for (int i = 0; i < static_cast<int>(result.win_turns.size()); ++i)
            {
                if (result.win_turns[i] <= 0)
                {
                    std::cout << "  game " << i
                              << "  seed " << (result.seed + static_cast<uint64_t>(i)) << "\n";
                }
            }
        }

        if (!log_dir.empty())
        {
            std::cerr << "Game logs written to " << log_dir.string() << "\n";
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
