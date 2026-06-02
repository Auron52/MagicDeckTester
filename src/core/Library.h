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

    void Shuffle(uint64_t seed)
    {
        std::mt19937_64 rng(seed);
        std::shuffle(begin(), end(), rng);
    }

    Card DrawTop()
    {
        if (empty())
        {
            // TODO: store an m_owner_index on Library at construction time, then throw
            // a DrawLossException carrying that index. GameEngine catches it and sets the
            // loss on the correct player (CR 704.5b). Required for mill win conditions
            // where the opponent is forced to draw from an empty library.
            throw std::runtime_error("DrawTop called on empty library");
        }
        Card c = front();
        erase(begin());
        return c;
    }
};
