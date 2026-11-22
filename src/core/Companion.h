#pragma once

#include "GameState.h"
#include "../cards/CardDatabase.h"

// Kaheera is the only companion currently modeled. The selected sideboard copy is a
// one-shot outside-the-game object; moving it to hand consumes that copy.
inline bool KaheeraCompanionEligible(const std::vector<Card>& mainboard)
{
    for (const Card& c : mainboard)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d) { return false; }
        if (!d->card.IsCreature()) { continue; }
        bool allowed = false;
        for (const std::string& subtype : d->card.m_subtypes)
        {
            if (subtype == "Cat" || subtype == "Elemental" || subtype == "Nightmare"
                || subtype == "Dinosaur" || subtype == "Beast") { allowed = true; break; }
        }
        if (!allowed) { return false; }
    }
    return true;
}

inline bool CompanionInSideboard(const GameState& state, int player, int number)
{
    if (number != 300000) { return false; }
    for (const Card& c : state.players[player].sideboard)
    { if (c.m_number == number && c.m_name == "Kaheera, the Orphanguard") { return true; } }
    return false;
}

inline bool PutCompanionIntoHand(GameState& state, int player, int number)
{
    if (number != 300000) { return false; }
    std::vector<Card>& side = state.players[player].sideboard;
    for (auto it = side.begin(); it != side.end(); ++it)
    {
        if (it->m_number != number || it->m_name != "Kaheera, the Orphanguard") { continue; }
        state.players[player].hand.push_back(*it);
        side.erase(it);
        return true;
    }
    return false;
}
