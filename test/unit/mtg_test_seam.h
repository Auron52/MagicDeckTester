// Friend of AIEngine (see AIEngine.h): the only sanctioned way for tests to reach the executor's
// private paths. ONE definition, shared by every test TU that needs it (a per-file copy with
// different members would be an ODR violation).
#pragma once

#include "ai/AIEngine.h"

#include <string>

struct MtgTestSeam
{
    static bool TapForCost(AIEngine& e, GameState& s, const ManaCost& c, ManaPool& avail,
                           bool for_creature)
    { return e.TapForCost(s, c, avail, for_creature); }

    // The executor's hand cast (pays, removes the card, pushes the StackEntry) -- exposed so a test
    // can read what the real executor carries on the stack entry for a given searched axis.
    static void CastSpellFromHand(AIEngine& e, GameState& s, Card& hand_card, ManaPool& avail,
                                  int chosen_x)
    { e.CastSpellFromHand(s, hand_card, avail, 0, std::string(), chosen_x); }
};
