#pragma once
#include "../core/GameState.h"
#include "../core/ManaPool.h"
#include "../cards/CardDatabase.h"
#include "MulliganProfile.h"
#include <vector>

class AIEngine
{
public:
    explicit AIEngine(MulliganProfile profile = MulliganProfile::DefaultProfile());

    // London mulligan: draw 7, keep or mulligan, bottom N cards on keep after N mulligans.
    void HandleMulligan(GameState& state);

    // Called each main phase. Plays lands, casts spells, activates abilities.
    void TakeTurn(GameState& state, bool is_pre_combat_main);

    // Returns pointers to battlefield permanents that will attack this turn.
    std::vector<Permanent*> DeclareAttackers(GameState& state);

    // Returns a pointer to a card in the active player's hand to discard.
    Card* ChooseDiscard(GameState& state);

private:
    MulliganProfile m_profile;

    // --- Mulligan helpers ---
    bool KeepHand(const std::vector<Card>& hand, int mulligan_count) const;
    void BottomCards(GameState& state, int count);

    // --- Turn helpers ---

    // Play a land from hand if a land drop is available.
    bool TryPlayLand(GameState& state);

    // Build a ManaPool from all currently untapped mana sources the active player controls.
    ManaPool BuildAvailableMana(const GameState& state) const;

    // Find the best castable spell given available mana, or nullptr if nothing is castable.
    Card* PickBestCastable(GameState& state, const ManaPool& available,
                           bool is_pre_combat_main) const;

    // Tap permanents to pay the cost, updating the available pool in place.
    // Returns false if the cost cannot be paid (leaves state unchanged on failure).
    bool TapForCost(GameState& state, const ManaCost& cost, ManaPool& available);

    // Remove a spell from hand, tap sources to pay, and push a StackEntry.
    void CastSpellFromHand(GameState& state, Card& hand_card, ManaPool& available);

    // Returns true if dealing extra_damage to the opponent wins the game this turn
    // when combined with pending attackers already declared.
    bool WinsThisTurn(const GameState& state, int extra_damage) const;

    // Returns true if there is at least one legal target for the given targeting type.
    bool HasLegalTarget(const GameState& state, Targeting targeting) const;

    // Returns the battlefield index of the first creature the opponent controls, or -1.
    int FindOpponentCreature(const GameState& state) const;
};
