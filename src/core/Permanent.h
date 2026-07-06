#pragma once
#include "Card.h"
#include <vector>

struct Counter
{
    enum class Type { PlusOnePlusOne, MinusOneMinusOne, Loyalty, Poison, Depletion };
    Type type;
    int count = 1;
};

struct Permanent
{
    Card card;
    int  controller_index          = 0;   // index into GameState::players
    int  owner_index               = 0;   // index into GameState::players
    bool tapped               = false;
    int       damage               = 0;    // reset each cleanup step
    // Accumulated "when this creature dies this turn" damage owed to its controller from delayed
    // triggers (Searing Blood: 3 per copy). Two Searing Bloods on one creature leave 6 pending; it
    // all fires when the creature dies (CR 603.7). Reset each cleanup with damage.
    int       pending_death_trigger = 0;
    std::vector<Counter> counters;
    bool      entered_this_turn    = false;  // summoning sickness tracker
    Permanent* attached_to         = nullptr;
    bool      marked_for_destruction = false;
    int       temp_power_bonus     = 0;    // accumulated "until end of turn" boosts; reset each cleanup
    int       temp_tough_bonus     = 0;
    int       charge_counters      = 0;    // Aether Vial charge counter count
    int       verse_counters       = 0;    // Aria of Flame verse counter count
    bool      is_animated          = false; // land animated as a creature (e.g. Mutavault); reset each cleanup

    int  EffectivePower()     const;
    int  EffectiveToughness() const;

    // A permanent can attack if it is an untapped creature (or animated land) without
    // Defender that is not summoning sick (CR 302.6, CR 508.1).
    // Animated permanents (is_animated) always have haste from their animation effect.
    // For lord-granted haste (e.g. Cloudshredder Sliver), use CanAttackFull() in
    // SpellEffects.h which has access to the full battlefield.
    bool CanAttack() const
    {
        if (!card.IsCreature() && !is_animated)
        {
            return false;
        }
        if (tapped)
        {
            return false;
        }
        if (card.HasKeyword(Keyword::Defender))
        {
            return false;
        }
        return !entered_this_turn || card.HasKeyword(Keyword::Haste) || is_animated;
    }

    // A permanent can be tapped for an activated ability unless it is a summoning-sick
    // creature. Non-creatures are never affected by summoning sickness (CR 302.6).
    bool CanTap() const
    {
        if (!card.IsCreature())
        {
            return true;
        }
        return !entered_this_turn || card.HasKeyword(Keyword::Haste);
    }
};

inline int Permanent::EffectivePower() const
{
    int p = card.m_power.value_or(0) + temp_power_bonus;
    for (const Counter& c : counters)
    {
        if (c.type == Counter::Type::PlusOnePlusOne)  { p += c.count; }
        if (c.type == Counter::Type::MinusOneMinusOne) { p -= c.count; }
    }
    return p;
}

inline int Permanent::EffectiveToughness() const
{
    int t = card.m_toughness.value_or(0) + temp_tough_bonus;
    for (const Counter& c : counters)
    {
        if (c.type == Counter::Type::PlusOnePlusOne)  { t += c.count; }
        if (c.type == Counter::Type::MinusOneMinusOne) { t -= c.count; }
    }
    return t;
}
