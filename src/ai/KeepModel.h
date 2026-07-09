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
    // --- on-time castability: can each spell's EXACT colour cost be produced BY its on-curve turn,
    //     accounting for lands that enter TAPPED (a tap-land costs a turn of tempo) + correct colour
    //     allocation. This is the "right colours at the right time" axis the coarse curve_depth misses. ---
    OnTimeCount,        // # nonland spells castable on their curve turn (mv) -- colour + tapped aware
    WorstLateness,      // worst # of turns any hand spell is late vs its curve turn (colour screw = capped)
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
        case KeepFeature::OnTimeCount:     return "on_time_count";
        case KeepFeature::WorstLateness:   return "worst_lateness";
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
    int leaf_score = -1;  // leaf only: >=0 => index into KeepModel::leaf_scores (HYBRID model-tree:
                          // this leaf decides by an additive score, not the constant `keep` bit). -1 =>
                          // constant-leaf (the pure tree). Append-only/back-compat: old trees load -1.
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
    Product,            // a,b = indices -> val[a] * val[b]  (lets a LINEAR score express a conjunction:
                        // e.g. key_piece_count x land_count is high only when BOTH hold -> "keep iff
                        // you have the engine piece AND the lands", the AND a pure additive sum can't do)
    CardCount,          // s = card name: # copies of that exact card in hand. Gives the model per-card
                        // IDENTITY -- and thus REDUNDANCY (a 2nd Aether Vial shows as card_count=2) --
                        // which the aggregate features (key_piece_count, subtype density) cannot see.
                        // This is the input needed to reject flooded/redundant hands the additive static
                        // keep wrongly keeps (its learned redundancy penalty is clamped away at runtime).
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
        case FeatureKind::Product:           return "product";
        case FeatureKind::CardCount:         return "card_count";
    }
    return "?";
}
inline int FeatureKindFromName(const std::string& s)
{
    for (int i = 0; i <= static_cast<int>(FeatureKind::CardCount); ++i)
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

// Additive hand-score keep model (an ALTERNATIVE form to the decision tree). Instead of a sequence of
// hard splits, it predicts the hand's expected (blind) win-turn as a LINEAR sum over the feature
// vector -- est_win = intercept + Sum_f coef_f * feat_f -- and KEEPS iff that prediction is no worse
// than the value of mulliganing on (the policy-simulated continuation value V[mull+1]). This matches
// the real mulligan decision shape the static profile already uses (a graded accumulation of soft
// penalties vs a threshold), so several individually-acceptable-but-non-ideal factors can SUM past the
// keep line even when no single hard gate trips -- the conjunctive judgment a shallow greedy tree
// can't represent. Integer-only (the fit's doubles are quantised to SCORE_SCALE fixed-point at
// generation time, exactly as card_scores are), so the runtime compare is exact and deterministic.
// All coefs/thresholds share the same fixed-point scale, so it cancels in the `score <= thr` compare;
// the scale only governs rounding precision and need not be stored.
struct KeepScore
{
    std::vector<long long>               coefs;      // per full-vector feature, fixed-point units
    long long                            intercept = 0;
    // continuation thresholds: thr[on_the_play][mulligan_count] = V[mull+1] in the same fixed-point
    // units. A mulligan_count beyond the trained range maps to forced-keep (the descent's anchor).
    std::vector<std::vector<long long>>  thr;

    bool empty() const { return coefs.empty(); }
};

struct KeepModel
{
    std::vector<KeepNode>    nodes;        // nodes[0] = root; empty => no model (use legacy KeepHand)
    std::vector<std::string> key_pieces;   // cards counted by KeyPieceCount (analyzer-chosen)
    std::vector<Color>       deck_colors;  // colors counted by ColorsCovered (analyzer-chosen)
    std::vector<FeatureSpec> extra_features; // data-defined features appended after the base vector
    KeepScore                score;        // additive-score form (takes precedence over `nodes` if set)
    std::vector<KeepScore>   leaf_scores;  // HYBRID model-tree: per-leaf additive scores (a leaf's
                                           // KeepNode::leaf_score indexes here). Empty => pure tree.

    bool empty() const { return nodes.empty() && score.empty(); }

    // Decide keep/mulligan from a feature vector indexed by KeepFeature. The additive-score form takes
    // precedence when present; otherwise walk the decision tree. Either way returns true = keep.
    // Defensive against a malformed/cyclic artifact: bounded, defaults to keep (never silently
    // mulligans a hand because of a bad model).
    bool Keep(const std::vector<int>& feats) const
    {
        if (!score.empty()) { return KeepByScoreOf(score, feats); }
        if (nodes.empty()) { return true; }
        int idx = 0;
        const int n = static_cast<int>(nodes.size());
        for (int steps = 0; steps <= n; ++steps)   // <= n: cycle guard
        {
            const KeepNode& nd = nodes[idx];
            if (nd.feat < 0)                                          // leaf
            {
                if (nd.leaf_score >= 0 && nd.leaf_score < static_cast<int>(leaf_scores.size()))
                { return KeepByScoreOf(leaf_scores[nd.leaf_score], feats); }   // hybrid: additive leaf
                return nd.keep != 0;                                  // constant leaf
            }
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

    // Additive-score decision: est_win = intercept + Sum coef*feat; KEEP iff est_win <= continuation
    // threshold for this hand's (on_the_play, mulligan_count). Both sides are the same fixed-point
    // scale, so the compare is exact. mulligan_count / on_the_play are read straight from the feature
    // vector (their fixed base indices), so the call site stays the form-agnostic Keep(feats).
    static bool KeepByScoreOf(const KeepScore& sc, const std::vector<int>& feats)
    {
        const int nf = static_cast<int>(feats.size());
        long long est = sc.intercept;
        const int n = std::min(nf, static_cast<int>(sc.coefs.size()));
        for (int i = 0; i < n; ++i) { est += sc.coefs[i] * static_cast<long long>(feats[i]); }

        const int play_i = static_cast<int>(KeepFeature::OnThePlay);
        const int mull_i = static_cast<int>(KeepFeature::MulliganCount);
        int play = (play_i < nf && feats[play_i] == 1) ? 1 : 0;
        int mull = (mull_i < nf) ? feats[mull_i] : 0;
        if (mull < 0) { mull = 0; }

        if (sc.thr.empty()) { return true; }
        const std::vector<long long>& row = sc.thr[play < static_cast<int>(sc.thr.size()) ? play : 0];
        if (mull >= static_cast<int>(row.size())) { return true; }   // beyond trained depth = forced keep
        return est <= row[mull];
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

// ================================================================================================
// Mid-game PLAY evaluator (learned d0 replacement) -- see docs/design/learned-d0-policy.md.
// A per-deck, analyzer-trained evaluator that RANKS candidate turn-plans by predicted expected
// win-turn, distilled from the deep search. It replaces the hand-tuned EvalCard judgment ONLY for
// ranking NON-lethal plans inside TurnSolver::Solve (the d0 decision AND every rollout leaf); the
// exact lethal check stays. Mid-game play only -- mulligan/bottoming keep their own model.
// Integer/fixed-point throughout (like KeepScore) so the plan argmax is byte-identical across
// platforms -- the digest determinism the regression harness relies on.
// ================================================================================================

struct GameState;   // forward decl: the featurizer reads it by const-ref (defined in core/GameState.h)

// Named mid-game features. Same APPEND-ONLY contract as KeepFeature: the runtime vector is indexed
// by these positions and a serialized model references them by MidGameFeatureName, so a reorder
// silently mis-evaluates every shipped evaluator. New features go at the end, just before Count.
enum class MidGameFeature : int
{
    OurLife = 0,
    OppLife,
    Turn,                 // turn_number (game clock)
    OnThePlay,            // 1 = we are on the play
    HandSize,
    LibrarySize,          // COUNT only (never the ORDER -- the non-clairvoyance contract)
    GraveyardSize,
    ExileSize,
    OurCreatures,         // creatures we control
    OurTotalPower,        // summed EffectivePower of our creatures
    OurReadyAttackers,    // our creatures that could attack now (untapped, not summoning-sick)
    OurLands,
    UntappedManaSources,  // untapped lands + usable dorks + rocks we control
    SourceW, SourceU, SourceB, SourceR, SourceG,  // untapped sources by colour we control
    OppCreatures,
    OppTotalPower,
    // --- the candidate plan being scored (its board effect, integer summary) ---
    PlanNumSpells,
    PlanCreaturesCast,
    PlanDirectDamage,     // burn to the opponent's face this plan
    PlanTotalMv,          // mana committed by the plan's casts
    PlanPlaysLand,        // 1 if the plan plays a land
    // --- appended (v2): richer, still NON-CLAIRVOYANT plan/board discriminators. Within one
    //     decision the pre-plan board features above are constant across candidates, so only the
    //     plan_* terms distinguish plans; v1's were coarse counts (num_spells/total_mv) -> the model
    //     could only "cast the most". These let it see WHICH plan and the board it leaves behind
    //     (own hand + card identity are public to us; we never read library order). learned-d0-policy.md
    PlanCardsDrawn,        // fixed card-draw the plan's casts add (Sum params.draw)
    PlanNoncreatureSpells, // noncreature casts (instants/sorceries) = num_spells - creatures_cast
    PlanMaxCastMv,         // largest single cast's MV (a big spell vs several small ones)
    PlanDrawEngine,        // 1 if a cast is a variable draw/dig engine (Treasure Hunt et al.)
    LandsInHand,           // lands in our hand (Land's Edge ammo / Treasure Hunt fuel; own-hand = public)
    ManaLeftAfter,         // untapped sources left after paying the plan (max(0, sources - total_mv))
    TapsOut,               // 1 if the plan commits all our mana (total_mv >= untapped sources)
    // --- appended (v3): the aggro axis. The exact lethal check handles kills, but NON-lethal face
    //     burn was invisible (the summary punts direct_damage->0 for X/target reasons), so the ranker
    //     undervalued racing and won ~0.25t slower on burn. This is the FIXED (non-X) burn a plan's
    //     casts deal -- known from the card, non-clairvoyant. learned-d0-policy.md
    PlanFaceDamage,        // Sum of FIXED direct damage the plan's casts deal (Lightning Bolt=3, ...)
    // --- appended (v4): the hand-tuned baseline's own verdict on this plan. The ranker was rebuilding
    //     "casting is good" from coarse plan_* proxies and underfit tuned-provider sequencing
    //     (burn ~0.2t slow, antilife combo). Expose Sum EvalCard(def,state) over the plan's casts --
    //     the baseline EvalCard ranking key -- so the model learns to AUGMENT the tuned heuristic
    //     (w=1 recovers baseline + a learned correction) instead of reconstructing it. Computed by the
    //     shared PlanBaselineEval helper at BOTH the seam and the label dump => lockstep, non-clairvoyant
    //     (EvalCard reads only the public board). 0 for the leaf value model (empty plan). learned-d0-policy.md
    PlanBaselineEval,
    // --- appended (v6): DISTRIBUTIONAL / rollout-under-uncertainty features. The value model's LABEL is
    //     already the de-clairvoyed (reshuffle-averaged) win turn, but its INPUTS were board-only -- it read
    //     library SIZE but nothing about the COMPOSITION, so it couldn't tell "6 burn left in 18 cards" from
    //     "0 burn left" and couldn't amortise the rollout. The remaining-library MULTISET is public
    //     (own decklist minus every visible zone); only the ORDER is hidden. These count the library
    //     ORDER-INVARIANTLY (sum over the whole multiset, never position i) => non-clairvoyant. This is the
    //     "quick mental simulation under uncertainty" a human does ("8 burn in 20 => ~40% to draw one").
    //     learned-d0-policy.md (rollout-under-uncertainty features).
    LibLands,             // lands remaining in library
    LibCreatures,         // creatures remaining in library
    LibDamageSources,     // remaining cards that deal damage (params.damage / landfall_damage > 0)
    LibDrawEngines,       // remaining card-advantage/dig engines (draw>0 / stages / cascade / DrawUntilNonland)
    LibLandDensityPct,    // 100 * lands / library_size  = P(next draw is a land); 0 if empty
    // --- appended (v7): HAND composition. Same idea as v6 but for the most IMMEDIATE driver of a shallow
    //     decision -- what we can act on THIS turn. Own hand is fully public => non-clairvoyant. Extends the
    //     rollout-under-uncertainty inputs to the board-driven decks (aggro win = threats in hand, not library).
    HandCreatures,        // creatures in our hand
    HandDamageSources,    // damage/removal spells in our hand (params.damage / landfall_damage / death_trigger)
    HandDrawEngines,      // card-advantage/dig spells in our hand
    HandCastableNow,      // nonland hand cards with MV <= untapped mana sources (playable this turn)
    Count                 // sentinel: number of features
};

inline const char* MidGameFeatureName(MidGameFeature f)
{
    switch (f)
    {
        case MidGameFeature::OurLife:             return "our_life";
        case MidGameFeature::OppLife:             return "opp_life";
        case MidGameFeature::Turn:                return "turn";
        case MidGameFeature::OnThePlay:           return "on_the_play";
        case MidGameFeature::HandSize:            return "hand_size";
        case MidGameFeature::LibrarySize:         return "library_size";
        case MidGameFeature::GraveyardSize:       return "graveyard_size";
        case MidGameFeature::ExileSize:           return "exile_size";
        case MidGameFeature::OurCreatures:        return "our_creatures";
        case MidGameFeature::OurTotalPower:       return "our_total_power";
        case MidGameFeature::OurReadyAttackers:   return "our_ready_attackers";
        case MidGameFeature::OurLands:            return "our_lands";
        case MidGameFeature::UntappedManaSources: return "untapped_mana_sources";
        case MidGameFeature::SourceW:             return "src_w";
        case MidGameFeature::SourceU:             return "src_u";
        case MidGameFeature::SourceB:             return "src_b";
        case MidGameFeature::SourceR:             return "src_r";
        case MidGameFeature::SourceG:             return "src_g";
        case MidGameFeature::OppCreatures:        return "opp_creatures";
        case MidGameFeature::OppTotalPower:       return "opp_total_power";
        case MidGameFeature::PlanNumSpells:       return "plan_num_spells";
        case MidGameFeature::PlanCreaturesCast:   return "plan_creatures_cast";
        case MidGameFeature::PlanDirectDamage:    return "plan_direct_damage";
        case MidGameFeature::PlanTotalMv:         return "plan_total_mv";
        case MidGameFeature::PlanPlaysLand:       return "plan_plays_land";
        case MidGameFeature::PlanCardsDrawn:      return "plan_cards_drawn";
        case MidGameFeature::PlanNoncreatureSpells: return "plan_noncreature_spells";
        case MidGameFeature::PlanMaxCastMv:       return "plan_max_cast_mv";
        case MidGameFeature::PlanDrawEngine:      return "plan_draw_engine";
        case MidGameFeature::LandsInHand:         return "lands_in_hand";
        case MidGameFeature::ManaLeftAfter:       return "mana_left_after";
        case MidGameFeature::TapsOut:             return "taps_out";
        case MidGameFeature::PlanFaceDamage:      return "plan_face_damage";
        case MidGameFeature::PlanBaselineEval:    return "plan_baseline_eval";
        case MidGameFeature::LibLands:            return "lib_lands";
        case MidGameFeature::LibCreatures:        return "lib_creatures";
        case MidGameFeature::LibDamageSources:    return "lib_damage_sources";
        case MidGameFeature::LibDrawEngines:      return "lib_draw_engines";
        case MidGameFeature::LibLandDensityPct:   return "lib_land_density_pct";
        case MidGameFeature::HandCreatures:       return "hand_creatures";
        case MidGameFeature::HandDamageSources:   return "hand_damage_sources";
        case MidGameFeature::HandDrawEngines:     return "hand_draw_engines";
        case MidGameFeature::HandCastableNow:     return "hand_castable_now";
        default:                                  return "?";
    }
}

inline int MidGameFeatureFromName(const std::string& s)
{
    for (int i = 0; i < static_cast<int>(MidGameFeature::Count); ++i)
    { if (s == MidGameFeatureName(static_cast<MidGameFeature>(i))) { return i; } }
    return -1;
}

// Integer summary of a candidate turn-plan's board effect. Decoupled from TurnSolver::Action so the
// featurizer stays layer-light; the solver builds this from the plan's chosen actions.
struct MidGamePlanSummary
{
    int num_spells     = 0;  // spells cast this plan (hand + graveyard/retrace)
    int creatures_cast = 0;  // of those, creatures
    int direct_damage  = 0;  // burn to the opponent's face this plan
    int total_mv       = 0;  // summed mana value of the casts (mana committed)
    int plays_land     = 0;  // 1 if the plan plays a land this turn
    int cards_drawn    = 0;  // fixed card-draw the casts add (Sum params.draw; variable draw excluded)
    int max_cast_mv    = 0;  // largest single cast's mana value
    int draw_engine    = 0;  // 1 if a cast is a variable draw/dig engine (draw_until_nonland etc.)
    int face_damage    = 0;  // fixed direct damage the casts deal (Sum params.damage; X-spells excluded)
    int baseline_eval  = 0;  // Sum EvalCard(def,state) over the casts (the hand-tuned baseline's plan
                             // value); set by PlanBaselineEval at the seam + dump. 0 for the null/leaf plan.
};

// One node of a fixed-point regression tree. Internal nodes split on an INTEGER feature threshold
// (go left iff feats[feature] <= threshold); leaves carry a fixed-point value. All-integer, so tree
// traversal + summation is byte-identical across platforms (the determinism contract). feature < 0
// marks a leaf. child indices point within the same tree's node vector.
struct MidGameTreeNode
{
    int       feature   = -1;   // split feature index; < 0 => leaf
    int       threshold = 0;    // go LEFT iff feats[feature] <= threshold
    int       left      = -1;   // child indices (internal nodes only)
    int       right     = -1;
    long long value     = 0;    // leaf output, fixed-point units (internal nodes: 0)
};

// Analyzer-trained additive evaluator: predicted plan goodness = intercept + Sum coef*feat + Sum of
// regression-tree outputs, with HIGHER = better (the trainer fits toward earlier expected win, so a
// plan that wins sooner scores higher). Fixed-point long long throughout -> associative integer sum
// + integer-threshold tree traversal, so the plan argmax (and the game digest) is deterministic. The
// linear part (coefs) and the GBDT part (trees) are independent: a model may be pure-linear (--rank),
// pure-GBDT, or linear-init + boosted trees (--gbdt). Stored as DATA in the per-deck eval sidecar
// (decks/<name>.eval.json); empty => no model (heuristic ranking).
struct MidGameEvaluator
{
    std::vector<long long>                    coefs;          // per MidGameFeature index, fixed-point
    long long                                 intercept = 0;
    std::vector<std::vector<MidGameTreeNode>> trees;          // GBDT ensemble (each: node vector, root=0)

    bool empty() const { return coefs.empty() && trees.empty(); }

    static long long EvalTree(const std::vector<MidGameTreeNode>& tree, const std::vector<int>& feats)
    {
        if (tree.empty()) { return 0; }
        int i = 0;
        // Bounded by node count so a malformed (cyclic) tree can never loop forever.
        for (int guard = 0; guard < static_cast<int>(tree.size()); ++guard)
        {
            const MidGameTreeNode& n = tree[i];
            if (n.feature < 0) { return n.value; }                       // leaf
            const int fv = (n.feature < static_cast<int>(feats.size())) ? feats[n.feature] : 0;
            const int nx = (fv <= n.threshold) ? n.left : n.right;
            if (nx < 0 || nx >= static_cast<int>(tree.size())) { return n.value; }  // malformed guard
            i = nx;
        }
        return tree[i].value;
    }

    long long Score(const std::vector<int>& feats) const
    {
        long long s = intercept;
        const int n = std::min(static_cast<int>(feats.size()), static_cast<int>(coefs.size()));
        for (int i = 0; i < n; ++i) { s += coefs[i] * static_cast<long long>(feats[i]); }
        for (const std::vector<MidGameTreeNode>& t : trees) { s += EvalTree(t, feats); }
        return s;
    }
};

// Single shared featurizer (defined in KeepModel.cpp): integer-pure and NON-CLAIRVOYANT (reads only
// public information -- never library order or the opponent's hand). Called from the SAME site in
// TurnSolver for both offline label emission and runtime inference, so training and serving see
// byte-identical features (lockstep). Returns a vector indexed by MidGameFeature (size == Count).
std::vector<int> ExtractMidGameFeatures(const GameState& state, const MidGamePlanSummary& plan);

// Build the plan summary from the cast-spell NAMES + whether a land is played. This is the SINGLE
// canonical summary builder, used by BOTH runtime inference (the TurnSolver seams) and offline label
// emission (AIEngine's EnumerateEarliestWins dump) -- so both compute byte-identical summaries from
// the same names -> no train/serve skew. Looks each name up in the card database (deterministic).
MidGamePlanSummary SummarizePlanByNames(const std::vector<std::string>& cast_names, bool plays_land);
