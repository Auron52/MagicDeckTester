#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <random>
#include "AnalyzerEngine.h"
#include "../deck/DeckLoader.h"
#include "../cards/CardDatabase.h"
#include "../ai/MulliganProfileIO.h"

static void PrintUsage(const char* prog)
{
    std::cerr << "Usage: " << prog
              << " <deckfile> [--seed S] [--max-turns T] [--cards-json path]\n"
              << "  <deckfile>      Plain text (.txt) or Cockatrice (.cod) decklist\n"
              << "  --seed S        Base RNG seed (omit to generate randomly)\n"
              << "  --max-turns T   Maximum turns per game (default: 20)\n"
              << "  --cards-json P  Path to card definitions JSON (default: src/cards/data/cards.json)\n"
              << "\nGenerates the deck's profile (optimised mulligan + card scores) and writes\n"
                 "it to <deckname>.profile.json. Win-rate evaluation is the regression suite's\n"
                 "job (mtg.exe), so the analyzer takes no game-count/depth/budget options.\n"
              << "Outputs the profile as JSON to stdout.\n";
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        PrintUsage(argv[0]);
        return 1;
    }

    std::filesystem::path deck_path   = argv[1];
    std::filesystem::path cards_json  = "src/cards/data/cards.json";
    int      max_turns     = 20;
    uint64_t seed          = 0;
    bool     seed_provided = false;

    for (int i = 2; i < argc - 1; ++i)
    {
        std::string flag = argv[i];
        try
        {
            if (flag == "--seed")
            {
                seed          = std::stoull(argv[i + 1]);
                seed_provided = true;
            }
            else if (flag == "--max-turns")
            {
                max_turns = std::stoi(argv[i + 1]);
            }
            else if (flag == "--cards-json")
            {
                cards_json = argv[i + 1];
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
        if (std::filesystem::exists(cards_json))
        {
            CardDatabase::Instance().LoadFromJson(cards_json);
        }

        Decklist deck = DeckLoader::LoadFromFile(deck_path);

        AnalyzerEngine engine;
        AnalysisResult result = engine.Run(deck, seed, max_turns);
        result.deck_name = deck_path.stem().string();

        // Write the optimised mulligan profile to deckname.profile.json so the runner
        // can load it without re-running the analyser.
        std::filesystem::path profile_path =
            deck_path.parent_path() / (deck_path.stem().string() + ".profile.json");
        if (SaveDeckProfile(profile_path, result.mulligan_profile))
        {
            std::cerr << "Profile written to " << profile_path.string() << "\n";
        }
        else
        {
            std::cerr << "Warning: could not write profile to " << profile_path.string() << "\n";
        }

        std::cout << AnalysisResultToJson(result) << "\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
