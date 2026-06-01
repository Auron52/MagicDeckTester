#pragma once
#include "Player.h"
#include "Permanent.h"
#include <array>
#include <vector>
#include <optional>

enum class Phase { Beginning, PreCombatMain, Combat, PostCombatMain, Ending };
enum class Step  { Untap, Upkeep, Draw, MainPhase,
                   BeginCombat, DeclareAttackers, DeclareBlockers, CombatDamage, EndCombat,
                   End, Cleanup };

struct Target {
    enum class Type { Player, Permanent };
    Type type;
    int  playerIndex    = -1;
    int  permanentIndex = -1;
};

struct StackEntry {
    enum class EntryType { Spell, Triggered, Activated };
    EntryType           type;
    Card                source;
    int                 controllerIndex = 0;
    std::vector<Target> targets;
    std::optional<int>  chosenX;
    // Resolve dispatch is added in Phase 1.2 when CardDatabase provides ability implementations.
};

struct GameState {
    std::array<Player, 2>    players;
    int                      activePlayerIndex   = 0;
    int                      priorityPlayerIndex = 0;
    Phase                    phase               = Phase::Beginning;
    Step                     step                = Step::Untap;
    std::vector<StackEntry>  stack;
    std::vector<Permanent>   battlefield;
    std::vector<Card>        exile;
    int                      consecutivePasses   = 0;
    int                      turnNumber          = 0;
    bool                     playerLostOnDraw    = false;

    Player&       activePlayer()       { return players[activePlayerIndex]; }
    const Player& activePlayer() const { return players[activePlayerIndex]; }
    Player&       opponent()           { return players[1 - activePlayerIndex]; }
    const Player& opponent()     const { return players[1 - activePlayerIndex]; }
};
