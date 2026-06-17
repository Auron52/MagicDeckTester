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

    // Cross-platform deterministic shuffle.
    //
    // std::mt19937_64 is bit-portable, but std::shuffle is implementation-defined:
    // libstdc++ and MSVC consume the engine differently, so the SAME seed yields a
    // DIFFERENT order per STL. That made the regression ground truth (generated on
    // MSVC) unreproducible on the Linux/GCC build -- proven to be the sole cause of
    // the divergence (every other engine path, incl. the eval, search, and TT hash,
    // is platform-portable). To guarantee identical games for a seed on every
    // platform, we open-code MSVC STL's std::shuffle (_Shuffle_unchecked +
    // _Rng_from_urng) rather than calling std::shuffle. This keeps the existing
    // MSVC-generated ground truth valid on Linux. std::shuffle uses the original
    // _Rng_from_urng (stable across VS versions; only uniform_int_distribution got
    // the _v2 rewrite), so this reproduction is version-stable.
    //
    // A simpler hand-rolled Fisher-Yates would also be portable but would change the
    // order and require a one-time ground-truth re-baseline; deferred intentionally.
    void Shuffle(uint64_t seed)
    {
        std::mt19937_64 rng(seed);
        const std::size_t n = size();
        if (n < 2) { return; }
        Library& a = *this;

        // _Rng_from_urng<ptrdiff_t, mt19937_64>::operator()(index): produce an
        // unbiased value in [0, index). For mt19937_64 (min=0, max=2^64-1) the
        // adapter's _Bits=64 and _Bmask=~0ull, so _Get_bits() is a single raw draw
        // and after one iteration _Ret is that draw with _Mask all-ones. Then it
        // rejection-samples to remove modulo bias, redrawing on the rejected slice.
        auto rng_index = [&](std::uint64_t index) -> std::uint64_t
        {
            const std::uint64_t mask = ~std::uint64_t(0);
            for (;;)
            {
                std::uint64_t ret = rng();
                if (ret / index < mask / index || mask % index == index - 1)
                {
                    return ret % index;
                }
            }
        };

        // _Shuffle_unchecked: forward Durstenfeld. For i = 1..n-1, draw _Off in
        // [0, i] via _Rng_from_urng(i + 1) and swap element i with element _Off.
        for (std::size_t i = 1; i < n; ++i)
        {
            std::uint64_t off = rng_index(static_cast<std::uint64_t>(i + 1)); // [0, i]
            if (off != i) { std::swap(a[i], a[static_cast<std::size_t>(off)]); }
        }
    }

    // Draws exactly n cards from the top and appends them to destination.
    // Throws if the library runs out — drawing from an empty library is a loss condition.
    void DrawN(int n, std::vector<Card>& destination)
    {
        for (int i = 0; i < n; ++i)
        {
            destination.push_back(DrawTop());
        }
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
