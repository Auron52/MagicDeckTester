#include "../core/EnvFlags.h"
#include "GoldFishRunner.h"
#include "../core/GameEngine.h"
#include "../core/GameLogger.h"
#include "../core/HardwareConcurrency.h"
#include "../ai/AIEngine.h"
#include "../ai/DecisionProviders.h"
#include "../ai/Profiler.h"
#include "../deck/DeckLoader.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
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

        //   * HINATA CRACKLE COMBO (Hinata, Dawn-Crowned): faithful Crackle with
        //     Power kills its declared discount targets (including Hinata herself),
        //     so casting it pre-combat throws away the attackers' finishing damage.
        //     The optimal line is "attack with Hinata, THEN Crackle post-combat"
        //     targeting her for the discount once she has already dealt combat
        //     damage. Unlike burn, this deck's combat matters (the attack closes the
        //     game), so we need a searched second main to sequence it. The deck does
        //     NOT need main 1 for combat pump (it plays no growing board), so main 1
        //     stays available but the win lands via the post-combat main.
        if (def->params.hinata_cost_reducer)        { return true; }

        //   * GOBLIN LACKEY (combat-damage cheat-into-play): dealing combat damage puts a Goblin
        //     permanent from hand onto the battlefield — a resource GENERATED DURING COMBAT (2c-bis).
        //     The cheated body (and any it draws in via a Muxus/Siege-Gang ETB) is only usable in a
        //     post-combat main, so the deck needs a searched second main to sequence its follow-ups.
        //     Mirrors spectacle_cost / hinata_cost_reducer detection above.
        if (!def->params.combat_damage_puts_subtype_from_hand.empty()) { return true; }

        //   * TWO-HEADED HELLKITE (attack-trigger draw): "Whenever this creature attacks, draw two
        //     cards" -- the drawn cards are a resource GENERATED DURING COMBAT (2c-bis); they are
        //     only spendable in a post-combat main, so the deck needs a searched second main or the
        //     engine silently wastes the trigger every turn.
        if (def->params.attack_draw_cards > 0) { return true; }

        //   * MAELSTROM ARCHANGEL (combat-damage free cast): connecting banks a free cast
        //     (free_casts_available) that is only spendable in the post-combat main -- without the
        //     second main the resource silently evaporates each turn.
        if (def->params.combat_damage_free_cast) { return true; }

        //   * ARMORED SKYHUNTER + PURESTEEL PALADIN (attack-dig put + equipment-ETB draw): the
        //     attack trigger puts an Equipment onto the battlefield DURING combat; with a
        //     Puresteel-style watcher in the deck that put draws a card mid-combat -- a resource
        //     generated during combat (2c-bis), spendable only in a post-combat main. Either
        //     param alone does not need it (the put itself is consumed by this combat's damage;
        //     the draw needs the combat-time put to fire mid-combat), so require BOTH in the
        //     deck. Mirrors attack_draw_cards above.
        if (def->params.attack_dig_attach_count > 0)
        {
            for (const Card& c2 : deck.mainboard)
            {
                const CardDefinition* d2 = CardDatabase::Instance().LookupCached(c2);
                if (d2 && d2->params.draw_on_equipment_etb) { return true; }
            }
        }
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
                               int lookahead_depth, int timeout_ms, int num_threads,
                               int forced_mull_count, std::vector<int> forced_bottom)
{
    const bool forced = forced_mull_count >= 0;
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
        run_id    = MakeRunId(base_seed);
    }
    // Card numbering is needed for logging AND for forced bottoming (bottoms by card m_number).
    if (logging || forced)
    {
        numbering = BuildCardNumbering(deck);
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
            if (forced) { ai.SetForcedMulligan(forced_mull_count, forced_bottom); }
            GameEngine engine(ai);

            for (;;)
            {
                int gi = next_game.fetch_add(1, std::memory_order_relaxed);
                if (gi >= num_games) { break; }

                GameState state = SetupGame(deck, base_seed + static_cast<uint64_t>(gi));
                state.vial_target_mv = profile.vial_target_mv;
                GoldFishRunner::PopulateOpponentSpawns(state, base_game_index + gi);

                if (logging || forced) { AssignCardNumbers(state, numbering); }

                GameLogger logger;
                if (logging)
                {
                    logger.StartGame(run_id, gi, "d1",
                                     base_seed + static_cast<uint64_t>(gi), numbering);
                    engine.SetLogger(&logger);
                }

                PROF_RESET_GAME();
                // Slow-game capture (diagnostic, ON by default at 30 s): time each game and stream any
                // that exceeds MTG_SLOW_GAME_MS, with the seed + index that fully reproduce it. Mirrors
                // the keep generator's MTG_KEEP_SLOW_MS. Without this a degenerate game is INVISIBLE --
                // the FiveColour Stage-4 atom presented as "the analyzer is hung" for 3.4 h because
                // nothing in a Release build reports per-game time (the PROF_RECORD_GAME below is
                // #ifdef MTG_PROFILE, i.e. a separate instrumented build). 30 s is far above any healthy
                // goldfish game, so a quiet stream means no degenerate games. MTG_SLOW_GAME_MS is a
                // LOWER-ONLY override, clamped into [1, 30000]: 0 and out-of-range values leave the baked
                // default rather than disabling the detector. A silent run that turns out to have been
                // crawling teaches nothing, which is the whole reason this exists -- so there is no off
                // switch (same rule as the keep generator's, docs/design/keepgen-no-off-switches.md).
                // One steady_clock read per game is the only overhead, and nothing here feeds a decision
                // -> byte-identical.
                static const long long s_slow_game_ms = []{
                    constexpr long long kDefault = 30000LL;
                    const char* s = std::getenv("MTG_SLOW_GAME_MS");
                    if (!s || !*s) { return kDefault; }
                    const long long raw = std::atoll(s);
                    return (raw <= 0) ? kDefault : std::min(raw, kDefault); }();
                std::chrono::steady_clock::time_point game_t0 = std::chrono::steady_clock::now();
#ifdef MTG_PROFILE
                std::chrono::steady_clock::time_point prof_t0 = std::chrono::steady_clock::now();
#endif
                int win_turn = engine.RunGame(state, max_turns);
#ifdef MTG_PROFILE
                double game_ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - prof_t0).count();
                PROF_RECORD_GAME(gi, game_ms);
#endif
                {
                    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - game_t0).count();
                    if (ms >= s_slow_game_ms)
                    {
                        // The replay seed is base_seed + gi, NOT base_seed: SetupGame above shuffles
                        // on base_seed + gi while PopulateOpponentSpawns uses base_game_index + gi,
                        // so a single-game replay needs --seed (base_seed+gi) --game-index gi (which
                        // then runs loop index 0 against that same shuffle + spawn pattern). Same
                        // convention as the "Unwon games" repro list in main.cpp. Printing the bare
                        // base_seed silently replays game 0 instead.
                        std::fprintf(stderr,
                            "[goldfish] SLOW-GAME %lldms  gi=%d wt=%d  repro: --seed %llu "
                            "--game-index %d --games 1\n",
                            ms, gi, win_turn,
                            static_cast<unsigned long long>(base_seed + static_cast<uint64_t>(gi)), gi);
                        std::fflush(stderr);
                    }
                }
                result.win_turns[gi] = win_turn;
                // Diagnostic (MTG_DUMP_WINS, inert by default): per-game win turn, for
                // per-game A/B diffs between builds (e.g. `join` two runs to find the
                // games a change moved). Single-thread for ordered output.
                static const bool s_dump_wins = EnvOn("MTG_DUMP_WINS");
                if (s_dump_wins)
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
    result.avg_turns = ComputeAvgTurns(result.win_turns, max_turns);

    if (logging)
    {
        constexpr uintmax_t MAX_LOG_BYTES = 500ULL * 1024 * 1024; // 500 MB
        CleanupLogs(log_dir, MAX_LOG_BYTES);
    }

    return result;
}

// ---- CRN across decklists: a supplied, inherited card numbering --------------
//
// MTG_DECK_NUMBERING=<numbering.json>  ->  {"Card Name": [n1, n2, ...], ...}
// Value-carrying, so it keeps the raw getenv+parse form (coding-conventions rule 3).
// Unset / "0" / empty -> OFF -> every path below is byte-identical.
//
// The problem. Comparing two COMBINATIONS of one card pool (4 Bolt/4 Skullcrack vs 5 Bolt/3
// Skullcrack) is normally an UNPAIRED measurement: the opening shuffle is a positional Fisher-Yates
// over deck.mainboard, so changing a count re-permutes the whole game and the two arms share nothing
// but the seed. Measured on burn (5000 games, d5): a naive count edit leaves only 76.2% of games on
// the same win turn, sd(diff)=0.592 -> ~3,500 games to resolve a 0.03t effect, vs 215 when aligned.
//
// The engine deliberately does NOT compute the numbering. Correspondence between two decklists is a
// property of the EDIT that produced one from the other, which only the caller knows; the rule is
// "a replacement INHERITS the number of the card it replaced, and nothing is ever renumbered". So
// the driver derives the map from base+edits and the engine simply applies it, pre-shuffle, and then
// orders the opening library by those stable identities (Library::ShuffleByKey) rather than by
// position. Every unchanged card then keeps its sort key -> the two decks share one key order, and
// the replaced slot swaps identity IN PLACE.
//
// Why inheritance and not a name-union (.claude/skills/mtg-ai.md's scheme, never implemented):
// a union numbers by NAME, so a swap deletes two numbers and inserts two others at their own keyed
// ranks, shifting everything in between. Measured on slivers: 80.3% of games identical vs 93.3% for
// in-place. remove+add is NOT the same operation as replace, and must not be "simplified" into it.
//
// Requirements on the map: numbers unique within a game (ShuffleByKey's total order is (key,
// m_number)) and NON-ZERO (0 is the unnumbered sentinel -- all-zero is what once made the batch's
// reshuffle a silent no-op). Contiguity is NOT required: m_number is never an array index anywhere,
// only an equality-compared per-copy identity, so holes and out-of-range values are fine.
//
// This is a COMPARISON mode, not a default: it moves every opening hand, so it would invalidate all
// ground truth. Off unless asked for.
namespace
{
const std::map<std::string, std::vector<int>>* DeckNumbering()
{
    static const std::map<std::string, std::vector<int>> s_map = []
    {
        std::map<std::string, std::vector<int>> m;
        const char* e = std::getenv("MTG_DECK_NUMBERING");
        if (e == nullptr || *e == '\0' || std::string(e) == "0") { return m; }
        // Throws on a bad path/parse -- a mis-specified numbering must fail loudly, never silently
        // fall back to per-deck numbering (that would read as "comparison mode on" while measuring
        // unpaired arms, which is exactly the failure this mode exists to remove).
        std::ifstream f(e);
        if (!f) { throw std::runtime_error(std::string("MTG_DECK_NUMBERING: cannot open ") + e); }
        nlohmann::json j; f >> j;
        for (auto it = j.begin(); it != j.end(); ++it)
        { m[it.key()] = it.value().get<std::vector<int>>(); }
        if (m.empty()) { throw std::runtime_error("MTG_DECK_NUMBERING: empty numbering map"); }
        return m;
    }();
    return s_map.empty() ? nullptr : &s_map;
}
}   // namespace

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
    static const bool     s_have_salt_search = EnvSet("MTG_SHUFFLE_SALT_SEARCH");
    static const uint64_t s_shuffle_salt_search = []{ const char* e = std::getenv("MTG_SHUFFLE_SALT_SEARCH"); return e ? std::strtoull(e, nullptr, 10) : 0ull; }();
    state.shuffle_salt         = s_shuffle_salt;
    state.shuffle_salt_opening = s_shuffle_salt_opening;
    state.shuffle_salt_search  = s_have_salt_search ? s_shuffle_salt_search : s_shuffle_salt;

    state.players[0].library.assign(deck.mainboard.begin(), deck.mainboard.end());

    // Supplied numbering (see DeckNumbering above): apply the caller's map PRE-shuffle, then order
    // the opening library by those stable identities instead of by position, so two combinations of
    // one pool draw the same cards except where the edit actually reaches. nullptr (the default) ->
    // the historical positional shuffle, byte-identical.
    // Per-job override (decknumbering::t_map) wins; nullptr falls back to the env static, so a
    // single run and every pre-existing manifest are byte-identical.
    const std::map<std::string, std::vector<int>>* supplied =
        decknumbering::t_map ? decknumbering::t_map : DeckNumbering();
    if (supplied != nullptr)
    {
        // Pre-shuffle the library is in decklist order, so AssignCardNumbers' per-name copy counter
        // hands copy k of a card the map's k-th entry -- independent of where the file lists it.
        AssignCardNumbers(state, *supplied);
        for (const Card& c : state.players[0].library)
        {
            // m_number 0 == the name is absent from the map, or this deck holds more copies than the
            // map allots. Either way ShuffleByKey would key every such card identically and clump
            // them: refuse rather than silently measure a broken shuffle.
            if (c.m_number == 0)
            {
                throw std::runtime_error("MTG_DECK_NUMBERING: no number for '" + c.m_name.str()
                                         + "' (the map must cover every card, with enough copies)");
            }
        }
        state.players[0].library.ShuffleByKey(SaltSeed(seed, state.shuffle_salt_opening));
    }
    else
    {
        state.players[0].library.Shuffle(SaltSeed(seed, state.shuffle_salt_opening));
    }

    // Assign stable per-copy card numbers at the single setup choke point, ALWAYS -- so every
    // caller (batch GT / goldfish CLI / analyzer / viewer / references) numbers identically and
    // the CRN mid-game reshuffle (Library::ShuffleByKey, keyed on m_number) is REAL and CONSISTENT
    // everywhere. Previously numbering happened only under --log-dir / forced-mulligan, so the
    // batch (which never numbered) ran ShuffleByKey with all m_number==0 -> every key equal ->
    // stable_sort a no-op -> the "reshuffle" left the library untouched, diverging from the
    // numbered viewer/reference/audit runs. Numbering is post-shuffle-order based (same as the old
    // logging path) and independent of the opening Fisher-Yates shuffle, so opening hands are
    // unchanged; only decks that mid-game reshuffle (Hinata Ponder, antilife fetch) move.
    // Escape hatch MTG_LEGACY_UNNUMBERED restores the old number-only-when-logging behavior for A/B.
    // A SUPPLIED numbering has already been applied (pre-shuffle) and MUST NOT be renumbered here:
    // per-deck numbering would overwrite the caller's stable identities with deck-local ones and undo
    // the alignment the ShuffleByKey above just bought.
    static const bool s_legacy_unnumbered = EnvOn("MTG_LEGACY_UNNUMBERED");
    if (supplied == nullptr && !s_legacy_unnumbered)
    {
        AssignCardNumbers(state, BuildCardNumbering(deck));
    }

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
