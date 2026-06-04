#pragma once
#include "Card.h"
#include "Library.h"
#include <vector>

// A card exiled by Light Up the Stage (or similar) that can be played until expiry_turn.
struct StagedCard
{
    Card card;
    int  expiry_turn = 0; // last turn on which this card may be played
};

struct Player
{
    int life                       = 20;
    std::vector<Card> hand;
    Library           library;      // index 0 = top of library
    std::vector<Card> graveyard;
    int lands_played_this_turn     = 0;
    int bonus_land_drops_this_turn = 0;  // one-time grants; reset in untap step
    int poison_counters            = 0;

    // Cards exiled by "Light Up the Stage" and similar; playable until their expiry turn.
    std::vector<StagedCard> staged_cards;

    bool HasLost() const { return life <= 0 || poison_counters >= 10; }

    // Static land-drop effects (e.g. Exploration) are evaluated by GameEngine
    // against the battlefield; one-time triggered grants increment bonus_land_drops_this_turn.
    int LandDropsAvailable() const { return 1 + bonus_land_drops_this_turn; }
};
