#include "GoldFishRunner.h"
#include "../core/GameEngine.h"
#include "../core/GameLogger.h"
#include "../core/HardwareConcurrency.h"
#include "../ai/AIEngine.h"
#include "../ai/DecisionProviders.h"
#include "../ai/Profiler.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <thread>

// ---- Second-main relevance -------------------------------------------------

// True if the deck contains any card whose value depends on the second
// (post-combat) main phase, so the engine should run a searched second main
// (see AIEngine::SetSearchPostCombat). For everything else the second main is
// skipped: combat creates no new resources in a goldfish, and a searched second
// main roughly doubles the per-turn search cost, so we only pay for it where it
// buys a better line. Two triggers:
//
//   * SPECTACLE: the alternate cost unlocks once the opponent has lost life this
//     turn, so the finisher is cast cheaply AFTER combat.
//
//   * ENABLER-GATED LIFEGAIN REACH (Anti-Lifegain: Tainted Remedy / Plague Drone
//     + alt-cost payloads like Reverent Silence): the deck's face damage flows
//     through "opponent gains life" effects flipped to loss by a lifegain_to_loss
//     enabler. The optimal line is "attack to drop the opponent into range, THEN
//     fire the payload post-combat" -- and some payloads (Reverent Silence wipes
//     the caster's own Remedy/Aria) are ONLY safe post-combat. Without a searched
//     second main the engine misses these post-combat lethals and slips the win a
//     full turn (measured: antilife d5 avg 4.92 -> 4.77, more T3/T4 kills). Plain
//     direct-damage reach (burn) does NOT need this: it is castable pre-combat and
//     the rollout already simulates the attack, so pre/post-combat are equivalent
//     and those decks stay byte-identical (verified: burn/slivers unchanged).
bool GoldFishRunner::DeckUsesSecondMain(const Decklist& deck)
{
    for (const Card& c : deck.mainboard)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        if (!def) { continue; }
        if (def->params.spectacle_cost.has_value()) { return true; }
        if (def->params.lifegain_to_loss)           { return true; }
    }
    return false;
}

// ---- Card numbering --------------------------------------------------------

// Assigns stable integer IDs to each card copy in the deck.
// Cards are sorted alphabetically by name; copies of the same name get
// consecutive numbers (e.g. 4x Lightning Bolt → 1,2,3,4).
std::map<std::string, std::vector<int>> GoldFishRunner::BuildCardNumbering(const Decklist& deck)
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
void GoldFishRunner::AssignCardNumbers(GameState& state,
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
        // 8: a 6/6 wall entering AHEAD of a 1/1 (both on T3). The 6/6 is first in board order and
        // un-killable by any single burn (Blood 2 / Bolt-Blaze 3), so it exercises Searing Blood's
        // targeting: the naive first-creature pick whiffs on the 6/6 (no death trigger), while
        // FindBurnKillTarget correctly hits the 1/1 for the 3-to-face rider. (Goldfish creatures
        // never block/attack, so P/T-per-turn realism is moot -- these are pure burn/removal targets.)
        {{3,6,6},{3,1,1}},
        {{1,1,1},{2,1,1},{3,2,2}},                                    // 9: small + small + mid
    };

    // PATTERNS is a program-lifetime static, so the GameState (and all its search deep copies)
    // can hold a non-owning pointer into it instead of copying the vector per node.
    state.opponent_spawns = &PATTERNS[game_index % 10];
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
                               int lookahead_depth, int timeout_ms, int num_threads)
{
    int requested = num_threads;
    num_threads = concurrency_util::ResolveWorkerThreads(num_threads);
    num_threads = std::min(num_threads, num_games);
    concurrency_util::LogWorkerThreads(std::cerr, "goldfish", requested, num_threads);

    // The search budget is now a deterministic work-unit count (virtual ms), not a
    // wall-clock deadline, so it is thread-invariant by construction: each decision
    // does the same amount of work regardless of how many threads contend for the
    // CPU. The old per-thread timeout scaling (a band-aid for wall-clock starvation
    // under parallel load, and itself a source of nondeterminism) is therefore gone.
    int per_thread_timeout = timeout_ms;

    RunResult result;
    result.seed         = base_seed;
    result.games_played = num_games;
    result.win_turns.resize(num_games, -1);

    // Detect once whether this deck's second main is relevant (e.g. spectacle
    // finishers cast after combat). All worker AIs get the same setting.
    const bool needs_second_main = DeckUsesSecondMain(deck);

    const bool logging = !log_dir.empty();
    std::map<std::string, std::vector<int>> numbering;
    std::string run_id;

    if (logging)
    {
        std::filesystem::create_directories(log_dir);
        numbering = BuildCardNumbering(deck);
        run_id    = MakeRunId(base_seed);
    }

    // Dynamic self-scheduling: rather than statically partitioning games into
    // contiguous per-thread chunks (which leaves a single thread grinding the tail
    // alone whenever a slow game lands late in its chunk), every worker pulls the
    // next game index from a shared atomic counter as soon as it finishes one. All
    // cores stay busy until fewer games remain than there are threads, so the tail
    // is bounded by one slow game per core instead of a whole chunk. This is
    // lossless and deterministic: each game gi is fully independent and indexed by
    // gi (seed = base_seed + gi), so which thread runs it cannot change the result.
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    std::atomic<int> next_game{0};

    for (int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([&]()
        {
            AIEngine   ai(profile, lookahead_depth, per_thread_timeout);
            ai.SetSearchPostCombat(needs_second_main);
            GameEngine engine(ai);

            for (;;)
            {
                int gi = next_game.fetch_add(1, std::memory_order_relaxed);
                if (gi >= num_games) { break; }

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

                PROF_RESET_GAME();
#ifdef MTG_PROFILE
                std::chrono::steady_clock::time_point game_t0 = std::chrono::steady_clock::now();
#endif
                int win_turn = engine.RunGame(state, max_turns);
#ifdef MTG_PROFILE
                double game_ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - game_t0).count();
                PROF_RECORD_GAME(gi, game_ms);
#endif
                result.win_turns[gi] = win_turn;
                // Diagnostic (MTG_DUMP_WINS, inert by default): per-game win turn, for
                // per-game A/B diffs between builds (e.g. `join` two runs to find the
                // games a change moved). Single-thread for ordered output.
                if (std::getenv("MTG_DUMP_WINS"))
                { std::fprintf(stderr, "[win] gi=%d wt=%d\n", gi, win_turn); }

                if (logging)
                {
                    engine.SetLogger(nullptr);
                    logger.EndGame(win_turn);
                    std::filesystem::path log_path =
                        log_dir / (run_id + "_game_" + std::to_string(gi) + ".json");
                    logger.WriteToFile(log_path);
                }
            }
            PROF_FLUSH_THREAD();
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

    // Shuffle-variance instrument (see GameState::shuffle_salt): an independent salt lets the SAME
    // game_seed be replayed with different shuffle realisations. Default 0 -> SaltSeed identity ->
    // byte-identical. shuffle_salt salts mid-game shuffles only (fixed opening); the _OPENING salt
    // also varies the initial deck shuffle + mulligan reshuffles.
    static const uint64_t s_shuffle_salt         = []{ const char* e = std::getenv("MTG_SHUFFLE_SALT");         return e ? std::strtoull(e, nullptr, 10) : 0ull; }();
    static const uint64_t s_shuffle_salt_opening = []{ const char* e = std::getenv("MTG_SHUFFLE_SALT_OPENING"); return e ? std::strtoull(e, nullptr, 10) : 0ull; }();
    // Clairvoyance-decoupling instrument (ANALYSIS ONLY): the salt the SEARCH evaluation uses for its
    // mid-game shuffles. Defaults EQUAL to shuffle_salt (unset -> same value) so normal play is
    // byte-identical/lockstep; set it DIFFERENT to make the search plan against a reshuffle the real
    // executor will not deal (strips shuffle-decision clairvoyance). See GameState::shuffle_salt_search.
    static const bool     s_have_salt_search = std::getenv("MTG_SHUFFLE_SALT_SEARCH") != nullptr;
    static const uint64_t s_shuffle_salt_search = []{ const char* e = std::getenv("MTG_SHUFFLE_SALT_SEARCH"); return e ? std::strtoull(e, nullptr, 10) : 0ull; }();
    state.shuffle_salt         = s_shuffle_salt;
    state.shuffle_salt_opening = s_shuffle_salt_opening;
    state.shuffle_salt_search  = s_have_salt_search ? s_shuffle_salt_search : s_shuffle_salt;

    state.players[0].library.assign(deck.mainboard.begin(), deck.mainboard.end());
    state.players[0].library.Shuffle(SaltSeed(seed, state.shuffle_salt_opening));

    state.active_player_index   = 0;
    state.priority_player_index = 0;
    state.turn_number           = 0;
    state.game_seed             = seed;

    // Alternate play/draw by seed parity. Every caller iterates games as
    // base_seed + i, so consecutive games flip on_the_play, giving a balanced
    // 50/50 split within any run without a separate play/draw parameter.
    state.on_the_play = (seed % 2 == 0);

    // Attach the deck's decision heuristics. This is the single choke point all callers
    // (GoldFishRunner / BatchRunner / AnalyzerEngine / main) funnel through, and every
    // search/rollout state is a copy of this one, so the pointer reaches every node.
    state.m_provider = &SelectDecisionProvider(deck);

    // Decks whose spells target the OPPONENT'S permanents for value (Hinata: Magma Opus taps
    // them, the spread-damage / cost-reduction targeting points at them) get a realistic
    // opponent board -- three lands (a realistic floor: most opponents have >=3 lands, and
    // aggressive decks with fewer bring creatures = better targets anyway; see HinataProvider::
    // OpponentPlaysLands). Pre-populated here (the single setup choke point) so every search
    // deep copy and the real game agree, and present by the time the deck targets them (T4+).
    if (state.m_provider->OpponentPlaysLands())
    {
        for (int i = 0; i < 3; ++i)
        {
            Card land;
            land.m_name = "Opponent Land";
            land.RehashName();
            land.AddType(CardType::Land);
            Permanent perm;
            perm.card             = land;
            perm.controller_index = 1;
            perm.owner_index      = 1;
            state.battlefield.push_back(perm);
        }
    }

    return state;
}
