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
    // A SUBSET COUNT of `wild` (never additional supply, never counted in Total): how many of those
    // wild units come from a source that can also produce {C}. A {C} PIP is not a colour and no
    // amount of coloured mana pays it (CR 107.4c) -- so a Fertile Ground's "add one mana of any
    // colour" cannot pay Eldrazi Displacer's {2}{C}, while a Yavimaya Coast (whose modes are
    // "{T}: Add {C}" / "{T}: Add {G} or {U}") can. Both credit `wild` (one tap, choice of output);
    // only the latter credits `wild_c`. Kept as a subset rather than a separate bucket so every
    // existing `.wild` reader sees the identical value it always did -- and because no cards.json
    // cost carried a {C} pip before this deck, `cost.colorless` is 0 everywhere else and CanPayFlat
    // below is provably byte-identical for every other deck. Clamped to `wild` at the one read site,
    // so the sites that decrement `wild` without maintaining this need no changes.
    int wild_c    = 0;
    // A SUBSET COUNT of `wild` (like `wild_c`): how many wild units are AMOUNT-PHANTOM -- credited
    // by a fed any-colour filter with NO free mode (Arcum's Astrolabe, `filter_no_free_colorless`),
    // whose fed mode is 1-in/1-out. Such a unit is a COLOUR CONVERSION of a feeder unit already
    // counted in this pool, not new supply -- so it may satisfy a colour deficit (the conversion is
    // real) but must not raise the pool's payable AMOUNT. Counting it fully made 4 Astrolabes read
    // as +4 mana and the enumerator offered mv-7 casts off 6 real sources on every main-phase
    // re-solve (4 claude-play sweep agents confirmed independently, 2026-09-06). CanPayFlat's
    // amount precheck below subtracts it; with no such filter on the battlefield it is 0 and the
    // precheck is implied by the existing deficit checks, so every other deck is byte-identical.
    int wild_phantom = 0;
    // A SUBSET COUNT of Total() (the wild_c pattern): how many of this pool's units were produced
    // by SNOW sources (battlefield permanents with Supertype::Snow -- real masks there). An {S}
    // pip is payable only from such a unit (CR 106.4b; see ManaCost::snow_pips), so CanPayFlat
    // requires snow_pips <= snow_units. Maintained at the pool-build choke point (the
    // AddSourceToPool wrapper measures each source's unit delta) and additive under AddPool.
    // 0 for every deck with no snow supertypes and no {S} costs -> the check is inert there.
    int snow_units = 0;

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

    void Clear()
    { white = blue = black = red = green = colorless = wild = wild_c = wild_phantom = snow_units = 0; }

    // Merge another pool into this one, colour for colour (used to retain leftover
    // mana from a payment into the turn-scoped reserve, state.floating_mana).
    void AddPool(const ManaPool& o)
    {
        white += o.white; blue += o.blue; black += o.black; red += o.red;
        green += o.green; colorless += o.colorless; wild += o.wild; wild_c += o.wild_c;
        wild_phantom += o.wild_phantom; snow_units += o.snow_units;
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
        // AMOUNT precheck: phantom wilds (see `wild_phantom`) are colour conversions of units this
        // pool already counts, so they cover colour deficits below but add no payable amount. Any
        // really-payable cost needs ManaValue() distinct real units, so subtracting the phantom
        // here can never hide a legal cast -- it only stops the +1-per-fed-Astrolabe offers the
        // payer then refuses. Clamped to `wild` for the same reason `wild_c` is.
        if (cost.ManaValue() > Total() - std::min(wild_phantom, wild)) { return false; }

        // SNOW precheck (necessary, not sufficient -- the payer is the exact gate, per the
        // enumerate-optimistic doctrine): {S} pips need units from snow sources. snow_pips = 0
        // everywhere but snow decks, and an all-snow manabase has snow_units == Total(), so both
        // the inert and the degenerate case are the flat check verbatim.
        if (static_cast<int>(cost.snow_pips) > std::min(snow_units, Total())) { return false; }

        // {C} pips first, and they are the ONE deficit `wild` alone cannot cover: only a source that
        // can actually produce colourless pays them (see `wild_c`). Clamped here so the many sites
        // that adjust `wild` without maintaining the subset can never make it exceed its superset.
        const int colorless_deficit = std::max(0, cost.colorless - colorless);
        if (colorless_deficit > std::min(wild_c, wild)) { return false; }

        // Deficit per color after using specific-color sources.
        int deficit =  std::max(0, cost.white     - white)
                     + std::max(0, cost.blue      - blue)
                     + std::max(0, cost.black     - black)
                     + std::max(0, cost.red       - red)
                     + std::max(0, cost.green     - green)
                     + colorless_deficit;

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
