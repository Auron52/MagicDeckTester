#pragma once
#include "Card.h"
#include <vector>
#include <random>
#include <algorithm>
#include <stdexcept>

class Library : public std::vector<Card>
{
public:
    using std::vector<Card>::vector;

    void shuffle(uint64_t seed)
    {
        std::mt19937_64 rng(seed);
        std::shuffle(begin(), end(), rng);
    }

    Card drawTop()
    {
        if (empty())
        {
            // TODO: replace with a DrawLossException that identifies the affected player,
            // caught by GameEngine to set the loss on the correct player (CR 704.5b).
            // The player index cannot be inferred from Library alone — pass it at the call site
            // or store an owner reference here. Needed for mill win conditions where the
            // opponent is forced to draw from an empty library.
            throw std::runtime_error("drawTop called on empty library");
        }
        Card c = front();
        erase(begin());
        return c;
    }
};
