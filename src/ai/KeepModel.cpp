#include "KeepModel.h"
#include "../cards/CardDatabase.h"
#include "../core/SpellEffects.h"   // EffectiveProducesInHand (Reflecting-Pool-aware colours)
#include <algorithm>
#include <map>

// Computes the integer feature vector for a hand, indexed by KeepFeature. Mirrors the quantities the
// legacy KeepHand derives (land/playable/curve/colour), but as the SINGLE shared definition the
// generator also calls, so a trained model and the runtime see byte-identical features (lockstep --
// the same discipline every rollout/executor pair in this engine follows).
std::vector<int> ComputeKeepFeatures(const std::vector<Card>& hand, int mulligan_count,
                                     bool on_the_play, const KeepModel& model)
{
    std::vector<int> f(static_cast<int>(KeepFeature::Count), 0);
    const int hand_size = static_cast<int>(hand.size());

    // Land / nonland / curve counts + a simple colour pool for the playability and colour features.
    int land = 0, mv1 = 0, mv2 = 0;
    std::map<Color, int> pool;   // colours produced by single-colour lands in hand
    int wild_mana = 0;           // one per multi-colour land (covers any single pip)
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
            }
            continue;
        }
        const int mv = def ? def->card.m_mana_cost.ManaValue() : c.m_mana_cost.ManaValue();
        if (mv <= 1) { ++mv1; }
        if (mv <= 2) { ++mv2; }
    }
    const int total_land_mana = land;

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
