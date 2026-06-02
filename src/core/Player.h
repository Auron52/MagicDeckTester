#pragma once
#include "Card.h"
#include "Library.h"
#include <vector>

struct Player {
    int life                    = 20;
    std::vector<Card> hand;
    Library           library;      // index 0 = top of library
    std::vector<Card> graveyard;
    int landsPlayedThisTurn     = 0;
    int bonusLandDropsThisTurn  = 0;  // one-time grants; reset in untap step
    int poisonCounters          = 0;

    bool hasLost() const { return life <= 0 || poisonCounters >= 10; }

    // Static land-drop effects (e.g. Exploration) are evaluated by GameEngine
    // against the battlefield; one-time triggered grants increment bonusLandDropsThisTurn.
    int landDropsAvailable() const { return 1 + bonusLandDropsThisTurn; }
};
