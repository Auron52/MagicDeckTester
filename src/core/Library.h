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
            // TODO: replace with a dedicated DrawLossException caught by GameEngine,
            // which sets the loss condition on GameState (CR 704.5b).
            throw std::runtime_error("drawTop called on empty library");
        }
        Card c = front();
        erase(begin());
        return c;
    }
};
