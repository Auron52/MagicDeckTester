#include <iostream>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <random>
#include <algorithm>
#include <vector>
#include <thread>
#include <sstream>
#include <cstdlib>
#include "deck/DeckLoader.h"
#include "cards/CardDatabase.h"
#include "runner/GoldFishRunner.h"
#include "runner/BatchRunner.h"
#include "ai/AIEngine.h"
#include "ai/TurnSolver.h"
#include "core/GameEngine.h"
#include "core/GameLogger.h"
#include "core/HardwareConcurrency.h"
#include "ai/MulliganProfileIO.h"
#include "ai/Profiler.h"

static void PrintUsage(const char* prog)
{
    std::cerr << "Usage: " << prog
              << " <deckfile> [--games N] [--seed S] [--max-turns T]"
                 " [--depth D] [--budget-ms M] [--profile path] [--log-dir path] [--cards-json path]\n"
              << "  <deckfile>      Plain text (.txt) or Cockatrice (.cod) decklist\n"
              << "  --games N       Number of games to simulate (default: 10000)\n"
              << "  --seed S        Base RNG seed (omit to generate randomly)\n"
              << "  --max-turns T   Maximum turns before declaring no-win (default: 20)\n"
              << "  --depth D       Lookahead depth (default: 0; higher = stronger but slower)\n"
              << "  --budget-ms M   Per-decision search budget in deterministic 'virtual ms';\n"
              << "                  0 = unlimited (default: 0). Alias: --timeout-ms\n"
              << "  --threads N     Worker threads (default: 0 = auto, affinity-based CPU count)\n"
              << "  --profile P     Path to a .profile.json file (default: auto-detect deckname.profile.json)\n"
              << "  --log-dir P     Write one JSON game log per game into this directory\n"
              << "  --cards-json P  Path to card definitions JSON (default: src/cards/data/cards.json)\n";
}

static std::vector<std::string> SortedHandNames(GameState& state)
{
    std::vector<std::string> names;
    for (const Card& c : state.ActivePlayer().hand) { names.push_back(c.m_name); }
    std::sort(names.begin(), names.end());
    return names;
}

// ---- Claude-play / human-play prototype (opt-in; --claude-play) ---------------
// An external decision provider drives the goldfish's MAIN phases (combat + cleanup
// stay on the engine heuristics). Stateless-replay protocol: each process run replays
// the deterministic game (fixed seed + game-index) applying the pre-supplied --choices,
// and when it reaches the first un-chosen main-phase decision it prints that decision
// (current legal info + the enumerated legal plans) and exits with code 70. The driver
// (a Claude agent) reads it, appends a plan index, and re-invokes. When every decision
// is supplied the game finishes and the result (win turn) is printed. Purpose: a flag-
// generating verification sweep -- a game Claude wins earlier than the AI, or a plan
// set that looks wrong, is a flag for the analyzer's convergence loop to investigate.
static void JsonStr(std::ostream& os, const std::string& s)
{
    os << '"';
    for (char c : s)
    {
        if (c == '"' || c == '\\') { os << '\\' << c; }
        else                        { os << c; }
    }
    os << '"';
}

static void JsonNameArray(std::ostream& os, const std::vector<std::string>& names)
{
    os << '[';
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (i) { os << ", "; }
        JsonStr(os, names[i]);
    }
    os << ']';
}

static std::vector<std::string> BattlefieldNames(const GameState& s, int controller)
{
    std::vector<std::string> names;
    for (const Permanent& p : s.battlefield)
    {
        if (p.controller_index == controller) { names.push_back(p.card.m_name); }
    }
    std::sort(names.begin(), names.end());
    return names;
}

// One-line human-readable summary of a candidate plan (land drop + casts).
static std::string SummarizePlan(const TurnSolver::Plan& plan)
{
    std::ostringstream os;
    if (plan.land_decided && !plan.land_to_play.empty()) { os << "land=" << plan.land_to_play << "; "; }
    else if (plan.land_decided)                           { os << "land=none; "; }
    std::vector<std::string> casts;
    for (const Action& a : plan.actions)
    {
        std::string tag;
        switch (a.kind)
        {
            case Action::Kind::CastFromHand:      tag = a.card_name; break;
            case Action::Kind::CastFromGraveyard: tag = a.card_name + " (retrace)"; break;
            case Action::Kind::ActivateVial:      tag = a.card_name + " (vial)"; break;
            case Action::Kind::PlayLand:          tag = a.card_name + " (land)"; break;
            default:                              tag = a.card_name + " (other)"; break;
        }
        if (a.sacrifice_land) { tag += " +sac-land"; }
        if (a.discard_lands)  { tag += " +discard" + std::to_string(a.discard_lands); }
        casts.push_back(tag);
    }
    if (casts.empty()) { os << "cast: (nothing)"; }
    else
    {
        os << "cast: ";
        for (size_t i = 0; i < casts.size(); ++i) { if (i) os << ", "; os << casts[i]; }
    }
    return os.str();
}

// Writes the decision as a JSON object (no markers) to `os`. Used for both the live
// stdout dump (wrapped in <<<CLAUDE_DECISION>>> markers by the caller) and the per-game
// trace log written on game completion (--log-dir).
static void WriteDecisionJson(std::ostream& os, const GameState& s,
                              const std::vector<TurnSolver::Plan>& plans,
                              bool is_pre_combat, int decision_index, int reveal_count)
{
    const Player& me  = s.ActivePlayer();
    int           opp = 1 - s.active_player_index;
    std::vector<std::string> hand;
    for (const Card& c : me.hand) { hand.push_back(c.m_name); }
    std::sort(hand.begin(), hand.end());
    std::vector<std::string> gy;
    for (const Card& c : me.graveyard) { gy.push_back(c.m_name); }
    std::sort(gy.begin(), gy.end());

    os << "{\n";
    os << "  \"decision_index\": " << decision_index << ",\n";
    os << "  \"type\": \"main_phase\",\n";
    os << "  \"turn\": " << s.turn_number << ",\n";
    os << "  \"phase\": \"" << (is_pre_combat ? "pre_main" : "post_main") << "\",\n";
    os << "  \"on_the_play\": " << (s.on_the_play ? "true" : "false") << ",\n";
    os << "  \"me\": { \"life\": " << me.life << ", \"battlefield\": ";
    JsonNameArray(os, BattlefieldNames(s, s.active_player_index));
    // Aether Vial charge counters (a Vial deploys a creature whose MV EQUALS its
    // counters) — exposed so the player needn't guess the Vial's state.
    {
        std::vector<int> vials;
        for (const Permanent& p : s.battlefield)
        {
            if (p.controller_index != s.active_player_index) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (d && d->params.upkeep_adds_charge) { vials.push_back(p.charge_counters); }
        }
        os << ", \"vial_counters\": [";
        for (size_t i = 0; i < vials.size(); ++i) { if (i) os << ", "; os << vials[i]; }
        os << "]";
    }
    // Hand as {name, cost, mv} objects so the player judges affordability from the real
    // card data, not memory (the slivers false positives came from guessing costs).
    os << ", \"hand\": [";
    for (size_t i = 0; i < hand.size(); ++i)
    {
        if (i) { os << ", "; }
        const CardDefinition* d = CardDatabase::Instance().Lookup(hand[i]);
        os << "{ \"name\": "; JsonStr(os, hand[i]);
        os << ", \"cost\": "; JsonStr(os, d ? d->card.m_mana_cost.ToString() : std::string());
        os << ", \"mv\": " << (d ? d->card.m_mana_cost.ManaValue() : 0) << " }";
    }
    os << "]";
    os << ", \"graveyard\": "; JsonNameArray(os, gy);
    os << ", \"library_size\": " << me.library.size();
    if (reveal_count > 0)
    {
        // Optional partial clairvoyance (--reveal N): the next N draws, in draw order
        // (library top = index 0). A small "accessible part" of the library, not the
        // whole thing -- enough foresight to plan a line without full clairvoyance.
        int n = std::min(reveal_count, static_cast<int>(me.library.size()));
        std::vector<std::string> up;
        for (int k = 0; k < n; ++k) { up.push_back(me.library[k].m_name); }
        os << ", \"upcoming_draws\": ";
        JsonNameArray(os, up);
    }
    os << " },\n";
    os << "  \"opponent\": { \"life\": " << s.players[opp].life << ", \"battlefield\": ";
    JsonNameArray(os, BattlefieldNames(s, opp));
    os << " },\n";
    os << "  \"plans\": [\n";
    for (size_t i = 0; i < plans.size(); ++i)
    {
        os << "    { \"index\": " << i << ", \"summary\": ";
        JsonStr(os, SummarizePlan(plans[i]));
        os << (i + 1 < plans.size() ? " },\n" : " }\n");
    }
    os << "  ],\n";
    os << "  \"note\": \"reply with one plan index (0-based), or -1 to pass / cast nothing\"\n";
    os << "}\n";
}

// Vial-as-a-choice decision: whether to add a charge counter to an Aether Vial this
// upkeep. The reply is 1 (add a counter) or 0 (hold). `heuristic` is the default the
// encoded AI would take.
static void WriteVialDecisionJson(std::ostream& os, const GameState& s,
                                  const Permanent& vial, int decision_index, bool heuristic)
{
    const Player& me  = s.ActivePlayer();
    int           opp = 1 - s.active_player_index;
    std::vector<std::string> hand;
    for (const Card& c : me.hand) { hand.push_back(c.m_name); }
    std::sort(hand.begin(), hand.end());

    os << "{\n";
    os << "  \"decision_index\": " << decision_index << ",\n";
    os << "  \"type\": \"vial_charge\",\n";
    os << "  \"turn\": " << s.turn_number << ",\n";
    os << "  \"vial\": "; JsonStr(os, vial.card.m_name);
    os << ", \"current_counters\": " << vial.charge_counters << ",\n";
    os << "  \"heuristic_default\": " << (heuristic ? 1 : 0) << ",\n";
    os << "  \"me\": { \"life\": " << me.life << ", \"hand\": "; JsonNameArray(os, hand);
    os << " },\n";
    os << "  \"opponent\": { \"life\": " << s.players[opp].life << " },\n";
    os << "  \"note\": \"reply 1 to add a charge counter this upkeep, 0 to hold. Aether "
          "Vial deploys a creature whose mana value EQUALS its counter count.\"\n";
    os << "}\n";
}

static int RunClaudePlay(const Decklist& deck, const MulliganProfile& profile,
                         uint64_t seed, int game_index, int max_turns,
                         int lookahead_depth, int timeout_ms, std::vector<int> choices,
                         int reveal_count, const std::filesystem::path& log_dir)
{
    GameState state = GoldFishRunner::SetupGame(deck, seed);
    state.vial_target_mv = profile.vial_target_mv;
    GoldFishRunner::PopulateOpponentSpawns(state, game_index);

    AIEngine ai(profile, lookahead_depth, timeout_ms);
    size_t cursor = 0;
    int decisions_made = 0;
    std::vector<std::string> trace;   // one entry per RESOLVED decision (for --log-dir)
    ai.SetExternalChooser(
        [&](const GameState& s, const std::vector<TurnSolver::Plan>& plans, bool is_pre) -> int
        {
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                if (!log_dir.empty())
                {
                    // Record this resolved decision (state + plans + the chosen index).
                    // Only the completing full-CSV run writes the trace file (below).
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteDecisionJson(ss, s, plans, is_pre, di, reveal_count);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteDecisionJson(std::cout, s, plans, is_pre, di, reveal_count);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);   // distinct code: "more input needed"
        });

    // Vial-as-a-choice: claude decides each Aether Vial upkeep charge. Shares the single
    // --choices stream + cursor with the main chooser (consulted at upkeep, before the
    // main phase). Reply 1 = add a counter, 0 = hold. (Future default: only surface this
    // when the decision is genuinely ambiguous, not every upkeep.)
    ai.SetExternalVialChooser(
        [&](const GameState& s, const Permanent& vial, bool heuristic) -> bool
        {
            int di = static_cast<int>(cursor);
            if (cursor < choices.size())
            {
                int chosen = choices[cursor++];
                ++decisions_made;
                if (!log_dir.empty())
                {
                    std::ostringstream ss;
                    ss << "{ \"chosen\": " << chosen << ", \"decision\": ";
                    WriteVialDecisionJson(ss, s, vial, di, heuristic);
                    ss << "}";
                    trace.push_back(ss.str());
                }
                return chosen != 0;
            }
            std::cout << "<<<CLAUDE_DECISION>>>\n";
            WriteVialDecisionJson(std::cout, s, vial, di, heuristic);
            std::cout << "<<<END_DECISION>>>\n";
            std::cout.flush();
            std::exit(70);
        });

    GameEngine engine(ai);
    int win_turn = engine.RunGame(state, max_turns);
    bool won = win_turn > 0 && win_turn <= max_turns;

    // Game completed (every decision was supplied). Write the per-game trace if asked.
    if (!log_dir.empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(log_dir, ec);
        std::ostringstream fn;
        fn << "claude_s" << seed << "_gi" << game_index << ".json";
        std::ofstream out(log_dir / fn.str());
        out << "{\n  \"seed\": " << seed << ", \"game_index\": " << game_index
            << ", \"win_turn\": " << (won ? win_turn : -1)
            << ", \"won\": " << (won ? "true" : "false") << ",\n  \"decisions\": [\n";
        for (size_t i = 0; i < trace.size(); ++i)
        {
            out << "    " << trace[i] << (i + 1 < trace.size() ? ",\n" : "\n");
        }
        out << "  ]\n}\n";
        std::cerr << "Claude-play trace written to " << (log_dir / fn.str()).string() << "\n";
    }

    std::cout << "<<<CLAUDE_RESULT>>>\n{ \"win_turn\": " << (won ? win_turn : -1)
              << ", \"won\": " << (won ? "true" : "false")
              << ", \"decisions_made\": " << decisions_made << " }\n<<<END_RESULT>>>\n";
    return 0;
}

// Plays a (post-mulligan) state to a win turn at the given lookahead depth.
// Takes state by value so the caller's copy is preserved for reuse.
// trace: enable per-pass candidate trace output for the T1 decision.
static int PlayOutWinTurn(GameState state, const MulliganProfile& profile,
                          int depth, int timeout_ms, int max_turns,
                          bool trace = false)
{
    TurnSolver::SetTraceSolve(trace);
    AIEngine   ai(profile, depth, timeout_ms);
    GameEngine engine(ai);
    int win_turn = engine.PlayOut(state, max_turns);
    TurnSolver::SetTraceSolve(false);
    return win_turn > 0 ? win_turn : max_turns + 1;
}

// Replays a post-mulligan state with a GameLogger attached, writing the log to log_path.
// Used by the depth-divergence diagnostic to record the actual game when d3 and d4 diverge.
static void PlayOutLogged(GameState state, const MulliganProfile& profile,
                          int depth, int timeout_ms, int max_turns,
                          uint64_t game_seed,
                          const std::map<std::string, std::vector<int>>& numbering,
                          const std::filesystem::path& log_path)
{
    GoldFishRunner::AssignCardNumbers(state, numbering);

    GameLogger logger;
    logger.StartGame("diag_d" + std::to_string(depth), 0, "d1", game_seed, numbering);

    AIEngine   ai(profile, depth, timeout_ms);
    ai.SetLogger(&logger);
    GameEngine engine(ai);
    engine.SetLogger(&logger);

    int win_turn = engine.PlayOut(state, max_turns);
    logger.EndGame(win_turn);
    logger.WriteToFile(log_path);
}

// Diagnostic: attribute the depth-4-worse-than-depth-3 (bottoming auto-on at depth>0)
// regression to its locus. For each game, run bottoming at depth 3 and depth 4
// (the keep decision is depth-independent, so both reach the same pre-bottom hand
// and identical library order — they differ only in which card bottoming chose).
// Then play the resulting state out at each depth, forming a 2x2:
//   W33 = bottom@3, play@3   W34 = bottom@3, play@4 (isolates main-phase depth)
//   W43 = bottom@4, play@3   W44 = bottom@4, play@4 (W43 isolates bottoming choice)
//
// Games are distributed evenly across num_threads (0 = hardware_concurrency).
// The budget is virtual/deterministic, so results are thread-invariant.
// Logging and trace work is serialised in a post-pass (bounded by MAX_EXAMPLES).
static void RunDepthDivergenceDiagnostic(const Decklist& deck, const MulliganProfile& profile,
                                         int num_games, uint64_t base_seed,
                                         int max_turns, int timeout_ms,
                                         int num_threads = 0,
                                         const std::filesystem::path& log_dir = {},
                                         bool trace_divergence = false)
{
    const int MAX_EXAMPLES = 10;

    bool logging = !log_dir.empty();
    std::map<std::string, std::vector<int>> numbering;
    if (logging)
    {
        numbering = GoldFishRunner::BuildCardNumbering(deck);
        std::filesystem::create_directories(log_dir);
    }

    // Per-game result; pre-allocated so threads write to disjoint indices — no mutex needed.
    struct GameResult
    {
        int W33 = 0, W34 = 0, W43 = 0, W44 = 0;
        bool bottom_differs    = false;
        bool mainphase_differs = false;
        bool mulliganed        = false;
        std::vector<std::string> hand3;
        std::vector<std::string> hand4;
    };
    std::vector<GameResult> results(num_games);

    // Thread count setup (mirrors GoldFishRunner).
    num_threads = concurrency_util::ResolveWorkerThreads(num_threads);
    num_threads = std::min(num_threads, num_games);

    int base_count = num_games / num_threads;
    int extra      = num_games % num_threads;
    int start      = 0;

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t)
    {
        int count        = base_count + (t < extra ? 1 : 0);
        int thread_start = start;
        start           += count;

        threads.emplace_back([&, thread_start, count]()
        {
            for (int li = 0; li < count; ++li)
            {
                int i = thread_start + li;
                uint64_t seed = base_seed + static_cast<uint64_t>(i);
                GameResult& gr = results[i];

                GameState s3 = GoldFishRunner::SetupGame(deck, seed);
                s3.vial_target_mv = profile.vial_target_mv;
                GoldFishRunner::PopulateOpponentSpawns(s3, i);
                AIEngine bot3(profile, 3, timeout_ms);
                bot3.HandleMulligan(s3, max_turns);
                gr.hand3 = SortedHandNames(s3);

                GameState s4 = GoldFishRunner::SetupGame(deck, seed);
                s4.vial_target_mv = profile.vial_target_mv;
                GoldFishRunner::PopulateOpponentSpawns(s4, i);
                AIEngine bot4(profile, 4, timeout_ms);
                bot4.HandleMulligan(s4, max_turns);
                gr.hand4 = SortedHandNames(s4);

                gr.bottom_differs = (gr.hand3 != gr.hand4);
                gr.mulliganed     = (static_cast<int>(gr.hand3.size()) < 7);

                gr.W33 = PlayOutWinTurn(s3, profile, 3, timeout_ms, max_turns);
                gr.W34 = PlayOutWinTurn(s3, profile, 4, timeout_ms, max_turns);
                gr.W43 = PlayOutWinTurn(s4, profile, 3, timeout_ms, max_turns);
                gr.W44 = PlayOutWinTurn(s4, profile, 4, timeout_ms, max_turns);

                gr.mainphase_differs = (gr.W34 != gr.W33);
            }
        });
    }

    for (std::thread& th : threads) { th.join(); }

    // Serial post-pass: accumulate counters and print examples in seed order.
    // Logging and tracing re-run the deterministic mulligan for each diverging game
    // rather than carrying full GameState copies through the parallel phase.
    int    bottom_diff     = 0;
    int    mainphase_diff  = 0;
    int    mulliganed      = 0;
    double sum33 = 0.0, sum34 = 0.0, sum43 = 0.0, sum44 = 0.0;
    int    examples_shown  = 0;
    int    mainphase_shown = 0;

    for (int i = 0; i < num_games; ++i)
    {
        const GameResult& gr = results[i];
        uint64_t seed = base_seed + static_cast<uint64_t>(i);

        if (gr.bottom_differs)    { ++bottom_diff; }
        if (gr.mainphase_differs) { ++mainphase_diff; }
        if (gr.mulliganed)        { ++mulliganed; }
        sum33 += gr.W33; sum34 += gr.W34; sum43 += gr.W43; sum44 += gr.W44;

        if (gr.mainphase_differs && mainphase_shown < MAX_EXAMPLES)
        {
            ++mainphase_shown;
            std::cout << "[mainphase] seed " << seed << "  W33=" << gr.W33
                      << " W34=" << gr.W34 << " (spawn pattern " << (i % 10) << ")  hand: ";
            for (const std::string& n : gr.hand3) { std::cout << n << " | "; }
            std::cout << "\n";

            if (logging || trace_divergence)
            {
                // Reconstruct the post-mulligan state for this seed (cheap + deterministic).
                GameState s3 = GoldFishRunner::SetupGame(deck, seed);
                s3.vial_target_mv = profile.vial_target_mv;
                GoldFishRunner::PopulateOpponentSpawns(s3, i);
                AIEngine bot3(profile, 3, timeout_ms);
                bot3.HandleMulligan(s3, max_turns);

                if (logging)
                {
                    std::string prefix = std::to_string(seed);
                    PlayOutLogged(s3, profile, 3, timeout_ms, max_turns, seed, numbering,
                                  log_dir / (prefix + "_play3.json"));
                    PlayOutLogged(s3, profile, 4, timeout_ms, max_turns, seed, numbering,
                                  log_dir / (prefix + "_play4.json"));
                    std::cout << "  -> logs: " << prefix << "_play3.json / " << prefix << "_play4.json\n";
                }
                if (trace_divergence)
                {
                    std::cerr << "\n=== T1 TRACE depth=3 (seed " << seed << ") ===\n";
                    PlayOutWinTurn(s3, profile, 3, timeout_ms, max_turns, /*trace=*/true);
                    std::cerr << "\n=== T1 TRACE depth=4 (seed " << seed << ") ===\n";
                    PlayOutWinTurn(s3, profile, 4, timeout_ms, max_turns, /*trace=*/true);
                }
            }
        }

        if (gr.bottom_differs && examples_shown < MAX_EXAMPLES)
        {
            ++examples_shown;
            std::cout << "[bottoming] seed " << seed << " (W33=" << gr.W33
                      << " W34=" << gr.W34 << " W43=" << gr.W43 << " W44=" << gr.W44 << ")\n";
            std::cout << "  bottom@3 keeps: ";
            for (const std::string& n : gr.hand3) { std::cout << n << " | "; }
            std::cout << "\n  bottom@4 keeps: ";
            for (const std::string& n : gr.hand4) { std::cout << n << " | "; }
            std::cout << "\n";

            if (logging || trace_divergence)
            {
                // Reconstruct both post-mulligan states (cheap + deterministic).
                GameState s3b = GoldFishRunner::SetupGame(deck, seed);
                s3b.vial_target_mv = profile.vial_target_mv;
                GoldFishRunner::PopulateOpponentSpawns(s3b, i);
                AIEngine bot3b(profile, 3, timeout_ms);
                if (trace_divergence)
                {
                    std::cerr << "\n=== BOTTOMING TRACE depth=3 (seed " << seed << ") ===\n";
                    TurnSolver::SetTraceSolve(true);
                }
                bot3b.HandleMulligan(s3b, max_turns);
                TurnSolver::SetTraceSolve(false);

                GameState s4b = GoldFishRunner::SetupGame(deck, seed);
                s4b.vial_target_mv = profile.vial_target_mv;
                GoldFishRunner::PopulateOpponentSpawns(s4b, i);
                AIEngine bot4b(profile, 4, timeout_ms);
                if (trace_divergence)
                {
                    std::cerr << "\n=== BOTTOMING TRACE depth=4 (seed " << seed << ") ===\n";
                    TurnSolver::SetTraceSolve(true);
                }
                bot4b.HandleMulligan(s4b, max_turns);
                TurnSolver::SetTraceSolve(false);

                if (logging)
                {
                    std::string prefix = std::to_string(seed);
                    PlayOutLogged(s3b, profile, 3, timeout_ms, max_turns, seed, numbering,
                                  log_dir / (prefix + "_bottom3_play3.json"));
                    PlayOutLogged(s4b, profile, 3, timeout_ms, max_turns, seed, numbering,
                                  log_dir / (prefix + "_bottom4_play3.json"));
                    std::cout << "  -> logs: " << prefix << "_bottom3_play3.json / "
                              << prefix << "_bottom4_play3.json\n";
                }
            }
        }
    }

    double n = static_cast<double>(num_games);
    std::cout << "\n=== DEPTH DIVERGENCE (" << num_games << " games, " << num_threads
              << " threads, budget " << timeout_ms << "ms, bottoming on at depth>0) ===\n";
    std::cout << "bottoming differs (d3 vs d4 kept hand): " << bottom_diff
              << " (" << (100.0 * bottom_diff / n) << "%)\n";
    std::cout << "main-phase differs (W34 != W33):        " << mainphase_diff
              << " (" << (100.0 * mainphase_diff / n) << "%)\n";
    std::cout << "mulliganed (kept < 7):                  " << mulliganed
              << " (" << (100.0 * mulliganed / n) << "%)\n";
    std::cout << "mean W33 (bottom@3 play@3): " << (sum33 / n) << "\n";
    std::cout << "mean W34 (bottom@3 play@4): " << (sum34 / n)
              << "   [main-phase effect (W34-W33): " << ((sum34 - sum33) / n) << "]\n";
    std::cout << "mean W43 (bottom@4 play@3): " << (sum43 / n)
              << "   [bottoming effect  (W43-W33): " << ((sum43 - sum33) / n) << "]\n";
    std::cout << "mean W44 (bottom@4 play@4): " << (sum44 / n)
              << "   [total             (W44-W33): " << ((sum44 - sum33) / n) << "]\n";
}

// Per-game ground-truth log: one "<game_index> <win_turn>" line per game (win_turn
// <= 0 means no win within max_turns). Written to <dir>/<name>.wins. These are the
// committed regression ground truth at the per-game level, so a later run can diff
// new logs against them to see EXACTLY which games changed -- without rebuilding the
// old binary. The fingerprint (won/avg) is derivable from this, but the per-game log
// is what makes "analyze every changed game before --accept" a cheap built-in diff.
static void WriteGameLog(const std::filesystem::path& dir, const std::string& name,
                         const std::vector<int>& win_turns)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::ofstream out(dir / (name + ".wins"));
    for (int gi = 0; gi < static_cast<int>(win_turns.size()); ++gi)
    {
        out << gi << ' ' << win_turns[gi] << '\n';
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        PrintUsage(argv[0]);
        return 1;
    }

    // Batch mode: pool every game from every job in a manifest into one work queue.
    //   mtg.exe --batch <manifest.json> [--threads N] [--cards-json P]
    if (std::string(argv[1]) == "--batch")
    {
        std::filesystem::path manifest;
        std::filesystem::path cards_json  = "src/cards/data/cards.json";
        std::filesystem::path game_log_dir;
        int                   num_threads = 0;
        for (int i = 2; i < argc; ++i)
        {
            std::string flag = argv[i];
            if (flag == "--threads"    && i + 1 < argc) { num_threads = std::stoi(argv[++i]); }
            else if (flag == "--cards-json" && i + 1 < argc) { cards_json = argv[++i]; }
            else if (flag == "--game-log-dir" && i + 1 < argc) { game_log_dir = argv[++i]; }
            else if (manifest.empty())                  { manifest = flag; }
        }
        if (manifest.empty())
        {
            std::cerr << "Usage: " << argv[0]
                      << " --batch <manifest.json> [--threads N] [--cards-json P]\n";
            return 1;
        }
        try
        {
            if (std::filesystem::exists(cards_json))
            {
                CardDatabase::Instance().LoadFromJson(cards_json);
            }
            std::cout << "=== BATCH (streaming per-job results as each job finishes) ===\n"
                      << std::flush;
            // Stream each job's line the moment it completes (jobs arrive in completion
            // order, not manifest order). Flush so progress is visible live rather than
            // buffered until the whole batch ends.
            auto on_job_done = [&](const BatchJobResult& r)
            {
                double pct = r.games_played > 0
                             ? 100.0 * r.games_won / r.games_played : 0.0;
                std::cout << r.name << ": played=" << r.games_played
                          << " won=" << r.games_won << " (" << pct << "%)"
                          << " avg=" << r.average_win_turn << "\n" << std::flush;
                if (!game_log_dir.empty())
                {
                    WriteGameLog(game_log_dir, r.name, r.win_turns);
                }
            };
            std::vector<BatchJobResult> results =
                BatchRunner::RunManifest(manifest, num_threads, on_job_done);
            int total_games = 0;
            for (const BatchJobResult& r : results) { total_games += r.games_played; }
            std::cout << "=== BATCH done (" << results.size() << " jobs, "
                      << total_games << " games) ===\n" << std::flush;
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

    std::filesystem::path deck_path    = argv[1];
    std::filesystem::path cards_json   = "src/cards/data/cards.json";
    std::filesystem::path profile_path;
    std::filesystem::path log_dir;
    int      num_games      = 10000;
    int      max_turns      = 8;   // goldfish horizon: wins on turn >8 are not useful data (a
                                   // real game is lost by then; goldfishing can't model control).
                                   // Bounding it also stops the search exploring deep no-early-win
                                   // lines -- override with --max-turns for a genuinely slow deck.
    int      base_game_index = 0;
    int      lookahead_depth = 0;
    int      timeout_ms     = 0;
    int      num_threads    = 0;
    uint64_t seed           = 0;
    bool     seed_provided  = false;
    bool     diag_depth     = false;
    bool     trace_t1       = false;
    bool        claude_play = false;
    std::string choices_str;          // comma-separated plan indices for --claude-play
    int         reveal_count = 0;     // --reveal N: expose top N upcoming draws (claude-play)

    for (int i = 2; i < argc; ++i)
    {
        std::string flag = argv[i];
        if (flag == "--diag-depth")          { diag_depth = true; continue; }
        if (flag == "--trace")               { trace_t1 = true; continue; }
        if (flag == "--claude-play")         { claude_play = true; continue; }
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
                else if (flag == "--choices")
                {
                    choices_str = argv[++i];
                }
                else if (flag == "--reveal")
                {
                    reveal_count = std::stoi(argv[++i]);
                }
                else if (flag == "--depth")
                {
                    lookahead_depth = std::stoi(argv[++i]);
                }
                else if (flag == "--timeout-ms" || flag == "--budget-ms")
                {
                    // Deterministic search budget in "virtual ms" (see SearchBudget).
                    // --timeout-ms kept as a back-compat alias for the same knob.
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

        if (diag_depth)
        {
            RunDepthDivergenceDiagnostic(deck, profile, num_games, seed, max_turns, timeout_ms,
                                         num_threads, log_dir, trace_t1);
            return 0;
        }

        if (claude_play)
        {
            std::vector<int> choices;
            std::stringstream ss(choices_str);
            std::string tok;
            while (std::getline(ss, tok, ','))
            {
                if (!tok.empty()) { choices.push_back(std::stoi(tok)); }
            }
            return RunClaudePlay(deck, profile, seed, base_game_index, max_turns,
                                 lookahead_depth, timeout_ms, choices, reveal_count, log_dir);
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

        PROF_REPORT(std::cerr);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
