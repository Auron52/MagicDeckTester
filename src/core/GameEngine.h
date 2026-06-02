#pragma once
#include "GameState.h"

class AIEngine;

class GameEngine
{
public:
    explicit GameEngine(AIEngine& ai);

    // Returns the turn number the active player won;
    // -1 if max turns exceeded or player lost on draw.
    int RunGame(GameState& state, int max_turns = 20);

private:
    AIEngine& m_ai;

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
