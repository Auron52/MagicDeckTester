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

// A card exiled with time counters by SUSPEND (Lotus Bloom: "Suspend 3-{0}"). Unlike a StagedCard
// it is NOT playable while it waits; at each of the controller's upkeeps a time counter is removed,
// and when the last is removed the card is CAST off suspend (arrive_turn = suspend_turn + counters).
// See CardParams::suspend_time_counters, GameEngine::UpkeepStep / SimulateEndAndStartNextTurn (the
// arrival), and CastOffSuspend (the shared free-cast site).
struct SuspendedCard
{
    Card card;
    int  arrive_turn = 0; // the upkeep turn on which the last time counter is removed -> cast off suspend
};

struct Player
{
    int life                       = 20;
    std::vector<Card> hand;
    Library           library;      // index 0 = top of library
    std::vector<Card> graveyard;
    int lands_played_this_turn     = 0;
    int bonus_land_drops_this_turn = 0;  // one-time grants; reset in untap step
    // Cards DRAWN this turn (CR 121 draws only: the draw step + spell/ability draws -- NOT
    // reveal-and-put-in-hand (Treasure Hunt), staged/impulse exiles, tutors, or the opening hand).
    // Read only by Fists of Flame's pump_per_cards_drawn_power payload; incremented at the real
    // draw sites (GameEngine::DrawStep, the rollout's start-of-turn draw, both worlds' draw-spell /
    // cast_draw / dig-draw resolvers, the shared trick draw). Reset at BOTH untap sites in
    // lockstep; folded into the sim key. 0 forever for decks with no drawn-count payload.
    int cards_drawn_this_turn = 0;
    int poison_counters            = 0;

    // Cards exiled by "Light Up the Stage" and similar; playable until their expiry turn.
    std::vector<StagedCard> staged_cards;

    // Cards exiled by SUSPEND (Lotus Bloom); each is cast off suspend at arrive_turn's upkeep.
    // Empty for every deck without a suspend card -> byte-identical.
    std::vector<SuspendedCard> suspended_cards;

    bool HasLost() const { return life <= 0 || poison_counters >= 10; }

    // Static land-drop effects (e.g. Exploration) are evaluated by GameEngine
    // against the battlefield; one-time triggered grants increment bonus_land_drops_this_turn.
    int LandDropsAvailable() const { return 1 + bonus_land_drops_this_turn; }
};
