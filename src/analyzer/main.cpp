#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <random>
#include "AnalyzerEngine.h"
#include "KeepModelTrainer.h"
#include "EquivalenceDiscovery.h"
#include "ExhaustiveKeep.h"
#include "../ai/AIEngine.h"
#include "../runner/GoldFishRunner.h"
#include "../core/HardwareConcurrency.h"
#include <fstream>
#include <atomic>
#include <thread>
#include <array>
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

        // Equivalence-discovery mode (MTG_EQUIV_DISCOVER): measure objective-relative card
        // equivalence by CRN substitution in the goldfish engine and print the merged classes for
        // review. Read-only -- writes no profile. Uses the deck's committed profile for rollout
        // fidelity (vial target / play style), matching how the keep-model labels are measured.
        //   MTG_EQUIV_PROBES (default 200), MTG_EQUIV_DEPTH (5), MTG_EQUIV_BUDGET (20).
        if (const char* e = std::getenv("MTG_EQUIV_DISCOVER"); e && *e && std::string(e) != "0")
        {
            auto env_int = [](const char* k, int dflt, int lo)
            { const char* s = std::getenv(k); return (s && *s) ? std::max(lo, std::atoi(s)) : dflt; };
            const int probes    = env_int("MTG_EQUIV_PROBES", 200, 1);
            const int depth      = env_int("MTG_EQUIV_DEPTH", 5, 0);
            const int budget_ms  = env_int("MTG_EQUIV_BUDGET", 20, 0);
            // Single-linkage merge distance (mean |Δ win-turn| per probe). Default 0.01 sits in the
            // empirical gap between merge-worthy (~0.005) and distinct (~0.05) card pairs; set 0 for
            // exact-match (fragments as probes grow). Parsed as a double.
            const double threshold = []{ const char* s = std::getenv("MTG_EQUIV_THRESHOLD");
                                         return (s && *s) ? std::max(0.0, std::atof(s)) : 0.01; }();

            std::filesystem::path in_path =
                deck_path.parent_path() / (deck_path.stem().string() + ".profile.json");
            MulliganProfile profile = std::filesystem::exists(in_path)
                                    ? LoadDeckProfile(in_path) : MulliganProfile::DefaultProfile();

            std::cerr << "Equivalence discovery: " << probes << " probes, depth " << depth
                      << ", threshold " << threshold << ", horizon " << max_turns << "\n";
            EquivReport rep = DiscoverEquivalence(deck, profile, probes, depth, budget_ms,
                                                  threshold, seed, max_turns);
            PrintEquivReport(std::cout, rep);
            return 0;
        }

        // Merge mode (MTG_KEEP_MERGE): pool the poolable raw sidecars from prior exhaustive runs (this
        // machine + a second machine) into one policy at the combined R. Inputs come from
        // MTG_MERGE_INPUTS (comma/space/newline-separated paths). Writes the same
        // <stem>.keepmodel.exhaustive.profile.json + .raw.json (override via MTG_MERGE_OUT_PROFILE /
        // MTG_MERGE_OUT_RAW; MTG_KEEP_NO_WRITE suppresses). No rollouts -- fast.
        if (const char* e = std::getenv("MTG_KEEP_MERGE"); e && *e && std::string(e) != "0")
        {
            std::filesystem::path in_path =
                deck_path.parent_path() / (deck_path.stem().string() + ".profile.json");
            MulliganProfile profile = std::filesystem::exists(in_path)
                                    ? LoadDeckProfile(in_path) : MulliganProfile::DefaultProfile();
            std::vector<std::string> inputs;
            if (const char* mi = std::getenv("MTG_MERGE_INPUTS"))
            {
                std::string cur;
                for (const char* p = mi; *p; ++p)
                {
                    if (*p == ',' || *p == ' ' || *p == '\n' || *p == '\t' || *p == '\r')
                    { if (!cur.empty()) { inputs.push_back(cur); cur.clear(); } }
                    else { cur += *p; }
                }
                if (!cur.empty()) { inputs.push_back(cur); }
            }
            const std::string stem = (deck_path.parent_path() / deck_path.stem().string()).string();
            std::string out_profile = stem + ".keepmodel.exhaustive.profile.json";
            std::string out_raw     = stem + ".keepmodel.exhaustive.raw.json";
            if (const char* p = std::getenv("MTG_MERGE_OUT_PROFILE")) { out_profile = p; }
            if (const char* r = std::getenv("MTG_MERGE_OUT_RAW"))     { out_raw = r; }
            if (std::getenv("MTG_KEEP_NO_WRITE") != nullptr) { out_profile.clear(); out_raw.clear(); }
            const bool bottoming_enabled = []{ const char* s = std::getenv("MTG_KEEP_BOTTOMING");
                                               return s && *s && std::string(s) != "0"; }();
            RunKeepMerge(std::cout, deck, profile, inputs, out_profile, out_raw, bottoming_enabled);
            return 0;
        }

        // Targeted comp-scorer (MTG_SCORE_COMPS): re-evaluate specific bucket compositions at high R
        // to non-circularly check bottoming decisions (is the kept subhand truly blind-better?). Reads
        // MTG_SCORE_FILE lines "H:c0,c1,...,cK-1" (hand size + per-bucket counts, bucket order = the
        // profile's exhaustive_keep.buckets); prints "H:comp  draw_mean draw_se  play_mean play_se".
        // Uses the deck's committed exhaustive profile for the bucket map. MTG_SCORE_R (default 400).
        if (const char* e = std::getenv("MTG_SCORE_COMPS"); e && *e && std::string(e) != "0")
        {
            std::filesystem::path in_path =
                deck_path.parent_path() / (deck_path.stem().string() + ".keepmodel.exhaustive.profile.json");
            MulliganProfile profile = LoadDeckProfile(in_path);
            const auto& buckets = profile.exhaustive_keep.buckets;
            const int K = static_cast<int>(buckets.size());
            std::map<std::string,int> bof;
            for (int b = 0; b < K; ++b) { for (const std::string& n : buckets[b]) { bof[n] = b; } }
            const int R = []{ const char* s = std::getenv("MTG_SCORE_R");
                              return (s && *s) ? std::max(1, std::atoi(s)) : 400; }();
            const int depth = []{ const char* s = std::getenv("MTG_EQUIV_DEPTH");
                                  return (s && *s) ? std::max(0, std::atoi(s)) : 5; }();
            const char* fp = std::getenv("MTG_SCORE_FILE");
            std::ifstream fin(fp ? fp : "");
            if (!fin) { std::cerr << "MTG_SCORE_FILE not readable\n"; return 1; }
            MulliganProfile rp = profile; rp.keep_model = KeepModel{};
            const bool second_main = GoldFishRunner::DeckUsesSecondMain(deck);

            // Parse all comps up front so the (independent) per-comp evaluations can be threaded.
            struct Item { std::string line; std::vector<int> comp; };
            std::vector<Item> items;
            std::string line;
            while (std::getline(fin, line))
            {
                if (line.empty()) { continue; }
                auto colon = line.find(':');
                std::vector<int> comp; std::string rest = line.substr(colon + 1); std::size_t p = 0;
                while (p < rest.size()) { std::size_t c = rest.find(',', p);
                    comp.push_back(std::stoi(rest.substr(p, c - p)));
                    if (c == std::string::npos) { break; } p = c + 1; }
                comp.resize(K, 0);
                items.push_back({ line, std::move(comp) });
            }
            std::vector<std::array<double, 4>> out(items.size(), { 0, 0, 0, 0 });  // dmean,dse,pmean,pse

            std::atomic<int> next{0};
            const std::map<std::string, int>& bofref = bof;   // const ref -> concurrent-safe find()
            auto worker = [&]()
            {
                AIEngine ai(rp, depth, 20); ai.SetSearchPostCombat(second_main);
                for (;;)
                {
                    int w = next.fetch_add(1);
                    if (w >= static_cast<int>(items.size())) { break; }
                    const std::vector<int>& comp = items[w].comp;
                    for (int pd = 0; pd < 2; ++pd)
                    {
                        double sum = 0, sumsq = 0;
                        for (int r = 0; r < R; ++r)
                        {
                            const uint64_t rs = 777'000'000ULL + 0x9E3779B97F4A7C15ULL * (r + 1)
                                              + 100003ULL * static_cast<uint64_t>(pd);
                            GameState s = GoldFishRunner::SetupGame(deck, rs);
                            s.m_required_pieces = &rp.required_pieces;
                            s.vial_target_mv    = rp.vial_target_mv;
                            s.on_the_play       = (pd == 1);
                            Player& ap = s.ActivePlayer(); ap.hand.clear();
                            for (int b = 0; b < K; ++b) { int need = comp[b];
                                for (std::size_t k = 0; k < ap.library.size() && need > 0; ) {
                                    auto bit = bofref.find(ap.library[k].m_name.str());
                                    if (bit != bofref.end() && bit->second == b)
                                    { ap.hand.push_back(ap.library[k]); ap.library.erase(ap.library.begin()+k); --need; }
                                    else { ++k; } } }
                            double wt = ai.RolloutKeepWinTurn(s, 0, max_turns);
                            sum += wt; sumsq += wt*wt;
                        }
                        double mean = sum / R;
                        double var  = R > 1 ? std::max(0.0, sumsq/R - mean*mean) : 0.0;
                        out[w][pd * 2]     = mean;
                        out[w][pd * 2 + 1] = R > 1 ? std::sqrt(var / R) : 0.0;
                    }
                }
            };
            int nthreads = std::max(1, std::min(concurrency_util::AffinityCpuCount(),
                                                static_cast<int>(items.size())));
            std::vector<std::thread> pool;
            for (int t = 0; t < nthreads; ++t) { pool.emplace_back(worker); }
            for (std::thread& th : pool) { th.join(); }

            for (std::size_t i = 0; i < items.size(); ++i)
            {
                std::cout << items[i].line << "\t" << out[i][0] << " " << out[i][1] << "\t"
                          << out[i][2] << " " << out[i][3] << "\n";
            }
            std::cout << std::flush;
            return 0;
        }

        // Exhaustive keep/bottom policy (MTG_KEEP_EXHAUSTIVE): bucket the deck, enumerate every
        // distinct bucket-hand for sizes 7..7-max_mull, evaluate each with R reshuffled rollouts,
        // and print the exact optimal keep+bottom policy value vs the static keep rule. Read-only.
        //   MTG_EQUIV_PROBES/_THRESHOLD/_DEPTH (bucketing), MTG_KEEP_ROLLOUTS (R, default 100),
        //   MTG_KEEP_MAXMULL (deepest mulligan, default 3).
        if (const char* e = std::getenv("MTG_KEEP_EXHAUSTIVE"); e && *e && std::string(e) != "0")
        {
            auto env_int = [](const char* k, int dflt, int lo)
            { const char* s = std::getenv(k); return (s && *s) ? std::max(lo, std::atoi(s)) : dflt; };
            std::filesystem::path in_path =
                deck_path.parent_path() / (deck_path.stem().string() + ".profile.json");
            MulliganProfile profile = std::filesystem::exists(in_path)
                                    ? LoadDeckProfile(in_path) : MulliganProfile::DefaultProfile();
            ExhaustiveKeepConfig cfg;
            cfg.probes    = env_int("MTG_EQUIV_PROBES", 400, 1);
            cfg.threshold = []{ const char* s = std::getenv("MTG_EQUIV_THRESHOLD");
                                return (s && *s) ? std::max(0.0, std::atof(s)) : 0.01; }();
            cfg.depth     = env_int("MTG_EQUIV_DEPTH", 5, 0);
            cfg.rollouts  = env_int("MTG_KEEP_ROLLOUTS", 100, 1);
            cfg.max_mull  = env_int("MTG_KEEP_MAXMULL", 3, 0);
            cfg.seed      = seed;   // rollout seed base (the run id / seed_base)
            cfg.equiv_seed = []{ const char* s = std::getenv("MTG_EQUIV_SEED");
                                 return (s && *s) ? std::strtoull(s, nullptr, 10) : 20260701ULL; }();
            cfg.max_turns = max_turns;
            cfg.bottoming_enabled = []{ const char* s = std::getenv("MTG_KEEP_BOTTOMING");
                                        return s && *s && std::string(s) != "0"; }();
            if (const char* c = std::getenv("MTG_COMMIT")) { cfg.commit = c; }
            // Write the serialized keep policy + poolable raw sidecar next to the deck unless suppressed.
            if (std::getenv("MTG_KEEP_NO_WRITE") == nullptr)
            {
                const std::string stem = (deck_path.parent_path() / deck_path.stem().string()).string();
                cfg.out_profile = stem + ".keepmodel.exhaustive.profile.json";
                cfg.out_raw     = stem + ".keepmodel.exhaustive.raw.json";
            }
            RunExhaustiveKeep(std::cout, deck, profile, cfg);
            return 0;
        }

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
            // MTG_KEEP_SPLIT=both fits BOTH the gini and regret trees from one (expensive) kv table:
            // the gini model is returned (standard file) and the regret model comes back via out_alt,
            // written to a .keepmodel.regret.profile.json side file for a matched A/B.
            const bool both = []{ const char* e = std::getenv("MTG_KEEP_SPLIT");
                                  return e && std::string(e) == "both"; }();
            // In both-mode, ALSO fit the additive score model from the same kv table (cheap second fit)
            // so a matched gini/regret/score 3-way A/B comes from one rollout pass.
            KeepModel alt, score, hybrid;
            base.keep_model = BuildKeepModel(deck, base, base.card_scores, cfg,
                                             both ? &alt : nullptr, both ? &score : nullptr,
                                             both ? &hybrid : nullptr);

            std::filesystem::path out_path =
                deck_path.parent_path() / (deck_path.stem().string() + ".keepmodel.profile.json");
            if (SaveDeckProfile(out_path, base))
            { std::cerr << "Keep-model profile written to " << out_path.string() << "\n"; }
            else
            { std::cerr << "Warning: could not write " << out_path.string() << "\n"; return 1; }

            auto write_side = [&](const KeepModel& km, const char* variant)
            {
                if (km.empty()) { return; }
                MulliganProfile prof = base;
                prof.keep_model = km;
                std::filesystem::path p =
                    deck_path.parent_path() / (deck_path.stem().string() + ".keepmodel." + variant + ".profile.json");
                if (SaveDeckProfile(p, prof))
                { std::cerr << "Keep-model (" << variant << ") profile written to " << p.string() << "\n"; }
                else
                { std::cerr << "Warning: could not write " << p.string() << "\n"; }
            };
            if (both)
            {
                write_side(alt,    "regret");
                write_side(score,  "score");
                write_side(hybrid, "hybrid");
            }
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
