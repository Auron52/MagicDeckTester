#pragma once
#include "Card.h"
#include <vector>

struct Player;

struct Counter
{
    enum class Type { PlusOnePlusOne, MinusOneMinusOne, Loyalty, Poison };
    Type type;
    int count = 1;
};

struct Permanent
{
    Card      card;
    Player*   controller           = nullptr;
    Player*   owner                = nullptr;
    bool      tapped               = false;
    int       damage               = 0;    // reset each cleanup step
    std::vector<Counter> counters;
    bool      entered_this_turn    = false;  // summoning sickness tracker
    Permanent* attached_to         = nullptr;
    bool      marked_for_destruction = false;

    int  EffectivePower()     const;
    int  EffectiveToughness() const;

    // A permanent can attack only if it is a creature that is not summoning sick (CR 302.6).
    bool CanAttack() const
    {
        if (!card.IsCreature())
        {
            return false;
        }
        return !entered_this_turn || card.HasKeyword(Keyword::Haste);
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
    // TODO: route through layer system when continuous effects are implemented (Phase 1.2)
    int p = card.m_power.value_or(0);
    for (const Counter& c : counters)
    {
        if (c.type == Counter::Type::PlusOnePlusOne)
        {
            p += c.count;
        }
        if (c.type == Counter::Type::MinusOneMinusOne)
        {
            p -= c.count;
        }
    }
    return p;
}

inline int Permanent::EffectiveToughness() const
{
    // TODO: route through layer system when continuous effects are implemented (Phase 1.2)
    int t = card.m_toughness.value_or(0);
    for (const Counter& c : counters)
    {
        if (c.type == Counter::Type::PlusOnePlusOne)
        {
            t += c.count;
        }
        if (c.type == Counter::Type::MinusOneMinusOne)
        {
            t -= c.count;
        }
    }
    return t;
}
