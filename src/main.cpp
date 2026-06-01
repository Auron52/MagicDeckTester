#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <random>
#include "deck/DeckLoader.h"
#include "runner/GoldFishRunner.h"

static void printUsage(const char* prog)
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
        printUsage(argv[0]);
        return 1;
    }

    std::filesystem::path deckPath = argv[1];
    int      numGames    = 10000;
    int      maxTurns    = 20;
    uint64_t seed        = 0;
    bool     seedProvided = false;

    for (int i = 2; i < argc - 1; ++i)
    {
        std::string flag = argv[i];
        try
        {
            if (flag == "--games")
            {
                numGames = std::stoi(argv[i + 1]);
            }
            else if (flag == "--seed")
            {
                seed          = std::stoull(argv[i + 1]);
                seedProvided  = true;
            }
            else if (flag == "--max-turns")
            {
                maxTurns = std::stoi(argv[i + 1]);
            }
        }
        catch (...)
        {
            std::cerr << "Invalid value for " << flag << ": " << argv[i + 1] << "\n";
            return 1;
        }
    }

    if (!seedProvided)
    {
        std::random_device rd;
        seed = (static_cast<uint64_t>(rd()) << 32) | rd();
    }

    try
    {
        Decklist deck = DeckLoader::loadFromFile(deckPath);
        std::cout << "Loaded " << deck.mainboard.size() << " mainboard card(s)";
        if (!deck.sideboard.empty())
        {
            std::cout << " + " << deck.sideboard.size() << " sideboard card(s)";
        }
        std::cout << "\n";

        GoldFishRunner runner;
        RunResult result = runner.run(deck, numGames, seed, maxTurns);

        std::cout << "Seed         : " << result.seed << "\n";
        std::cout << "Games played : " << result.gamesPlayed << "\n";
        std::cout << "Games won    : " << result.gamesWon
                  << " (" << (100.0 * result.gamesWon / result.gamesPlayed) << "%)\n";
        if (result.gamesWon > 0)
        {
            std::cout << "Avg win turn : " << result.averageWinTurn << "\n";
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
