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
#include <set>
#include <thread>

namespace
{
constexpr int FN = static_cast<int>(KeepFeature::Count);  // # features

// Deepest mulligan we evaluate. A keep value is computed for m = 0..KV_MAX; labels are
// emitted for m = 0..LABEL_MAX (those the runtime actually consults -- final_hand_size >
// stop_at). m = KV_MAX is the forced-keep baseline that anchors the backward induction.
constexpr int KV_MAX    = 3;   // hands of effective size 7..4
constexpr int LABEL_MAX = 2;   // decision depths 0,1,2 (stop_at >= 4 keeps eff size 4 outright)

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
    int  kv[2][KV_MAX + 1];          // keep value (win turn), [on_play][mulligan_count]
    int  feats[2][LABEL_MAX + 1][FN]; // feature vector, [on_play][mulligan_count][feature]
};

// One labeled training row.
struct Row
{
    std::array<int, FN> x;   // feature vector
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

    std::vector<std::pair<std::string, double>> scored;
    for (const auto& kv : card_scores)
    {
        if (kv.second.empty()) { continue; }
        const CardDefinition* def = CardDatabase::Instance().Lookup(kv.first);
        if (def && def->card.IsLand()) { continue; }   // lands are not "key pieces"
        if (kv.second[0] >= 0.30) { scored.emplace_back(kv.first, kv.second[0]); }
    }
    std::sort(scored.begin(), scored.end(),
        [](const auto& a, const auto& b)
        {
            if (a.second != b.second) { return a.second > b.second; }
            return a.first < b.first;
        });

    std::vector<std::string> out;
    for (const auto& s : scored)
    {
        out.push_back(s.first);
        if (static_cast<int>(out.size()) >= 4) { break; }
    }
    return out;
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
    TreeBuilder(const std::vector<Row>& rows, int max_depth, int min_leaf)
        : m_rows(rows), m_max_depth(max_depth), m_min_leaf(min_leaf) {}

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
    std::vector<KeepNode>   m_nodes;

    int BuildNode(const std::vector<int>& idx, int depth)
    {
        const int n = static_cast<int>(idx.size());
        int keep = 0;
        for (int i : idx) { keep += m_rows[i].y; }

        const int node_idx = static_cast<int>(m_nodes.size());
        m_nodes.push_back(KeepNode{});   // placeholder; patched below

        const double parent_gini = Gini(keep, n);
        bool make_leaf = depth >= m_max_depth || n <= m_min_leaf || keep == 0 || keep == n;

        int    best_feat = -1, best_val = 0;
        double best_score = parent_gini;   // require a strict improvement to split

        if (!make_leaf)
        {
            for (int f = 0; f < FN; ++f)
            {
                // (value, label) pairs for this feature, sorted by value.
                std::vector<std::pair<int, int>> vv;
                vv.reserve(n);
                for (int i : idx) { vv.emplace_back(m_rows[i].x[f], m_rows[i].y); }
                std::sort(vv.begin(), vv.end(),
                          [](const auto& a, const auto& b) { return a.first < b.first; });

                // Sweep thresholds at each distinct value boundary; "<= v" goes left.
                int yes_keep = 0, yes_n = 0;
                for (int k = 0; k < n; ++k)
                {
                    yes_keep += vv[k].second;
                    ++yes_n;
                    // evaluate a split only at the last index of a run of equal values
                    if (k + 1 < n && vv[k + 1].first == vv[k].first) { continue; }
                    if (yes_n < m_min_leaf || n - yes_n < m_min_leaf) { continue; }
                    if (k + 1 >= n) { continue; }   // no right child (split at max value)

                    const int no_keep = keep - yes_keep;
                    const int no_n    = n - yes_n;
                    const double score =
                        (yes_n * Gini(yes_keep, yes_n) + no_n * Gini(no_keep, no_n)) / n;
                    // strict-< with feature/value iteration order makes ties deterministic
                    if (score < best_score - 1e-12)
                    {
                        best_score = score;
                        best_feat  = f;
                        best_val   = vv[k].first;
                    }
                }
            }
        }

        if (best_feat < 0)   // leaf: majority label, ties -> keep (conservative)
        {
            KeepNode& nd = m_nodes[node_idx];
            nd.feat = -1;
            nd.keep = (keep * 2 >= n) ? 1 : 0;
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

// Pretty-print the tree as nested if/else rules to stderr (the Stage-6a disclosure).
void PrintTree(const std::vector<KeepNode>& nodes, int idx, int indent)
{
    std::string pad(indent * 2, ' ');
    const KeepNode& nd = nodes[idx];
    if (nd.feat < 0)
    {
        std::cerr << pad << (nd.keep ? "KEEP" : "MULLIGAN") << "\n";
        return;
    }
    std::cerr << pad << "if " << KeepFeatureName(static_cast<KeepFeature>(nd.feat))
              << " " << KeepOpName(static_cast<KeepOp>(nd.op)) << " " << nd.val << ":\n";
    PrintTree(nodes, nd.yes, indent + 1);
    std::cerr << pad << "else:\n";
    PrintTree(nodes, nd.no, indent + 1);
}
} // namespace

// ==========================================================================
KeepModel BuildKeepModel(const Decklist& deck,
                         const MulliganProfile& base_profile,
                         const std::map<std::string, std::vector<double>>& card_scores,
                         const KeepModelTrainConfig& cfg)
{
    std::cerr << "Building mulligan keep model (" << cfg.games
              << " hands, depth=" << cfg.depth << ")...\n";

    // The feature context the model references. The SAME object is used to featurize during
    // training and is emitted in the final model, so runtime features match training exactly.
    KeepModel feat_model;
    feat_model.deck_colors = DeckColors(deck);
    feat_model.key_pieces  = KeyPieces(base_profile, card_scores);

    // The profile the rollouts use: identical to the analyzer's chosen profile but with NO
    // keep model (the keep model is what we are generating; the rollout decides nothing here).
    MulliganProfile rollout_profile = base_profile;
    rollout_profile.keep_model = KeepModel{};

    const int      G        = std::max(0, cfg.games);
    const uint64_t seed_off = cfg.seed + 7'000'000ULL;   // far from the other analyzer phases
    const bool     second_main = GoldFishRunner::DeckUsesSecondMain(deck);

    // ---- 1. sample hands + clairvoyant keep values (parallel over games) -------------
    std::vector<GameData> data(G);
    std::atomic<int> done{0};
    const int prog_step = std::max(1, G / 10);
    ParallelFor(G, [&](int g)
    {
        AIEngine ai(rollout_profile, cfg.depth, cfg.budget_ms);
        ai.SetSearchPostCombat(second_main);

        GameState s = GoldFishRunner::SetupGame(deck, seed_off + static_cast<uint64_t>(g));
        s.m_required_pieces = &rollout_profile.required_pieces;
        s.vial_target_mv    = rollout_profile.vial_target_mv;
        s.ActivePlayer().library.DrawN(7, s.ActivePlayer().hand);
        if (static_cast<int>(s.ActivePlayer().hand.size()) != 7) { return; }
        const std::vector<Card> hand = s.ActivePlayer().hand;

        for (int p = 0; p < 2; ++p)
        {
            GameState base_p   = s;
            base_p.on_the_play = (p == 1);
            for (int m = 0; m <= KV_MAX; ++m)
            {
                data[g].kv[p][m] = ai.RolloutKeepWinTurn(base_p, m, cfg.max_turns);
            }
            for (int m = 0; m <= LABEL_MAX; ++m)
            {
                const std::vector<int> f =
                    ComputeKeepFeatures(hand, m, (p == 1), feat_model);
                for (int k = 0; k < FN; ++k) { data[g].feats[p][m][k] = f[k]; }
            }
        }
        data[g].valid = true;

        const int d = ++done;
        if (d % prog_step == 0 || d == G)
        {
            std::cerr << "    keep-model: " << d << "/" << G
                      << " hands (" << (100 * d / G) << "%)\n" << std::flush;
        }
    });

    // ---- 2. backward induction of the mulligan value, then label ---------------------
    // M[p][m] = expected win turn of the optimal keep/mulligan policy starting from a fresh
    // hand at depth m. M[p][KV_MAX] is the forced-keep baseline (mean keep value at the
    // deepest depth); shallower depths take the best of keeping this hand or mulliganing on.
    double M[2][KV_MAX + 1] = {{0}};
    for (int p = 0; p < 2; ++p)
    {
        double sum = 0.0; int cnt = 0;
        for (int g = 0; g < G; ++g)
        {
            if (!data[g].valid) { continue; }
            sum += data[g].kv[p][KV_MAX]; ++cnt;
        }
        M[p][KV_MAX] = cnt ? sum / cnt : static_cast<double>(cfg.max_turns + 1);
        for (int m = KV_MAX - 1; m >= 0; --m)
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

    std::vector<Row> rows;
    for (int g = 0; g < G; ++g)
    {
        if (!data[g].valid) { continue; }
        for (int p = 0; p < 2; ++p)
        for (int m = 0; m <= LABEL_MAX; ++m)
        {
            const int eff = 7 - m;
            if (eff <= base_profile.stop_at) { continue; }   // runtime force-keeps these
            Row r;
            for (int k = 0; k < FN; ++k) { r.x[k] = data[g].feats[p][m][k]; }
            r.kv   = data[g].kv[p][m];
            r.thr  = M[p][m + 1];                            // value of mulliganing once more
            r.y    = (r.kv <= r.thr) ? 1 : 0;
            r.game = g;
            rows.push_back(r);
        }
    }

    if (static_cast<int>(rows.size()) < 200)
    {
        std::cerr << "  keep-model: too few labeled hands (" << rows.size()
                  << ") -- keeping the legacy keep path.\n";
        return KeepModel{};
    }

    int keep_rows = 0;
    for (const Row& r : rows) { keep_rows += r.y; }
    std::cerr << "  keep-model: " << rows.size() << " labeled hands ("
              << keep_rows << " keep / " << (rows.size() - keep_rows) << " mull).\n";

    // ---- 3. fit, then pick the shallowest tree within the accuracy bar ---------------
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
    std::vector<KeepNode> deep = TreeBuilder(rows, 9, std::max(4, N / 1000)).Build(train);
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

    std::cerr << "  keep-model accuracy bar (held-out mean regret, turns):\n"
              << "    always-keep=" << always_keep << "  always-mull=" << always_mull
              << "  deep-tree=" << deep_regret << "\n";

    // Pick the SHALLOWEST depth whose held-out regret is within MARGIN of the deep baseline
    // (the readable form may not pay a meaningful win-turn cost). If none qualifies, fall back
    // to the depth with the LOWEST regret -- accuracy over brevity when they genuinely trade off.
    constexpr double MARGIN = 0.02;   // turns: max regret a readable tree may pay over deep
    int    chosen_depth  = -1;
    double chosen_regret = 1e18;
    int    best_depth    = 3;
    double best_regret   = 1e18;
    for (int d = 3; d <= 6; ++d)
    {
        std::vector<KeepNode> t = TreeBuilder(rows, d, min_leaf).Build(train);
        const double rg = MeanRegret(t, rows, test);
        std::cerr << "    depth=" << d << " -> regret=" << rg
                  << " (" << t.size() << " nodes)\n";
        if (rg < best_regret) { best_regret = rg; best_depth = d; }
        if (chosen_depth < 0 && rg <= deep_regret + MARGIN) { chosen_depth = d; chosen_regret = rg; }
    }
    if (chosen_depth < 0) { chosen_depth = best_depth; chosen_regret = best_regret; }

    // Final model: refit the chosen depth on ALL rows (report held-out, ship full-data fit).
    std::vector<KeepNode> final_nodes =
        TreeBuilder(rows, chosen_depth, min_leaf).Build([&]{
            std::vector<int> all(N);
            for (int i = 0; i < N; ++i) { all[i] = i; }
            return all;
        }());

    KeepModel model;
    model.nodes       = final_nodes;
    model.key_pieces  = feat_model.key_pieces;
    model.deck_colors = feat_model.deck_colors;

    std::cerr << "  keep-model: chose depth " << chosen_depth
              << " (held-out regret " << chosen_regret << " turns, "
              << final_nodes.size() << " nodes). Rules:\n";
    PrintTree(final_nodes, 0, 2);

    std::cerr << "  keep-model key_pieces:";
    if (model.key_pieces.empty()) { std::cerr << " (none)"; }
    for (const std::string& s : model.key_pieces) { std::cerr << " [" << s << "]"; }
    std::cerr << "\n  keep-model deck_colors:";
    for (Color c : model.deck_colors) { std::cerr << " " << ColorToChar(c); }
    std::cerr << "\n";

    return model;
}
