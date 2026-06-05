#pragma once
#include "../core/GameState.h"
#include "../core/GameLogger.h"
#include "../core/ManaPool.h"
#include "../cards/CardDatabase.h"
#include "MulliganProfile.h"
#include "TurnSolver.h"
#include <vector>

class AIEngine
{
public:
    // lookahead_depth: 0 = single-turn heuristic (fast, for the runner);
    //                  N = N-turn lookahead via game simulation (for the analyzer).
    // timeout_ms:      per-turn time budget for SolveWithLookahead in milliseconds.
    //                  0 = no timeout (evaluate all candidates).
    explicit AIEngine(MulliganProfile profile = MulliganProfile::DefaultProfile(),
                      int lookahead_depth = 0,
                      int timeout_ms = 0);

    // London mulligan: draw 7, keep or mulligan, bottom N cards on keep after N mulligans.
    void HandleMulligan(GameState& state);

    // Returns the card names of the hand kept after the most recent HandleMulligan call.
    const std::vector<std::string>& GetKeptOpeningHand() const { return m_kept_opening_hand; }

    // Called each main phase. Plays lands, casts spells, activates abilities.
    void TakeTurn(GameState& state, bool is_pre_combat_main);

    // Returns pointers to battlefield permanents that will attack this turn.
    std::vector<Permanent*> DeclareAttackers(GameState& state);

    // Returns a pointer to a card in the active player's hand to discard.
    Card* ChooseDiscard(GameState& state);

    void SetLogger(GameLogger* logger) { m_logger = logger; }

private:
    MulliganProfile          m_profile;
    int                      m_lookahead_depth   = 0;
    int                      m_timeout_ms        = 0;
    std::vector<std::string> m_kept_opening_hand;
    GameLogger*              m_logger            = nullptr;

    // --- Mulligan helpers ---
    bool KeepHand(const std::vector<Card>& hand, int mulligan_count) const;
    void BottomCards(GameState& state, int count);

    // --- Turn helpers ---

    // Play a land from hand if a land drop is available.
    bool TryPlayLand(GameState& state);

    // Animate untapped animatable lands (e.g. Mutavault) if mana is available.
    void AnimateLands(GameState& state, ManaPool& available);

    // Activate tap-and-pay token abilities (e.g. Sliver Hive) with spare mana.
    void ActivateTapTokens(GameState& state, ManaPool& available);

    // Build a ManaPool from all currently untapped mana sources the active player controls.
    ManaPool BuildAvailableMana(const GameState& state) const;

    // Tap permanents to pay the cost, updating the available pool in place.
    // for_creature: if false, skip creature-only mana sources (e.g. Ancient Ziggurat).
    // Returns false if the cost cannot be paid (leaves state unchanged on failure).
    bool TapForCost(GameState& state, const ManaCost& cost, ManaPool& available,
                    bool for_creature = true);

    // Remove a spell from hand, tap sources to pay, and push a StackEntry.
    void CastSpellFromHand(GameState& state, Card& hand_card, ManaPool& available);

    // Returns the battlefield index of the first creature the opponent controls, or -1.
    int FindOpponentCreature(const GameState& state) const;

    // Returns the mana cost to pay for this card this turn (spectacle cost if eligible).
    ManaCost EffectiveCost(const CardDefinition& def, const GameState& state) const;
};
