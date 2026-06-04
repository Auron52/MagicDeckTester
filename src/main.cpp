#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <random>
#include "deck/DeckLoader.h"
#include "cards/CardDatabase.h"
#include "runner/GoldFishRunner.h"
#include "ai/MulliganProfileIO.h"

static void PrintUsage(const char* prog)
{
    std::cerr << "Usage: " << prog
              << " <deckfile> [--games N] [--seed S] [--max-turns T]"
                 " [--depth D] [--timeout-ms M] [--profile path] [--log-dir path] [--cards-json path]\n"
              << "  <deckfile>      Plain text (.txt) or Cockatrice (.cod) decklist\n"
              << "  --games N       Number of games to simulate (default: 10000)\n"
              << "  --seed S        Base RNG seed (omit to generate randomly)\n"
              << "  --max-turns T   Maximum turns before declaring no-win (default: 20)\n"
              << "  --depth D       Lookahead depth (default: 0; higher = stronger but slower)\n"
              << "  --timeout-ms M  Per-turn time budget in ms; 0 = unlimited (default: 0)\n"
              << "  --threads N     Worker threads (default: 0 = hardware_concurrency)\n"
              << "  --profile P     Path to a .profile.json file (default: auto-detect deckname.profile.json)\n"
              << "  --log-dir P     Write one JSON game log per game into this directory\n"
              << "  --cards-json P  Path to card definitions JSON (default: src/cards/data/cards.json)\n";
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

    for (int i = 2; i < argc; ++i)
    {
        std::string flag = argv[i];
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
                else if (flag == "--timeout-ms")
                {
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

        GoldFishRunner runner;
        RunResult result = runner.Run(deck, num_games, seed, max_turns, profile, log_dir,
                                       base_game_index, lookahead_depth, timeout_ms, num_threads);

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
