#pragma once
#include "GameState.h"
#include "GameLogger.h"

class AIEngine;

class GameEngine
{
public:
    explicit GameEngine(AIEngine& ai);

    // Returns the turn number the active player won;
    // -1 if max turns exceeded or player lost on draw.
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
    enum class ResumeAt { NewTurn = 0, Draw, Main1, Combat, Main2, End, Cleanup };

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
    void DrawStep(GameState& state);
    void MainPhase(GameState& state, bool is_pre_combat);
    void CombatPhase(GameState& state);
    void EndStep(GameState& state);
    void CleanupStep(GameState& state);

    void ResolveStack(GameState& state);
    void CheckStateBasedActions(GameState& state);

    bool CheckWinCondition(const GameState& state) const;
};
