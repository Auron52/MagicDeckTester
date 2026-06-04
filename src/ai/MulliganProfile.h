#pragma once
#include "../core/Card.h"
#include <map>
#include <string>
#include <vector>

// Priority order for the two castability metrics when choosing which excess land to bottom.
enum class BottomOrder
{
    CountFirst, // minimise uncastable spell count first, then total deficit (default)
    TotalFirst, // minimise total deficit first, then uncastable count
};

// How aggressively to check for early plays when evaluating an opening hand.
// Applies until the hand has been mulliganed twice (after that, kept unconditionally).
enum class CurveCheck
{
    None,       // no check — decks with no required early play (control, all-land, etc.)
    TwoDrop,    // need at least one spell with MV ≤ 2 and ≥ 2 lands (default, generic fair decks)
    OneDrop,    // need at least one spell with MV ≤ 1 (aggressive decks that must act turn 1)
    OneAndTwo,  // need a turn-1 play (MV ≤ 1) AND at least one additional play by turn 2
                // (≥ 2 total MV ≤ 2 spells with ≥ 2 lands) — ultra-aggressive curve requirements
};

struct MulliganProfile
{
    int min_lands = 1;
    int max_lands = 5;

    // Mulligan unless at least one of these card names is in the opening hand (OR logic).
    // For AND requirements, encode as a custom KeepHand override or flag the deck for review.
    std::vector<std::string> required_pieces;

    // Minimum number of permanent mana sources (lands, mana dorks) for each color that
    // must appear in the opening hand. Only colors present in the deck are populated.
    // Spell-based mana sources (Dark Ritual, Lotus Bloom, etc.) are not counted here —
    // those require separate handling per card.
    std::map<Color, int> min_color_sources;

    // Minimum number of non-land cards in the opening hand that are castable given the lands
    // also in hand (each card evaluated independently against the full land base in hand).
    // Catches color-mismatch hands (e.g. Swamps + green cards in a Golgari deck) and
    // mana-quantity mismatches (e.g. 1 land + all 2-drops). 0 = disabled.
    int min_playable = 0;

    // Priority order when scoring excess lands for bottoming. See BottomOrder enum above.
    BottomOrder bottom_order = BottomOrder::CountFirst;

    // Curve requirement for the opening hand. See CurveCheck enum above.
    // None and TwoDrop are the most common; OneDrop and OneAndTwo exist for decks
    // whose analysis requires distinguishing T1 presence from T1+T2 coverage.
    CurveCheck curve_check = CurveCheck::TwoDrop;

    // Keep unconditionally once hand reaches this size (London mulligan stop point).
    int stop_at = 4;

    static MulliganProfile DefaultProfile() { return {}; }
};
