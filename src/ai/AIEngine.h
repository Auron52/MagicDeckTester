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
    // max_turns bounds the rollout horizon used by lookahead bottoming (see below).
    void HandleMulligan(GameState& state, int max_turns = 20);

    // Returns the card names of the hand kept after the most recent HandleMulligan call.
    const std::vector<std::string>& GetKeptOpeningHand() const { return m_kept_opening_hand; }

    // Called each main phase. Plays lands, casts spells, activates abilities.
    void TakeTurn(GameState& state, bool is_pre_combat_main);

    // Returns pointers to battlefield permanents that will attack this turn.
    std::vector<Permanent*> DeclareAttackers(GameState& state);

    // Returns a pointer to a card in the active player's hand to discard.
    Card* ChooseDiscard(GameState& state);

    void SetLogger(GameLogger* logger) { m_logger = logger; }

    // When enabled, bottoming evaluates each candidate removal with a full
    // clairvoyant game rollout and bottoms the card whose removal preserves the
    // earliest win (heuristic breaks win-turn ties). More accurate, ~2x slower;
    // off by default so the analyzer's scoring passes keep their speed.
    void SetLookaheadBottoming(bool enabled) { m_lookahead_bottoming = enabled; }

private:
    MulliganProfile          m_profile;
    int                      m_lookahead_depth   = 0;
    int                      m_timeout_ms        = 0;
    bool                     m_lookahead_bottoming = false;
    std::vector<std::string> m_kept_opening_hand;
    GameLogger*              m_logger            = nullptr;

    // --- Mulligan helpers ---
    bool KeepHand(const std::vector<Card>& hand, int mulligan_count) const;
    void BottomCards(GameState& state, int count, int max_turns);

    // Picks the index of the card the curve/castability heuristic would bottom,
    // considering only cards whose allowed[i] is non-zero. Returns -1 if none.
    int HeuristicBottomPick(const std::vector<Card>& hand,
                            const std::vector<char>& allowed) const;

    // Plays a full clairvoyant game from a (post-mulligan) trial state and returns
    // the win turn, or max_turns + 1 if no win. Suppresses logging during the rollout.
    int RolloutWinTurn(GameState trial, int max_turns);

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
