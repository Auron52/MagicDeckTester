#pragma once
#include <string>
#include <vector>

struct MulliganProfile
{
    int min_lands = 1;
    int max_lands = 5;

    // Mulligan unless at least one of these card names is in the opening hand (OR logic).
    // For AND requirements, encode as a custom KeepHand override or flag the deck for review.
    std::vector<std::string> required_pieces;

    // Set true for decks that don't need early plays (e.g. all-land, high-curve control).
    bool skip_curve_check = false;

    // Keep unconditionally once hand reaches this size (London mulligan stop point).
    int stop_at = 4;

    static MulliganProfile DefaultProfile() { return {}; }
};
