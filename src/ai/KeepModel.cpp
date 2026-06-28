#include "KeepModel.h"
#include "../cards/CardDatabase.h"
#include "../core/SpellEffects.h"   // EffectiveProducesInHand (Reflecting-Pool-aware colours)
#include <algorithm>
#include <map>

// Computes the integer feature vector for a hand, indexed by KeepFeature. Mirrors the quantities the
// legacy KeepHand derives (land/playable/curve/colour), but as the SINGLE shared definition the
// generator also calls, so a trained model and the runtime see byte-identical features (lockstep --
// the same discipline every rollout/executor pair in this engine follows).
// Colours we model per-colour (W/U/B/R/G); {C}/colorless folds into generic (no current deck has
// {C} pips). Indexed by the Color enum ordinal (White=0..Green=4), so static_cast<int>(c) is the slot.
constexpr int kNumColors = 5;
inline int CostPip(const ManaCost& mc, int ci)
{
    switch (ci)
    {
        case 0:  return mc.white;
        case 1:  return mc.blue;
        case 2:  return mc.black;
        case 3:  return mc.red;
        default: return mc.green;
    }
}

// Raw per-hand quantities the data-defined extra features read. Pointers into ComputeKeepFeatures'
// locals (same scope), so the base vector and the spec evaluator see byte-identical facts (lockstep).
struct HandFacts
{
    const int*              src;          // [kNumColors] sources that can make each colour
    const int*              demand_max;   // [kNumColors] heaviest single-card pip demand per colour
    const std::vector<int>* castable_mv;  // strictly-castable nonland MVs (sorted)
    const std::vector<int>* nonland_mv;   // every nonland MV
};

// Generic deterministic evaluator for one data-defined feature. `full` is the feature vector built so
// far (base ++ earlier extras), so composite operands reference earlier positions by index.
int EvalFeatureSpec(const FeatureSpec& spec, const std::vector<Card>& hand,
                    const HandFacts& hf, const std::vector<int>& full)
{
    auto at = [&](int i) { return (i >= 0 && i < static_cast<int>(full.size())) ? full[i] : 0; };
    switch (static_cast<FeatureKind>(spec.kind))
    {
        case FeatureKind::PerColorSource:
            return (spec.p >= 0 && spec.p < kNumColors) ? hf.src[spec.p] : 0;
        case FeatureKind::PerColorDemand:
            return (spec.p >= 0 && spec.p < kNumColors) ? hf.demand_max[spec.p] : 0;
        case FeatureKind::PerColorUncovered:
            return (spec.p >= 0 && spec.p < kNumColors
                    && hf.demand_max[spec.p] > 0 && hf.src[spec.p] == 0) ? 1 : 0;
        case FeatureKind::PerColorSurplus:
            return (spec.p >= 0 && spec.p < kNumColors) ? (hf.src[spec.p] - hf.demand_max[spec.p]) : 0;
        case FeatureKind::NonlandAtMv:
        {
            int c = 0;
            for (int mv : *hf.nonland_mv) { if ((mv >= 6 ? 6 : mv) == spec.p) { ++c; } }
            return c;
        }
        case FeatureKind::CastableAtMvLe:
        {
            int c = 0;
            for (int mv : *hf.castable_mv) { if (mv <= spec.p) { ++c; } }
            return c;
        }
        case FeatureKind::SubtypeDensity:
        {
            int c = 0;
            for (const Card& card : hand)
            {
                for (const std::string& st : card.m_subtypes) { if (st == spec.s) { ++c; break; } }
            }
            return c;
        }
        case FeatureKind::Diff: return at(spec.a) - at(spec.b);
        case FeatureKind::Min:  return std::min(at(spec.a), at(spec.b));
    }
    return 0;
}

std::vector<int> ComputeKeepFeatures(const std::vector<Card>& hand, int mulligan_count,
                                     bool on_the_play, const KeepModel& model)
{
    std::vector<int> f(static_cast<int>(KeepFeature::Count), 0);
    const int hand_size = static_cast<int>(hand.size());

    // Land / nonland / curve counts + a simple colour pool for the playability and colour features.
    int land = 0, mv1 = 0, mv2 = 0, mv3 = 0;
    std::vector<int> nonland_mv;     // mana values of every nonland card (feeds NonlandAtMv specs)
    std::map<Color, int> pool;   // colours produced by single-colour lands in hand
    int wild_mana = 0;           // one per multi-colour land (covers any single pip)

    // Richer per-colour mana model (drives source_X / playable_strict). A land that makes exactly
    // one of W/U/B/R/G goes to `single[colour]`; a land that makes >1 of them is a `dual` carrying
    // the exact colour set it can pay (NOT a fungible wild); a land that makes only colourless is a
    // generic source. `src[c]` = # sources that CAN make colour c (duals count for each).
    int single[kNumColors] = {0};
    int src[kNumColors]    = {0};
    int generic_sources    = 0;            // produce only {C}/colourless -> pay generic only
    std::vector<int> duals;                // each = bitmask over the 5 colours the dual can produce

    for (const Card& c : hand)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        const bool is_land = def ? def->card.IsLand() : c.IsLand();
        if (is_land)
        {
            ++land;
            if (def)
            {
                const std::vector<Color>& prod = EffectiveProducesInHand(hand, *def);  // RP-aware
                if (prod.size() == 1)     { ++pool[prod[0]]; }
                else if (!prod.empty())   { ++wild_mana; }

                // Per-colour decomposition (folds colourless into a generic source).
                int mask = 0, colored = 0;
                for (Color pc : prod)
                {
                    const int ci = static_cast<int>(pc);
                    if (ci >= 0 && ci < kNumColors) { mask |= (1 << ci); ++src[ci]; ++colored; }
                }
                if (colored == 0)      { ++generic_sources; }
                else if (colored == 1) { for (int ci = 0; ci < kNumColors; ++ci) if (mask & (1 << ci)) ++single[ci]; }
                else                   { duals.push_back(mask); }
            }
            continue;
        }
        const int mv = def ? def->card.m_mana_cost.ManaValue() : c.m_mana_cost.ManaValue();
        nonland_mv.push_back(mv);
        if (mv <= 1) { ++mv1; }
        if (mv <= 2) { ++mv2; }
        if (mv <= 3) { ++mv3; }
    }
    const int total_land_mana = land;

    // Per-colour DEMAND: the heaviest single-card requirement of each colour in hand (you cast one
    // card at a time, so the hardest single card's pips is the screw-relevant number, not the sum).
    int demand_max[kNumColors] = {0};
    for (const Card& c : hand)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        if (!def || def->card.IsLand()) { continue; }
        const ManaCost& cost = def->card.m_mana_cost;
        for (int ci = 0; ci < kNumColors; ++ci)
        { demand_max[ci] = std::max(demand_max[ci], CostPip(cost, ci)); }
    }
    int uncovered_colors = 0, max_pip_deficit = 0;
    for (int ci = 0; ci < kNumColors; ++ci)
    {
        if (demand_max[ci] > 0 && src[ci] == 0) { ++uncovered_colors; }
        max_pip_deficit = std::max(max_pip_deficit, demand_max[ci] - src[ci]);
    }
    if (max_pip_deficit < 0) { max_pip_deficit = 0; }

    // Playable: # of nonland cards castable from the hand's own lands (each evaluated independently
    // against the full pool -- "is this card castable at all", matching KeepHand's min_playable).
    int playable = 0;
    for (const Card& c : hand)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        if (!def || def->card.IsLand()) { continue; }
        const ManaCost& cost = def->card.m_mana_cost;
        if (total_land_mana < cost.ManaValue()) { continue; }
        const int deficit = std::max(0, cost.white     - pool[Color::White])
                          + std::max(0, cost.blue      - pool[Color::Blue])
                          + std::max(0, cost.black     - pool[Color::Black])
                          + std::max(0, cost.red       - pool[Color::Red])
                          + std::max(0, cost.green     - pool[Color::Green])
                          + std::max(0, cost.colorless - pool[Color::Colorless]);
        if (deficit > wild_mana) { continue; }
        ++playable;
    }

    // Colours covered: how many of the deck's colours have >= 1 producing source in hand.
    int colors_covered = 0;
    for (Color want : model.deck_colors)
    {
        bool covered = false;
        for (const Card& c : hand)
        {
            const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
            if (!def) { continue; }
            const bool is_src = (def->tmpl == CardTemplate::BasicLand)
                             || (def->tmpl == CardTemplate::ManaDork)
                             || def->params.mana_rock;
            if (!is_src) { continue; }
            for (Color produced : EffectiveProducesInHand(hand, *def))
            {
                if (produced == want) { covered = true; break; }
            }
            if (covered) { break; }
        }
        if (covered) { ++colors_covered; }
    }

    // Key pieces present (generalizes required_pieces): count hand cards whose name the analyzer
    // flagged as pivotal. Counts copies, so "two payoffs" reads higher than "one".
    int key_pieces = 0;
    if (!model.key_pieces.empty())
    {
        for (const Card& c : hand)
        {
            if (std::find(model.key_pieces.begin(), model.key_pieces.end(), c.m_name)
                != model.key_pieces.end())
            {
                ++key_pieces;
            }
        }
    }

    // Strict castability: can this card's colours be paid by a CORRECT allocation (single-colour
    // lands to their colour, duals only to colours they actually produce, generic/{C} from leftover)?
    // Unlike `playable` above, a {G}{R} dual can never pay a {U} pip here -- fixes the fungible-wild
    // overcount. Each card evaluated independently against the full pool ("is it castable at all").
    // The collected MVs of the castable cards also feed the curve-depth feature below.
    auto strict_castable = [&](const ManaCost& cost) -> bool
    {
        if (total_land_mana < cost.ManaValue()) { return false; }
        int need[kNumColors];
        for (int ci = 0; ci < kNumColors; ++ci) { need[ci] = CostPip(cost, ci); }
        int s[kNumColors];
        for (int ci = 0; ci < kNumColors; ++ci) { s[ci] = single[ci]; }
        // 1. pay coloured pips from matching single-colour sources first.
        for (int ci = 0; ci < kNumColors; ++ci)
        { const int take = std::min(need[ci], s[ci]); need[ci] -= take; s[ci] -= take; }
        // 2. cover the remaining coloured pips with duals restricted to the colours they produce.
        std::vector<char> used(duals.size(), 0);
        for (int ci = 0; ci < kNumColors; ++ci)
        {
            while (need[ci] > 0)
            {
                int pick = -1;
                for (int d = 0; d < static_cast<int>(duals.size()); ++d)
                { if (!used[d] && (duals[d] & (1 << ci))) { pick = d; break; } }
                if (pick < 0) { break; }
                used[pick] = 1; --need[ci];
            }
            if (need[ci] > 0) { return false; }   // a coloured requirement can't be met
        }
        // 3. generic/{C} from whatever mana is left over.
        int leftover = generic_sources;
        for (int ci = 0; ci < kNumColors; ++ci) { leftover += s[ci]; }
        for (int d = 0; d < static_cast<int>(duals.size()); ++d) { if (!used[d]) { ++leftover; } }
        return leftover >= (cost.generic + cost.colorless);
    };

    int playable_strict = 0;
    std::vector<int> castable_mv;   // mana values of the strictly-castable nonland cards
    for (const Card& c : hand)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        if (!def || def->card.IsLand()) { continue; }
        const ManaCost& cost = def->card.m_mana_cost;
        if (strict_castable(cost)) { ++playable_strict; castable_mv.push_back(cost.ManaValue()); }
    }

    // Curve depth: how many of turns 1..4 get a sequenceable on-curve play. Mana available by turn t
    // = min(t, lands-in-hand); greedily spend the cheapest unplayed castable spell each turn. Sorting
    // by MV makes it order-independent among equal-cost spells (deterministic).
    std::sort(castable_mv.begin(), castable_mv.end());
    int curve_depth = 0;
    std::vector<char> spent(castable_mv.size(), 0);
    for (int t = 1; t <= 4; ++t)
    {
        const int mana = std::min(t, land);
        for (int i = 0; i < static_cast<int>(castable_mv.size()); ++i)
        {
            if (!spent[i] && castable_mv[i] <= mana) { spent[i] = 1; ++curve_depth; break; }
        }
    }

    f[static_cast<int>(KeepFeature::FinalHandSize)] = hand_size - mulligan_count;
    f[static_cast<int>(KeepFeature::MulliganCount)] = mulligan_count;
    f[static_cast<int>(KeepFeature::OnThePlay)]     = on_the_play ? 1 : 0;
    f[static_cast<int>(KeepFeature::LandCount)]     = land;
    f[static_cast<int>(KeepFeature::NonlandCount)]  = hand_size - land;
    f[static_cast<int>(KeepFeature::PlayableCount)] = playable;
    f[static_cast<int>(KeepFeature::ColorsCovered)] = colors_covered;
    f[static_cast<int>(KeepFeature::CountMv1)]      = mv1;
    f[static_cast<int>(KeepFeature::CountMv2)]      = mv2;
    f[static_cast<int>(KeepFeature::KeyPieceCount)] = key_pieces;
    f[static_cast<int>(KeepFeature::SourceW)]         = src[static_cast<int>(Color::White)];
    f[static_cast<int>(KeepFeature::SourceU)]         = src[static_cast<int>(Color::Blue)];
    f[static_cast<int>(KeepFeature::SourceB)]         = src[static_cast<int>(Color::Black)];
    f[static_cast<int>(KeepFeature::SourceR)]         = src[static_cast<int>(Color::Red)];
    f[static_cast<int>(KeepFeature::SourceG)]         = src[static_cast<int>(Color::Green)];
    f[static_cast<int>(KeepFeature::UncoveredColors)] = uncovered_colors;
    f[static_cast<int>(KeepFeature::MaxPipDeficit)]   = max_pip_deficit;
    f[static_cast<int>(KeepFeature::PlayableStrict)]  = playable_strict;
    f[static_cast<int>(KeepFeature::CurveDepth)]      = curve_depth;
    f[static_cast<int>(KeepFeature::CountMv3)]        = mv3;

    // Append the data-defined extra features (empty for a Stage-1/legacy model -> byte-identical).
    // Computed left-to-right so a composite's operands (earlier indices) are already in `f`.
    if (!model.extra_features.empty())
    {
        const HandFacts hf{ src, demand_max, &castable_mv, &nonland_mv };
        f.reserve(f.size() + model.extra_features.size());
        for (const FeatureSpec& spec : model.extra_features)
        { f.push_back(EvalFeatureSpec(spec, hand, hf, f)); }
    }
    return f;
}

// Human-constraint hard guard, applied before the model. Phase 1 supports required_pieces (mulligan
// unless at least one is present); the tri-state leaves room for future always/never-keep rules.
KeepGuard ApplyKeepConstraints(const std::vector<Card>& hand, const KeepConstraints& c)
{
    if (!c.required_pieces.empty())
    {
        bool found = false;
        for (const std::string& piece : c.required_pieces)
        {
            for (const Card& card : hand)
            {
                if (card.m_name == piece) { found = true; break; }
            }
            if (found) { break; }
        }
        if (!found) { return KeepGuard::ForceMulligan; }
    }
    return KeepGuard::Undecided;
}
