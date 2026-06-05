#include "GoldFishRunner.h"
#include "../core/GameEngine.h"
#include "../core/GameLogger.h"
#include "../ai/AIEngine.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <thread>

// ---- Card numbering --------------------------------------------------------

// Assigns stable integer IDs to each card copy in the deck.
// Cards are sorted alphabetically by name; copies of the same name get
// consecutive numbers (e.g. 4x Lightning Bolt → 1,2,3,4).
static std::map<std::string, std::vector<int>> BuildCardNumbering(const Decklist& deck)
{
    std::set<std::string> unique_names;
    for (const Card& c : deck.mainboard) { unique_names.insert(c.m_name); }

    std::map<std::string, std::vector<int>> numbering;
    int next = 1;
    for (const std::string& name : unique_names)
    {
        int count = 0;
        for (const Card& c : deck.mainboard) { if (c.m_name == name) { ++count; } }
        for (int i = 0; i < count; ++i) { numbering[name].push_back(next++); }
    }
    return numbering;
}

// Assigns m_number to each card in the shuffled library based on the numbering map.
// Copies are numbered in the order they appear after shuffling.
static void AssignCardNumbers(GameState& state,
                               const std::map<std::string, std::vector<int>>& numbering)
{
    std::map<std::string, int> copy_index;
    for (Card& c : state.players[0].library)
    {
        int idx = copy_index[c.m_name]++;
        auto it = numbering.find(c.m_name);
        if (it != numbering.end() && idx < static_cast<int>(it->second.size()))
        {
            c.m_number = it->second[idx];
        }
    }
}

// ---- Opponent spawn pattern ------------------------------------------------

// 10-game repeating cycle of passive opponent board states.
// Creatures are added to the opponent's side at the scheduled turn; they never
// attack or block — their purpose is to provide targets for creature-targeting spells.
void GoldFishRunner::PopulateOpponentSpawns(GameState& state, int game_index)
{
    // Each row is one slot in the 10-game cycle.
    // Format: { turn, power, toughness }
    static const std::vector<std::vector<OpponentSpawn>> PATTERNS = {
        {},                                                           // 0: pure goldfish
        {},                                                           // 1: pure goldfish
        {{1,1,1},{1,1,1},{2,1,1},{2,1,1},{3,1,1},{3,1,1}},           // 2: weenie swarm
        {{1,2,2},{2,2,2},{3,2,2}},                                    // 3: midrange board
        {{1,1,1}},                                                    // 4: single 1/1
        {{1,2,2}},                                                    // 5: single 2/2
        {{1,3,3}},                                                    // 6: single 3/3
        {{3,4,4}},                                                    // 7: 4/4 on T3
        {{4,6,6}},                                                    // 8: 6/6 on T4
        {{1,1,1},{2,1,1},{3,2,2}},                                    // 9: small + small + mid
    };

    for (const OpponentSpawn& spawn : PATTERNS[game_index % 10])
    {
        state.opponent_spawns.push_back(spawn);
    }
}

// ---- Run ID ----------------------------------------------------------------

static std::string MakeRunId(uint64_t base_seed)
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S") << "_" << base_seed;
    return oss.str();
}

// ---- Disk cleanup ----------------------------------------------------------

// Deletes the oldest run-file groups from log_dir until total size <= max_bytes.
// Files are grouped by their "run_<runId>_" prefix; oldest prefix = deleted first.
static void CleanupLogs(const std::filesystem::path& log_dir,
                         uintmax_t max_bytes)
{
    if (!std::filesystem::exists(log_dir)) { return; }

    // Calculate total size and collect prefix → [paths] map
    std::map<std::string, std::vector<std::filesystem::path>> runs;
    uintmax_t total = 0;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(log_dir))
    {
        if (!entry.is_regular_file()) { continue; }
        std::string fname = entry.path().filename().string();
        // Extract prefix: "run_<runId>_" up to the second underscore after "run_"
        std::string prefix;
        if (fname.rfind("run_", 0) == 0)
        {
            // Find the last underscore segment ("_game_N.json") and strip it
            std::string::size_type game_pos = fname.rfind("_game_");
            prefix = (game_pos != std::string::npos) ? fname.substr(0, game_pos) : fname;
        }
        runs[prefix].push_back(entry.path());
        total += entry.file_size();
    }

    if (total <= max_bytes) { return; }

    // Delete oldest groups (lexicographic = chronological since runId starts with timestamp)
    for (std::pair<const std::string, std::vector<std::filesystem::path>>& kv : runs)
    {
        if (total <= max_bytes) { break; }
        for (const std::filesystem::path& p : kv.second)
        {
            total -= std::filesystem::file_size(p);
            std::filesystem::remove(p);
        }
    }
}

// ============================================================
// Public API
// ============================================================

RunResult GoldFishRunner::Run(const Decklist& deck, int num_games, uint64_t base_seed,
                               int max_turns, const MulliganProfile& profile,
                               const std::filesystem::path& log_dir, int base_game_index,
                               int lookahead_depth, int timeout_ms, int num_threads,
                               bool lookahead_bottoming)
{
    int hw_concurrency = static_cast<int>(std::thread::hardware_concurrency());
    if (hw_concurrency < 1) { hw_concurrency = 1; }

    if (num_threads <= 0) { num_threads = hw_concurrency; }
    num_threads = std::min(num_threads, num_games);

    // Scale the per-thread wall-clock budget to compensate for reduced per-thread
    // throughput under parallel load, so the search reaches a similar depth — and the
    // avg win turn stays ~invariant — regardless of thread count.
    //
    // Continuous model: a lone thread runs at full single-core turbo. As the machine
    // fills toward physical-core saturation (~hw_concurrency/2 threads, assuming 2-way
    // SMT), all-core clock throttling roughly halves per-thread throughput, so we reach
    // x2 there; beyond that, hyperthread sharing / oversubscription keeps it growing
    // linearly. The previous formula, ceil(2*threads/hw), stayed at x1 all the way up to
    // hw/2 and only stepped up past it — so at the common --threads = hw/2 setting it
    // applied NO compensation and deeper (e.g. depth-4) searches were silently starved
    // of compute, reading as a spurious "deeper is worse" result.
    //
    // This is a heuristic band-aid. The robust fix is to bound the search by a
    // deterministic node/iteration count instead of wall-clock time, which would make
    // results both thread-invariant and reproducible; revisit if depth ever matters.
    int per_thread_timeout = timeout_ms;
    if (timeout_ms > 0 && num_threads > 1)
    {
        double scale = std::max(1.0, 4.0 * num_threads / static_cast<double>(hw_concurrency));
        per_thread_timeout = static_cast<int>(std::lround(timeout_ms * scale));
    }

    RunResult result;
    result.seed         = base_seed;
    result.games_played = num_games;
    result.win_turns.resize(num_games, -1);

    const bool logging = !log_dir.empty();
    std::map<std::string, std::vector<int>> numbering;
    std::string run_id;

    if (logging)
    {
        std::filesystem::create_directories(log_dir);
        numbering = BuildCardNumbering(deck);
        run_id    = MakeRunId(base_seed);
    }

    // Divide games evenly across threads; first (num_games % num_threads) threads get one extra.
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    int base_count = num_games / num_threads;
    int extra      = num_games % num_threads;
    int start      = 0;

    for (int t = 0; t < num_threads; ++t)
    {
        int count        = base_count + (t < extra ? 1 : 0);
        int thread_start = start;
        start           += count;

        threads.emplace_back([&, thread_start, count]()
        {
            AIEngine   ai(profile, lookahead_depth, per_thread_timeout);
            ai.SetLookaheadBottoming(lookahead_bottoming);
            GameEngine engine(ai);

            for (int li = 0; li < count; ++li)
            {
                int gi = thread_start + li;
                GameState state = SetupGame(deck, base_seed + static_cast<uint64_t>(gi));
                state.vial_target_mv = profile.vial_target_mv;
                GoldFishRunner::PopulateOpponentSpawns(state, base_game_index + gi);

                if (logging) { AssignCardNumbers(state, numbering); }

                GameLogger logger;
                if (logging)
                {
                    logger.StartGame(run_id, gi, "d1",
                                     base_seed + static_cast<uint64_t>(gi), numbering);
                    engine.SetLogger(&logger);
                }

                int win_turn = engine.RunGame(state, max_turns);
                result.win_turns[gi] = win_turn;

                if (logging)
                {
                    engine.SetLogger(nullptr);
                    logger.EndGame(win_turn);
                    std::filesystem::path log_path =
                        log_dir / (run_id + "_game_" + std::to_string(gi) + ".json");
                    logger.WriteToFile(log_path);
                }
            }
        });
    }

    for (std::thread& th : threads) { th.join(); }

    long long sum = 0;
    for (int t : result.win_turns)
    {
        if (t > 0) { ++result.games_won; sum += t; }
    }
    if (result.games_won > 0)
    {
        result.average_win_turn = static_cast<double>(sum) / result.games_won;
    }

    if (logging)
    {
        constexpr uintmax_t MAX_LOG_BYTES = 500ULL * 1024 * 1024; // 500 MB
        CleanupLogs(log_dir, MAX_LOG_BYTES);
    }

    return result;
}

GameState GoldFishRunner::SetupGame(const Decklist& deck, uint64_t seed)
{
    GameState state;

    state.players[0].life = 20;
    state.players[1].life = 20;

    state.players[0].library.assign(deck.mainboard.begin(), deck.mainboard.end());
    state.players[0].library.Shuffle(seed);

    state.active_player_index   = 0;
    state.priority_player_index = 0;
    state.turn_number           = 0;
    state.game_seed             = seed;

    // Alternate play/draw by seed parity. Every caller iterates games as
    // base_seed + i, so consecutive games flip on_the_play, giving a balanced
    // 50/50 split within any run without a separate play/draw parameter.
    state.on_the_play = (seed % 2 == 0);

    return state;
}
