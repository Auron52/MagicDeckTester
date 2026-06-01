#pragma once
#include "Card.h"
#include <vector>

struct Player;

struct Counter {
    enum class Type { PlusOnePlusOne, MinusOneMinusOne, Loyalty, Poison };
    Type type;
    int count = 1;
};

struct Permanent {
    Card      card;
    Player*   controller        = nullptr;
    Player*   owner             = nullptr;
    bool      tapped            = false;
    int       damage            = 0;    // reset each cleanup step
    std::vector<Counter> counters;
    bool      enteredThisTurn   = false;  // summoning sickness tracker
    Permanent* attachedTo       = nullptr;
    bool      markedForDestruction = false;

    int  effectivePower()     const;
    int  effectiveToughness() const;

    // Summoning sickness: creatures that entered this turn cannot attack or
    // activate tap abilities unless they have haste (CR 302.6).
    bool canAttackOrTap() const { return !enteredThisTurn || card.hasKeyword(Keyword::Haste); }
};

inline int Permanent::effectivePower() const {
    // TODO: route through layer system when continuous effects are implemented (Phase 1.2)
    int p = card.power.value_or(0);
    for (const auto& c : counters) {
        if (c.type == Counter::Type::PlusOnePlusOne)   p += c.count;
        if (c.type == Counter::Type::MinusOneMinusOne) p -= c.count;
    }
    return p;
}

inline int Permanent::effectiveToughness() const {
    // TODO: route through layer system when continuous effects are implemented (Phase 1.2)
    int t = card.toughness.value_or(0);
    for (const auto& c : counters) {
        if (c.type == Counter::Type::PlusOnePlusOne)   t += c.count;
        if (c.type == Counter::Type::MinusOneMinusOne) t -= c.count;
    }
    return t;
}
