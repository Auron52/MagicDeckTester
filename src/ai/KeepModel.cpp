#include "KeepModel.h"
#include "../cards/CardDatabase.h"
#include "../core/GameState.h"      // ExtractMidGameFeatures reads the live board (mid-game play)
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
        case FeatureKind::Diff:    return at(spec.a) - at(spec.b);
        case FeatureKind::Min:     return std::min(at(spec.a), at(spec.b));
        case FeatureKind::Product: return at(spec.a) * at(spec.b);
        case FeatureKind::CardCount:
        {
            int c = 0;
            for (const Card& card : hand) { if (card.m_name == spec.s) { ++c; } }
            return c;
        }
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
    int tapped_lands       = 0;            // lands that enter tapped (cost a turn of tempo -> on-time)

    for (const Card& c : hand)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        const bool is_land = def ? def->card.IsLand() : c.IsLand();
        if (is_land)
        {
            ++land;
            if (def && (def->params.enters_tapped || def->params.enters_tapped_with_depletion > 0))
            { ++tapped_lands; }
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

    // On-time castability: for each nonland spell, can its EXACT colour cost be produced BY its
    // curve turn (= mana value), accounting for lands that enter tapped? Mana ONLINE on turn t when you
    // play one land/turn, tapped lands first (so they untap in time): a tapped land is online only if
    // played by turn t-1, an untapped land if played by turn t. So two taplands delay a 2-drop to turn 3.
    // Colour correctness reuses the strict per-colour allocation (a {G}{R} dual can't pay {U}); ramp
    // (dorks/rocks) is NOT modelled here -- this is the land-tempo + colour axis curve_depth lacks.
    auto online_mana = [&](int t) -> int
    {
        const int drops = std::min(t, land);
        const int tapped_played   = std::min(tapped_lands, drops);
        const int untapped_played = drops - tapped_played;
        const int tapped_online   = std::min(tapped_played, std::max(0, t - 1));
        return tapped_online + untapped_played;
    };
    constexpr int kLateCap = 6;
    int on_time_count = 0, worst_lateness = 0;
    for (const Card& c : hand)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
        if (!def || def->card.IsLand()) { continue; }
        const ManaCost& cost = def->card.m_mana_cost;
        const int mv = cost.ManaValue();
        if (mv <= 0) { ++on_time_count; continue; }      // free spell: trivially on time
        const bool color_ok = strict_castable(cost);
        int earliest = mv + kLateCap;                    // sentinel "never / colour screwed"
        if (color_ok)
        {
            for (int t = mv; t <= mv + kLateCap; ++t)
            { if (online_mana(t) >= mv) { earliest = t; break; } }
        }
        const int lateness = std::min(kLateCap, earliest - mv);
        if (color_ok && earliest <= mv) { ++on_time_count; }
        worst_lateness = std::max(worst_lateness, lateness);
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
    f[static_cast<int>(KeepFeature::OnTimeCount)]     = on_time_count;
    f[static_cast<int>(KeepFeature::WorstLateness)]   = worst_lateness;

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

// --- Mid-game PLAY featurizer (learned d0 evaluator) ------------------------------------------
// Integer-pure and NON-CLAIRVOYANT: reads only public information -- our hand/board/graveyard/exile/
// life, the opponent's visible board + life, and library SIZE (never library ORDER or the opponent's
// hand). Perspective is the active player (Solve runs on our turn). See docs/design/learned-d0-policy.md.
std::vector<int> ExtractMidGameFeatures(const GameState& state, const MidGamePlanSummary& plan)
{
    std::vector<int> f(static_cast<int>(MidGameFeature::Count), 0);
    const Player& ap  = state.ActivePlayer();
    const Player& opp = state.Opponent();
    const int     active = state.active_player_index;
    auto set = [&](MidGameFeature k, int v) { f[static_cast<int>(k)] = v; };

    set(MidGameFeature::OurLife,       ap.life);
    set(MidGameFeature::OppLife,       opp.life);
    set(MidGameFeature::Turn,          state.turn_number);
    set(MidGameFeature::OnThePlay,     state.on_the_play ? 1 : 0);
    set(MidGameFeature::HandSize,      static_cast<int>(ap.hand.size()));
    set(MidGameFeature::LibrarySize,   static_cast<int>(ap.library.size()));
    set(MidGameFeature::GraveyardSize, static_cast<int>(ap.graveyard.size()));
    set(MidGameFeature::ExileSize,     static_cast<int>(state.exile.size()));

    // Lands in our OWN hand (public to us -> non-clairvoyant). Land's Edge discards these for burn and
    // Treasure Hunt refills them, so hand land-count is a load-bearing signal this model was blind to.
    // v7 also categorises the rest of the hand (the most immediate driver of a shallow decision). MVs of
    // nonland cards are collected for HandCastableNow, computed once untapped sources are known below.
    int lands_in_hand = 0, hand_creatures = 0, hand_damage = 0, hand_draw = 0;
    std::vector<int> hand_nonland_mv;
    for (const Card& c : ap.hand)
    {
        if (c.IsLand()) { ++lands_in_hand; continue; }
        if (c.IsCreature()) { ++hand_creatures; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        hand_nonland_mv.push_back(d ? d->card.m_mana_cost.ManaValue() : c.m_mana_cost.ManaValue());
        if (d)
        {
            if (d->params.damage > 0 || d->params.landfall_damage > 0 || d->params.death_trigger_damage > 0)
            { ++hand_damage; }
            if (d->params.draw > 0 || d->params.stages_cards || d->params.cascade_max_mv > 0
                || d->params.retrace || d->params.expressive_iteration
                || d->tmpl == CardTemplate::DrawUntilNonland)
            { ++hand_draw; }
        }
    }

    int our_creatures = 0, our_power = 0, our_ready = 0, our_lands = 0, untapped_sources = 0;
    int src[kNumColors] = {0};
    int opp_creatures = 0, opp_power = 0;

    for (const Permanent& p : state.battlefield)
    {
        const CardDefinition* def = CardDatabase::Instance().LookupCached(p.card);
        const bool is_creature = p.card.IsCreature() || p.is_animated;
        if (p.controller_index == active)
        {
            if (is_creature)
            {
                ++our_creatures;
                our_power += p.EffectivePower();
                if (p.CanAttack()) { ++our_ready; }
            }
            if (p.card.IsLand()) { ++our_lands; }
            // Mana source: an untapped land, a usable dork, or a rock we control.
            const bool is_land = p.card.IsLand();
            const bool is_dork = def && def->tmpl == CardTemplate::ManaDork && p.CanTap();
            const bool is_rock = def && def->params.mana_rock;
            if (!p.tapped && (is_land || is_dork || is_rock))
            {
                ++untapped_sources;
                if (def)
                {
                    for (Color c : def->params.produces)
                    {
                        const int ci = static_cast<int>(c);
                        if (ci >= 0 && ci < kNumColors) { ++src[ci]; }
                    }
                }
            }
        }
        else if (is_creature)
        {
            ++opp_creatures;
            opp_power += p.EffectivePower();
        }
    }

    set(MidGameFeature::OurCreatures,        our_creatures);
    set(MidGameFeature::OurTotalPower,       our_power);
    set(MidGameFeature::OurReadyAttackers,   our_ready);
    set(MidGameFeature::OurLands,            our_lands);
    set(MidGameFeature::UntappedManaSources, untapped_sources);
    set(MidGameFeature::SourceW, src[static_cast<int>(Color::White)]);
    set(MidGameFeature::SourceU, src[static_cast<int>(Color::Blue)]);
    set(MidGameFeature::SourceB, src[static_cast<int>(Color::Black)]);
    set(MidGameFeature::SourceR, src[static_cast<int>(Color::Red)]);
    set(MidGameFeature::SourceG, src[static_cast<int>(Color::Green)]);
    set(MidGameFeature::OppCreatures,  opp_creatures);
    set(MidGameFeature::OppTotalPower, opp_power);

    set(MidGameFeature::PlanNumSpells,     plan.num_spells);
    set(MidGameFeature::PlanCreaturesCast, plan.creatures_cast);
    set(MidGameFeature::PlanDirectDamage,  plan.direct_damage);
    set(MidGameFeature::PlanTotalMv,       plan.total_mv);
    set(MidGameFeature::PlanPlaysLand,     plan.plays_land);

    // v2 discriminators. plan_* come straight from the (name-derived) summary; the two board estimates
    // combine the plan with the pre-plan board so plans differ by the state they LEAVE, not just size.
    set(MidGameFeature::PlanCardsDrawn,        plan.cards_drawn);
    set(MidGameFeature::PlanNoncreatureSpells, plan.num_spells - plan.creatures_cast);
    set(MidGameFeature::PlanMaxCastMv,         plan.max_cast_mv);
    set(MidGameFeature::PlanDrawEngine,        plan.draw_engine);
    set(MidGameFeature::LandsInHand,           lands_in_hand);
    set(MidGameFeature::ManaLeftAfter,         std::max(0, untapped_sources - plan.total_mv));
    set(MidGameFeature::TapsOut,               (plan.num_spells > 0 && plan.total_mv >= untapped_sources) ? 1 : 0);
    set(MidGameFeature::PlanFaceDamage,        plan.face_damage);
    set(MidGameFeature::PlanBaselineEval,      plan.baseline_eval);  // hand-tuned EvalCard sum (set by seam/dump)

    // v6 DISTRIBUTIONAL features: the remaining-library COMPOSITION (a public multiset -- own decklist minus
    // visible zones; only ORDER is hidden). Counted ORDER-INVARIANTLY (sum over the whole library, never
    // position i) -> non-clairvoyant. Lets the value model amortise the rollout-under-uncertainty its
    // de-clairvoyed label already targets. See learned-d0-policy.md.
    int lib_lands = 0, lib_creatures = 0, lib_damage = 0, lib_draw = 0;
    for (const Card& c : ap.library)
    {
        if (c.IsLand())     { ++lib_lands; }
        if (c.IsCreature()) { ++lib_creatures; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (d)
        {
            if (d->params.damage > 0 || d->params.landfall_damage > 0 || d->params.death_trigger_damage > 0)
            { ++lib_damage; }
            if (d->params.draw > 0 || d->params.stages_cards || d->params.cascade_max_mv > 0
                || d->params.retrace || d->params.expressive_iteration
                || d->tmpl == CardTemplate::DrawUntilNonland)
            { ++lib_draw; }
        }
    }
    const int lib_size = static_cast<int>(ap.library.size());
    set(MidGameFeature::LibLands,          lib_lands);
    set(MidGameFeature::LibCreatures,      lib_creatures);
    set(MidGameFeature::LibDamageSources,  lib_damage);
    set(MidGameFeature::LibDrawEngines,    lib_draw);
    set(MidGameFeature::LibLandDensityPct, lib_size > 0 ? (100 * lib_lands) / lib_size : 0);

    // v7 hand composition (own hand = public). HandCastableNow: nonland cards whose MV fits untapped mana.
    int hand_castable = 0;
    for (int mv : hand_nonland_mv) { if (mv <= untapped_sources) { ++hand_castable; } }
    set(MidGameFeature::HandCreatures,     hand_creatures);
    set(MidGameFeature::HandDamageSources, hand_damage);
    set(MidGameFeature::HandDrawEngines,   hand_draw);
    set(MidGameFeature::HandCastableNow,   hand_castable);

    // v8 plan-varying resulting-state (the d0 RANKER discriminators): the board a plan develops.
    set(MidGameFeature::PlanPowerAdded,     plan.power_added);
    set(MidGameFeature::PlanToughnessAdded, plan.toughness_added);
    // v9 plan x board interaction: firing a dig engine into a land-dense library (expected yield).
    const int lib_land_density = lib_size > 0 ? (100 * lib_lands) / lib_size : 0;
    set(MidGameFeature::PlanDigYield, plan.draw_engine ? lib_land_density : 0);
    return f;
}

// Single canonical plan-summary builder (see KeepModel.h). Shared by the runtime seams and the
// offline label dump so both see byte-identical summaries from the same card names (no skew).
MidGamePlanSummary SummarizePlanByNames(const std::vector<std::string>& cast_names, bool plays_land)
{
    MidGamePlanSummary sum;
    sum.plays_land = plays_land ? 1 : 0;
    for (const std::string& nm : cast_names)
    {
        ++sum.num_spells;
        const CardDefinition* def = CardDatabase::Instance().Lookup(nm);
        if (!def) { continue; }
        if (def->card.IsCreature())
        {
            ++sum.creatures_cast;
            sum.power_added     += def->card.m_power.value_or(0);   // printed P/T (v8 plan-varying board dev)
            sum.toughness_added += def->card.m_toughness.value_or(0);
        }
        const int mv = def->card.m_mana_cost.ManaValue();
        sum.total_mv += mv;
        if (mv > sum.max_cast_mv) { sum.max_cast_mv = mv; }
        sum.cards_drawn += def->params.draw;   // FIXED draw only (variable draw would be clairvoyant)
        // A variable draw / dig / recur engine (Treasure Hunt = draw_until_nonland, cascade, retrace,
        // Expressive Iteration, staged digs). Mirrors TurnSolver::OrderingOpaque so the "card-advantage
        // engine" signal is defined once. Non-clairvoyant: flags the KIND of spell, never how many
        // cards it will draw from the hidden-order library.
        if (def->tmpl == CardTemplate::DrawUntilNonland
            || def->params.stages_cards
            || def->params.cascade_max_mv > 0
            || def->params.retrace
            || def->params.expressive_iteration
            || def->params.draw > 0)
        { sum.draw_engine = 1; }
        // Fixed direct damage (Lightning Bolt = 3). X-spells (Crackle/Fireball) carry a variable
        // damage the name can't resolve, so exclude them (x_damage_multiplier > 1); the exact lethal
        // check still covers their kills. Goldfish opp has no creatures, so this burn is face burn.
        if (def->params.x_damage_multiplier <= 1) { sum.face_damage += def->params.damage; }
    }
    // direct_damage stays 0 for v1: it depends on the chosen X and the target (face vs creature),
    // which a card NAME alone can't resolve, so computing it here would diverge from a resolved plan
    // and create train/serve skew. The exact lethal check already handles burn-to-face; add a
    // consistent burn-to-face feature later if the learning curve shows headroom. See
    // docs/design/learned-d0-policy.md.
    return sum;
}
