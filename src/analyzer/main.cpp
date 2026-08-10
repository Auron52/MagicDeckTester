#include "../core/EnvFlags.h"
#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <random>
#include "AnalyzerEngine.h"
#include "KeepModelTrainer.h"
#include "../core/HeuristicDefaults.h"
#include "../core/FlagRegistry.h"
#include "EquivalenceDiscovery.h"
#include "ExhaustiveKeep.h"
#include "SlowRollouts.h"
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
#include <cstdio>

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


// ---- Analyzer modes -----------------------------------------------------------------------------
// main() is a dispatcher over seven opt-in modes, each gated by its own MTG_* flag and each ending in
// a return -- none of them falls through to the land/score grid. They were inline `if` blocks, which
// is why main() ran to 670 lines. One function each; the shared CLI state they all read is this
// struct, so a mode's inputs are its signature rather than whatever happened to be in scope.
struct AnalyzerArgs
{
    Decklist              deck;
    std::filesystem::path deck_path;
    int                   max_turns     = 8;
    uint64_t              seed          = 0;
    bool                  seed_provided = false;
    std::string           gen_recipe;   // --gen-mulligan <fast|complete|recommend>
};


// Keep-model-only mode (MTG_KEEP_MODEL_ONLY): skip the whole land/score grid; load the
// deck's EXISTING committed profile and fit ONLY the interpretable keep model onto it,
// writing <deck>.keepmodel.profile.json. This is the fast Phase-3 A/B path -- the output is
// byte-identical to the committed profile except for the added keep_model, so a suite A/B
// isolates exactly the keep-decision change without re-running the (slow) grid.
static int RunKeepModelOnlyMode(const AnalyzerArgs& a)
{
    std::filesystem::path in_path =
        a.deck_path.parent_path() / (a.deck_path.stem().string() + ".profile.json");
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
    cfg.max_turns = a.max_turns;
    cfg.games     = keep_games ? keep_games : std::max(200, 2000 / scale);
    cfg.seed      = a.seed;
    // MTG_KEEP_SPLIT=both fits BOTH the gini and regret trees from one (expensive) kv table:
    // the gini model is returned (standard file) and the regret model comes back via out_alt,
    // written to a .keepmodel.regret.profile.json side file for a matched A/B.
    const bool both = []{ const char* e = std::getenv("MTG_KEEP_SPLIT");
                          return e && std::string(e) == "both"; }();
    // In both-mode, ALSO fit the additive score model from the same kv table (cheap second fit)
    // so a matched gini/regret/score 3-way A/B comes from one rollout pass.
    KeepModel alt, score, hybrid;
    base.keep_model = BuildKeepModel(a.deck, base, base.card_scores, cfg,
                                     both ? &alt : nullptr, both ? &score : nullptr,
                                     both ? &hybrid : nullptr);

    std::filesystem::path out_path =
        a.deck_path.parent_path() / (a.deck_path.stem().string() + ".keepmodel.profile.json");
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
            a.deck_path.parent_path() / (a.deck_path.stem().string() + ".keepmodel." + variant + ".profile.json");
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

// Exhaustive keep/bottom policy (MTG_KEEP_EXHAUSTIVE): bucket the deck, enumerate every
// distinct bucket-hand for sizes 7..7-max_mull, evaluate each with R reshuffled rollouts,
// and print the exact optimal keep+bottom policy value vs the static keep rule. Read-only.
//   MTG_EQUIV_PROBES/_THRESHOLD/_DEPTH (bucketing), MTG_KEEP_ROLLOUTS (R, default 100).
//   max_mull is FIXED at 6 (deepest mulligan = down to keep-1): the terminal keep-1 anchor is the only
//   correct forced-keep, so 6 is the uniquely-correct depth and there is no knob (a shallower table
//   forced-keeps a hand it should ship -- see docs/design/bottomcards-undercount-beyond-maxmull.md).
static int RunExhaustiveKeepMode(const AnalyzerArgs& a)
{
    auto env_int = [](const char* k, int dflt, int lo)
    { const char* s = std::getenv(k); return (s && *s) ? std::max(lo, std::atoi(s)) : dflt; };
    std::filesystem::path in_path =
        a.deck_path.parent_path() / (a.deck_path.stem().string() + ".profile.json");
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
    cfg.rollouts  = env_int("MTG_KEEP_ROLLOUTS", 100, 1);   // cap R; <2 is rejected by the generator
    // The adaptive FLOOR is derived by the generator (clamped into [1, cap-1]) -- there is no knob. The
    // old MTG_KEEP_R_FLOOR did not tune the schedule, it SELECTED one: unset (the default!) or >= cap meant
    // "uniform", which silently ran with no continuous pool, no journal, no gen-time projection and no
    // slow-cell report. See docs/design/keepgen-no-off-switches.md. FLIP_EPS is the stop threshold;
    // R_BATCH the per-wave add -- those tune the schedule without being able to switch it off.
    cfg.r_batch   = env_int("MTG_KEEP_R_BATCH", 16, 1);
    cfg.flip_eps  = []{ const char* s = std::getenv("MTG_KEEP_FLIP_EPS");
                        return (s && *s) ? std::max(0.0, std::atof(s)) : 0.02; }();
    cfg.se_prior  = []{ const char* s = std::getenv("MTG_KEEP_SE_PRIOR");
                        return (s && *s) ? std::max(0.0, std::atof(s)) : 8.0; }();
    // max_mull is FIXED at 6 (a 7-card hand down to keep-1) -- there is no knob. A shipped profile
    // must model mulligans all the way to 1 card; a shallower table leaves deep mulligans unmodelled
    // and under-bottoms (docs/design/bottomcards-undercount-beyond-maxmull.md). The deep levels are
    // cheap (small hands have very few compositions) and there is no reliable a-priori "always-keep"
    // cutoff without running them, so there is no reason to ever shorten it -- the old MTG_KEEP_MAXMULL
    // knob was pure misconfiguration risk and is gone (setting it now has no effect).
    cfg.max_mull  = 6;
    cfg.seed      = a.seed;   // rollout seed base (the run id / seed_base)
    cfg.equiv_seed = []{ const char* s = std::getenv("MTG_EQUIV_SEED");
                         return (s && *s) ? std::strtoull(s, nullptr, 10) : 20260701ULL; }();
    cfg.max_turns = a.max_turns;
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
    // (Reuse of a prior recommend-probe chunk as this gen's r=0 slice is unconditional and has no flag --
    // it is byte-identical to rolling r=0 fresh, and fingerprint-gated, so there was never a reason to
    // turn it off. MTG_KEEP_PROBE_CARRY / MTG_KEEP_NO_PROBE_CARRY are gone.)
    if (const char* c = std::getenv("MTG_COMMIT")) { cfg.commit = c; }
    if (const char* fm = std::getenv("MTG_EQUIV_FORCE_MERGE")) { cfg.force_merge = fm; }
    // Write the serialized keep policy + poolable raw sidecar next to the deck.
    {
        const std::string stem = (a.deck_path.parent_path() / a.deck_path.stem().string()).string();
        cfg.out_profile = stem + ".keepmodel.exhaustive.profile.json";
        // The RAW is not optional. It is the run's journal anchor (<out_raw>.journal), its slow-rollout
        // log (<out_raw>.slow.log) and its probe chunk (<out_raw>.probe) -- i.e. everything that makes a
        // gen resumable and diagnosable. MTG_KEEP_NO_WRITE used to clear it too, which produced the one
        // configuration that could run rollouts for hours and persist NOTHING; it now suppresses only the
        // profile (its actual purpose: don't ship a policy from a scratch/diagnostic run).
        cfg.out_raw     = stem + ".keepmodel.exhaustive.raw.json";
        // Fingerprint-gated cache of the equivalence classes, so a resume (or a recommend -> complete
        // hand-off) never re-derives the minutes-long bucketing. MTG_EQUIV_CACHE overrides the path.
        cfg.gen_cache   = stem + ".keepmodel.gencache.json";
        if (EnvOn("MTG_KEEP_NO_WRITE")) { cfg.out_profile.clear(); }
        // Override output paths (mirrors the merge path's MTG_MERGE_OUT_*): lets chunked R-sweep
        // generation write each chunk's raw straight to its own path, skipping the (unneeded)
        // per-chunk profile via MTG_KEEP_OUT_PROFILE=/dev/null-style empty.
        if (const char* p = std::getenv("MTG_KEEP_OUT_PROFILE")) { cfg.out_profile = p; }
        // The RAW override REDIRECTS, never clears: an empty MTG_KEEP_OUT_RAW is ignored, because an
        // empty out_raw would silently take the journal, the slow-rollout log and the probe chunk with
        // it -- exactly the incrementality off-switch this route no longer has. Only the PROFILE may be
        // suppressed (above), matching MTG_KEEP_NO_WRITE.
        if (const char* r = std::getenv("MTG_KEEP_OUT_RAW"); r && *r) { cfg.out_raw = r; }
    }

    // --gen-mulligan <recipe>: one-flag mulligan-profile generation. A recipe is a fixed preset over
    // the two cost levers -- bottoming mode (full vs adaptive) and cap R -- so the whole gen needs no
    // parameters but the flag. depth/budget come from the play profile's value_play (its mulligan-gen
    // override if set, else the play depth), NOT from the MTG_EQUIV_* env (which stay as advanced
    // overrides for the raw MTG_KEEP_EXHAUSTIVE path). Keep is always adaptive (floor 2); the recipes
    // differ in whether BOTTOMING is adaptive and in the cap R. See the recipe study in memory/docs.
    std::string depth_src = "gen-default", budget_src = "gen-default";
    if (!a.gen_recipe.empty())
    {
        // Deterministic seed by DEFAULT for the recipe path. The global default randomizes the seed
        // per invocation (fine for one-off play analysis), but that would defeat everything the recipe
        // flow wants to "just work": recommend's probe chunk and the real gen must share a seed for the
        // byte-identical probe reuse, and re-running an interrupted recipe must resume its own out_raw
        // (also seed-gated). A fixed seed makes recipe gens reproducible too. Explicit --seed still
        // overrides -- multi-machine pools pass their own disjoint seeds on the advanced env path.
        //
        // 1000000 is a deliberate sentinel clear of every seed range the repo uses: reference games
        // (s1..~s30), regression/smoke/overnight (s1001..~s8100), and the explicit mulligan-gen seeds
        // (12345, 700001, 900001, 10000001, 20000001, date-like 2026xxxx). It reads as "the recipe
        // default" and won't be mistaken for a hand-played or test seed.
        if (!a.seed_provided) { cfg.seed = 1000000; }
        // Auto-stamp the gen commit from the repo HEAD, so the poolable raw sidecar records its
        // build identity WITHOUT anyone having to remember MTG_COMMIT. Appends +dirty when the work-tree
        // isn't clean (a dirty tree makes the fingerprint unreliable for cross-machine pooling). MTG_COMMIT
        // still overrides (captured above) when reproducing a specific historical identity.
        if (cfg.commit.empty())
        {
            auto sh = [](const char* cmd) -> std::string {
                std::string out; FILE* p = popen(cmd, "r"); if (!p) { return out; }
                char buf[256]; while (std::fgets(buf, sizeof buf, p)) { out += buf; }
                pclose(p);
                while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) { out.pop_back(); }
                return out;
            };
            const std::string head = sh("git rev-parse --short HEAD 2>/dev/null");
            if (!head.empty())
            {
                const std::string dirty = sh("git status --porcelain --untracked-files=no 2>/dev/null");
                cfg.commit = head + (dirty.empty() ? "" : "+dirty");
            }
        }
        // (max_mull is fixed at 6 for every path -- see the cfg.max_mull note above; not a recipe knob.)
        // Resolve rollout depth/budget from value_play (mull_gen_* override -> play -> built-in default).
        const auto& vp = profile.value_play;
        cfg.depth     = vp.MullGenDepth(5);
        cfg.budget_ms = vp.MullGenBudgetMs(20);
        depth_src  = vp.mull_gen_depth > 0 ? "value_play.mull_gen_depth"
                   : vp.target_depth   > 0 ? "value_play.target_depth" : "gen-default";
        budget_src = vp.mull_gen_budget_ms > 0 ? "value_play.mull_gen_budget_ms"
                   : vp.budget_ms          > 0 ? "value_play.budget_ms" : "gen-default";

        if (a.gen_recipe == "complete")
        {
            cfg.rollouts = 40; cfg.r_floor = 2; cfg.adaptive_bottom = false;  // full bottoming, native-R
        }
        else if (a.gen_recipe == "fast")
        {
            cfg.rollouts = 30; cfg.r_floor = 2; cfg.adaptive_bottom = true;   // adaptive bottoming, R30
        }
        else if (a.gen_recipe == "recommend")
        {
            // Bounded scout: discovery + ONE rollout of every cell (r_floor=1), then project the
            // full-gen wall-clock vs an overnight window AND report the slowest cells, then STOP (no
            // refine, no profile). The point (per the workflow) is to catch degenerate performance up
            // front -- rather than kick off the full run and discover hours in that it is crippled by
            // a pathological cell. MUST use adaptive bottoming so the probe stays one rollout/cell;
            // full bottoming would drive the sub-tables to the cap = as expensive as a real gen.
            cfg.rollouts = 40; cfg.r_floor = 1; cfg.adaptive_bottom = true; cfg.recommend_only = true;
        }
        else
        {
            std::cerr << "Unknown --gen-mulligan recipe '" << a.gen_recipe
                      << "'. Use: complete | fast | recommend\n";
            return 1;
        }
    }

    // Always report the effective gen settings (even when they are just defaults) so a run is
    // self-documenting -- what recipe, depth, R, and bottoming mode actually produced this profile.
    {
        const char* trigger = !a.gen_recipe.empty() ? a.gen_recipe.c_str() : "env (MTG_KEEP_EXHAUSTIVE)";
        std::cout << "=== MULLIGAN PROFILE GEN SETTINGS ===\n";
        std::cout << "  recipe          : " << trigger << "\n";
        std::cout << "  bottoming       : " << (cfg.adaptive_bottom ? "ADAPTIVE" : "FULL")
                  << "  (baked ON at runtime)\n";
        std::cout << "  cap R (rollouts): " << cfg.rollouts << "\n";
        std::cout << "  floor R         : " << (cfg.r_floor > 0 ? cfg.r_floor : 2)
                  << "  (adaptive keep -- the only schedule; clamped below the cap)\n";
        std::cout << "  rollout depth   : " << cfg.depth     << "  (source: " << depth_src  << ")\n";
        std::cout << "  rollout budget  : " << cfg.budget_ms << " ms  (source: " << budget_src << ")\n";
        std::cout << "  flip_eps        : " << cfg.flip_eps << "   se_prior: " << cfg.se_prior
                  << "   r_batch: " << cfg.r_batch << "\n";
        std::cout << "  max_mull        : " << cfg.max_mull << "   max_turns: " << cfg.max_turns << "\n";
        std::cout << "  bucket probes   : " << cfg.probes << "   threshold: " << cfg.threshold << "\n";
        std::cout << "  seed            : " << cfg.seed << "\n";
        std::cout << "  out profile     : " << (cfg.out_profile.empty() ? "(none)" : cfg.out_profile) << "\n";
        std::cout << "  out raw         : " << (cfg.out_raw.empty() ? "(none)" : cfg.out_raw) << "\n";
        std::cout << "  gen cache       : " << (cfg.gen_cache.empty() ? "(none)" : cfg.gen_cache) << "\n";
        std::cout << "  slow-rollout log: " << (cfg.out_raw.empty() ? "(none)" : cfg.out_raw + ".slow.log")
                  << "  (stream >= " << SlowTracker::StreamMs() << "ms; cannot be disabled)\n";
        if (cfg.recommend_only)
        { std::cout << "  probe chunk out : " << (cfg.out_raw.empty() ? "(none)" : cfg.out_raw + ".probe")
                    << "\n"; }
        else
        { std::cout << "  probe carry     : ON (reuse " << (cfg.out_raw.empty() ? "<out_raw>" : cfg.out_raw)
                    << ".probe if it matches)\n"; }
        std::cout << "=====================================\n" << std::flush;
    }

    RunExhaustiveKeep(std::cout, a.deck, profile, cfg);
    return 0;
}

// Targeted comp-scorer (MTG_SCORE_COMPS): re-evaluate specific bucket compositions at high R
// to non-circularly check bottoming decisions (is the kept subhand truly blind-better?). Reads
// MTG_SCORE_FILE lines "H:c0,c1,...,cK-1" (hand size + per-bucket counts, bucket order = the
// profile's exhaustive_keep.buckets); prints "H:comp  draw_mean draw_se  play_mean play_se".
// Uses the deck's committed exhaustive profile for the bucket map. MTG_SCORE_R (default 400).
static int RunScoreCompsMode(const AnalyzerArgs& a)
{
    std::filesystem::path in_path =
        a.deck_path.parent_path() / (a.deck_path.stem().string() + ".keepmodel.exhaustive.profile.json");
    MulliganProfile profile = LoadDeckProfile(in_path);
    static const ExhaustiveKeepPolicy kNoExhaustive;
    const auto& buckets = (profile.exhaustive_keep ? *profile.exhaustive_keep : kNoExhaustive).buckets;
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
    const bool second_main = GoldFishRunner::DeckUsesSecondMain(a.deck);

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
                    GameState s = GoldFishRunner::SetupGame(a.deck, rs);
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
                    double wt = ai.RolloutKeepWinTurn(s, 0, a.max_turns);
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

// Mulligan-EV probe (MTG_MULL_EV): measure the value of mulliganing a RANDOM opening hand down
// to (7-m) cards and playing out, for m=0..MAXM. This is the KEEP THRESHOLD the exhaustive policy
// compares a specific hand against: keep iff V[hand] <= Dopt[m+1], and Dopt[m+1] ~ the mean here
// at m+1 (value of mulliganing again). Draws a natural top-7 per fresh shuffle, bottoms m via the
// deck's bottoming policy, plays out; averages over MTG_SCORE_R shuffles. Read-only diagnostic.
static int RunMullEvMode(const AnalyzerArgs& a)
{
    std::filesystem::path in_path =
        a.deck_path.parent_path() / (a.deck_path.stem().string() + ".keepmodel.exhaustive.profile.json");
    MulliganProfile profile = std::filesystem::exists(in_path) ? LoadDeckProfile(in_path)
                                                               : MulliganProfile::DefaultProfile();
    const int R = []{ const char* s = std::getenv("MTG_SCORE_R");
                      return (s && *s) ? std::max(1, std::atoi(s)) : 400; }();
    const int depth = []{ const char* s = std::getenv("MTG_EQUIV_DEPTH");
                          return (s && *s) ? std::max(0, std::atoi(s)) : 5; }();
    const int MAXM = []{ const char* s = std::getenv("MTG_MULL_EV_MAXM");
                         return (s && *s) ? std::max(0, std::atoi(s)) : 2; }();
    MulliganProfile rp = profile; rp.keep_model = KeepModel{};
    const bool second_main = GoldFishRunner::DeckUsesSecondMain(a.deck);
    std::vector<std::array<double, 2>> sum(MAXM + 1, { 0, 0 }), sumsq(MAXM + 1, { 0, 0 });
    std::mutex mtx;
    std::atomic<int> next{ 0 };
    auto worker = [&]()
    {
        AIEngine ai(rp, depth, 20); ai.SetSearchPostCombat(second_main);
        std::vector<std::array<double, 2>> ls(MAXM + 1, { 0, 0 }), lsq(MAXM + 1, { 0, 0 });
        for (;;)
        {
            int r = next.fetch_add(1);
            if (r >= R) { break; }
            for (int pd = 0; pd < 2; ++pd)
            {
                const uint64_t rs = 555'000'000ULL + 0x9E3779B97F4A7C15ULL * (r + 1)
                                  + 100003ULL * static_cast<uint64_t>(pd);
                GameState base = GoldFishRunner::SetupGame(a.deck, rs);
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
                    double wt = ai.RolloutKeepWinTurn(s, m, a.max_turns);
                    ls[m][pd] += wt; lsq[m][pd] += wt * wt;
                }
            }
        }
        std::lock_guard<std::mutex> g(mtx);
        for (int m = 0; m <= MAXM; ++m)
            for (int pd = 0; pd < 2; ++pd)
            { sum[m][pd] += ls[m][pd]; sumsq[m][pd] += lsq[m][pd]; }
    };
    int nthreads = std::max(1, concurrency_util::AffinityCpuCount());
    std::vector<std::thread> pool;
    for (int t = 0; t < nthreads; ++t) { pool.emplace_back(worker); }
    for (std::thread& th : pool) { th.join(); }
    std::cout << "# MULL-EV deck=" << a.deck_path.stem().string() << " R=" << R << " depth=" << depth
              << " max_turns=" << a.max_turns << "\n";
    std::cout << "# m = cards bottomed (hand size 7-m); mean = keep threshold Dopt[m] (lower=better)\n";
    for (int m = 0; m <= MAXM; ++m)
        for (int pd = 0; pd < 2; ++pd)
        {
            double mean = sum[m][pd] / R;
            double var  = std::max(0.0, sumsq[m][pd] / R - mean * mean);
            std::cout << "m" << m << " keep" << (7 - m) << " " << (pd ? "PLAY" : "DRAW")
                      << "  mean=" << mean << "  se=" << std::sqrt(var / R) << "\n";
        }
    std::cout << std::flush;
    return 0;
}

// Constructed-hand game logger (MTG_LOG_HAND="c0,c1,...,cK-1"): set up games with an EXACT
// bucket composition as the opening hand, play them out with a real logger, and save the ones
// that win on MTG_LOG_TURN (default 3) as replay JSON under MTG_LOG_DIR (default logs/hand_lines).
// MTG_LOG_MAXTOP2LANDS (default 1) skips games whose first two draws are BOTH lands (the "clean"
// path), so the saved lines are the harder wins. MTG_LOG_N games, MTG_LOG_PLAY=1 for on-the-play.
// Value-carrying flag: the value IS the hand composition (comma list), "0" = off. Keep the
// raw read (EnvOn would drop access to the value); already value-aware, so =0 disables.
static int RunLogHandMode(const AnalyzerArgs& a, const char* hand_spec)
{
    std::filesystem::path in_path =
        a.deck_path.parent_path() / (a.deck_path.stem().string() + ".keepmodel.exhaustive.profile.json");
    MulliganProfile profile = std::filesystem::exists(in_path) ? LoadDeckProfile(in_path)
                                                               : MulliganProfile::DefaultProfile();
    static const ExhaustiveKeepPolicy kNoExhaustive;
    const auto& buckets = (profile.exhaustive_keep ? *profile.exhaustive_keep : kNoExhaustive).buckets;
    const int K = static_cast<int>(buckets.size());
    std::map<std::string,int> bof;
    for (int b = 0; b < K; ++b) { for (const std::string& n : buckets[b]) { bof[n] = b; } }
    std::vector<int> comp(K, 0);
    { std::stringstream cs(hand_spec); std::string t; int b = 0;
      while (std::getline(cs, t, ',') && b < K) { comp[b++] = std::atoi(t.c_str()); } }
    const int depth = []{ const char* s = std::getenv("MTG_EQUIV_DEPTH");
                          return (s && *s) ? std::max(0, std::atoi(s)) : 5; }();
    const int target = []{ const char* s = std::getenv("MTG_LOG_TURN");
                           return (s && *s) ? std::atoi(s) : 3; }();
    const int want   = []{ const char* s = std::getenv("MTG_LOG_N");
                           return (s && *s) ? std::max(1, std::atoi(s)) : 3; }();
    const int max2   = []{ const char* s = std::getenv("MTG_LOG_MAXTOP2LANDS");
                           return (s && *s) ? std::atoi(s) : 1; }();
    const bool on_play = EnvOn("MTG_LOG_PLAY");
    std::filesystem::path log_dir = std::getenv("MTG_LOG_DIR") ? std::getenv("MTG_LOG_DIR")
                                                               : "logs/hand_lines";
    std::filesystem::create_directories(log_dir);
    auto numbering = GoldFishRunner::BuildCardNumbering(a.deck);
    MulliganProfile rp = profile; rp.keep_model = KeepModel{};
    const bool second_main = GoldFishRunner::DeckUsesSecondMain(a.deck);
    AIEngine ai(rp, depth, 20); ai.SetSearchPostCombat(second_main);
    int found = 0, scanned = 0;
    const uint64_t base = 900'000'000ULL;
    for (uint64_t seed = base; found < want && seed < base + 200000; ++seed)
    {
        ++scanned;
        GameState s = GoldFishRunner::SetupGame(a.deck, seed);
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
        int wt = engine.PlayOut(s, a.max_turns);
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

// Merge mode (MTG_KEEP_MERGE): pool the poolable raw sidecars from prior exhaustive runs (this
// machine + a second machine) into one policy at the combined R. Inputs come from
// MTG_MERGE_INPUTS (comma/space/newline-separated paths). Writes the same
// <stem>.keepmodel.exhaustive.profile.json + .raw.json (override via MTG_MERGE_OUT_PROFILE /
// MTG_MERGE_OUT_RAW; MTG_KEEP_NO_WRITE suppresses). No rollouts -- fast.
static int RunKeepMergeMode(const AnalyzerArgs& a)
{
    std::filesystem::path in_path =
        a.deck_path.parent_path() / (a.deck_path.stem().string() + ".profile.json");
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
    const std::string stem = (a.deck_path.parent_path() / a.deck_path.stem().string()).string();
    std::string out_profile = stem + ".keepmodel.exhaustive.profile.json";
    std::string out_raw     = stem + ".keepmodel.exhaustive.raw.json";
    if (const char* p = std::getenv("MTG_MERGE_OUT_PROFILE")) { out_profile = p; }
    if (const char* r = std::getenv("MTG_MERGE_OUT_RAW"))     { out_raw = r; }
    if (EnvOn("MTG_KEEP_NO_WRITE")) { out_profile.clear(); out_raw.clear(); }
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
    RunKeepMerge(std::cout, a.deck, profile, inputs, out_profile, out_raw, bottoming_enabled);
    return 0;
}

// Equivalence-discovery mode (MTG_EQUIV_DISCOVER): measure objective-relative card
// equivalence by CRN substitution in the goldfish engine and print the merged classes for
// review. Read-only -- writes no profile. Uses the deck's committed profile for rollout
// fidelity (vial target / play style), matching how the keep-model labels are measured.
//   MTG_EQUIV_PROBES (default 200), MTG_EQUIV_DEPTH (5), MTG_EQUIV_BUDGET (20).
static int RunEquivDiscoverMode(const AnalyzerArgs& a)
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
        a.deck_path.parent_path() / (a.deck_path.stem().string() + ".profile.json");
    MulliganProfile profile = std::filesystem::exists(in_path)
                            ? LoadDeckProfile(in_path) : MulliganProfile::DefaultProfile();

    std::cerr << "Equivalence discovery: " << probes << " probes, depth " << depth
              << ", threshold " << threshold << ", horizon " << a.max_turns << "\n";
    EquivReport rep = DiscoverEquivalence(a.deck, profile, probes, depth, budget_ms,
                                          threshold, a.seed, a.max_turns);
    PrintEquivReport(std::cout, rep);
    return 0;
}

int main(int argc, char* argv[])
{
    // Apply committed heuristic defaults BEFORE anything reads a toggle (env vars still override).
    ApplyHeuristicDefaults();
    // Warn on MTG_* env vars this binary does not read (typo / deleted flag = silent no-op).
    WarnUnknownMtgFlags();
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
    std::string gen_recipe;   // --gen-mulligan <fast|complete|recommend>: one-flag mulligan-profile gen

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
            else if (flag == "--gen-mulligan")
            {
                gen_recipe = argv[i + 1];
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

        // What every mode below reads. Built once so a mode's inputs are its signature.
        AnalyzerArgs a{ deck, deck_path, max_turns, seed, seed_provided, gen_recipe };

        if (EnvOn("MTG_EQUIV_DISCOVER")) { return RunEquivDiscoverMode(a); }

        if (EnvOn("MTG_KEEP_MERGE")) { return RunKeepMergeMode(a); }

        if (const char* e = std::getenv("MTG_LOG_HAND"); e && *e && std::string(e) != "0")
        { return RunLogHandMode(a, e); }

        if (EnvOn("MTG_MULL_EV")) { return RunMullEvMode(a); }

        if (EnvOn("MTG_SCORE_COMPS")) { return RunScoreCompsMode(a); }

        const bool env_exhaustive = []{ const char* e = std::getenv("MTG_KEEP_EXHAUSTIVE");
                                        return e && *e && std::string(e) != "0"; }();
        if (env_exhaustive || !a.gen_recipe.empty()) { return RunExhaustiveKeepMode(a); }

        if (EnvOn("MTG_KEEP_MODEL_ONLY")) { return RunKeepModelOnlyMode(a); }

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
