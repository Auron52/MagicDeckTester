#pragma once
#include "Card.h"

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

    int Total() const { return white + blue + black + red + green + colorless; }

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

    void Clear() { white = blue = black = red = green = colorless = 0; }

    // Returns true if this pool can pay the given cost.
    // Assumes no hybrid or Phyrexian mana (TODO Phase 1.2+).
    bool CanPay(const ManaCost& cost) const
    {
        if (white     < cost.white)     { return false; }
        if (blue      < cost.blue)      { return false; }
        if (black     < cost.black)     { return false; }
        if (red       < cost.red)       { return false; }
        if (green     < cost.green)     { return false; }
        if (colorless < cost.colorless) { return false; }

        int committed = cost.white + cost.blue + cost.black
                      + cost.red + cost.green + cost.colorless;
        return (Total() - committed) >= cost.generic;
    }
};
