#pragma once
#include "Card.h"
#include <vector>
#include <random>
#include <cstdint>
#include <stdexcept>
#include <algorithm>

// Draw-from-top library backed by a single vector with a TOP CURSOR.
//
// Draws advance an index (m_top) instead of erasing element 0, so DrawTop is O(1)
// rather than shifting the whole remaining library on every draw. That front-erase
// (erase(begin())) was ~7.6% of a search game -- callgrind, vector<Card>::_M_erase.
//
// The already-drawn prefix [0, m_top) is dead weight; the COPY constructor/assignment
// drop it (they copy only the live window [m_top, end)), so a deep-copied state -- the
// search's hot path -- carries only the live library and pays the SAME copy cost as the
// old erase-shrink form (one backing alloc + n_live Card copies). Net effect: the
// per-draw O(n) shift disappears, the per-node copy cost is unchanged.
//
// Everything observable is identical to the old `: public std::vector<Card>` form --
// the draw ORDER, size(), front()/operator[0]=top, the shuffle order -- so games stay
// BYTE-IDENTICAL (no ground-truth re-baseline). The public surface mirrors exactly the
// std::vector<Card> members the engine used, but every index/iterator is relative to the
// live window [m_top, end).
class Library
{
public:
    using value_type     = Card;
    using iterator       = std::vector<Card>::iterator;
    using const_iterator = std::vector<Card>::const_iterator;

    Library() = default;

    // Copy drops the drawn prefix: a deep-copied state only needs the live library, and
    // dropping keeps the per-node copy cost equal to the old (erase-shrunk) vector form.
    Library(const Library& o)
        : m_cards(o.m_cards.begin() + static_cast<std::ptrdiff_t>(o.m_top), o.m_cards.end())
    {
    }
    Library& operator=(const Library& o)
    {
        if (this != &o)
        {
            m_cards.assign(o.m_cards.begin() + static_cast<std::ptrdiff_t>(o.m_top), o.m_cards.end());
            m_top = 0;
        }
        return *this;
    }
    Library(Library&&) noexcept            = default;
    Library& operator=(Library&&) noexcept = default;

    // --- size / access over the live window [m_top, end) ---
    bool        empty() const { return m_top >= m_cards.size(); }
    std::size_t size()  const { return m_cards.size() - m_top; }

    Card&       front()       { return m_cards[m_top]; }
    const Card& front() const { return m_cards[m_top]; }

    Card&       operator[](std::size_t i)       { return m_cards[m_top + i]; }
    const Card& operator[](std::size_t i) const { return m_cards[m_top + i]; }

    iterator       begin()       { return m_cards.begin() + static_cast<std::ptrdiff_t>(m_top); }
    iterator       end()         { return m_cards.end(); }
    const_iterator begin() const { return m_cards.begin() + static_cast<std::ptrdiff_t>(m_top); }
    const_iterator end()   const { return m_cards.end(); }

    // --- mutation (forwarded; the passed iterators already point into the live window) ---
    void push_back(const Card& c) { m_cards.push_back(c); }
    void push_back(Card&& c)      { m_cards.push_back(std::move(c)); }

    iterator erase(const_iterator pos)                 { return m_cards.erase(pos); }
    iterator insert(const_iterator pos, const Card& c) { return m_cards.insert(pos, c); }
    iterator insert(const_iterator pos, Card&& c)      { return m_cards.insert(pos, std::move(c)); }

    template <class InputIt>
    void assign(InputIt first, InputIt last)
    {
        m_cards.assign(first, last);
        m_top = 0;
    }

    // --- draws ---
    // Throws if the library is empty -- drawing from an empty library is a loss condition.
    Card DrawTop()
    {
        if (empty())
        {
            // TODO: store an m_owner_index on Library at construction time, then throw a
            // DrawLossException carrying that index. GameEngine catches it and sets the loss
            // on the correct player (CR 704.5b). Required for mill win conditions where the
            // opponent is forced to draw from an empty library.
            throw std::runtime_error("DrawTop called on empty library");
        }
        // Move the top card out and advance the cursor (O(1), no shift). The vacated slot
        // joins the dead prefix and is never read again (dropped on the next copy).
        return std::move(m_cards[m_top++]);
    }

    // Draws up to n cards from the top and appends them to destination, stopping if the library
    // runs out (deck-out safe -- never throws). Returns the number actually drawn, so a caller can
    // detect a short draw (fewer than n) and apply the draw-from-empty loss (CR 104.3c). Byte-
    // identical for every game that does not deck out (the suite never empties its library).
    int DrawN(int n, std::vector<Card>& destination)
    {
        int drawn = 0;
        for (; drawn < n && !empty(); ++drawn)
        {
            destination.push_back(DrawTop());
        }
        return drawn;
    }

    // Cross-platform deterministic shuffle of the LIVE window [m_top, end).
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
    //
    // Shuffling the live window [m_top, end) (n = size()) reproduces the old whole-vector
    // shuffle byte-for-byte: at game setup m_top == 0, and on a mulligan the live window
    // holds exactly the same cards in the same order as the old (erase-shrunk) vector, so
    // the same algorithm over n cards yields the same permutation.
    void Shuffle(uint64_t seed)
    {
        std::mt19937_64   rng(seed);
        const std::size_t n = size();
        if (n < 2) { return; }
        auto at = [&](std::size_t k) -> Card& { return m_cards[m_top + k]; };

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
            if (off != i) { std::swap(at(i), at(static_cast<std::size_t>(off))); }
        }
    }

    // COMMON-RANDOM-NUMBERS reshuffle: order the live library by a per-copy priority
    // key = splitmix64(seed, m_number). Unlike Shuffle() (a Fisher-Yates whose output
    // depends on the exact multiset + draw history), each card's rank here depends ONLY
    // on its own stable m_number (assigned at deck setup, identical across two same-seed
    // games). So removing a card -- e.g. a fetchland pulling one land out -- leaves every
    // OTHER card's relative order UNCHANGED. Two games (two policies) that reach this
    // reshuffle with nearly the same library therefore draw nearly the same cards: they
    // diverge ONLY by the specifically-different cards each removed, not by a full
    // re-permutation. That holds the realized future consistent across an A/B while still
    // being a genuine (non-game-start, non-clairvoyant) reshuffle. m_number is unique per
    // live card, so the key is a total order (tie-break on m_number for hash collisions).
    void ShuffleByKey(uint64_t seed)
    {
        if (size() < 2) { return; }
        auto key = [seed](const Card& c) -> std::uint64_t
        {
            std::uint64_t x = seed * 0x9E3779B97F4A7C15ull
                            + (static_cast<std::uint64_t>(c.m_number) + 1) * 0xD1B54A32D192ED03ull;
            x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
            x ^= x >> 27; x *= 0x94D049BB133111EBull;
            x ^= x >> 31;
            return x;
        };
        std::stable_sort(m_cards.begin() + static_cast<std::ptrdiff_t>(m_top), m_cards.end(),
            [&key](const Card& a, const Card& b)
            {
                std::uint64_t ka = key(a), kb = key(b);
                return ka != kb ? ka < kb : a.m_number < b.m_number;
            });
    }

private:
    std::vector<Card> m_cards;
    std::size_t       m_top = 0;   // index of the current top; live library is [m_top, size())
};
