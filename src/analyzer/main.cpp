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
#include "BucketPolicy.h"
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
#include "../ai/GameWorkMeter.h"
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
    // A keep table is an artifact of how the deck PLAYS, so generating one without the deck's play
    // profile silently fits it to a deck we do not ship -- and the failure is invisible in the output.
    // Measured 2026-08-11: a scratch-directory gen ran with vial_target_mv=0, so the engine never cast
    // Aether Vial, so bucket discovery MERGED Ancient Ziggurat (the one land that cannot pay for Vial)
    // into the land bucket -- 9 buckets instead of 10, ~1.8x fewer cells, and a gen that looked cheap
    // because it was wrong. With the profile present, R=10 discovery reproduces the committed R=60
    // bucketing exactly. See docs/design/deck-combination-screening.md.
    // Refuse rather than warn: this route runs for tens of minutes to hours, and a warning scrolled
    // past at minute 0 is not seen at minute 90. DEFAULT OFF; =1 permits a profile-less gen for a deck
    // that genuinely has no play profile yet.
    if (!std::filesystem::exists(in_path) && !EnvOn("MTG_KEEP_ALLOW_NO_PROFILE"))
    {
        std::cerr << "keep-gen: no play profile beside the decklist (" << in_path.string() << ").\n"
                  << "  A table generated without it is fit to a DIFFERENT deck than the one shipped.\n"
                  << "  Copy the deck's <stem>.profile.json (and its <stem>.value.json, attached just\n"
                  << "  below) next to this decklist, or set MTG_KEEP_ALLOW_NO_PROFILE=1 if the deck\n"
                  << "  genuinely has no profile yet.\n";
        return 1;
    }
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
    // The deck's stored bucket ruling (BucketPolicy.h). Loaded here, before anything reads a
    // bucketing, and validated against the decklist so a renamed card turns into an error rather
    // than a silently-dropped decision. Its merge groups join the env force-merge spec; its
    // keep_apart groups ride into discovery on cfg.policy.
    BucketPolicy bucket_policy = LoadBucketPolicy(a.deck_path);
    {
        std::vector<std::string> distinct;
        for (const Card& c : a.deck.mainboard)
        { if (std::find(distinct.begin(), distinct.end(), c.m_name.str()) == distinct.end())
          { distinct.push_back(c.m_name.str()); } }
        ValidateBucketPolicy(bucket_policy, distinct);
    }
    ExhaustiveKeepConfig cfg;
    cfg.probes    = env_int("MTG_EQUIV_PROBES", 400, 1);
    cfg.threshold = []{ const char* s = std::getenv("MTG_EQUIV_THRESHOLD");
                        return (s && *s) ? std::max(0.0, std::atof(s)) : 0.01; }();
    cfg.depth     = env_int("MTG_EQUIV_DEPTH", 5, 0);
    cfg.budget_ms = env_int("MTG_EQUIV_BUDGET", 20, 0);  // per-decision rollout search budget (ms);
                                                         // feeds rollouts AND play digest
    // DISCOVERY settings. On this (manual/env) route they intentionally track the same env pair, so the
    // path stays byte-identical; the recipe branch below re-points them at the deck's PLAY settings.
    cfg.equiv_depth     = cfg.depth;
    cfg.equiv_budget_ms = cfg.budget_ms;
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
    cfg.policy = bucket_policy.Empty() ? nullptr : &bucket_policy;
    // The file's merge groups go through the SAME force-merge path as the env override rather than
    // a second implementation; when both are present the env spec is appended, so an experiment can
    // add a merge without editing the committed ruling.
    if (const std::string ms = bucket_policy.MergeSpec(); !ms.empty())
    { cfg.force_merge = cfg.force_merge.empty() ? ms : (ms + ";" + cfg.force_merge); }
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
    std::string equiv_src = "gen-default";   // where the DISCOVERY (bucketing) depth came from
    std::string rollouts_src = "recipe/default";   // where the cap R came from (see the cap-R pin)
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
            // popen/pclose are POSIX; MSVC spells them _popen/_pclose. The null sink differs too
            // ("/dev/null" vs cmd.exe's "NUL") -- getting THAT wrong doesn't fail loudly, it makes
            // git's stderr leak onto the console while the stamp silently comes back empty.
            auto sh = [](const std::string& cmd) -> std::string {
                std::string out;
#ifdef _WIN32
                FILE* p = _popen(cmd.c_str(), "r");
#else
                FILE* p = popen(cmd.c_str(), "r");
#endif
                if (!p) { return out; }
                char buf[256]; while (std::fgets(buf, sizeof buf, p)) { out += buf; }
#ifdef _WIN32
                _pclose(p);
#else
                pclose(p);
#endif
                while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) { out.pop_back(); }
                return out;
            };
#ifdef _WIN32
            const std::string quiet = " 2>NUL";
#else
            const std::string quiet = " 2>/dev/null";
#endif
            const std::string head = sh("git rev-parse --short HEAD" + quiet);
            if (!head.empty())
            {
                const std::string dirty = sh("git status --porcelain --untracked-files=no" + quiet);
                cfg.commit = head + (dirty.empty() ? "" : "+dirty");
            }
        }
        // (max_mull is fixed at 6 for every path -- see the cfg.max_mull note above; not a recipe knob.)
        // Resolve rollout depth/budget from value_play (mull_gen_* override -> play -> built-in default).
        const auto& vp = profile.value_play;
        cfg.depth     = vp.MullGenDepth(5);
        cfg.budget_ms = vp.MullGenBudgetMs(20);
        // DISCOVERY runs under the deck's SHIPPED PLAY settings, NOT mull_gen_* (user call, 2026-08-15).
        // Bucketing asks whether two cards are interchangeable for this deck AS PLAYED, so it must not
        // move when we pick cheaper label rollouts. Before this split, `cfg.depth` fed discovery too, so
        // lowering the gen depth silently re-bucketed the deck -- and since hand count grows as
        // C(K+6,7), a cheaper setting could cost far more (slivers: K 10->13 at gen d2 = 4.3x the hands).
        // An explicit MTG_EQUIV_* pin still wins, which is what the pooling checklist documents and what
        // the recipe path previously ignored outright.
        cfg.equiv_depth     = env_int("MTG_EQUIV_DEPTH",  vp.target_depth > 0 ? vp.target_depth : 5, 0);
        cfg.equiv_budget_ms = env_int("MTG_EQUIV_BUDGET", vp.budget_ms    > 0 ? vp.budget_ms    : 20, 0);
        equiv_src = std::getenv("MTG_EQUIV_DEPTH") ? "MTG_EQUIV_DEPTH (pinned)"
                  : vp.target_depth > 0            ? "value_play.target_depth (play)" : "gen-default";
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
        // CAP-R PIN (EnvFlags rule 2 -- value-pinning, NOT a truthiness gate: "did the user pin a
        // cap?" is a different question from the cap's value). A recipe is a preset over the two
        // cost levers, and both shipped presets are sized for a SHIPPING artifact. A SCREENING
        // apparatus is a different job: it wants the cheapest cap that still discriminates, matched
        // across arms, and neither preset can express that. Measured on Mirrorwing Trick Suite, the
        // recommend scout projected complete(R40) ~20.1 h and fast(R30) ~10.0 h -- both past the
        // overnight window, against a few-hours budget.
        //
        // This pins ONLY the cap. The recipe keeps ownership of bottoming mode, the adaptive floor
        // (there is no uniform-R mode -- see the MTG_KEEP_R_FLOOR note above) and the
        // discovery-vs-gen settings split, so a pinned run is still a recipe run in every other
        // respect. Unset -> the recipe's own cap, byte-identical.
        if (EnvSet("MTG_KEEP_ROLLOUTS"))
        {
            cfg.rollouts = env_int("MTG_KEEP_ROLLOUTS", cfg.rollouts, 1);
            rollouts_src = "MTG_KEEP_ROLLOUTS (pinned)";
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
        std::cout << "  cap R (rollouts): " << cfg.rollouts
                  << "  (source: " << rollouts_src << ")\n";
        std::cout << "  floor R         : " << (cfg.r_floor > 0 ? cfg.r_floor : 2)
                  << "  (adaptive keep -- the only schedule; clamped below the cap)\n";
        std::cout << "  rollout depth   : " << cfg.depth     << "  (source: " << depth_src  << ")\n";
        std::cout << "  rollout budget  : " << cfg.budget_ms << " ms  (source: " << budget_src << ")\n";
        // Print discovery separately: these drive the BUCKETS (and thus the hand count), and conflating
        // them with the rollout pair is exactly the confusion this split removes.
        std::cout << "  discovery depth : " << cfg.equiv_depth     << "  (source: " << equiv_src << ")\n";
        std::cout << "  discovery budget: " << cfg.equiv_budget_ms << " ms\n";
        std::cout << "  flip_eps        : " << cfg.flip_eps << "   se_prior: " << cfg.se_prior
                  << "   r_batch: " << cfg.r_batch << "\n";
        std::cout << "  max_mull        : " << cfg.max_mull << "   max_turns: " << cfg.max_turns << "\n";
        std::cout << "  bucket probes   : " << cfg.probes << "   threshold: " << cfg.threshold << "\n";
        PrintBucketPolicy(std::cout, bucket_policy);
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
    // Resolve `.gz` FIRST, as AttachExhaustiveSidecar does. Every deck ships this sidecar gzipped, so
    // the plain-name-only lookup found nothing, left `buckets` empty, and made K=0 -- whereupon the
    // hand-filling loop below copies zero cards and EVERY composition scores as unwon (max_turns+1)
    // with se 0. A silent all-unwon result reads as data, not as a failure.
    // HAND MODE (MTG_SCORE_HANDS=N): score N random OPENING HANDS instead of bucket compositions.
    //
    // Exists so the generation setting can be derived for a deck that has NO mulligan artifacts yet.
    // The comp path needs a committed exhaustive sidecar for its bucket map, but that sidecar is an
    // OUTPUT of the very generation we are trying to configure -- so at the end of a value-leaf run on
    // a new deck it does not exist, and the derivation would be impossible exactly when it is wanted.
    // Ranking fidelity does not need buckets: sampling openers straight from the decklist asks the
    // same question ("does this depth order hands like the shipped policy does?") with no dependency
    // on discovery, bucketing, or a prior profile.
    const int nhands = EnvInt("MTG_SCORE_HANDS", 0);

    std::filesystem::path in_path =
        a.deck_path.parent_path() / (a.deck_path.stem().string() + ".keepmodel.exhaustive.profile.json");
    if (!std::filesystem::exists(in_path) && std::filesystem::exists(in_path.string() + ".gz"))
    { in_path = in_path.string() + ".gz"; }
    MulliganProfile profile;
    if (std::filesystem::exists(in_path)) { profile = LoadDeckProfile(in_path); }
    else if (nhands > 0)
    {
        // No exhaustive sidecar (the new-deck case): fall back to the deck's PLAY profile so rollouts
        // still see required_pieces / vial_target_mv / the value sidecar.
        std::filesystem::path pp =
            a.deck_path.parent_path() / (a.deck_path.stem().string() + ".profile.json");
        profile = std::filesystem::exists(pp) ? LoadDeckProfile(pp) : MulliganProfile::DefaultProfile();
    }
    else { profile = LoadDeckProfile(in_path); }

    // Attach the deck's learned leaf VALUE sidecar, exactly as the keep-GENERATION path does.
    //
    // Without this the scorer rolls out a VALUE-LESS policy that no deck with a value model actually
    // plays -- the same latent bug called out at the generation call site, which had simply never been
    // fixed on this path. It corrupts BOTH halves of a depth comparison:
    //   * QUALITY: the "reference" arm (the deck's shipped play settings) is not the shipped policy at
    //     all, so every fidelity number is measured against the wrong target.
    //   * COST: at or above value_trust_depth the search TRUSTS the leaf instead of playing on to the
    //     horizon. Value-less, a play-settings rollout plays the whole game out, which made the play
    //     arm look far more expensive than it is (slivers measured 6.3x the cost of d3b3 -- the
    //     opposite of this deck's known behaviour at its trusted depth).
    // Presence-gated, so it is a no-op for value-less decks and cannot change their numbers.
    //
    // NOTE the path: AttachValueSidecar derives `<stem>.value.json` by stripping a literal
    // ".profile.json" SUFFIX, so it must be handed the deck's PLAY profile path. Passing `in_path`
    // (the exhaustive sidecar, "<deck>.keepmodel.exhaustive.profile.json.gz") silently no-ops -- the
    // ".gz" fails the suffix test, and even unzipped the stem would resolve to
    // "<deck>.keepmodel.exhaustive.value.json", which does not exist. A silent no-op here is
    // indistinguishable from "this deck has no value model", which is how the bug hid.
    AttachValueSidecar(profile,
                       a.deck_path.parent_path() / (a.deck_path.stem().string() + ".profile.json"));

    static const ExhaustiveKeepPolicy kNoExhaustive;
    const auto& buckets = (profile.exhaustive_keep ? *profile.exhaustive_keep : kNoExhaustive).buckets;
    const int K = static_cast<int>(buckets.size());
    if (K == 0 && nhands <= 0)
    { std::cerr << "MTG_SCORE_COMPS: no exhaustive keep sidecar beside " << a.deck_path
                << " (looked for " << in_path << ") -- refusing to score with an empty bucket map\n";
      return 1; }
    std::map<std::string,int> bof;
    for (int b = 0; b < K; ++b) { for (const std::string& n : buckets[b]) { bof[n] = b; } }
    const int R = []{ const char* s = std::getenv("MTG_SCORE_R");
                      return (s && *s) ? std::max(1, std::atoi(s)) : 400; }();
    const int depth = []{ const char* s = std::getenv("MTG_EQUIV_DEPTH");
                          return (s && *s) ? std::max(0, std::atoi(s)) : 5; }();
    // Was hard-coded to 20 (ExhaustiveKeepConfig::budget_ms), which made the scorer unable to answer
    // "what does a cheaper generation budget do to the cell values it would label?" -- the question
    // behind budget-vs-R as a generation cost lever. Default is unchanged, so existing runs are
    // byte-identical.
    const int budget_ms = EnvInt("MTG_SCORE_BUDGET_MS", 20);
    MulliganProfile rp = profile; rp.keep_model = KeepModel{};
    const bool second_main = GoldFishRunner::DeckUsesSecondMain(a.deck);

    // An item is either a bucket COMPOSITION (comp mode) or an explicit list of card NAMES (hand
    // mode). Parsed up front so the independent per-item evaluations can be threaded.
    struct Item { std::string line; std::vector<int> comp; std::vector<std::string> names; };
    std::vector<Item> items;

    if (nhands > 0)
    {
        // Deal N openers from the deck itself, so the sampled hands follow the real opening-hand
        // distribution. Seeded off a fixed base: the SAME hands at every depth, which is what makes
        // the depth comparison paired (common random numbers) rather than a race between samples.
        const uint64_t hand_base = static_cast<uint64_t>(EnvInt("MTG_SCORE_HAND_SEED", 424242));
        for (int h = 0; h < nhands; ++h)
        {
            const uint64_t hs = hand_base + 0x9E3779B97F4A7C15ULL * static_cast<uint64_t>(h + 1);
            GameState s0 = GoldFishRunner::SetupGame(a.deck, hs);
            // SetupGame stocks the library but does NOT deal an opener, so shuffle and take the top 7
            // to get a hand from the real opening distribution.
            Player& p0 = s0.ActivePlayer();
            p0.library.Shuffle(SaltSeed(hs, 0xA11CEULL));
            std::vector<std::string> names;
            for (std::size_t i = 0; i < p0.library.size() && names.size() < 7; ++i)
            { names.push_back(p0.library[i].m_name.str()); }
            if (names.size() < 7)
            { std::cerr << "MTG_SCORE_HANDS: deck yielded only " << names.size()
                        << " cards for an opener -- refusing\n"; return 1; }
            std::string lbl = "H" + std::to_string(h) + ":";
            for (std::size_t i = 0; i < names.size(); ++i)
            { lbl += (i ? "|" : "") + names[i]; }
            items.push_back({ lbl, {}, std::move(names) });
        }
    }
    else
    {
        const char* fp = std::getenv("MTG_SCORE_FILE");
        std::ifstream fin(fp ? fp : "");
        if (!fin) { std::cerr << "MTG_SCORE_FILE not readable\n"; return 1; }
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
            items.push_back({ line, std::move(comp), {} });
        }
    }
    std::vector<std::array<double, 4>> out(items.size(), { 0, 0, 0, 0 });  // dmean,dse,pmean,pse
    std::atomic<int> next{0};
    // DETERMINISTIC COST. Wall-clock cannot price a generation setting: it moves with whatever else
    // is on the box (measured -- two unrelated 12-core runs made an arm look 3x its true cost) and
    // differs per machine, so a setting derived from seconds is not reproducible and cannot survive
    // the cross-machine profile handoff. Work units are the currency SearchBudget is denominated in
    // and are a pure function of (deck, seed, depth, budget), so this counter is exact after a
    // handful of rollouts and identical everywhere. See ai/GameWorkMeter.h.
    std::atomic<long long> total_work{0};
    const std::map<std::string, int>& bofref = bof;   // const ref -> concurrent-safe find()
    auto worker = [&]()
    {
        AIEngine ai(rp, depth, budget_ms); ai.SetSearchPostCombat(second_main);
        long long my_work = 0;
        for (;;)
        {
            int w = next.fetch_add(1);
            if (w >= static_cast<int>(items.size())) { break; }
            const std::vector<int>& comp = items[w].comp;
            const std::vector<std::string>& want = items[w].names;
            for (int pd = 0; pd < 2; ++pd)
            {
                double sum = 0, sumsq = 0;
                for (int r = 0; r < R; ++r)
                {
                    const uint64_t rs = 777'000'000ULL + 0x9E3779B97F4A7C15ULL * (r + 1)
                                      + 100003ULL * static_cast<uint64_t>(pd);
                    GameState s = GoldFishRunner::SetupGame(a.deck, rs);
                    s.m_required_pieces = &rp.required_pieces;
                    s.m_card_scores     = rp.card_scores.empty() ? nullptr : &rp.card_scores;
                    s.vial_target_mv    = rp.vial_target_mv;
                    s.on_the_play       = (pd == 1);
                    Player& ap = s.ActivePlayer(); ap.hand.clear();
                    // Branch on the MODE, not on whether `want` happens to be non-empty: an empty
                    // name list used to fall through to the comp path, where comp[] is sized 0 while
                    // K is not, and index out of bounds (a segfault, found exactly this way).
                    if (nhands > 0)
                    {
                        // hand mode: pull the exact named cards out of the library
                        for (const std::string& nm : want) {
                            for (std::size_t k = 0; k < ap.library.size(); ++k) {
                                if (ap.library[k].m_name.str() == nm)
                                { ap.hand.push_back(ap.library[k]);
                                  ap.library.erase(ap.library.begin()+k); break; } } }
                    }
                    else
                    for (int b = 0; b < K; ++b) { int need = comp[b];
                        for (std::size_t k = 0; k < ap.library.size() && need > 0; ) {
                            auto bit = bofref.find(ap.library[k].m_name.str());
                            if (bit != bofref.end() && bit->second == b)
                            { ap.hand.push_back(ap.library[k]); ap.library.erase(ap.library.begin()+k); --need; }
                            else { ++k; } } }
                    ap.library.Shuffle(SaltSeed(rs, 0x5EED5ULL));   // unbias continuation (see ExhaustiveKeep)
                    gamework::Begin(0);   // disarmed: reset the counter, never abandon a scoring rollout
                    double wt = ai.RolloutKeepWinTurn(s, 0, a.max_turns);
                    my_work += gamework::Used();
                    sum += wt; sumsq += wt*wt;
                }
                double mean = sum / R;
                double var  = R > 1 ? std::max(0.0, sumsq/R - mean*mean) : 0.0;
                out[w][pd * 2]     = mean;
                out[w][pd * 2 + 1] = R > 1 ? std::sqrt(var / R) : 0.0;
            }
        }
        total_work.fetch_add(my_work, std::memory_order_relaxed);
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
    // stderr, so stdout stays exactly the parseable comp table it has always been.
    const long long rollouts = static_cast<long long>(items.size()) * 2 * R;
    std::cerr << "[score] depth=" << depth << " budget_ms=" << budget_ms
              << " comps=" << items.size() << " R=" << R
              << " rollouts=" << rollouts
              << " work_units=" << total_work.load()
              << " units_per_rollout="
              << (rollouts > 0 ? static_cast<double>(total_work.load()) / static_cast<double>(rollouts) : 0.0)
              << "\n";
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
                base.m_card_scores     = rp.card_scores.empty() ? nullptr : &rp.card_scores;
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
        s.m_card_scores     = rp.card_scores.empty() ? nullptr : &rp.card_scores;
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
    // The SAME sidecar attach the generator does (line ~170). Without it this mode measured a
    // value-less policy the deck never plays -- the very bug that comment describes -- so the whole
    // point of the mode was defeated: it exists to print the buckets FOR REVIEW, and it printed
    // buckets the generator would not produce. Measured on KittyEquipment at identical recorded
    // parameters (400 probes, d5/b20, equiv_seed 20260701, threshold 0.01): this mode said K=18 with
    // Colossus Hammer and O-Naginata 0.015 apart, while the generator said K=17 with the two merged.
    // Every shared pair disagreed too (Loxodon/Grafted 0.0325 vs 0.0400, Jitte/Greaves 0.065 vs
    // 0.070), which is what a different rollout policy looks like -- not a clustering difference.
    // A reviewer who trusts this mode to sanity-check a bucketing gets the wrong K on any deck that
    // ships a value sidecar, which is most of them.
    AttachValueSidecar(profile, in_path);
    // The review mode honours the deck's ruling too. It exists to preview what the generator will
    // produce, and a preview that ignores a stored decision is the same class of bug as one that
    // ignores the value sidecar.
    BucketPolicy bucket_policy = LoadBucketPolicy(a.deck_path);
    {
        std::vector<std::string> distinct;
        for (const Card& c : a.deck.mainboard)
        { if (std::find(distinct.begin(), distinct.end(), c.m_name.str()) == distinct.end())
          { distinct.push_back(c.m_name.str()); } }
        ValidateBucketPolicy(bucket_policy, distinct);
    }
    PrintBucketPolicy(std::cerr, bucket_policy);

    std::cerr << "Equivalence discovery: " << probes << " probes, depth " << depth
              << ", threshold " << threshold << ", horizon " << a.max_turns << "\n";
    EquivReport rep = DiscoverEquivalence(a.deck, profile, probes, depth, budget_ms,
                                          threshold, a.seed, a.max_turns,
                                          bucket_policy.Empty() ? nullptr : &bucket_policy);
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
