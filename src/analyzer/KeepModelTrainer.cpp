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

// Fixed-point scale for the additive-score model's integer coefs/thresholds (mirrors how card_scores
// quantise a fitted double into the profile). 1e6 makes per-coef rounding error ~1e-6 turn/unit ->
// negligible vs the turn-scale decision, while staying far inside int64 for the dot product.
constexpr long long SCORE_SCALE = 1000000;

// Solve A x = b for a small dense DxD system by Gaussian elimination with partial pivoting (doubles,
// offline -- only the QUANTISED result ships, so this never feeds a GT decision directly). Returns
// false if singular. A and b are consumed (modified in place).
inline bool SolveLinear(std::vector<std::vector<double>>& A, std::vector<double>& b, std::vector<double>& x)
{
    const int D = static_cast<int>(b.size());
    for (int col = 0; col < D; ++col)
    {
        int piv = col; double best = std::abs(A[col][col]);
        for (int r = col + 1; r < D; ++r)
        { if (std::abs(A[r][col]) > best) { best = std::abs(A[r][col]); piv = r; } }
        if (best < 1e-12) { return false; }
        std::swap(A[col], A[piv]); std::swap(b[col], b[piv]);
        const double d = A[col][col];
        for (int r = col + 1; r < D; ++r)
        {
            const double f = A[r][col] / d;
            if (f == 0.0) { continue; }
            for (int c = col; c < D; ++c) { A[r][c] -= f * A[col][c]; }
            b[r] -= f * b[col];
        }
    }
    x.assign(D, 0.0);
    for (int row = D - 1; row >= 0; --row)
    {
        double s = b[row];
        for (int c = row + 1; c < D; ++c) { s -= A[row][c] * x[c]; }
        x[row] = s / A[row][row];
    }
    return true;
}

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
    double kv[2][M_CAP + 1];                     // keep value (win turn, blind-averaged), [on_play][mull]
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

    // CONJUNCTION (interaction) candidates -- products that are high only when BOTH operands hold, so a
    // LINEAR score can express an AND. These target combo/engine decks (e.g. Treasure Hunt) whose keep
    // is "have the payoff piece AND the support", which a pure additive sum of single-factor penalties
    // structurally cannot represent. Operands are BASE indices (stable under compaction).
    auto prod = [&](KeepFeature a, KeepFeature b, const std::string& nm)
    { add(FeatureKind::Product, 0, static_cast<int>(a), static_cast<int>(b), "", nm); };
    prod(KeepFeature::KeyPieceCount, KeepFeature::LandCount,      "piece_x_land");   // engine + mana
    prod(KeepFeature::KeyPieceCount, KeepFeature::PlayableStrict, "piece_x_castable");
    prod(KeepFeature::KeyPieceCount, KeepFeature::OnTimeCount,    "piece_x_ontime");
    prod(KeepFeature::KeyPieceCount, KeepFeature::CurveDepth,     "piece_x_curve");
    prod(KeepFeature::PlayableStrict, KeepFeature::LandCount,     "castable_x_land");
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
        if (nd.leaf_score >= 0) { std::cerr << pad << "SCORE#" << nd.leaf_score << " (additive leaf)\n"; }
        else { std::cerr << pad << (nd.keep ? "KEEP" : "MULLIGAN") << "\n"; }
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
                         KeepModel* out_alt,
                         KeepModel* out_score,
                         KeepModel* out_hybrid)
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
    // Multi-rollout BLIND keep value: average over R reshuffles of the REST of the library (the opening
    // 7 stay fixed). A single rollout's draw order is one realised game -- high variance for combo/flood
    // decks (TH/slivers/Hinata) -- so the per-hand label is noisy. Reshuffling the rest marginalises out
    // the draw (the OPPOSITE of clairvoyance): the label becomes the hand's expected win-turn over random
    // continuations, which is exactly what the keep decision should be predicting. R=1 (default) keeps
    // the original single-rollout behaviour (no reshuffle) so existing fits are unchanged.
    const int rollouts = []{ const char* e = std::getenv("MTG_KEEP_ROLLOUTS");
                             int v = (e && *e) ? std::atoi(e) : 1; return v < 1 ? 1 : v; }();
    auto compute_kv_level = [&](int m)
    {
        std::cerr << "    keep-model: keep-values at hand size " << (7 - m)
                  << (rollouts > 1 ? " (x" + std::to_string(rollouts) + " blind rollouts)" : "")
                  << "...\n" << std::flush;
        ParallelFor(G, [&](int g)
        {
            if (!data[g].valid) { return; }
            AIEngine ai(rollout_profile, cfg.depth, cfg.budget_ms);
            ai.SetSearchPostCombat(second_main);
            for (int p = 0; p < 2; ++p)
            {
                double sum = 0.0;
                for (int r = 0; r < rollouts; ++r)
                {
                    GameState s = GoldFishRunner::SetupGame(deck, seed_off + static_cast<uint64_t>(g));
                    s.m_required_pieces = &rollout_profile.required_pieces;
                    s.vial_target_mv    = rollout_profile.vial_target_mv;
                    s.ActivePlayer().library.DrawN(7, s.ActivePlayer().hand);
                    s.on_the_play       = (p == 1);
                    // Reshuffle the post-draw library (the rest) with a per-(g,p,r) deterministic seed.
                    // r==0 at R>1 still reshuffles, so the average is over R independent continuations.
                    if (rollouts > 1)
                    {
                        const uint64_t rs = seed_off + 0x9E3779B97F4A7C15ULL * (static_cast<uint64_t>(r) + 1)
                                          + 1000003ULL * static_cast<uint64_t>(g) + static_cast<uint64_t>(p);
                        s.ActivePlayer().library.Shuffle(rs);
                    }
                    sum += ai.RolloutKeepWinTurn(s, m, cfg.max_turns);
                }
                data[g].kv[p][m] = sum / rollouts;
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
    // Build the labeled rows for a given mulligan baseline[p][m] (= "value of mulliganing on" at depth
    // m). Shared by the tree fit and the additive-score fit so both see byte-identical rows/order.
    auto build_rows = [&](const double (&baseline)[2][M_CAP + 1]) -> std::vector<Row>
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
        return rows;
    };

    auto fit_nodes = [&](const double (&baseline)[2][M_CAP + 1], const std::string& tag,
                         bool dump_rows, bool regret) -> std::vector<KeepNode>
    {
        std::vector<Row> rows = build_rows(baseline);

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

    // ---- additive hand-score fit: ridge-regress kv on the full feature vector ----------------------
    // Predicts the hand's expected blind win-turn as a LINEAR sum, then KEEP iff that prediction is no
    // worse than the value of mulliganing on (= baseline[p][m+1]) -- optimal-stopping with a learned
    // predictor in place of the single noisy realised kv. Encodes the "graded accumulation of soft
    // factors" a shallow tree can't. The HYBRID below reuses this per tree-leaf.

    // Ridge fit in STANDARDISED feature space (lambda scale-free + well-conditioned despite collinear
    // colour features), mapped back to raw units. Operates on an index subset of `rows`.
    const int D = g_nf;
    auto ridge_fit = [&](const std::vector<Row>& rows, const std::vector<int>& idx, double lambda,
                         std::vector<double>& coef, double& inter) -> bool
    {
        const int n = static_cast<int>(idx.size());
        if (n == 0) { return false; }
        std::vector<double> mean(D, 0.0), sd(D, 0.0); double ybar = 0.0;
        for (int i : idx) { ybar += rows[i].kv; for (int j = 0; j < D; ++j) { mean[j] += rows[i].x[j]; } }
        ybar /= n; for (int j = 0; j < D; ++j) { mean[j] /= n; }
        for (int i : idx) for (int j = 0; j < D; ++j)
        { const double d = rows[i].x[j] - mean[j]; sd[j] += d * d; }
        for (int j = 0; j < D; ++j) { sd[j] = (sd[j] > 1e-9) ? std::sqrt(sd[j] / n) : 0.0; }
        std::vector<std::vector<double>> A(D, std::vector<double>(D, 0.0));
        std::vector<double> rhs(D, 0.0), z(D, 0.0);
        for (int i : idx)
        {
            for (int j = 0; j < D; ++j) { z[j] = sd[j] > 0.0 ? (rows[i].x[j] - mean[j]) / sd[j] : 0.0; }
            const double yc = rows[i].kv - ybar;
            for (int a = 0; a < D; ++a)
            {
                if (z[a] == 0.0) { continue; }
                rhs[a] += z[a] * yc;
                for (int b = 0; b < D; ++b) { A[a][b] += z[a] * z[b]; }
            }
        }
        for (int j = 0; j < D; ++j) { A[j][j] += lambda; }
        std::vector<double> w;
        if (!SolveLinear(A, rhs, w)) { return false; }
        coef.assign(D, 0.0); inter = ybar;
        for (int j = 0; j < D; ++j)
        { if (sd[j] > 0.0) { coef[j] = w[j] / sd[j]; inter -= coef[j] * mean[j]; } }
        return true;
    };
    // Held-out turn-regret of an additive DECISION (keep iff pred<=thr) over a row subset.
    auto score_regret = [&](const std::vector<Row>& rows, const std::vector<double>& coef, double inter,
                            const std::vector<int>& idx) -> double
    {
        if (idx.empty()) { return 0.0; }
        double sum = 0.0;
        for (int i : idx)
        {
            double pred = inter;
            for (int j = 0; j < D; ++j) { pred += coef[j] * rows[i].x[j]; }
            const bool keep = pred <= rows[i].thr;
            sum += (keep ? rows[i].kv : rows[i].thr) - std::min(rows[i].kv, rows[i].thr);
        }
        return sum / idx.size();
    };
    // Fit ONE additive score on a row subset (internal lambda sweep on a sub-split, refit on the full
    // subset), quantised to fixed-point. thr from the baseline. Returns empty on degenerate input.
    auto ridge_score = [&](const std::vector<Row>& rows, const std::vector<int>& subset,
                           const double (&baseline)[2][M_CAP + 1]) -> KeepScore
    {
        if (static_cast<int>(subset.size()) < 30) { return KeepScore{}; }
        std::vector<int> tr, te;
        for (int i : subset) { if (rows[i].game % 5 == 0) { te.push_back(i); } else { tr.push_back(i); } }
        if (tr.empty()) { tr = subset; }
        const double lambdas[] = { 0.001, 0.1, 1.0, 10.0, 100.0 };
        double best_lambda = 1.0, best_rg = 1e18; std::vector<double> coef; double inter = 0.0;
        for (double lam : lambdas)
        {
            if (!ridge_fit(rows, tr, lam, coef, inter)) { continue; }
            const double rg = te.empty() ? 0.0 : score_regret(rows, coef, inter, te);
            if (rg < best_rg - 1e-12) { best_rg = rg; best_lambda = lam; }
        }
        std::vector<double> ca; double ia = 0.0;
        if (!ridge_fit(rows, subset, best_lambda, ca, ia)) { return KeepScore{}; }
        KeepScore sc; sc.coefs.resize(D);
        for (int j = 0; j < D; ++j) { sc.coefs[j] = std::llround(ca[j] * SCORE_SCALE); }
        sc.intercept = std::llround(ia * SCORE_SCALE);
        sc.thr.assign(2, {});
        for (int p = 0; p < 2; ++p) for (int m = 0; m <= label_max; ++m)
        { sc.thr[p].push_back(std::llround(baseline[p][m + 1] * SCORE_SCALE)); }
        return sc;
    };

    auto fit_score = [&](const double (&baseline)[2][M_CAP + 1], const std::string& tag) -> KeepScore
    {
        std::vector<Row> rows = build_rows(baseline);
        if (static_cast<int>(rows.size()) < 200)
        { std::cerr << "  keep-model" << tag << ": too few labeled hands (" << rows.size() << ").\n"; return KeepScore{}; }
        std::vector<int> all(rows.size()); for (size_t i = 0; i < rows.size(); ++i) { all[i] = static_cast<int>(i); }
        return ridge_score(rows, all, baseline);
    };

    // Walk an UNCOMPACTED tree to the leaf node a feature vector lands in (feat indices into full vector).
    auto leaf_of = [&](const std::vector<KeepNode>& nodes, const std::vector<int>& x) -> int
    {
        const int n = static_cast<int>(nodes.size()); int idx = 0;
        for (int s = 0; s <= n && idx >= 0 && idx < n; ++s)
        {
            const KeepNode& nd = nodes[idx];
            if (nd.feat < 0) { return idx; }
            const int v = (nd.feat < static_cast<int>(x.size())) ? x[nd.feat] : 0;
            const bool t = (nd.op == (int)KeepOp::Ge) ? v >= nd.val : (nd.op == (int)KeepOp::Eq) ? v == nd.val : v <= nd.val;
            idx = t ? nd.yes : nd.no;
        }
        return 0;
    };

    // ---- HYBRID model-tree: a regret tree PARTITIONS the hand space; each leaf decides by its OWN
    // additive score (not a constant keep/mull). Strictly generalises both forms -- a 1-leaf tree = pure
    // score, all-zero leaf coefs = pure tree -- so it can represent whichever structure a deck needs
    // (partition a bimodal/conjunction deck like TH, accumulate soft factors within each partition).
    // Returns false on too-few rows; else fills out_nodes (leaves carry leaf_score indices) + out_leaves.
    auto fit_hybrid = [&](const double (&baseline)[2][M_CAP + 1], const std::string& tag,
                          std::vector<KeepNode>& out_nodes, std::vector<KeepScore>& out_leaves) -> bool
    {
        std::vector<Row> rows = build_rows(baseline);
        const int N = static_cast<int>(rows.size());
        if (N < 400) { std::cerr << "  keep-model" << tag << ": too few rows for hybrid (" << N << ").\n"; return false; }
        std::vector<int> train, test, all(N);
        for (int i = 0; i < N; ++i) { all[i] = i; (rows[i].game % 5 == 0 ? test : train).push_back(i); }
        const int min_leaf = std::max(80, N / 40);   // each leaf needs enough rows for a stable ridge fit

        // Assemble a hybrid from a partition tree + per-leaf scores fit on `fit_idx` rows; eval regret on
        // `eval_idx`. Reuses leaf_of to route rows. Returns regret + (optionally) the built model.
        auto build_eval = [&](int depth, const std::vector<int>& fit_idx, const std::vector<int>& eval_idx,
                              std::vector<KeepNode>* nodes_out, std::vector<KeepScore>* leaves_out) -> double
        {
            std::vector<KeepNode> nodes = TreeBuilder(rows, depth, min_leaf, /*regret=*/true).Build(fit_idx);
            // group fit rows by leaf
            std::map<int, std::vector<int>> by_leaf;
            for (int i : fit_idx) { by_leaf[leaf_of(nodes, rows[i].x)].push_back(i); }
            std::map<int, int> leaf_index;            // leaf node idx -> position in leaves vector
            std::vector<KeepScore> leaves;
            for (auto& kv : by_leaf)
            {
                KeepScore sc = ridge_score(rows, kv.second, baseline);
                if (sc.empty()) { continue; }         // tiny leaf -> fall back to the node's constant keep
                leaf_index[kv.first] = static_cast<int>(leaves.size());
                leaves.push_back(sc);
                nodes[kv.first].leaf_score = static_cast<int>(leaves.size()) - 1;
            }
            // evaluate hybrid regret on eval_idx
            double sum = 0.0; int cnt = 0;
            for (int i : eval_idx)
            {
                const int lf = leaf_of(nodes, rows[i].x);
                bool keep;
                auto it = leaf_index.find(lf);
                if (it != leaf_index.end()) { keep = KeepModel::KeepByScoreOf(leaves[it->second], rows[i].x); }
                else { keep = nodes[lf].keep != 0; }
                sum += (keep ? rows[i].kv : rows[i].thr) - std::min(rows[i].kv, rows[i].thr); ++cnt;
            }
            if (nodes_out) { *nodes_out = nodes; *leaves_out = leaves; }
            return cnt ? sum / cnt : 1e18;
        };

        // Choose partition depth by held-out hybrid regret (0 = single leaf = pure score baseline).
        int best_depth = 0; double best_rg = 1e18;
        for (int depth = 0; depth <= 3; ++depth)
        {
            const double rg = build_eval(depth, train, test, nullptr, nullptr);
            std::cerr << "    hybrid partition-depth=" << depth << " -> held-out regret=" << rg << "\n";
            if (rg < best_rg - 1e-9) { best_rg = rg; best_depth = depth; }
        }
        build_eval(best_depth, all, test, &out_nodes, &out_leaves);
        std::cerr << "  keep-model" << tag << ": hybrid partition-depth " << best_depth << " ("
                  << out_leaves.size() << " additive leaves, held-out regret " << best_rg << ").\n";
        return !out_nodes.empty() && !out_leaves.empty();
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

    // Split criterion selection (TREE form). "gini" (default) / "regret" / "both" (fit BOTH from the
    // one shared, expensive kv table: only the cheap CART fit differs, so emitting both costs ~nothing).
    const std::string split_mode = []{ const char* e = std::getenv("MTG_KEEP_SPLIT");
                                       return std::string(e && *e ? e : "gini"); }();

    // Model FORM. "tree" (default): the interpretable CART (gini/regret split). "score": the learned
    // ADDITIVE hand-score (ridge regression -> keep iff predicted win-turn <= continuation value). The
    // score form encodes the graded accumulation of soft factors a shallow greedy tree can't represent.
    const std::string form_mode = []{ const char* e = std::getenv("MTG_KEEP_FORM");
                                      return std::string(e && *e ? e : "tree"); }();

    // Build ONE model variant over the shared kv/baseline. `form` = "tree"|"score"|"hybrid"; for tree
    // `regret` selects the split. vtag prefixes the disclosure.
    auto make_model = [&](const std::string& form, bool regret, const std::string& vtag) -> KeepModel
    {
        const bool score_form  = (form == "score");
        const bool hybrid_form = (form == "hybrid");
        std::vector<KeepNode>  final_nodes;        // tree / hybrid
        KeepScore              final_score;        // score
        std::vector<KeepScore> final_leaf_scores;  // hybrid: per-leaf additive scores
        // Fit against a baseline; commits ONLY on success (a failed refit during policy iteration leaves
        // the last good model intact). dump only meaningful for tree.
        auto do_fit = [&](const double (&base)[2][M_CAP + 1], const std::string& t, bool dump) -> bool
        {
            if (hybrid_form)
            {
                std::vector<KeepNode> n; std::vector<KeepScore> ls;
                if (!fit_hybrid(base, t, n, ls)) { return false; }
                final_nodes = n; final_leaf_scores = ls; return true;
            }
            if (score_form)
            { KeepScore s = fit_score(base, t); if (s.empty()) { return false; } final_score = s; return true; }
            std::vector<KeepNode> n = fit_nodes(base, t, dump, regret);
            if (n.empty()) { return false; } final_nodes = n; return true;
        };
        // Equality of two consecutive fits (policy-iteration convergence test), per form.
        auto score_equal = [](const KeepScore& a, const KeepScore& b)
        {
            if (a.intercept != b.intercept || a.coefs != b.coefs || a.thr.size() != b.thr.size())
            { return false; }
            for (size_t i = 0; i < a.thr.size(); ++i) { if (a.thr[i] != b.thr[i]) { return false; } }
            return true;
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
        // The current fit as an UNCOMPACTED policy (indices/coefs align with the full feature vector) so
        // its Keep can be evaluated on the cached feats during policy iteration.
        auto current_pi = [&]() -> KeepModel
        {
            KeepModel pi;
            pi.key_pieces     = feat_model.key_pieces;
            pi.deck_colors    = feat_model.deck_colors;
            pi.extra_features = feat_model.extra_features;
            if (score_form) { pi.score = final_score; }
            else { pi.nodes = final_nodes; if (hybrid_form) { pi.leaf_scores = final_leaf_scores; } }
            return pi;
        };

        if (baseline_mode != "policy")
        {
            if (!do_fit(M, vtag, /*dump=*/!regret)) { return KeepModel{}; }
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
                            s2 += keep ? data[g].kv[p][m] : V[p][m + 1];
                            ++c2;
                        }
                        V[p][m] = c2 ? s2 / c2 : V[p][m + 1];
                    }
                }
            };

            // Iteration 0: bootstrap pi from the static profile keep decision (outcome-blind).
            simulate_V([&](int p, int m, int g)
                       { return ref_ai.ReferenceKeep(data[g].hand, m, (p == 1)); });
            if (!do_fit(V, vtag + " [it0]", /*dump=*/!regret && policy_iters == 0)) { return KeepModel{}; }

            // Policy iteration: pi <- fitted model, re-simulate V, refit. Converges to the fixed point.
            for (int it = 1; it <= policy_iters; ++it)
            {
                std::vector<KeepNode> prev_nodes = final_nodes;
                KeepScore             prev_score = final_score;
                const KeepModel pi = current_pi();
                simulate_V([&](int p, int m, int g) { return pi.Keep(data[g].feats[p][m]); });

                const std::string tag = vtag + " [it" + std::to_string(it) + "]";
                if (!do_fit(V, tag, /*dump=*/!regret && it == policy_iters)) { break; }
                const bool same = hybrid_form ? false   // run all iters (leaf scores rarely byte-identical)
                                : score_form  ? score_equal(final_score, prev_score)
                                              : nodes_equal(final_nodes, prev_nodes);
                if (same) { std::cerr << "  keep-model" << vtag << ": policy converged at iter " << it << ".\n"; break; }
            }
        }

        // Hybrid: keep the FULL feature basis (no compaction) so node feat indices + every leaf score's
        // coefs stay aligned with the runtime feature vector. Bigger profile, but the per-leaf coefs make
        // the surfaced-lever set leaf-specific anyway.
        if (hybrid_form)
        {
            KeepModel model;
            model.key_pieces     = feat_model.key_pieces;
            model.deck_colors    = feat_model.deck_colors;
            model.extra_features = feat_model.extra_features;   // full basis
            model.nodes          = final_nodes;
            model.leaf_scores    = final_leaf_scores;
            int splits = 0; for (const KeepNode& nd : final_nodes) { if (nd.feat >= 0) { ++splits; } }
            std::cerr << "  keep-model" << vtag << ": final HYBRID model-tree (" << splits
                      << " partition splits, " << final_leaf_scores.size() << " additive leaves). Rules:\n";
            PrintTree(model, final_nodes, 0, 2);
            return model;
        }

        // Compact: keep ONLY the constructed extra specs the final model actually USES (tree split or
        // nonzero score coef), remapping to the compacted [base ++ kept-extras] layout. Composite
        // operands reference BASE indices (stable under compaction). The rest of the candidate basis
        // went unused -- this is the "surfaced levers" set.
        std::vector<int> used_extra;
        if (score_form)
        { for (int e = BASE_FN; e < g_nf; ++e) { if (final_score.coefs[e] != 0) { used_extra.push_back(e); } } }
        else
        { for (const KeepNode& nd : final_nodes) { if (nd.feat >= BASE_FN) { used_extra.push_back(nd.feat); } } }
        std::sort(used_extra.begin(), used_extra.end());
        used_extra.erase(std::unique(used_extra.begin(), used_extra.end()), used_extra.end());

        std::vector<FeatureSpec> kept;
        std::map<int, int> remap;
        for (int oldidx : used_extra)
        {
            remap[oldidx] = BASE_FN + static_cast<int>(kept.size());
            kept.push_back(feat_model.extra_features[oldidx - BASE_FN]);
        }

        KeepModel model;
        model.key_pieces     = feat_model.key_pieces;
        model.deck_colors    = feat_model.deck_colors;
        model.extra_features = kept;

        if (score_form)
        {
            // Compacted coef vector = base coefs ++ kept-extra coefs (in remap order).
            KeepScore sc;
            sc.intercept = final_score.intercept;
            sc.thr       = final_score.thr;
            for (int j = 0; j < BASE_FN; ++j) { sc.coefs.push_back(final_score.coefs[j]); }
            for (int oldidx : used_extra) { sc.coefs.push_back(final_score.coefs[oldidx]); }
            model.score  = sc;

            std::cerr << "  keep-model" << vtag << ": final ADDITIVE score model ("
                      << kept.size() << " constructed features used). est_win = "
                      << (sc.intercept / static_cast<double>(SCORE_SCALE));
            // Disclose nonzero coefs by descending |weight| (turns added per unit feature).
            std::vector<std::pair<int, long long>> terms;
            for (int j = 0; j < static_cast<int>(sc.coefs.size()); ++j)
            { if (sc.coefs[j] != 0) { terms.emplace_back(j, sc.coefs[j]); } }
            std::sort(terms.begin(), terms.end(),
                      [](const auto& a, const auto& b) { return std::llabs(a.second) > std::llabs(b.second); });
            std::cerr << "\n  keep-model" << vtag << " weights (turns/unit):";
            for (const auto& t : terms)
            { std::cerr << "  " << (t.second > 0 ? "+" : "") << (t.second / static_cast<double>(SCORE_SCALE))
                        << "*" << FeatureNameAt(model, t.first); }
            std::cerr << "\n  keep-model" << vtag << " keep-thresholds V[mull+1] (play/draw):";
            for (int p = 0; p < static_cast<int>(sc.thr.size()); ++p)
            {
                std::cerr << (p == 1 ? "  play[" : "  draw[");
                for (size_t m = 0; m < sc.thr[p].size(); ++m)
                { std::cerr << (m ? "," : "") << (sc.thr[p][m] / static_cast<double>(SCORE_SCALE)); }
                std::cerr << "]";
            }
            std::cerr << "\n";
        }
        else
        {
            for (KeepNode& nd : final_nodes)
            { if (nd.feat >= BASE_FN) { nd.feat = remap[nd.feat]; } }
            model.nodes = final_nodes;

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
        }
        if (!kept.empty())
        {
            std::cerr << "  keep-model" << vtag << " constructed features kept:";
            for (const FeatureSpec& s : kept) { std::cerr << " [" << s.name << "]"; }
            std::cerr << "\n";
        }
        return model;
    };

    // Primary form follows MTG_KEEP_FORM ("tree" default with gini/regret split, "score", "hybrid").
    // "both" (tree primary) ALSO emits regret/score/hybrid side files from the SAME kv table -- a matched
    // 4-way A/B for one rollout cost. The kv table is computed once; every make_model re-runs only the
    // cheap fit.
    const bool score_form  = (form_mode == "score");
    const bool hybrid_form = (form_mode == "hybrid");
    const bool tree_form   = !score_form && !hybrid_form;
    const bool want_both   = tree_form && (split_mode == "both");
    KeepModel primary =
        score_form  ? make_model("score",  false, "")
      : hybrid_form ? make_model("hybrid", false, "")
      :               make_model("tree",   split_mode == "regret", want_both ? " (gini)" : "");
    if (want_both && out_alt)    { *out_alt    = make_model("tree",   true,  " (regret)"); }
    if (want_both && out_score)  { *out_score  = make_model("score",  false, " (score)"); }
    if (want_both && out_hybrid) { *out_hybrid = make_model("hybrid", false, " (hybrid)"); }

    std::cerr << "  keep-model key_pieces:";
    if (primary.key_pieces.empty()) { std::cerr << " (none)"; }
    for (const std::string& s : primary.key_pieces) { std::cerr << " [" << s << "]"; }
    std::cerr << "\n  keep-model deck_colors:";
    for (Color c : primary.deck_colors) { std::cerr << " " << ColorToChar(c); }
    std::cerr << "\n";

    return primary;
}
