#pragma once
#include "../core/Card.h"   // Color
#include <string>
#include <vector>

// Analyzer-generated, interpretable mulligan-KEEP model: a serialized decision tree over NAMED,
// integer/boolean features. Stored as DATA in the deck profile JSON and walked by a generic
// evaluator -- it is "codified" (explicit named rules you can read and patch) without being
// compiled C++, so it hot-swaps per-deck with no rebuild like the rest of the profile. Integer-only
// comparisons keep evaluation deterministic (no float-divergence risk in a GT-feeding decision).
//
// This header is intentionally LIGHT (only Card.h) because MulliganProfile.h embeds a KeepModel and
// is included widely; the featurization (which needs the card database) is declared here and defined
// in KeepModel.cpp so the heavy includes stay out of the header.

// Named features the tree may split on. The ORDER is the contract: the runtime feature vector is
// indexed by these positions, while the serialized model references features by KeepFeatureName.
enum class KeepFeature : int
{
    FinalHandSize = 0,  // cards kept after London bottoming = 7 - mulligan_count (e.g. "mull to 5")
    MulliganCount,      // mulligans taken so far (0 = opening seven)
    OnThePlay,          // 1 = on the play (skips the turn-1 draw), 0 = on the draw
    LandCount,
    NonlandCount,
    PlayableCount,      // # nonland cards castable from the hand's OWN lands
    ColorsCovered,      // # of the deck's colors with >= 1 source in hand
    CountMv1,           // # nonland spells with mana value <= 1
    CountMv2,           // # nonland spells with mana value <= 2
    KeyPieceCount,      // # of the deck's key pieces present (generalizes required_pieces)
    Count               // sentinel: number of features
};

inline const char* KeepFeatureName(KeepFeature f)
{
    switch (f)
    {
        case KeepFeature::FinalHandSize: return "final_hand_size";
        case KeepFeature::MulliganCount: return "mulligan_count";
        case KeepFeature::OnThePlay:     return "on_the_play";
        case KeepFeature::LandCount:     return "land_count";
        case KeepFeature::NonlandCount:  return "nonland_count";
        case KeepFeature::PlayableCount: return "playable_count";
        case KeepFeature::ColorsCovered: return "colors_covered";
        case KeepFeature::CountMv1:      return "count_mv1";
        case KeepFeature::CountMv2:      return "count_mv2";
        case KeepFeature::KeyPieceCount: return "key_piece_count";
        default:                         return "?";
    }
}

// Returns the feature index for a serialized name, or -1 if unknown (loader treats as malformed).
inline int KeepFeatureFromName(const std::string& s)
{
    for (int i = 0; i < static_cast<int>(KeepFeature::Count); ++i)
    {
        if (s == KeepFeatureName(static_cast<KeepFeature>(i))) { return i; }
    }
    return -1;
}

// Split comparison. Tiny + integer; "<=" is the CART default, "==" handles booleans cleanly.
enum class KeepOp : int { Le = 0, Ge = 1, Eq = 2 };
inline const char* KeepOpName(KeepOp o)
{
    return o == KeepOp::Ge ? ">=" : o == KeepOp::Eq ? "==" : "<=";
}
inline KeepOp KeepOpFromName(const std::string& s)
{
    return s == ">=" ? KeepOp::Ge : s == "==" ? KeepOp::Eq : KeepOp::Le;
}

// One tree node. Internal node: feat >= 0, test (feature <op> val) routes to yes/no child index.
// Leaf node: feat < 0, `keep` in {0,1}.
struct KeepNode
{
    int feat = -1;   // KeepFeature index; -1 => leaf
    int op   = 0;    // KeepOp
    int val  = 0;    // integer threshold
    int yes  = -1;   // child index when the test is TRUE
    int no   = -1;   // child index when the test is FALSE
    int keep = -1;   // leaf only: 1 = keep, 0 = mulligan
};

struct KeepModel
{
    std::vector<KeepNode>    nodes;        // nodes[0] = root; empty => no model (use legacy KeepHand)
    std::vector<std::string> key_pieces;   // cards counted by KeyPieceCount (analyzer-chosen)
    std::vector<Color>       deck_colors;  // colors counted by ColorsCovered (analyzer-chosen)

    bool empty() const { return nodes.empty(); }

    // Walk the tree on a feature vector indexed by KeepFeature. Returns true = keep, false = mull.
    // Defensive against a malformed/cyclic serialized tree: bounded by node count, defaults to keep
    // (the conservative choice -- never silently mulligans a hand because of a bad artifact).
    bool Keep(const std::vector<int>& feats) const
    {
        if (nodes.empty()) { return true; }
        int idx = 0;
        const int n = static_cast<int>(nodes.size());
        for (int steps = 0; steps <= n; ++steps)   // <= n: cycle guard
        {
            const KeepNode& nd = nodes[idx];
            if (nd.feat < 0) { return nd.keep != 0; }                 // leaf
            if (nd.feat >= static_cast<int>(feats.size())) { return true; }  // malformed
            const int v = feats[nd.feat];
            const bool test = (nd.op == static_cast<int>(KeepOp::Ge)) ? (v >= nd.val)
                            : (nd.op == static_cast<int>(KeepOp::Eq)) ? (v == nd.val)
                            :                                           (v <= nd.val);
            const int next = test ? nd.yes : nd.no;
            if (next < 0 || next >= n) { return true; }               // malformed
            idx = next;
        }
        return true;   // cycle guard tripped
    }
};

// Durable, human-authored per-deck constraints: a SEPARATE input file that the generator READS as a
// prior and the runtime APPLIES as a hard guard wrapping the model. Never written by the analyzer,
// so it survives every regeneration. Tri-state guard (see ApplyKeepConstraints).
struct KeepConstraints
{
    // Mulligan unless at least one of these card names is in hand (OR logic). Empty = no requirement.
    std::vector<std::string> required_pieces;

    bool empty() const { return required_pieces.empty(); }
};

enum class KeepGuard { Undecided, ForceKeep, ForceMulligan };

// --- Shared featurization + constraint logic (defined in KeepModel.cpp) ------------------------
// Single source of truth used by BOTH the runtime keep decision (AIEngine::KeepHand) and the
// analyzer's generator, so the features a model is trained on match the features it's evaluated on
// exactly (lockstep). model supplies the key-piece names and deck colors the features reference.
std::vector<int> ComputeKeepFeatures(const std::vector<Card>& hand, int mulligan_count,
                                     bool on_the_play, const KeepModel& model);

// Apply the human constraints as a hard guard before the model is consulted.
KeepGuard ApplyKeepConstraints(const std::vector<Card>& hand, const KeepConstraints& c);
