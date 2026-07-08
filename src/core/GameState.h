#pragma once
#include "Player.h"
#include "Permanent.h"
#include "ManaPool.h"
#include <array>
#include <vector>
#include <optional>
#include <cstdint>

// Shuffle-variance instrument: fold an independent `salt` into a base shuffle seed. salt==0 is the
// IDENTITY (returns base unchanged) so every default (un-instrumented) shuffle is byte-identical;
// a nonzero salt produces an independent, deterministic reshuffle of the same library. splitmix64
// finaliser over (base XOR salt*golden) so distinct salts decorrelate. Lives here (not SpellEffects)
// so the game-setup path (GoldFishRunner) and the mulligan reshuffle (AIEngine) can reach it too.
// See GameState::shuffle_salt.
inline uint64_t SaltSeed(uint64_t base, uint64_t salt)
{
    if (salt == 0) { return base; }
    uint64_t x = base ^ (salt * 0x9E3779B97F4A7C15ull);
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27; x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x;
}

// Clairvoyance-decoupling instrument (ANALYSIS ONLY): true while the engine is EVALUATING a line
// (inside SimulateToEnd / EnumerateEarliestWins / RolloutWinTurn), so ShuffleAfterSearch picks
// GameState::shuffle_salt_search; false during the one real committed application, which uses
// GameState::shuffle_salt. Thread-local (worker threads each play one game). Defaults false and the
// two salts default equal, so normal play is byte-identical. RAII guard restores on scope exit
// (nested guards keep it true through recursion). See GameState::shuffle_salt_search.
inline thread_local bool g_shuffle_eval = false;
struct ShuffleEvalGuard
{
    bool prev;
    explicit ShuffleEvalGuard(bool v) : prev(g_shuffle_eval) { g_shuffle_eval = v; }
    ~ShuffleEvalGuard() { g_shuffle_eval = prev; }
};

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

// Per-deck learned mid-game PLAY evaluator (defined in ai/KeepModel.h); GameState carries a
// non-owning pointer, threaded like m_provider. Forward-declared to keep the AI layer out of core.
struct MidGameEvaluator;

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
    std::optional<int>  soulfire_own_targets;  // Soulfire Eruption: searched # of own creatures
                                               // added as extra targets (deeper dig). EffectHandler
                                               // passes it to SoulfireDig so the executor's dig
                                               // matches the rollout's (lockstep). Unset elsewhere.
    std::optional<int>  crackle_targets;       // Crackle with Power: searched # of extra beneficial
                                               // targets beyond the opp face (creatures/self) whose
                                               // {1}-each Hinata discount was taken; the cast deals 5X
                                               // to each and kills the lethal ones (SBA). EffectHandler
                                               // + ApplyPlanDirect resolve it in lockstep. Unset else.
    std::optional<int>  ponder_keep;           // Ponder cast_reorder: searched keep(1)-vs-shuffle(0)
                                               // call. ResolveDrawSpell passes it to
                                               // ReorderTopOrShuffle so the executor matches the
                                               // rollout (lockstep). Unset for non-reorder spells.
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
    // Turn-scoped RESERVE mana: mana produced by a ritual (Reality Spasm untap-retap, Irencrag
    // Feat) that has not yet been spent. Payment (TapForCost / TapForCostDirect) drains this
    // BEFORE tapping any permanent, so a ritual cast earlier in a turn funds a bigger X-spell
    // later the same turn (the Hinata combo). Empty for every non-ritual deck and reset at the
    // start of each turn's planning/execution -> byte-identical when nothing fills it. NEVER
    // folded into BuildSimKey (it is empty at every cross-turn decision point).
    ManaPool                 floating_mana;
    uint64_t                 game_seed             = 0;   // seed used to set up this game; used for mulligan reshuffles
    uint64_t                 search_count          = 0;   // # library SEARCHES (fetch/tutor) this game; seeds the
                                                          // deterministic mid-game shuffle (ShuffleAfterSearch).
                                                          // Copied with state so the search rollout reproduces the
                                                          // same shuffle the executor will -> lockstep. Inert (always
                                                          // 0) unless MTG_SEARCH_SHUFFLE is set.
    // Shuffle-variance instrument (SaltSeed): an INDEPENDENT salt folded into the deterministic
    // shuffle seeds so the SAME game (fixed game_seed / decisions) can be replayed with different
    // shuffle REALISATIONS. shuffle_salt salts MID-GAME shuffles only (SearchShuffleSeed / Gamble),
    // holding the opening fixed; shuffle_salt_opening additionally salts the initial deck shuffle +
    // mulligan reshuffles (vary the opening too). Both DEFAULT 0 -> SaltSeed is the identity ->
    // byte-identical to the un-instrumented engine. Copied with state so rollout+executor use the
    // same salt -> lockstep/commit-the-line preserved WITHIN each realisation (each salt is an
    // ordinary deterministic-seeded game). Set once per game from MTG_SHUFFLE_SALT[_OPENING].
    uint64_t                 shuffle_salt          = 0;
    uint64_t                 shuffle_salt_opening  = 0;
    // Clairvoyance-decoupling instrument (ANALYSIS ONLY, opt-in MTG_SHUFFLE_SALT_SEARCH): the salt
    // the SEARCH/rollout evaluation uses for its mid-game shuffles, which may DIFFER from shuffle_salt
    // (the salt the real executor resolves). When they differ, the clairvoyant search plans against a
    // reshuffle the real game will NOT deal -- so a decision that only wins because the search foresaw
    // a specific reshuffle (a clairvoyance artifact) collapses, while a decision good on its features
    // (a sound heuristic) survives. Which salt is used is selected per-shuffle by the thread-local
    // g_shuffle_eval flag (true inside SimulateToEnd / EnumerateEarliestWins / RolloutWinTurn). Defaults
    // EQUAL to shuffle_salt (set at game setup) -> byte-identical / lockstep intact for normal play.
    uint64_t                 shuffle_salt_search   = 0;
    // Non-owning pointer to this game's passive opponent-spawn schedule (creatures to place on
    // the opp side at scheduled turns). Read-only after setup but copied into every search node;
    // a pointer (like m_provider) drops the per-node vector copy. Owner is the program-lifetime
    // PATTERNS table in GoldFishRunner (PopulateOpponentSpawns). nullptr -> no spawns.
    const std::vector<OpponentSpawn>* opponent_spawns = nullptr;
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
    // Non-owning pointer to the deck's learned mid-game PLAY evaluator (MulliganProfile::eval_model),
    // stamped in AIEngine::HandleMulligan and propagated through every deep copy. Ranks NON-lethal
    // turn-plans in TurnSolver::Solve (the d0 decision + every rollout leaf) when MTG_EVAL_MODEL is
    // set; nullptr / empty / flag-off -> the heuristic EvalCard ranking (byte-identical). NEVER folded
    // into BuildSimKey (a per-deck constant, like m_provider). Mid-game play only -- not mulligan.
    const MidGameEvaluator* m_evaluator = nullptr;

    Player&       ActivePlayer()       { return players[active_player_index]; }
    const Player& ActivePlayer() const { return players[active_player_index]; }
    Player&       Opponent()           { return players[1 - active_player_index]; }
    const Player& Opponent()     const { return players[1 - active_player_index]; }
};
