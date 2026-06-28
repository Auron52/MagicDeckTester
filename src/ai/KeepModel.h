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
// APPEND-ONLY: never reorder or remove an entry -- existing committed profiles reference features by
// the indices 0..N here, so a reorder silently mis-evaluates every shipped tree. New features go at
// the end, just before Count (this keeps old profiles byte-identical and lets the CART pick up the
// new axes on the next regeneration).
enum class KeepFeature : int
{
    FinalHandSize = 0,  // cards kept after London bottoming = 7 - mulligan_count (e.g. "mull to 5")
    MulliganCount,      // mulligans taken so far (0 = opening seven)
    OnThePlay,          // 1 = on the play (skips the turn-1 draw), 0 = on the draw
    LandCount,
    NonlandCount,
    PlayableCount,      // # nonland cards castable from the hand's OWN lands (legacy fungible-wild model)
    ColorsCovered,      // # of the deck's colors with >= 1 source in hand
    CountMv1,           // # nonland spells with mana value <= 1
    CountMv2,           // # nonland spells with mana value <= 2
    KeyPieceCount,      // # of the deck's key pieces present (generalizes required_pieces)
    // --- richer per-colour / curve basis (so the tree can express colour screw + a functional curve,
    //     not just an aggregate count). All integer + deterministic; unused colours stay constant 0
    //     for a deck and the CART simply never splits on them (zero Gini gain). ---
    SourceW,            // # mana sources in hand that can produce White (a dual counts for each colour)
    SourceU,            // # sources producing Blue
    SourceB,            // # sources producing Black
    SourceR,            // # sources producing Red
    SourceG,            // # sources producing Green
    UncoveredColors,    // # of colours the hand's spells DEMAND but have ZERO source for (colour screw)
    MaxPipDeficit,      // worst single-card coloured-pip shortfall over colours (severity of the screw)
    PlayableStrict,     // # nonland castable under a CORRECT per-colour allocation (no fungible wild pool)
    CurveDepth,         // # of turns 1..4 with a sequenceable on-curve play given the hand's land ramp
    CountMv3,           // # nonland spells with mana value <= 3
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
        case KeepFeature::SourceW:         return "source_w";
        case KeepFeature::SourceU:         return "source_u";
        case KeepFeature::SourceB:         return "source_b";
        case KeepFeature::SourceR:         return "source_r";
        case KeepFeature::SourceG:         return "source_g";
        case KeepFeature::UncoveredColors: return "uncovered_colors";
        case KeepFeature::MaxPipDeficit:   return "max_pip_deficit";
        case KeepFeature::PlayableStrict:  return "playable_strict";
        case KeepFeature::CurveDepth:      return "curve_depth";
        case KeepFeature::CountMv3:        return "count_mv3";
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

// A DATA-DEFINED feature, computed by a generic deterministic evaluator from a hand. This is what
// lets the model "train on the inputs" rather than a fixed lever list: the analyzer CONSTRUCTS a rich
// candidate set (per-colour, per-CMC, tribal density, arithmetic composites of other features) and the
// fitted tree keeps only the specs it actually splits on, serialised here so the runtime recomputes
// them in lockstep. All integer (no float-determinism risk). The compiled base KeepFeature vector
// (indices 0..KeepFeature::Count-1) is ALWAYS present; extra specs are appended after it, so a tree
// `feat` index >= Count selects extra_features[feat - Count]. Composite operands (a,b) are indices
// into that FULL vector and MUST reference earlier positions (base, or an earlier extra) so the
// vector is computable left-to-right in one pass.
enum class FeatureKind : int
{
    PerColorSource = 0, // p = colour ordinal 0..4: # sources in hand that can make that colour
    PerColorDemand,     // p = colour: heaviest single-card coloured-pip requirement of that colour
    PerColorUncovered,  // p = colour: 1 if that colour is demanded but has no source, else 0
    PerColorSurplus,    // p = colour: src - heaviest demand (signed; negative = colour screw severity)
    NonlandAtMv,        // p = mana value (>=6 bucketed): # nonland spells of exactly that MV
    CastableAtMvLe,     // p = mana value: # strictly-castable nonland spells with MV <= p
    SubtypeDensity,     // s = subtype name: # hand cards carrying that subtype (tribal payoff/density)
    Diff,               // a,b = feature indices into the full vector -> val[a] - val[b]
    Min,                // a,b = indices -> min(val[a], val[b])
};

inline const char* FeatureKindName(FeatureKind k)
{
    switch (k)
    {
        case FeatureKind::PerColorSource:    return "per_color_source";
        case FeatureKind::PerColorDemand:    return "per_color_demand";
        case FeatureKind::PerColorUncovered: return "per_color_uncovered";
        case FeatureKind::PerColorSurplus:   return "per_color_surplus";
        case FeatureKind::NonlandAtMv:       return "nonland_at_mv";
        case FeatureKind::CastableAtMvLe:    return "castable_at_mv_le";
        case FeatureKind::SubtypeDensity:    return "subtype_density";
        case FeatureKind::Diff:              return "diff";
        case FeatureKind::Min:               return "min";
    }
    return "?";
}
inline int FeatureKindFromName(const std::string& s)
{
    for (int i = 0; i <= static_cast<int>(FeatureKind::Min); ++i)
    { if (s == FeatureKindName(static_cast<FeatureKind>(i))) { return i; } }
    return -1;
}

struct FeatureSpec
{
    int         kind = 0;       // FeatureKind
    int         p    = 0;       // colour ordinal / mana value parameter
    int         a    = -1;      // composite operand: feature index into the full vector
    int         b    = -1;      // composite operand: feature index into the full vector
    std::string s;              // subtype name (SubtypeDensity)
    std::string name;           // serialized feature name; also the tree-node split name for this spec
};

struct KeepModel
{
    std::vector<KeepNode>    nodes;        // nodes[0] = root; empty => no model (use legacy KeepHand)
    std::vector<std::string> key_pieces;   // cards counted by KeyPieceCount (analyzer-chosen)
    std::vector<Color>       deck_colors;  // colors counted by ColorsCovered (analyzer-chosen)
    std::vector<FeatureSpec> extra_features; // data-defined features appended after the base vector

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

// Resolve a tree-node feat INDEX to its serialized name: a base KeepFeature name for indices
// 0..Count-1, else the data-defined extra spec's name. Used by the profile (de)serializer so a tree
// can split on either a built-in or a constructed feature uniformly.
inline std::string FeatureNameAt(const KeepModel& m, int idx)
{
    const int base = static_cast<int>(KeepFeature::Count);
    if (idx >= 0 && idx < base) { return KeepFeatureName(static_cast<KeepFeature>(idx)); }
    const int e = idx - base;
    if (e >= 0 && e < static_cast<int>(m.extra_features.size())) { return m.extra_features[e].name; }
    return "?";
}
// Inverse: a serialized feat name -> its index in the full vector (base names first, then extra
// spec names). -1 if unknown. extra_features must be loaded before the nodes that reference them.
inline int FeatureIndexFromName(const KeepModel& m, const std::string& s)
{
    const int i = KeepFeatureFromName(s);
    if (i >= 0) { return i; }
    const int base = static_cast<int>(KeepFeature::Count);
    for (int e = 0; e < static_cast<int>(m.extra_features.size()); ++e)
    { if (m.extra_features[e].name == s) { return base + e; } }
    return -1;
}

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
