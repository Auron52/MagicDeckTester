#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <random>
#include "AnalyzerEngine.h"
#include "KeepModelTrainer.h"
#include "../core/HeuristicDefaults.h"
#include "EquivalenceDiscovery.h"
#include "ExhaustiveKeep.h"
#include "../ai/AIEngine.h"
#include "../core/GameEngine.h"
#include "../runner/GoldFishRunner.h"
#include "../core/HardwareConcurrency.h"
#include <fstream>
#include <atomic>
#include <thread>
#include <mutex>
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
    // Apply committed heuristic defaults BEFORE anything reads a toggle (env vars still override).
    ApplyHeuristicDefaults();
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
            // Bottoming is ALWAYS baked ON. Blind exhaustive bottoming is the theoretically-correct policy
            // when blind to the shuffle (a real player can't peek at the library the way clairvoyant
            // lookahead bottoming does), and the confounded in-game A/B (MTG_CONFOUND_BOTTOM) has
            // consistently confirmed blind >= lookahead once the peek is removed -- the table plays the
            // hand over the whole shuffle distribution while the lookahead commits to one peeked library.
            // There is deliberately NO generation-time off switch: a bottoming-off profile is a footgun no
            // agent should be able to ship. (Runtime A/B still isolates bottoming via MTG_EXHAUSTIVE_BOTTOM,
            // which changes play ephemerally and writes no profile; decks with no exhaustive table fall
            // back to lookahead bottoming.)
            const bool bottoming_enabled = true;
            RunKeepMerge(std::cout, deck, profile, inputs, out_profile, out_raw, bottoming_enabled);
            return 0;
        }

        // Constructed-hand game logger (MTG_LOG_HAND="c0,c1,...,cK-1"): set up games with an EXACT
        // bucket composition as the opening hand, play them out with a real logger, and save the ones
        // that win on MTG_LOG_TURN (default 3) as replay JSON under MTG_LOG_DIR (default logs/hand_lines).
        // MTG_LOG_MAXTOP2LANDS (default 1) skips games whose first two draws are BOTH lands (the "clean"
        // path), so the saved lines are the harder wins. MTG_LOG_N games, MTG_LOG_PLAY=1 for on-the-play.
        if (const char* e = std::getenv("MTG_LOG_HAND"); e && *e && std::string(e) != "0")
        {
            std::filesystem::path in_path =
                deck_path.parent_path() / (deck_path.stem().string() + ".keepmodel.exhaustive.profile.json");
            MulliganProfile profile = std::filesystem::exists(in_path) ? LoadDeckProfile(in_path)
                                                                       : MulliganProfile::DefaultProfile();
            const auto& buckets = profile.exhaustive_keep.buckets;
            const int K = static_cast<int>(buckets.size());
            std::map<std::string,int> bof;
            for (int b = 0; b < K; ++b) { for (const std::string& n : buckets[b]) { bof[n] = b; } }
            std::vector<int> comp(K, 0);
            { std::stringstream cs(e); std::string t; int b = 0;
              while (std::getline(cs, t, ',') && b < K) { comp[b++] = std::atoi(t.c_str()); } }
            const int depth = []{ const char* s = std::getenv("MTG_EQUIV_DEPTH");
                                  return (s && *s) ? std::max(0, std::atoi(s)) : 5; }();
            const int target = []{ const char* s = std::getenv("MTG_LOG_TURN");
                                   return (s && *s) ? std::atoi(s) : 3; }();
            const int want   = []{ const char* s = std::getenv("MTG_LOG_N");
                                   return (s && *s) ? std::max(1, std::atoi(s)) : 3; }();
            const int max2   = []{ const char* s = std::getenv("MTG_LOG_MAXTOP2LANDS");
                                   return (s && *s) ? std::atoi(s) : 1; }();
            const bool on_play = std::getenv("MTG_LOG_PLAY") != nullptr;
            std::filesystem::path log_dir = std::getenv("MTG_LOG_DIR") ? std::getenv("MTG_LOG_DIR")
                                                                       : "logs/hand_lines";
            std::filesystem::create_directories(log_dir);
            auto numbering = GoldFishRunner::BuildCardNumbering(deck);
            MulliganProfile rp = profile; rp.keep_model = KeepModel{};
            const bool second_main = GoldFishRunner::DeckUsesSecondMain(deck);
            AIEngine ai(rp, depth, 20); ai.SetSearchPostCombat(second_main);
            int found = 0, scanned = 0;
            const uint64_t base = 900'000'000ULL;
            for (uint64_t seed = base; found < want && seed < base + 200000; ++seed)
            {
                ++scanned;
                GameState s = GoldFishRunner::SetupGame(deck, seed);
                s.m_required_pieces = &rp.required_pieces;
                s.vial_target_mv    = rp.vial_target_mv;
                s.on_the_play       = on_play;
                GoldFishRunner::AssignCardNumbers(s, numbering);
                Player& ap = s.ActivePlayer(); ap.hand.clear();
                for (int b = 0; b < K; ++b) { int need = comp[b];
                    for (std::size_t k = 0; k < ap.library.size() && need > 0; ) {
                        auto bit = bof.find(ap.library[k].m_name.str());
                        if (bit != bof.end() && bit->second == b)
                        { ap.hand.push_back(ap.library[k]); ap.library.erase(ap.library.begin()+k); --need; }
                        else { ++k; } } }
                ap.library.Shuffle(SaltSeed(seed, 0x5EED5ULL));   // unbias continuation (see ExhaustiveKeep)
                // Peek the first 3 draws (on the draw: turns 1-3) for the filter + context.
                std::vector<std::string> top;
                for (int k = 0; k < 3 && k < static_cast<int>(ap.library.size()); ++k)
                { top.push_back(ap.library[k].m_name.str()); }
                int top2lands = 0;
                for (int k = 0; k < 2 && k < static_cast<int>(ap.library.size()); ++k)
                { const CardDefinition* d = CardDatabase::Instance().Lookup(ap.library[k].m_name.str());
                  if (d && d->card.IsLand()) { ++top2lands; } }
                if (top2lands > max2) { continue; }   // skip the easy 2-lands-on-top wins
                std::vector<int> nums; std::vector<std::string> names;
                for (const Card& c : ap.hand) { nums.push_back(c.m_number); names.push_back(c.m_name.str()); }
                GameLogger logger;
                logger.StartGame("hand", static_cast<int>(seed - base), "d1", seed, numbering);
                logger.LogOpeningHand(nums, names);
                GameEngine engine(ai); engine.SetLogger(&logger);
                int wt = engine.PlayOut(s, max_turns);
                engine.SetLogger(nullptr);
                if (wt != target) { continue; }
                logger.EndGame(wt);
                std::filesystem::path path = log_dir /
                    ("t" + std::to_string(target) + "_seed" + std::to_string(seed) + ".json");
                logger.WriteToFile(path);
                ++found;
                std::cout << "WIN t" << wt << "  " << path.string() << "\n  opening: ";
                for (const std::string& n : names) { std::cout << n << ", "; }
                std::cout << "\n  first 3 draws: ";
                for (const std::string& n : top) { std::cout << n << ", "; }
                std::cout << "\n";
            }
            std::cout << "found " << found << " turn-" << target << " wins (scanned " << scanned
                      << " seeds, on_the_" << (on_play ? "play" : "draw") << ")\n" << std::flush;
            return 0;
        }

        // Mulligan-EV probe (MTG_MULL_EV): measure the value of mulliganing a RANDOM opening hand down
        // to (7-m) cards and playing out, for m=0..MAXM. This is the KEEP THRESHOLD the exhaustive policy
        // compares a specific hand against: keep iff V[hand] <= Dopt[m+1], and Dopt[m+1] ~ the mean here
        // at m+1 (value of mulliganing again). Draws a natural top-7 per fresh shuffle, bottoms m via the
        // deck's bottoming policy, plays out; averages over MTG_SCORE_R shuffles. MTG_SCORE_HIST adds the
        // per-(m,pd) win-turn histogram. Read-only diagnostic.
        if (const char* e = std::getenv("MTG_MULL_EV"); e && *e && std::string(e) != "0")
        {
            std::filesystem::path in_path =
                deck_path.parent_path() / (deck_path.stem().string() + ".keepmodel.exhaustive.profile.json");
            MulliganProfile profile = std::filesystem::exists(in_path) ? LoadDeckProfile(in_path)
                                                                       : MulliganProfile::DefaultProfile();
            const int R = []{ const char* s = std::getenv("MTG_SCORE_R");
                              return (s && *s) ? std::max(1, std::atoi(s)) : 400; }();
            const int depth = []{ const char* s = std::getenv("MTG_EQUIV_DEPTH");
                                  return (s && *s) ? std::max(0, std::atoi(s)) : 5; }();
            const int MAXM = []{ const char* s = std::getenv("MTG_MULL_EV_MAXM");
                                 return (s && *s) ? std::max(0, std::atoi(s)) : 2; }();
            const bool hist_on = std::getenv("MTG_SCORE_HIST") != nullptr;
            MulliganProfile rp = profile; rp.keep_model = KeepModel{};
            const bool second_main = GoldFishRunner::DeckUsesSecondMain(deck);
            std::vector<std::array<double, 2>> sum(MAXM + 1, { 0, 0 }), sumsq(MAXM + 1, { 0, 0 });
            std::vector<std::array<std::array<long long, 12>, 2>> hist(MAXM + 1);
            std::mutex mtx;
            std::atomic<int> next{ 0 };
            auto worker = [&]()
            {
                AIEngine ai(rp, depth, 20); ai.SetSearchPostCombat(second_main);
                std::vector<std::array<double, 2>> ls(MAXM + 1, { 0, 0 }), lsq(MAXM + 1, { 0, 0 });
                std::vector<std::array<std::array<long long, 12>, 2>> lh(MAXM + 1);
                for (;;)
                {
                    int r = next.fetch_add(1);
                    if (r >= R) { break; }
                    for (int pd = 0; pd < 2; ++pd)
                    {
                        const uint64_t rs = 555'000'000ULL + 0x9E3779B97F4A7C15ULL * (r + 1)
                                          + 100003ULL * static_cast<uint64_t>(pd);
                        GameState base = GoldFishRunner::SetupGame(deck, rs);
                        base.m_required_pieces = &rp.required_pieces;
                        base.vial_target_mv    = rp.vial_target_mv;
                        base.on_the_play       = (pd == 1);
                        Player& ap = base.ActivePlayer();
                        ap.hand.clear();
                        for (int k = 0; k < 7 && !ap.library.empty(); ++k)
                        { ap.hand.push_back(ap.library.front()); ap.library.erase(ap.library.begin()); }
                        for (int m = 0; m <= MAXM; ++m)
                        {
                            GameState s = base;
                            double wt = ai.RolloutKeepWinTurn(s, m, max_turns);
                            ls[m][pd] += wt; lsq[m][pd] += wt * wt;
                            int wb = static_cast<int>(wt); if (wb < 0) { wb = 0; } if (wb > 11) { wb = 11; }
                            ++lh[m][pd][wb];
                        }
                    }
                }
                std::lock_guard<std::mutex> g(mtx);
                for (int m = 0; m <= MAXM; ++m)
                    for (int pd = 0; pd < 2; ++pd)
                    { sum[m][pd] += ls[m][pd]; sumsq[m][pd] += lsq[m][pd];
                      for (int t = 0; t < 12; ++t) { hist[m][pd][t] += lh[m][pd][t]; } }
            };
            int nthreads = std::max(1, concurrency_util::AffinityCpuCount());
            std::vector<std::thread> pool;
            for (int t = 0; t < nthreads; ++t) { pool.emplace_back(worker); }
            for (std::thread& th : pool) { th.join(); }
            std::cout << "# MULL-EV deck=" << deck_path.stem().string() << " R=" << R << " depth=" << depth
                      << " max_turns=" << max_turns << "\n";
            std::cout << "# m = cards bottomed (hand size 7-m); mean = keep threshold Dopt[m] (lower=better)\n";
            for (int m = 0; m <= MAXM; ++m)
                for (int pd = 0; pd < 2; ++pd)
                {
                    double mean = sum[m][pd] / R;
                    double var  = std::max(0.0, sumsq[m][pd] / R - mean * mean);
                    std::cout << "m" << m << " keep" << (7 - m) << " " << (pd ? "PLAY" : "DRAW")
                              << "  mean=" << mean << "  se=" << std::sqrt(var / R);
                    if (hist_on)
                    { std::cout << "  hist:";
                      for (int t = 0; t < 12; ++t) { if (hist[m][pd][t]) { std::cout << " t" << t << "=" << hist[m][pd][t]; } } }
                    std::cout << "\n";
                }
            std::cout << std::flush;
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
            // Optional per-comp win-turn histogram (MTG_SCORE_HIST): [item][pd][win_turn 0..11], last
            // buckets catch max_turns+1 (no win). Per-item ownership => race-free like `out`.
            const bool score_hist = std::getenv("MTG_SCORE_HIST") != nullptr;
            std::vector<std::array<std::array<long long, 12>, 2>> hist(items.size());

            // Optional detail mode (MTG_SCORE_DETAIL): for each win-turn bucket, the lands-in-play split
            // (player-0 lands controlled at the win) and how many of those wins cast Light Up the Stage
            // (via the execution-trace touch index). Answers "how many turn-3 wins ran on 1 vs 2 lands,
            // and did the spectacle dig participate?".
            const bool detail_on = std::getenv("MTG_SCORE_DETAIL") != nullptr;
            struct Detail { std::array<std::array<long long, 10>, 12> lands{};   // [winturn][lands 0..9]
                            std::array<long long, 12> lightup{};                 // [winturn] wins that cast LUS
                            std::array<std::array<long long, 3>, 12> top2{}; };  // [winturn][lands in first 2 draws]
            std::vector<std::array<Detail, 2>> detail(detail_on ? items.size() : 0);
            std::map<std::string, int> touch_index; std::vector<std::string> touch_names;
            int lightup_idx = -1;
            if (detail_on)
            {
                for (const Card& c : deck.mainboard)
                    if (touch_index.emplace(c.m_name.str(), static_cast<int>(touch_names.size())).second)
                    { touch_names.push_back(c.m_name.str()); }
                auto it = touch_index.find("Light Up the Stage");
                if (it != touch_index.end()) { lightup_idx = it->second; }
            }

            std::atomic<int> next{0};
            const std::map<std::string, int>& bofref = bof;   // const ref -> concurrent-safe find()
            auto worker = [&]()
            {
                AIEngine ai(rp, depth, 20); ai.SetSearchPostCombat(second_main);
                std::vector<char> hit;
                if (detail_on) { ai.SetTouchIndex(&touch_index); hit.assign(touch_names.size(), 0); }
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
                            ap.library.Shuffle(SaltSeed(rs, 0x5EED5ULL));   // unbias continuation (see ExhaustiveKeep)
                            int top2lands = 0;
                            if (detail_on)
                                for (int q = 0; q < 2 && q < static_cast<int>(ap.library.size()); ++q)
                                { const CardDefinition* dd = CardDatabase::Instance().Lookup(ap.library[q].m_name.str());
                                  if (dd && dd->card.IsLand()) { ++top2lands; } }
                            double wt; int lands = -1;
                            if (detail_on)
                            { std::fill(hit.begin(), hit.end(), 0);
                              wt = ai.RolloutKeepWinTurn(s, 0, max_turns, &hit, &lands); }
                            else { wt = ai.RolloutKeepWinTurn(s, 0, max_turns); }
                            sum += wt; sumsq += wt*wt;
                            int wb = static_cast<int>(wt); if (wb < 0) wb = 0; if (wb > 11) wb = 11;
                            ++hist[w][pd][wb];
                            if (detail_on)
                            { int lb = lands < 0 ? 0 : (lands > 9 ? 9 : lands);
                              ++detail[w][pd].lands[wb][lb];
                              ++detail[w][pd].top2[wb][top2lands < 0 ? 0 : (top2lands > 2 ? 2 : top2lands)];
                              if (lightup_idx >= 0 && hit[lightup_idx]) { ++detail[w][pd].lightup[wb]; } }
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
            if (score_hist)
            {
                std::cout << "\n# win-turn histogram (tN=count; the last populated bucket = no win by "
                             "max_turns, scored max_turns+1)\n";
                for (std::size_t i = 0; i < items.size(); ++i)
                    for (int pd = 0; pd < 2; ++pd)
                    {
                        std::cout << items[i].line << (pd ? "  PLAY " : "  DRAW ");
                        for (int t = 0; t < 12; ++t)
                        { if (hist[i][pd][t]) { std::cout << "t" << t << "=" << hist[i][pd][t] << " "; } }
                        std::cout << "\n";
                    }
            }
            if (detail_on)
            {
                std::cout << "\n# per-win-turn lands-in-play split (L<n>=count of wins with n player-0 "
                             "lands) + LightUp=wins that cast Light Up the Stage\n";
                for (std::size_t i = 0; i < items.size(); ++i)
                    for (int pd = 0; pd < 2; ++pd)
                        for (int t = 0; t < 12; ++t)
                        {
                            long long tot = 0; for (int l = 0; l < 10; ++l) { tot += detail[i][pd].lands[t][l]; }
                            if (!tot) { continue; }
                            std::cout << items[i].line << (pd ? "  PLAY t" : "  DRAW t") << t << " n=" << tot << "  ";
                            for (int l = 0; l < 10; ++l)
                            { if (detail[i][pd].lands[t][l]) { std::cout << "L" << l << "=" << detail[i][pd].lands[t][l] << " "; } }
                            std::cout << " LightUp=" << detail[i][pd].lightup[t]
                                      << "  top2draws[0L=" << detail[i][pd].top2[t][0]
                                      << " 1L=" << detail[i][pd].top2[t][1]
                                      << " 2L=" << detail[i][pd].top2[t][2] << "]\n";
                        }
            }
            std::cout << std::flush;
            return 0;
        }

        // Exhaustive keep/bottom policy (MTG_KEEP_EXHAUSTIVE): bucket the deck, enumerate every
        // distinct bucket-hand for sizes 7..7-max_mull, evaluate each with R reshuffled rollouts,
        // and print the exact optimal keep+bottom policy value vs the static keep rule. Read-only.
        //   MTG_EQUIV_PROBES/_THRESHOLD/_DEPTH (bucketing), MTG_KEEP_ROLLOUTS (R, default 100),
        //   MTG_KEEP_MAXMULL (deepest mulligan, default 6 = down to keep-1; the terminal keep-1 anchor is
        //   the only correct forced-keep, so 6 is the uniquely-correct depth. Shallower forced-keeps a hand
        //   it should ship -- see docs/design/bottomcards-undercount-beyond-maxmull.md).
        if (const char* e = std::getenv("MTG_KEEP_EXHAUSTIVE"); e && *e && std::string(e) != "0")
        {
            auto env_int = [](const char* k, int dflt, int lo)
            { const char* s = std::getenv(k); return (s && *s) ? std::max(lo, std::atoi(s)) : dflt; };
            std::filesystem::path in_path =
                deck_path.parent_path() / (deck_path.stem().string() + ".profile.json");
            MulliganProfile profile = std::filesystem::exists(in_path)
                                    ? LoadDeckProfile(in_path) : MulliganProfile::DefaultProfile();
            // Attach the deck's learned leaf VALUE sidecar (<deck>.value.json) so the keep/bottom
            // ROLLOUTS play like the SHIPPED deck: value-leaf is default-ON at the runner (UseValueModel,
            // 06e6ebe) and lets the fixed-depth search TRUST the value estimate at the leaf instead of
            // playing out past it -- both the train/serve-correct policy AND the large gen speedup.
            // Without this the analyzer ran a value-less policy the deck never uses (a latent bug).
            // Presence-gated (no-op for value-less decks) + runtime-gated by UseValueModel()
            // (MTG_VALUE_MODEL=0 forces the pure-heuristic leaf for an A/B). RolloutConfigDigest folds
            // this into the pooling identity, so cross-machine pools stay correct.
            AttachValueSidecar(profile, in_path);
            ExhaustiveKeepConfig cfg;
            cfg.probes    = env_int("MTG_EQUIV_PROBES", 400, 1);
            cfg.threshold = []{ const char* s = std::getenv("MTG_EQUIV_THRESHOLD");
                                return (s && *s) ? std::max(0.0, std::atof(s)) : 0.01; }();
            cfg.depth     = env_int("MTG_EQUIV_DEPTH", 5, 0);
            cfg.budget_ms = env_int("MTG_EQUIV_BUDGET", 20, 0);  // per-decision rollout search budget (ms);
                                                                 // feeds discovery, rollouts AND play digest
            cfg.rollouts  = env_int("MTG_KEEP_ROLLOUTS", 100, 1);
            // Adaptive sampling: R_FLOOR<ROLLOUTS engages confidence-driven refinement (default off =>
            // uniform R => byte-identical). FLIP_EPS is the stop threshold; R_BATCH the per-wave add.
            cfg.r_floor   = env_int("MTG_KEEP_R_FLOOR", 0, 1);   // 0 => uniform (= rollouts)
            cfg.r_batch   = env_int("MTG_KEEP_R_BATCH", 16, 1);
            cfg.flip_eps  = []{ const char* s = std::getenv("MTG_KEEP_FLIP_EPS");
                                return (s && *s) ? std::max(0.0, std::atof(s)) : 0.02; }();
            cfg.se_prior  = []{ const char* s = std::getenv("MTG_KEEP_SE_PRIOR");
                                return (s && *s) ? std::max(0.0, std::atof(s)) : 8.0; }();
            cfg.max_mull  = env_int("MTG_KEEP_MAXMULL", 6, 0);
            cfg.seed      = seed;   // rollout seed base (the run id / seed_base)
            cfg.equiv_seed = []{ const char* s = std::getenv("MTG_EQUIV_SEED");
                                 return (s && *s) ? std::strtoull(s, nullptr, 10) : 20260701ULL; }();
            cfg.max_turns = max_turns;
            // Bottoming is ALWAYS baked ON -- there is deliberately no generation-time off switch (see the
            // note on the merge path). Blind exhaustive bottoming is the blind-to-shuffle policy the whole
            // table exists to produce (its second purpose after keep), and the confounded A/B
            // (MTG_CONFOUND_BOTTOM) has consistently shown it >= clairvoyant lookahead. Shipping a
            // bottoming-off profile is a footgun no agent should be able to reach, so the knob is gone.
            cfg.bottoming_enabled = true;
            // EXPERIMENT: adaptively sample sub-tables even with bottoming on + curse-filter the bottom
            // argmin to refined cells (recovers keep-only savings for bottoming-on). Off => full-R sub-tables.
            cfg.adaptive_bottom = []{ const char* s = std::getenv("MTG_KEEP_ADAPTIVE_BOTTOM");
                                      return s && *s && std::string(s) != "0"; }();
            if (const char* c = std::getenv("MTG_COMMIT")) { cfg.commit = c; }
            if (const char* fm = std::getenv("MTG_EQUIV_FORCE_MERGE")) { cfg.force_merge = fm; }
            // Write the serialized keep policy + poolable raw sidecar next to the deck unless suppressed.
            if (std::getenv("MTG_KEEP_NO_WRITE") == nullptr)
            {
                const std::string stem = (deck_path.parent_path() / deck_path.stem().string()).string();
                cfg.out_profile = stem + ".keepmodel.exhaustive.profile.json";
                cfg.out_raw     = stem + ".keepmodel.exhaustive.raw.json";
                // Override output paths (mirrors the merge path's MTG_MERGE_OUT_*): lets chunked R-sweep
                // generation write each chunk's raw straight to its own path, skipping the (unneeded)
                // per-chunk profile via MTG_KEEP_OUT_PROFILE=/dev/null-style empty.
                if (const char* p = std::getenv("MTG_KEEP_OUT_PROFILE")) { cfg.out_profile = p; }
                if (const char* r = std::getenv("MTG_KEEP_OUT_RAW"))     { cfg.out_raw     = r; }
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
