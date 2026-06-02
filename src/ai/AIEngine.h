#pragma once
#include "../core/GameState.h"
#include "MulliganProfile.h"
#include <vector>

class AIEngine
{
public:
    explicit AIEngine(MulliganProfile profile = MulliganProfile::defaultProfile());

    // London mulligan: draw 7, keep or mulligan, bottom N cards on keep after N mulligans.
    void handleMulligan(GameState& state);

    // Called each main phase. Plays lands, casts spells, activates abilities.
    // No-op until CardDatabase is wired up in Phase 1.2.
    void takeTurn(GameState& state, bool isPreCombatMain);

    // Returns pointers to battlefield permanents that will attack this turn.
    std::vector<Permanent*> declareAttackers(GameState& state);

    // Returns a pointer to a card in the active player's hand to discard.
    Card* chooseDiscard(GameState& state);

private:
    MulliganProfile m_profile;

    bool keepHand(const std::vector<Card>& hand, int mulliganCount) const;
    void bottomCards(GameState& state, int count);
};
