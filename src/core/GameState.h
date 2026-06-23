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

// Per-deck heuristic provider (decision logic lives in ai/DecisionProvider.h); GameState
// carries a non-owning pointer so the search's deep copies all see it. Forward-declared
// here to avoid pulling the AI layer into this core header.
class DecisionProvider;

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
    uint64_t                 search_count          = 0;   // # library SEARCHES (fetch/tutor) this game; seeds the
                                                          // deterministic mid-game shuffle (ShuffleAfterSearch).
                                                          // Copied with state so the search rollout reproduces the
                                                          // same shuffle the executor will -> lockstep. Inert (always
                                                          // 0) unless MTG_SEARCH_SHUFFLE is set.
    std::vector<OpponentSpawn> opponent_spawns;           // passive creatures to place on opp side each turn
    int                      vial_target_mv        = 0;   // most common creature MV in the deck; Aether Vial stops here
    bool                     on_the_play           = false; // if true, skip the turn-1 draw step (player is on the play)
    // Non-owning pointer to the deck's decision heuristics (set in GoldFishRunner::SetupGame,
    // propagated through every deep copy). Never folded into BuildSimKey. nullptr -> callers
    // use DefaultProvider() (see DecisionProviders.h), so a raw GameState stays valid.
    const DecisionProvider*  m_provider            = nullptr;
    // Non-owning pointer to the deck's required combo pieces (MulliganProfile::required_pieces),
    // set in AIEngine::HandleMulligan and propagated through every deep copy. Used by the shared
    // cleanup-discard selector (SelectCleanupDiscardIndex) so the search rollout protects the same
    // pieces the real engine's ChooseDiscard does -- without it the rollout shed high-MV spells and
    // hoarded lands, over-counting a Land's Edge flood the real game never accumulates (gi=220).
    // nullptr -> no protection (matches a raw GameState with no profile attached).
    const std::vector<std::string>* m_required_pieces = nullptr;

    Player&       ActivePlayer()       { return players[active_player_index]; }
    const Player& ActivePlayer() const { return players[active_player_index]; }
    Player&       Opponent()           { return players[1 - active_player_index]; }
    const Player& Opponent()     const { return players[1 - active_player_index]; }
};
