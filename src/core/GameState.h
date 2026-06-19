#pragma once
#include "Player.h"
#include "Permanent.h"
#include <array>
#include <vector>
#include <optional>

// A passive creature that enters the opponent's battlefield at a scheduled turn.
// Used to provide creature targets for spells like Searing Blood in goldfishing.
struct OpponentSpawn
{
    int turn;
    int power;
    int toughness;
};

enum class Phase { Beginning, PreCombatMain, Combat, PostCombatMain, Ending };
enum class Step  { Untap, Upkeep, Draw, MainPhase,
                   BeginCombat, DeclareAttackers, DeclareBlockers, CombatDamage, EndCombat,
                   End, Cleanup };

struct Target
{
    enum class Type { Player, Permanent };
    Type type;
    int  player_index    = -1;
    int  permanent_index = -1;
};

struct StackEntry
{
    enum class EntryType { Spell, Triggered, Activated };
    EntryType           type;
    Card                source;
    int                 controller_index = 0;
    std::vector<Target> targets;
    std::optional<int>  chosen_x;
    std::string         tutor_target;   // for a tutor spell: the specific library card to fetch
                                        // (searched choice). Empty -> PerformTutor uses the
                                        // heuristic's top pick.
    // Resolve dispatch is added in Phase 1.2 when CardDatabase provides ability implementations.
};

struct GameState
{
    std::array<Player, 2>    players;
    int                      active_player_index   = 0;
    int                      priority_player_index = 0;
    Phase                    phase                 = Phase::Beginning;
    Step                     step                  = Step::Untap;
    std::vector<StackEntry>  stack;
    std::vector<Permanent>   battlefield;
    std::vector<Card>        exile;
    int                      consecutive_passes           = 0;
    int                      turn_number                  = 0;
    bool                     player_lost_on_draw          = false;
    bool                     opponent_lost_life_this_turn = false;
    uint64_t                 game_seed             = 0;   // seed used to set up this game; used for mulligan reshuffles
    std::vector<OpponentSpawn> opponent_spawns;           // passive creatures to place on opp side each turn
    int                      vial_target_mv        = 0;   // most common creature MV in the deck; Aether Vial stops here
    bool                     on_the_play           = false; // if true, skip the turn-1 draw step (player is on the play)

    Player&       ActivePlayer()       { return players[active_player_index]; }
    const Player& ActivePlayer() const { return players[active_player_index]; }
    Player&       Opponent()           { return players[1 - active_player_index]; }
    const Player& Opponent()     const { return players[1 - active_player_index]; }
};
