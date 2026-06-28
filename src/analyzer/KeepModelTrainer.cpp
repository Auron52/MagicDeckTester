#include "KeepModelTrainer.h"
#include "../ai/AIEngine.h"
#include "../ai/MulliganProfileIO.h"   // ColorToChar (logging)
#include "../cards/CardDatabase.h"
#include "../core/GameEngine.h"
#include "../core/HardwareConcurrency.h"
#include "../runner/GoldFishRunner.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <map>
#include <set>
#include <thread>
#include <tuple>

namespace
{
constexpr int BASE_FN = static_cast<int>(KeepFeature::Count);  // # compiled base features
// Total feature count = base + constructed data-defined specs; set per-run in BuildKeepModel and
// threaded through the structures below (the CART is generic over the full vector).
int g_nf = BASE_FN;

// The keep model OWNS the keep/mulligan decision at every level it is reached at -- it is NOT given a
// stop_at (the runtime force-keep floor is bypassed when a keep model is present). The training depth
// is chosen ADAPTIVELY (see the reach-probability descent below): we emit a decision at each mulligan
// depth that is actually reached with probability >= eps, and compute one depth below it as the
// forced-keep baseline ("odds of a better hand by mulliganing on"). Deep, rarely-reached depths are
// never rolled out -- that both saves the expensive bottom-many-cards rollouts and lets the effective
// floor EMERGE from the data instead of being inherited from the static path's grid-searched stop_at.
// (The old hardcoded LABEL_MAX=2 assumed stop_at>=4 and decided untrained hand sizes when stop_at was
// lower -- the Slivers gi=35 landless-keep bug.) M_CAP bounds the per-game arrays: m = 0..6 = sizes 7..1.
constexpr int M_CAP = 6;

// ---- dynamic self-scheduled parallel-for (mirrors AnalyzerEngine's) ------------------
template <class Fn>
void ParallelFor(int n, Fn fn)
{
    if (n <= 0) { return; }
    int nth = std::min(concurrency_util::AffinityCpuCount(), n);
    std::atomic<int> next{0};
    std::vector<std::thread> threads;
    threads.reserve(nth);
    for (int t = 0; t < nth; ++t)
    {
        threads.emplace_back([&]()
        {
            for (;;)
            {
                int i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= n) { break; }
                fn(i);
            }
        });
    }
    for (std::thread& th : threads) { th.join(); }
}

// ---- per-sampled-game results --------------------------------------------------------
struct GameData
{
    bool valid = false;
    int  kv[2][M_CAP + 1];                       // keep value (win turn), [on_play][mulligan_count]
    std::vector<int> feats[2][M_CAP + 1];        // feature vector (size g_nf), [on_play][mull][feature]
    std::vector<Card> hand;                       // the sampled opening 7 (for bootstrap reference keep)
};

// One labeled training row.
struct Row
{
    std::vector<int> x;      // feature vector (size g_nf)
    int    y;                // 1 = keep, 0 = mulligan (the oracle label)
    double kv;               // win turn if kept
    double thr;              // win turn if mulliganed (= value of mulliganing once more)
    int    game;             // sampling game index (for the deterministic train/test split)
};

// ---- the deck's colours (those appearing in any card's mana cost) --------------------
std::vector<Color> DeckColors(const Decklist& deck)
{
    std::set<Color> seen;
    for (const Card& c : deck.mainboard)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        if (!def) { continue; }
        const ManaCost& cost = def->card.m_mana_cost;
        if (cost.white > 0) { seen.insert(Color::White); }
        if (cost.blue  > 0) { seen.insert(Color::Blue);  }
        if (cost.black > 0) { seen.insert(Color::Black); }
        if (cost.red   > 0) { seen.insert(Color::Red);   }
        if (cost.green > 0) { seen.insert(Color::Green); }
    }
    return std::vector<Color>(seen.begin(), seen.end());
}

// ---- key pieces (the cards KeyPieceCount counts) -------------------------------------
// Prefer the analyzer's confirmed required_pieces; otherwise fall back to the highest
// first-copy card scores. Deterministic: required_pieces in profile order, else scores
// sorted by marginal desc then name.
std::vector<std::string> KeyPieces(const MulliganProfile& base_profile,
                                   const std::map<std::string, std::vector<double>>& card_scores)
{
    if (!base_profile.required_pieces.empty()) { return base_profile.required_pieces; }

    // All nonland cards by first-copy marginal, sorted desc (name tiebreak -> deterministic).
    std::vector<std::pair<std::string, double>> scored;
    for (const auto& kv : card_scores)
    {
        if (kv.second.empty()) { continue; }
        const CardDefinition* def = CardDatabase::Instance().Lookup(kv.first);
        if (def && def->card.IsLand()) { continue; }   // lands are not "key pieces"
        scored.emplace_back(kv.first, kv.second[0]);
    }
    std::sort(scored.begin(), scored.end(),
        [](const auto& a, const auto& b)
        {
            if (a.second != b.second) { return a.second > b.second; }
            return a.first < b.first;
        });

    // Primary: cards carrying a real first-copy marginal (>= 0.30). Synergy decks (e.g. slivers) have
    // FLAT marginals -- no single payoff clears 0.30 -- so when fewer than 2 qualify, fall back to a
    // deck-RELATIVE top quartile (the densest few cards) so the model still gets a payoff/density axis
    // instead of a constant-0 key_piece_count. Data-driven, no per-deck authoring.
    int primary = 0;
    for (const auto& s : scored) { if (s.second >= 0.30) { ++primary; } }
    int want = (primary >= 2) ? std::min(primary, 4)
                              : std::min<int>(4, std::max<int>(2, static_cast<int>(scored.size()) / 4));

    std::vector<std::string> out;
    for (const auto& s : scored)
    {
        out.push_back(s.first);
        if (static_cast<int>(out.size()) >= want) { break; }
    }
    return out;
}

// ---- the deck's dominant creature subtype (tribal density signal) --------------------
// The subtype that appears on the most mainboard cards (>= 4 copies to qualify as a real theme).
// Empty if no subtype is shared widely -- then no SubtypeDensity feature is constructed.
std::string DeckDominantSubtype(const Decklist& deck)
{
    std::map<std::string, int> counts;
    for (const Card& c : deck.mainboard)
    {
        if (c.IsLand()) { continue; }
        for (const std::string& st : c.m_subtypes) { ++counts[st]; }
    }
    std::string best; int best_n = 0;
    for (const auto& kv : counts)   // map -> deterministic name order on ties
    {
        if (kv.second > best_n) { best_n = kv.second; best = kv.first; }
    }
    return best_n >= 4 ? best : std::string{};
}

// ---- construct the rich candidate feature set (the "train on the inputs" basis) ------
// These are APPENDED after the base vector; the CART then keeps only what it splits on. We avoid
// duplicating base features: base already carries source_w..g / uncovered_colors / playable_strict /
// curve_depth / count_mv1..3, so the extras add the axes the base CANNOT express:
//   - per-colour DEMAND, per-colour UNCOVERED, per-colour SURPLUS (src-demand, signed screw severity)
//   - per-CMC inventory (nonland_mv0..6) and per-CMC castability (castable_le1..4)
//   - tribal density (dominant subtype)
//   - arithmetic composites over base features the tree can't form by splitting (uncastable, excess
//     development) -- operands are BASE indices (stable), so no extra->extra ordering dependence.
std::vector<FeatureSpec> BuildCandidateSpecs(const Decklist& deck, const std::vector<Color>& deck_colors)
{
    auto colorTag = [](Color c) { return std::string(ColorToChar(c)); };
    std::vector<FeatureSpec> specs;
    auto add = [&](FeatureKind k, int p, int a, int b, const std::string& s, const std::string& name)
    { FeatureSpec fs; fs.kind = static_cast<int>(k); fs.p = p; fs.a = a; fs.b = b; fs.s = s; fs.name = name; specs.push_back(fs); };

    for (Color c : deck_colors)
    {
        const int ci = static_cast<int>(c);
        const std::string t = colorTag(c);
        add(FeatureKind::PerColorDemand,    ci, -1, -1, "", "demand_"   + t);
        add(FeatureKind::PerColorUncovered, ci, -1, -1, "", "uncov_"    + t);
        add(FeatureKind::PerColorSurplus,   ci, -1, -1, "", "surplus_"  + t);
    }
    for (int mv = 0; mv <= 6; ++mv)
    { add(FeatureKind::NonlandAtMv, mv, -1, -1, "", "nonland_mv" + std::to_string(mv)); }
    for (int mv = 1; mv <= 4; ++mv)
    { add(FeatureKind::CastableAtMvLe, mv, -1, -1, "", "castable_le" + std::to_string(mv)); }

    const std::string dom = DeckDominantSubtype(deck);
    if (!dom.empty()) { add(FeatureKind::SubtypeDensity, 0, -1, -1, dom, "subtype_" + dom); }

    // Composites over base features (stable indices).
    add(FeatureKind::Diff, 0, static_cast<int>(KeepFeature::NonlandCount),
        static_cast<int>(KeepFeature::PlayableStrict), "", "uncastable");      // cards stuck in hand
    add(FeatureKind::Diff, 0, static_cast<int>(KeepFeature::LandCount),
        static_cast<int>(KeepFeature::CurveDepth), "", "excess_dev");          // lands beyond the curve
    return specs;
}

// ==========================================================================
// Greedy CART over integer features (binary keep/mull classification).
// All splits are "feature <= val"; that subsumes boolean features (on_the_play <= 0 ==
// "on the draw") so a single op keeps the serialized model uniform.
// ==========================================================================
double Gini(int keep, int n)
{
    if (n == 0) { return 0.0; }
    double p = static_cast<double>(keep) / n;
    return 2.0 * p * (1.0 - p);
}

class TreeBuilder
{
public:
    // regret=false: classic Gini over the binary keep label (the default; majority-vote leaves =>
    // decides on the MEDIAN kv vs thr). regret=true: split + leaf minimize TOTAL expected win-turn
    // directly, using each row's (kv, thr=V[m+1]) -- a leaf keeps iff mean(kv) <= mean(thr), i.e. it
    // compares EXPECTED outcomes (distribution-aware: the loss tail counts), which is the exact
    // optimal-stopping rule and the user's turn-regret objective. The recursive option to mulligan
    // FURTHER is already baked into thr=V[m+1] (the policy continuation value); the regret leaf only
    // fixes the mean-vs-median aggregation at the leaf.
    TreeBuilder(const std::vector<Row>& rows, int max_depth, int min_leaf, bool regret = false)
        : m_rows(rows), m_max_depth(max_depth), m_min_leaf(min_leaf), m_regret(regret) {}

    std::vector<KeepNode> Build(const std::vector<int>& idx)
    {
        m_nodes.clear();
        BuildNode(idx, 0);
        return m_nodes;
    }

private:
    const std::vector<Row>& m_rows;
    int                     m_max_depth;
    int                     m_min_leaf;
    bool                    m_regret = false;
    std::vector<KeepNode>   m_nodes;

    int BuildNode(const std::vector<int>& idx, int depth)
    {
        const int n = static_cast<int>(idx.size());
        int keep = 0;
        double skv = 0.0, sthr = 0.0;   // regret mode: sum of kept / mulliganed win-turns over idx
        for (int i : idx) { keep += m_rows[i].y; skv += m_rows[i].kv; sthr += m_rows[i].thr; }

        const int node_idx = static_cast<int>(m_nodes.size());
        m_nodes.push_back(KeepNode{});   // placeholder; patched below

        // Parent impurity to beat: Gini of the labels, or (regret) the min achievable expected
        // win-turn of a single leaf action = min(sum kv, sum thr).
        const double parent_score = m_regret ? std::min(skv, sthr) : Gini(keep, n);
        bool make_leaf = depth >= m_max_depth || n <= m_min_leaf
                      || (!m_regret && (keep == 0 || keep == n));

        int    best_feat = -1, best_val = 0;
        double best_score = parent_score;   // require a strict improvement to split

        if (!make_leaf)
        {
            for (int f = 0; f < g_nf; ++f)
            {
                // (value, kv, thr, label) tuples for this feature, sorted by value.
                std::vector<std::tuple<int, double, double, int>> vv;
                vv.reserve(n);
                for (int i : idx)
                { vv.emplace_back(m_rows[i].x[f], m_rows[i].kv, m_rows[i].thr, m_rows[i].y); }
                std::sort(vv.begin(), vv.end(),
                          [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });

                // Sweep thresholds at each distinct value boundary; "<= v" goes left.
                int yes_keep = 0, yes_n = 0;
                double yes_kv = 0.0, yes_thr = 0.0;
                for (int k = 0; k < n; ++k)
                {
                    yes_keep += std::get<3>(vv[k]);
                    yes_kv   += std::get<1>(vv[k]);
                    yes_thr  += std::get<2>(vv[k]);
                    ++yes_n;
                    // evaluate a split only at the last index of a run of equal values
                    if (k + 1 < n && std::get<0>(vv[k + 1]) == std::get<0>(vv[k])) { continue; }
                    if (yes_n < m_min_leaf || n - yes_n < m_min_leaf) { continue; }
                    if (k + 1 >= n) { continue; }   // no right child (split at max value)

                    double score;
                    if (m_regret)
                    {
                        // Total expected win-turn if each side takes its own best leaf action.
                        score = std::min(yes_kv, yes_thr) + std::min(skv - yes_kv, sthr - yes_thr);
                    }
                    else
                    {
                        const int no_keep = keep - yes_keep;
                        const int no_n    = n - yes_n;
                        score = (yes_n * Gini(yes_keep, yes_n) + no_n * Gini(no_keep, no_n)) / n;
                    }
                    // strict-< with feature/value iteration order makes ties deterministic. 1e-12
                    // keeps the gini path byte-identical to the committed engine; regret-mode score
                    // deltas are turn-scale sums (>> 1e-12 when nonzero), so the same epsilon only
                    // rejects exact ties there too.
                    if (score < best_score - 1e-12)
                    {
                        best_score = score;
                        best_feat  = f;
                        best_val   = std::get<0>(vv[k]);
                    }
                }
            }
        }

        if (best_feat < 0)   // leaf: best single action. ties -> keep (conservative)
        {
            KeepNode& nd = m_nodes[node_idx];
            nd.feat = -1;
            nd.keep = m_regret ? (skv <= sthr ? 1 : 0) : ((keep * 2 >= n) ? 1 : 0);
            return node_idx;
        }

        std::vector<int> yes, no;
        yes.reserve(n);
        no.reserve(n);
        for (int i : idx)
        {
            if (m_rows[i].x[best_feat] <= best_val) { yes.push_back(i); }
            else                                    { no.push_back(i); }
        }
        const int yi = BuildNode(yes, depth + 1);
        const int ni = BuildNode(no,  depth + 1);

        // Decision-preserving prune: if a split sends both sides to the SAME leaf verdict it
        // changes no decision, so collapse it into one leaf. Children were appended last and
        // contiguously, so when both are bare leaves they are the top two nodes -- pop them.
        // Applied bottom-up (children build first), so this also lets a parent re-collapse,
        // reaching a fixpoint in one pass. Keeps the emitted rules minimal/readable.
        if (m_nodes[yi].feat < 0 && m_nodes[ni].feat < 0 && m_nodes[yi].keep == m_nodes[ni].keep)
        {
            const int keepv = m_nodes[yi].keep;
            m_nodes.pop_back();   // ni
            m_nodes.pop_back();   // yi
            KeepNode& leaf = m_nodes[node_idx];
            leaf      = KeepNode{};
            leaf.feat = -1;
            leaf.keep = keepv;
            return node_idx;
        }

        KeepNode& nd = m_nodes[node_idx];
        nd.feat = best_feat;
        nd.op   = static_cast<int>(KeepOp::Le);
        nd.val  = best_val;
        nd.yes  = yi;
        nd.no   = ni;
        return node_idx;
    }
};

// Mean regret (win-turn cost vs the oracle action) of a fitted tree over a row subset.
double MeanRegret(const std::vector<KeepNode>& nodes,
                  const std::vector<Row>& rows, const std::vector<int>& idx)
{
    if (idx.empty()) { return 0.0; }
    KeepModel m;
    m.nodes = nodes;
    double sum = 0.0;
    for (int i : idx)
    {
        const Row& r = rows[i];
        const std::vector<int> feats(r.x.begin(), r.x.end());
        const bool keep = m.Keep(feats);
        const double chosen = keep ? r.kv : r.thr;
        const double oracle = std::min(r.kv, r.thr);
        sum += chosen - oracle;   // >= 0; turns of expected win-turn lost to a wrong call
    }
    return sum / idx.size();
}

// Pretty-print the tree as nested if/else rules to stderr (the Stage-6a disclosure). Resolves feature
// names via the model so data-defined extra features print by their constructed name.
void PrintTree(const KeepModel& model, const std::vector<KeepNode>& nodes, int idx, int indent)
{
    std::string pad(indent * 2, ' ');
    const KeepNode& nd = nodes[idx];
    if (nd.feat < 0)
    {
        std::cerr << pad << (nd.keep ? "KEEP" : "MULLIGAN") << "\n";
        return;
    }
    std::cerr << pad << "if " << FeatureNameAt(model, nd.feat)
              << " " << KeepOpName(static_cast<KeepOp>(nd.op)) << " " << nd.val << ":\n";
    PrintTree(model, nodes, nd.yes, indent + 1);
    std::cerr << pad << "else:\n";
    PrintTree(model, nodes, nd.no, indent + 1);
}
} // namespace

// ==========================================================================
KeepModel BuildKeepModel(const Decklist& deck,
                         const MulliganProfile& base_profile,
                         const std::map<std::string, std::vector<double>>& card_scores,
                         const KeepModelTrainConfig& cfg,
                         KeepModel* out_alt)
{
    std::cerr << "Building mulligan keep model (" << cfg.games
              << " hands, depth=" << cfg.depth << ")...\n";

    // The feature context the model references. The SAME object is used to featurize during
    // training and is emitted in the final model, so runtime features match training exactly.
    KeepModel feat_model;
    feat_model.deck_colors    = DeckColors(deck);
    feat_model.key_pieces     = KeyPieces(base_profile, card_scores);
    feat_model.extra_features = BuildCandidateSpecs(deck, feat_model.deck_colors);
    g_nf = BASE_FN + static_cast<int>(feat_model.extra_features.size());
    std::cerr << "  keep-model: " << g_nf << " candidate features ("
              << BASE_FN << " base + " << feat_model.extra_features.size() << " constructed).\n";

    // The profile the rollouts use: identical to the analyzer's chosen profile but with NO
    // keep model (the keep model is what we are generating; the rollout decides nothing here).
    MulliganProfile rollout_profile = base_profile;
    rollout_profile.keep_model = KeepModel{};

    const int      G        = std::max(0, cfg.games);
    const uint64_t seed_off = cfg.seed + 7'000'000ULL;   // far from the other analyzer phases
    const bool     second_main = GoldFishRunner::DeckUsesSecondMain(deck);

    // ---- 1. sample hands + features (parallel). Keep VALUES are computed adaptively below. ----
    std::vector<GameData> data(G);
    {
        std::atomic<int> done{0};
        const int prog_step = std::max(1, G / 10);
        ParallelFor(G, [&](int g)
        {
            GameState s = GoldFishRunner::SetupGame(deck, seed_off + static_cast<uint64_t>(g));
            s.ActivePlayer().library.DrawN(7, s.ActivePlayer().hand);
            if (static_cast<int>(s.ActivePlayer().hand.size()) != 7) { return; }
            const std::vector<Card> hand = s.ActivePlayer().hand;
            for (int p = 0; p < 2; ++p)
            for (int m = 0; m <= M_CAP; ++m)
            {
                data[g].feats[p][m] = ComputeKeepFeatures(hand, m, (p == 1), feat_model);
            }
            data[g].hand  = hand;   // kept for the bootstrap reference keep policy (policy baseline)
            data[g].valid = true;
            const int d = ++done;
            if (d % prog_step == 0 || d == G)
            { std::cerr << "    keep-model: sampled " << d << "/" << G << " hands\n" << std::flush; }
        });
    }

    // Compute the clairvoyant keep value at mulligan depth m for every sampled hand (parallel).
    // Re-derives each game's exact opening state from its seed (deterministic), bottoms m, rolls out.
    // Called on demand by the adaptive descent so we never pay for rarely-reached (expensive) depths.
    auto compute_kv_level = [&](int m)
    {
        std::cerr << "    keep-model: keep-values at hand size " << (7 - m) << "...\n" << std::flush;
        ParallelFor(G, [&](int g)
        {
            if (!data[g].valid) { return; }
            AIEngine ai(rollout_profile, cfg.depth, cfg.budget_ms);
            ai.SetSearchPostCombat(second_main);
            for (int p = 0; p < 2; ++p)
            {
                GameState s = GoldFishRunner::SetupGame(deck, seed_off + static_cast<uint64_t>(g));
                s.m_required_pieces = &rollout_profile.required_pieces;
                s.vial_target_mv    = rollout_profile.vial_target_mv;
                s.ActivePlayer().library.DrawN(7, s.ActivePlayer().hand);
                s.on_the_play       = (p == 1);
                data[g].kv[p][m]    = ai.RolloutKeepWinTurn(s, m, cfg.max_turns);
            }
        });
    };

    // Backward induction of the mulligan value M[p][m] given a forced-keep anchor at depth `anchor`:
    // M[p][anchor] = mean keep value there; shallower M[p][m] = mean min(keep this hand, mulligan on).
    double M[2][M_CAP + 1] = {{0}};
    auto induct = [&](int anchor)
    {
        for (int p = 0; p < 2; ++p)
        {
            double sum = 0.0; int cnt = 0;
            for (int g = 0; g < G; ++g)
            { if (data[g].valid) { sum += data[g].kv[p][anchor]; ++cnt; } }
            M[p][anchor] = cnt ? sum / cnt : static_cast<double>(cfg.max_turns + 1);
            for (int m = anchor - 1; m >= 0; --m)
            {
                double s2 = 0.0; int c2 = 0;
                for (int g = 0; g < G; ++g)
                {
                    if (!data[g].valid) { continue; }
                    s2 += std::min(static_cast<double>(data[g].kv[p][m]), M[p][m + 1]);
                    ++c2;
                }
                M[p][m] = c2 ? s2 / c2 : M[p][m + 1];
            }
        }
    };

    // P(reach mulligan depth `anchor`) = product over shallower levels of that level's mulligan rate
    // (fraction of hands whose keep value is worse than mulliganing on). Max over play/draw.
    auto reach_prob = [&](int anchor) -> double
    {
        double pmax = 0.0;
        for (int p = 0; p < 2; ++p)
        {
            double pr = 1.0;
            for (int j = 0; j < anchor; ++j)
            {
                int mull = 0, n = 0;
                for (int g = 0; g < G; ++g)
                {
                    if (!data[g].valid) { continue; }
                    if (data[g].kv[p][j] > M[p][j + 1]) { ++mull; }
                    ++n;
                }
                pr *= n ? static_cast<double>(mull) / n : 0.0;
            }
            pmax = std::max(pmax, pr);
        }
        return pmax;
    };

    // ---- 2. ADAPTIVE descent: deepen the forced-keep anchor only while the next depth is actually
    // reached with probability >= eps. Decisions are emitted for depths 0..anchor-1; the anchor depth
    // itself is the "odds of a better hand by mulliganing on" baseline. Deep, rarely-reached depths
    // (the expensive bottom-many-cards rollouts) are skipped without changing the realised policy.
    const double eps = []{ const char* e = std::getenv("MTG_KEEP_REACH_EPS");
                           double v = (e && *e) ? std::atof(e) : 0.01; return v > 0 ? v : 0.01; }();
    compute_kv_level(0);
    compute_kv_level(1);
    int anchor = 1;
    while (anchor < M_CAP)
    {
        induct(anchor);
        const double pr = reach_prob(anchor);
        std::cerr << "    keep-model: depth " << anchor << " (hand size " << (7 - anchor)
                  << ") reached p=" << pr << (pr >= eps ? " -> deepen\n" : " -> stop\n");
        if (pr < eps) { break; }
        compute_kv_level(anchor + 1);
        ++anchor;
    }
    const int kv_max    = anchor;       // forced-keep baseline depth
    const int label_max = anchor - 1;   // deepest decision depth
    induct(kv_max);                     // final M consistent with the chosen anchor
    std::cerr << "  keep-model: decisions at hand sizes 7.." << (7 - label_max)
              << ", forced-keep anchor at size " << (7 - kv_max)
              << " (reach-prob eps=" << eps << ").\n";

    // ---- 3. fit a tree against a per-(play,depth) BASELINE ---------------------------
    // Tree split criterion (`regret` arg): regret=true minimises total EXPECTED win-turn directly
    // (distribution-aware mean(kv)<=V leaves -- the optimal-stopping rule); regret=false uses the
    // classic Gini majority-vote (median) leaf. Regret pairs naturally with the policy baseline.
    // fit_nodes builds the labeled rows for a given mulligan baseline[p][m] (= "value of mulliganing
    // on" at depth m, on-play p), labels y=(kv<=thr), 80/20 splits by game, and picks the shallowest
    // tree within MARGIN of a deep baseline. Returns the UNCOMPACTED node vector (feat indices into
    // the full candidate vector, so a fitted tree can be re-evaluated on the cached feats during
    // policy iteration); compaction happens once at the end. Empty -> too few rows (caller bails).
    auto fit_nodes = [&](const double (&baseline)[2][M_CAP + 1], const std::string& tag,
                         bool dump_rows, bool regret) -> std::vector<KeepNode>
    {
        std::vector<Row> rows;
        for (int g = 0; g < G; ++g)
        {
            if (!data[g].valid) { continue; }
            for (int p = 0; p < 2; ++p)
            for (int m = 0; m <= label_max; ++m)
            {
                const int eff = 7 - m;
                if (eff <= 1) { continue; }   // size 1 is the forced-keep anchor, not a decision
                Row r;
                r.x    = data[g].feats[p][m];
                r.kv   = data[g].kv[p][m];
                r.thr  = baseline[p][m + 1];                 // value of mulliganing once more
                r.y    = (r.kv <= r.thr) ? 1 : 0;
                r.game = g;
                rows.push_back(r);
            }
        }

        if (static_cast<int>(rows.size()) < 200)
        {
            std::cerr << "  keep-model" << tag << ": too few labeled hands (" << rows.size()
                      << ") -- keeping the legacy keep path.\n";
            return {};
        }

        int keep_rows = 0;
        for (const Row& r : rows) { keep_rows += r.y; }
        std::cerr << "  keep-model" << tag << ": " << rows.size() << " labeled hands ("
                  << keep_rows << " keep / " << (rows.size() - keep_rows) << " mull).\n";

        // DIAGNOSTIC (MTG_KEEP_DUMP=path): dump every labeled training row so we can inspect what
        // signal the labels actually carry (keep-rate / kv vs features). Off by default; env-gated.
        if (dump_rows)
        if (const char* dp = std::getenv("MTG_KEEP_DUMP"); dp && *dp)
        {
            std::ofstream out(dp);
            // Generic over the full feature vector (base + constructed) so the dump never goes stale.
            for (int k = 0; k < g_nf; ++k) { out << FeatureNameAt(feat_model, k) << ','; }
            out << "kv,thr,y\n";
            for (const Row& r : rows)
            {
                for (int k = 0; k < g_nf; ++k) { out << r.x[k] << ','; }
                out << r.kv << ',' << r.thr << ',' << r.y << '\n';
            }
            std::cerr << "  keep-model: dumped " << rows.size() << " rows to " << dp << "\n";
        }

        // Deterministic 80/20 train/test split by game index (every row of a game stays together).
        std::vector<int> train, test;
        for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        {
            if (rows[i].game % 5 == 0) { test.push_back(i); } else { train.push_back(i); }
        }

        const int N        = static_cast<int>(rows.size());
        const int min_leaf = std::max(10, N / 200);

        // Strong (unconstrained) baseline: a deep tree. Its held-out regret is the bar the
        // interpretable form must approach -- the win-turn cost a far more complex model pays.
        std::vector<KeepNode> deep = TreeBuilder(rows, 9, std::max(4, N / 1000), regret).Build(train);
        const double deep_regret = MeanRegret(deep, rows, test);

        // Context: the cost of NOT learning at all (always keep / always mulligan).
        double always_keep = 0.0, always_mull = 0.0;
        for (int i : test)
        {
            const Row& r = rows[i];
            const double oracle = std::min(r.kv, r.thr);
            always_keep += r.kv  - oracle;
            always_mull += r.thr - oracle;
        }
        if (!test.empty()) { always_keep /= test.size(); always_mull /= test.size(); }

        std::cerr << "  keep-model" << tag << " accuracy bar (held-out mean regret, turns):\n"
                  << "    always-keep=" << always_keep << "  always-mull=" << always_mull
                  << "  deep-tree=" << deep_regret << "\n";

        // Pick the SHALLOWEST depth whose held-out regret is within MARGIN of the deep baseline
        // (the readable form may not pay a meaningful win-turn cost). If none qualifies, fall back
        // to the depth with the LOWEST regret -- accuracy over brevity when they genuinely trade off.
        // NOTE: the bar is deliberately CONSERVATIVE (prefers shallow). Held-out regret here is
        // measured on the same sampled-hand split, not on fresh games, so a deeper tree that lowers
        // in-sample regret can still GENERALISE worse downstream. Bias toward simplicity; let the
        // richer feature axes earn their splits only when the SAMPLE is large enough to justify them.
        constexpr double MARGIN = 0.02;   // turns: max regret a readable tree may pay over deep
        int    chosen_depth  = -1;
        double chosen_regret = 1e18;
        int    best_depth    = 3;
        double best_regret   = 1e18;
        for (int d = 3; d <= 6; ++d)
        {
            std::vector<KeepNode> t = TreeBuilder(rows, d, min_leaf, regret).Build(train);
            const double rg = MeanRegret(t, rows, test);
            std::cerr << "    depth=" << d << " -> regret=" << rg
                      << " (" << t.size() << " nodes)\n";
            if (rg < best_regret) { best_regret = rg; best_depth = d; }
            if (chosen_depth < 0 && rg <= deep_regret + MARGIN) { chosen_depth = d; chosen_regret = rg; }
        }
        if (chosen_depth < 0) { chosen_depth = best_depth; chosen_regret = best_regret; }

        // Final model: refit the chosen depth on ALL rows (report held-out, ship full-data fit).
        std::vector<KeepNode> fit =
            TreeBuilder(rows, chosen_depth, min_leaf, regret).Build([&]{
                std::vector<int> all(N);
                for (int i = 0; i < N; ++i) { all[i] = i; }
                return all;
            }());
        std::cerr << "  keep-model" << tag << ": chose depth " << chosen_depth
                  << " (held-out regret " << chosen_regret << " turns, " << fit.size() << " nodes).\n";
        return fit;
    };

    // ---- 3b. baseline selection: legacy optimizer's-curse min vs policy-simulated --------------
    // Default ("min"): the legacy backward-induction M (computed above) -- M[p][m]=mean_g min(kv,M[m+1]).
    // Each hand self-selects keep-vs-mull on its OWN single realised game, so M is biased LOW (the
    // OPTIMIZER'S/WINNER'S CURSE) and the fitted model OVER-MULLIGANS. Opt-in ("policy"): a
    // POLICY-SIMULATED baseline V[p][m] = expected win-turn of being at depth m and following a FIXED,
    // OUTCOME-BLIND keep policy pi to the anchor:
    //     V[p][anchor] = mean_g kv                                   (forced keep at the anchor)
    //     V[p][m]      = mean_g ( pi(hand_g@m) ? kv[p][m][g] : V[p][m+1] )
    // pi never sees a hand's realised kv (it is a function of the hand/features only), so a kept hand
    // contributes its own UNBIASED mean win-turn and a mulliganed hand the common continuation value --
    // no self-selection on the outcome, so the curse is gone. pi is bootstrapped from the static profile
    // keep decision and POLICY-ITERATED (refit -> re-simulate V -> refit) toward the fixed point.
    const std::string baseline_mode = []{ const char* e = std::getenv("MTG_KEEP_BASELINE");
                                          return std::string(e && *e ? e : "min"); }();
    const int policy_iters = []{ const char* e = std::getenv("MTG_KEEP_POLICY_ITERS");
                                 int v = (e && *e) ? std::atoi(e) : 3; return v < 0 ? 0 : v; }();

    // Split criterion selection. "gini" (default) / "regret" / "both" (fit BOTH from the one shared,
    // expensive kv table: only the cheap CART fit differs, so emitting both costs ~nothing extra).
    const std::string split_mode = []{ const char* e = std::getenv("MTG_KEEP_SPLIT");
                                       return std::string(e && *e ? e : "gini"); }();

    // Build ONE model variant (regret or gini split) over the shared kv/baseline. vtag prefixes the
    // disclosure so the two variants are distinguishable in "both" mode. Returns empty on too-few rows.
    auto make_model = [&](bool regret, const std::string& vtag) -> KeepModel
    {
        std::vector<KeepNode> final_nodes;
        if (baseline_mode != "policy")
        {
            final_nodes = fit_nodes(M, vtag, /*dump_rows=*/!regret, regret);
            if (final_nodes.empty()) { return KeepModel{}; }
        }
        else
        {
            std::cerr << "  keep-model" << vtag << ": POLICY-SIMULATED baseline (bootstrap=static, iters="
                      << policy_iters << ").\n";
            AIEngine ref_ai(rollout_profile, cfg.depth, cfg.budget_ms);
            ref_ai.SetSearchPostCombat(second_main);

            double V[2][M_CAP + 1] = {{0}};
            auto simulate_V = [&](auto keep_fn)
            {
                for (int p = 0; p < 2; ++p)
                {
                    double sum = 0.0; int cnt = 0;
                    for (int g = 0; g < G; ++g)
                    { if (data[g].valid) { sum += data[g].kv[p][kv_max]; ++cnt; } }
                    V[p][kv_max] = cnt ? sum / cnt : static_cast<double>(cfg.max_turns + 1);
                    for (int m = kv_max - 1; m >= 0; --m)
                    {
                        double s2 = 0.0; int c2 = 0;
                        for (int g = 0; g < G; ++g)
                        {
                            if (!data[g].valid) { continue; }
                            const bool keep = keep_fn(p, m, g);
                            s2 += keep ? static_cast<double>(data[g].kv[p][m]) : V[p][m + 1];
                            ++c2;
                        }
                        V[p][m] = c2 ? s2 / c2 : V[p][m + 1];
                    }
                }
            };
            auto nodes_equal = [](const std::vector<KeepNode>& a, const std::vector<KeepNode>& b)
            {
                if (a.size() != b.size()) { return false; }
                for (size_t i = 0; i < a.size(); ++i)
                {
                    const KeepNode& x = a[i]; const KeepNode& y = b[i];
                    if (x.feat != y.feat || x.op != y.op || x.val != y.val
                     || x.yes  != y.yes  || x.no  != y.no  || x.keep != y.keep) { return false; }
                }
                return true;
            };

            // Iteration 0: bootstrap pi from the static profile keep decision (outcome-blind).
            simulate_V([&](int p, int m, int g)
                       { return ref_ai.ReferenceKeep(data[g].hand, m, (p == 1)); });
            final_nodes = fit_nodes(V, vtag + " [it0]", /*dump_rows=*/!regret && policy_iters == 0, regret);
            if (final_nodes.empty()) { return KeepModel{}; }

            // Policy iteration: pi <- fitted tree, re-simulate V, refit. Converges to the fixed point.
            for (int it = 1; it <= policy_iters; ++it)
            {
                KeepModel pi;
                pi.nodes          = final_nodes;   // UNCOMPACTED -> indices align with the full vector
                pi.key_pieces     = feat_model.key_pieces;
                pi.deck_colors    = feat_model.deck_colors;
                pi.extra_features = feat_model.extra_features;
                simulate_V([&](int p, int m, int g) { return pi.Keep(data[g].feats[p][m]); });

                const std::string tag = vtag + " [it" + std::to_string(it) + "]";
                std::vector<KeepNode> next = fit_nodes(V, tag, /*dump_rows=*/!regret && it == policy_iters, regret);
                if (next.empty()) { break; }
                const bool same = nodes_equal(next, final_nodes);
                final_nodes = next;
                if (same) { std::cerr << "  keep-model" << vtag << ": policy converged at iter " << it << ".\n"; break; }
            }
        }

        // Keep ONLY the constructed extra specs the final tree splits on, remapping node feat indices to
        // the compacted [base ++ kept-extras] layout (composite operands reference BASE indices, stable
        // under compaction). The "surfaced levers" set; the rest of the candidate basis went unused.
        std::vector<int> used_extra;
        for (const KeepNode& nd : final_nodes) { if (nd.feat >= BASE_FN) { used_extra.push_back(nd.feat); } }
        std::sort(used_extra.begin(), used_extra.end());
        used_extra.erase(std::unique(used_extra.begin(), used_extra.end()), used_extra.end());

        std::vector<FeatureSpec> kept;
        std::map<int, int> remap;
        for (int oldidx : used_extra)
        {
            remap[oldidx] = BASE_FN + static_cast<int>(kept.size());
            kept.push_back(feat_model.extra_features[oldidx - BASE_FN]);
        }
        for (KeepNode& nd : final_nodes)
        { if (nd.feat >= BASE_FN) { nd.feat = remap[nd.feat]; } }

        KeepModel model;
        model.nodes          = final_nodes;
        model.key_pieces     = feat_model.key_pieces;
        model.deck_colors    = feat_model.deck_colors;
        model.extra_features = kept;

        std::cerr << "  keep-model" << vtag << ": final tree (" << model.nodes.size()
                  << " nodes, " << (regret ? "regret" : "gini") << " split). Rules:\n";
        PrintTree(model, final_nodes, 0, 2);

        std::map<int, int> splits;
        for (const KeepNode& nd : final_nodes) { if (nd.feat >= 0) { ++splits[nd.feat]; } }
        std::cerr << "  keep-model" << vtag << " features used:";
        if (splits.empty()) { std::cerr << " (none -- constant policy)"; }
        for (const auto& kv : splits)
        { std::cerr << " " << FeatureNameAt(model, kv.first) << "x" << kv.second; }
        std::cerr << "\n";
        if (!kept.empty())
        {
            std::cerr << "  keep-model" << vtag << " constructed features kept:";
            for (const FeatureSpec& s : kept) { std::cerr << " [" << s.name << "]"; }
            std::cerr << "\n";
        }
        return model;
    };

    // "both": primary = gini (the standard <deck>.keepmodel.profile.json), out_alt = regret (side file).
    // "regret": primary = regret. default/"gini": primary = gini. The kv table is computed once above.
    const bool want_both = (split_mode == "both");
    KeepModel primary = make_model(/*regret=*/split_mode == "regret", want_both ? " (gini)" : "");
    if (want_both && out_alt) { *out_alt = make_model(/*regret=*/true, " (regret)"); }

    std::cerr << "  keep-model key_pieces:";
    if (primary.key_pieces.empty()) { std::cerr << " (none)"; }
    for (const std::string& s : primary.key_pieces) { std::cerr << " [" << s << "]"; }
    std::cerr << "\n  keep-model deck_colors:";
    for (Color c : primary.deck_colors) { std::cerr << " " << ColorToChar(c); }
    std::cerr << "\n";

    return primary;
}
