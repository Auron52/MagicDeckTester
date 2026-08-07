#pragma once
#include "Card.h"
#include <algorithm>  // std::max

// Tracks mana currently available to a player for paying costs.
// Does not include mana held by untapped sources — see AIEngine::BuildAvailableMana
// for the function that constructs a pool from untapped permanents.
struct ManaPool
{
    int white     = 0;
    int blue      = 0;
    int black     = 0;
    int red       = 0;
    int green     = 0;
    int colorless = 0;  // {C} mana specifically
    int wild      = 0;  // one tap of a multi-color land (satisfies any single color or generic)

    int Total() const { return white + blue + black + red + green + colorless + wild; }

    void Add(Color c, int amount = 1)
    {
        switch (c)
        {
            case Color::White:     white     += amount; break;
            case Color::Blue:      blue      += amount; break;
            case Color::Black:     black     += amount; break;
            case Color::Red:       red       += amount; break;
            case Color::Green:     green     += amount; break;
            case Color::Colorless: colorless += amount; break;
        }
    }

    void Clear() { white = blue = black = red = green = colorless = wild = 0; }

    // Merge another pool into this one, colour for colour (used to retain leftover
    // mana from a payment into the turn-scoped reserve, state.floating_mana).
    void AddPool(const ManaPool& o)
    {
        white += o.white; blue += o.blue; black += o.black; red += o.red;
        green += o.green; colorless += o.colorless; wild += o.wild;
    }

    // Returns true if this pool can pay the given cost. Two-colour hybrid pips are handled by
    // expanding every concrete assignment (2^hybrid_count, <= 16) over the flat check.
    bool CanPay(const ManaCost& cost) const
    {
        if (cost.hybrid_count == 0) { return CanPayFlat(cost); }
        for (unsigned bits = 0; bits < (1u << cost.hybrid_count); ++bits)
        {
            if (CanPayFlat(cost.ExpandHybrids(bits))) { return true; }
        }
        return false;
    }

    // Flat (hybrid-free) affordability.
    // Multi-color land taps are stored in `wild` — each unit satisfies exactly one
    // pip (colored or generic). Specific-color sources pay their own color first;
    // wild covers any shortfall. Assumes no hybrid or Phyrexian mana.
    bool CanPayFlat(const ManaCost& cost) const
    {
        // Deficit per color after using specific-color sources.
        int deficit =  std::max(0, cost.white     - white)
                     + std::max(0, cost.blue      - blue)
                     + std::max(0, cost.black     - black)
                     + std::max(0, cost.red       - red)
                     + std::max(0, cost.green     - green)
                     + std::max(0, cost.colorless - colorless);

        if (deficit > wild) { return false; }

        // Remaining wild after covering colored deficits, plus leftover specific mana,
        // must cover the generic requirement.
        int wild_left      = wild - deficit;
        int specific_left  = std::max(0, white     - cost.white)
                           + std::max(0, blue      - cost.blue)
                           + std::max(0, black     - cost.black)
                           + std::max(0, red       - cost.red)
                           + std::max(0, green     - cost.green)
                           + std::max(0, colorless - cost.colorless);
        return (specific_left + wild_left) >= cost.generic;
    }
};
