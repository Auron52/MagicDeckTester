#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <random>
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"

static void PrintUsage(const char* prog)
{
    std::cerr << "Usage: " << prog
              << " <deckfile> [--games N] [--seed S] [--max-turns T]\n"
              << "  <deckfile>     Plain text (.txt) or Cockatrice (.cod) decklist\n"
              << "  --games N      Number of games to simulate (default: 10000)\n"
              << "  --seed S       Base RNG seed (omit to generate randomly)\n"
              << "  --max-turns T  Maximum turns before declaring no-win (default: 20)\n";
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        PrintUsage(argv[0]);
        return 1;
    }

    std::filesystem::path deck_path = argv[1];
    int      num_games     = 10000;
    int      max_turns     = 20;
    uint64_t seed          = 0;
    bool     seed_provided = false;

    for (int i = 2; i < argc - 1; ++i)
    {
        std::string flag = argv[i];
        try
        {
            if (flag == "--games")
            {
                num_games = std::stoi(argv[i + 1]);
            }
            else if (flag == "--seed")
            {
                seed          = std::stoull(argv[i + 1]);
                seed_provided = true;
            }
            else if (flag == "--max-turns")
            {
                max_turns = std::stoi(argv[i + 1]);
            }
        }
        catch (...)
        {
            std::cerr << "Invalid value for " << flag << ": " << argv[i + 1] << "\n";
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
        Decklist deck = DeckLoader::LoadFromFile(deck_path);
        std::cout << "Loaded " << deck.mainboard.size() << " mainboard card(s)";
        if (!deck.sideboard.empty())
        {
            std::cout << " + " << deck.sideboard.size() << " sideboard card(s)";
        }
        std::cout << "\n";

        GoldFishRunner runner;
        RunResult result = runner.Run(deck, num_games, seed, max_turns);

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
            std::cout << "No wins recorded (card logic not yet implemented — Phase 1.2)\n";
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
