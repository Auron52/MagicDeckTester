#pragma once
#include "GameState.h"
#include "GameLogger.h"

class AIEngine;

class GameEngine
{
public:
    explicit GameEngine(AIEngine& ai);

    // A game the per-game work meter VOIDED (see ai/GameWorkMeter.h). Distinct from -1, which is a
    // real result meaning "no win within max_turns": an abandoned game has NO result and must be
    // excluded from every average rather than folded in as a loss. Callers that never arm the meter
    // can never see it.
    static constexpr int kAbandoned = -2;

    // Returns the turn number the active player won;
    // -1 if max turns exceeded or player lost on draw;
    // kAbandoned if the per-game work ceiling was hit (only possible when armed).
    int RunGame(GameState& state, int max_turns = 20);

    // Plays turns from the current state to a win or max_turns WITHOUT running a
    // mulligan first. Used both by RunGame (after mulligan) and by lookahead
    // bottoming, which rolls out candidate hands and must not re-enter mulligan.
    int PlayOut(GameState& state, int max_turns = 20);

    // Where in the CURRENT turn a resumed playout picks up. PlayOut always begins a FRESH turn
    // (RunTurn opens with ++turn_number), which is right for a state captured between turns and
    // WRONG for one captured mid-turn: the rest of that turn is silently skipped. Any decision
    // taken part-way through a turn (the upkeep Vial charge, a cleanup discard) that wants to be
    // evaluated by playing the game out must resume at its own step instead, or it scores a state
    // the real game can never reach. See PlayOutFrom / searched-cleanup-discard.md, where exactly
    // that skip made an untrimmed 18-card hand into ~20 points of phantom Land's Edge damage.
    // UpkeepTail is the resume point for the Aether Vial charge: the charge decision is taken
    // part-way through UpkeepStep, so a trial resuming at Draw would silently skip the REST of the
    // upkeep (the upkeep token creation -- slivers' Thrumming Hivepool -- and the closing
    // ResolveStack). Same failure the Cleanup entry exists to prevent, one step earlier.
    enum class ResumeAt { NewTurn = 0, UpkeepTail, Draw, Main1, Combat, Main2, End, Cleanup };

    // PlayOut, but finishing the CURRENT turn from `from` first. `from == NewTurn` is exactly
    // PlayOut. The resumed turn runs the same steps in the same order the real turn would.
    int PlayOutFrom(GameState& state, int max_turns, ResumeAt from);

    void SetLogger(GameLogger* logger);

private:
    AIEngine&    m_ai;
    GameLogger*  m_logger = nullptr;

    void RunTurn(GameState& state);
    void RunTurnFrom(GameState& state, ResumeAt from);
    void UntapStep(GameState& state);
    void UpkeepStep(GameState& state);
    // The part of the upkeep AFTER the Aether Vial charge loop (storage hold, upkeep tokens,
    // ResolveStack). Split out so a trial launched from inside the charge decision can finish the
    // step it interrupted -- see ResumeAt::UpkeepTail.
    void UpkeepTail(GameState& state);
    void DrawStep(GameState& state);
    void MainPhase(GameState& state, bool is_pre_combat);
    void CombatPhase(GameState& state);
    void EndStep(GameState& state);
    void CleanupStep(GameState& state);

    void ResolveStack(GameState& state);
    void CheckStateBasedActions(GameState& state);

    bool CheckWinCondition(const GameState& state) const;
};
