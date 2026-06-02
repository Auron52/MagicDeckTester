#pragma once
#include "GameState.h"

class AIEngine;

class GameEngine
{
public:
    explicit GameEngine(AIEngine& ai);

    // Returns the turn number the active player won;
    // -1 if max turns exceeded or player lost on draw.
    int runGame(GameState& state, int maxTurns = 20);

private:
    AIEngine& ai_;

    void runTurn(GameState& state);
    void untapStep(GameState& state);
    void upkeepStep(GameState& state);
    void drawStep(GameState& state);
    void mainPhase(GameState& state, bool isPreCombat);
    void combatPhase(GameState& state);
    void endStep(GameState& state);
    void cleanupStep(GameState& state);

    void resolveStack(GameState& state);
    void checkStateBasedActions(GameState& state);

    bool checkWinCondition(const GameState& state) const;
};
