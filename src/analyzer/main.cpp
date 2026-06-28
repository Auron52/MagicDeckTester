#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <random>
#include "AnalyzerEngine.h"
#include "KeepModelTrainer.h"
#include "../deck/DeckLoader.h"
#include "../cards/CardDatabase.h"
#include "../ai/MulliganProfileIO.h"
#include <cstdlib>

static void PrintUsage(const char* prog)
{
    std::cerr << "Usage: " << prog
              << " <deckfile> [--seed S] [--max-turns T] [--cards-json path]\n"
              << "  <deckfile>      Plain text (.txt) or Cockatrice (.cod) decklist\n"
              << "  --seed S        Base RNG seed (omit to generate randomly)\n"
              << "  --max-turns T   Maximum turns per game (default: 8)\n"
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
    int      max_turns     = 8;   // match the runner's goldfish horizon (mtg.exe also defaults to 8):
                                  // a real game is lost by then, so "wins" on turns 9+ are de-facto
                                  // losses -- optimising the mulligan against them (the old 20-turn
                                  // horizon) rewarded slow non-wins as if they beat a loss. Override
                                  // with --max-turns for a genuinely slow deck.
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

        // Keep-model-only mode (MTG_KEEP_MODEL_ONLY): skip the whole land/score grid; load the
        // deck's EXISTING committed profile and fit ONLY the interpretable keep model onto it,
        // writing <deck>.keepmodel.profile.json. This is the fast Phase-3 A/B path -- the output is
        // byte-identical to the committed profile except for the added keep_model, so a suite A/B
        // isolates exactly the keep-decision change without re-running the (slow) grid.
        if (const char* e = std::getenv("MTG_KEEP_MODEL_ONLY"); e && *e && std::string(e) != "0")
        {
            std::filesystem::path in_path =
                deck_path.parent_path() / (deck_path.stem().string() + ".profile.json");
            MulliganProfile base = LoadDeckProfile(in_path);

            const int scale = []{ const char* s = std::getenv("MTG_ANALYZE_SCALE");
                                  int v = (s && *s) ? std::atoi(s) : 2; return v < 1 ? 1 : v; }();
            const int depth = []{ const char* s = std::getenv("MTG_ANALYZE_DEPTH");
                                  return (s && *s) ? std::max(0, std::atoi(s)) : 5; }();
            // MTG_KEEP_GAMES overrides the keep-model sample size (distinct opening hands) directly,
            // decoupling it from the land-grid's MTG_ANALYZE_SCALE. The default (2000/scale) is a fast
            // probe; a robust policy wants grid-comparable scale (tens of thousands of hands), since
            // each hand is one clairvoyant library realisation and the tree denoises by pooling hands.
            const int keep_games = []{ const char* s = std::getenv("MTG_KEEP_GAMES");
                                       return (s && *s) ? std::max(200, std::atoi(s)) : 0; }();
            KeepModelTrainConfig cfg;
            cfg.depth     = depth;
            cfg.budget_ms = 20;
            cfg.max_turns = max_turns;
            cfg.games     = keep_games ? keep_games : std::max(200, 2000 / scale);
            cfg.seed      = seed;
            base.keep_model = BuildKeepModel(deck, base, base.card_scores, cfg);

            std::filesystem::path out_path =
                deck_path.parent_path() / (deck_path.stem().string() + ".keepmodel.profile.json");
            if (SaveDeckProfile(out_path, base))
            { std::cerr << "Keep-model profile written to " << out_path.string() << "\n"; }
            else
            { std::cerr << "Warning: could not write " << out_path.string() << "\n"; return 1; }
            return 0;
        }

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
