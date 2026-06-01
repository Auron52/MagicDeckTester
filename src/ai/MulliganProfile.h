#pragma once
#include <string>
#include <vector>

struct MulliganProfile {
    int minLands = 1;
    int maxLands = 5;

    // Mulligan unless at least one of these card names is in the opening hand (OR logic).
    // For AND requirements, encode as a custom keepHand override or flag the deck for review.
    std::vector<std::string> requiredPieces;

    // Set true for decks that don't need early plays (e.g. all-land, high-curve control).
    bool skipCurveCheck = false;

    // Keep unconditionally once hand reaches this size (London mulligan stop point).
    int stopAt = 4;

    static MulliganProfile defaultProfile() { return {}; }
};
