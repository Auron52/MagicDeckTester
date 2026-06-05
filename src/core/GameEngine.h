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

    void SetLogger(GameLogger* logger);

private:
    AIEngine&    m_ai;
    GameLogger*  m_logger = nullptr;

    void RunTurn(GameState& state);
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
