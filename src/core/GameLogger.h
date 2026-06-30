#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

// Records a single game's events in the structured format defined by the AI skill.
// One GameLogger instance per game; GoldFishRunner owns the lifecycle.
//
// Call sequence per game:
//   StartGame(...)
//   for each main phase / combat phase:
//     StartPhase(turn, "MAIN_1" | "COMBAT" | "MAIN_2")
//     Log*(...) for each action
//     CommitPhase(board state)
//   EndGame(win_turn)
//   WriteToFile(path)
class GameLogger
{
public:
    void StartGame(const std::string& run_id, int game_number,
                   const std::string& deck_id, uint64_t seed,
                   const std::map<std::string, std::vector<int>>& card_numbering);

    void StartPhase(int turn, const std::string& phase);

    // Called once per hand drawn during the mulligan process.
    // attempt=0 is the initial 7-card hand; each mulligan increments by 1.
    void LogMulliganAttempt(int attempt,
                            const std::vector<int>& hand_nums,
                            const std::vector<std::string>& hand_names,
                            bool kept);

    // Called once per card bottomed after a keep. Appends to the last attempt.
    void LogBottomed(int card_num, const std::string& card_name);

    void LogOpeningHand(const std::vector<int>& card_nums,
                        const std::vector<std::string>& card_names);

    // A resolved spell/ability target, captured at cast time (permanent indices shift as the
    // battlefield changes, so we record stable card identity + controller instead of an index).
    struct TargetDesc
    {
        std::string kind;        // "player" | "permanent"
        std::string who;         // "you" | "opponent"  (the player, or the permanent's controller)
        int         card_num = 0;     // permanent only
        std::string card_name;        // permanent only
    };

    // A counter (or counter-like badge) on a permanent, surfaced for the viewer: depletion on
    // Saprazzan Skerry, charge on Aether Vial, verse on Aria of Flame, +1/+1, etc. kind is a short
    // human label; count is the number shown.
    struct CounterInfo
    {
        std::string kind;
        int         count = 0;
    };

    // One battlefield permanent. card_name is carried explicitly so TOKENS (Forbidden Orchard
    // spirits, scheduled opponent spawns, Magma/replicate tokens) -- which have no deck card
    // number -- are still nameable in the viewer; deck cards also resolve via cardNumbering.
    // is_land is the REAL card type (from the permanent's card), so the viewer no longer guesses
    // land-ness from the name (which mis-zoned nonbasic lands and creatures like Monastery Swiftspear).
    struct PermSnapshot
    {
        int                      card_num = 0;
        std::string              card_name;
        bool                     tapped   = false;
        bool                     is_land  = false;
        std::vector<CounterInfo> counters;
    };

    void LogPlayLand(int card_num, const std::string& card_name);
    // chosen_x: the resolved X for {X} spells (Crackle / Reality Spasm); -1 if the
    // spell has no {X} in its cost (so the viewer only shows "X=N" when meaningful).
    // targets: who/what the spell points at (Crackle -> opponent face, removal -> a creature).
    void LogCastSpell(int card_num, const std::string& card_name,
                      const std::string& mana_paid, int chosen_x = -1,
                      const std::vector<TargetDesc>& targets = {});
    void LogDraw(int card_num, const std::string& card_name);
    void LogDiscard(int card_num, const std::string& card_name);
    void LogAttack(int damage, int opp_life_after);

    // A scry/dig/look-at-top reveal (Ponder, Preordain, Scry, etc.): the cards seen
    // at the top of the library and what happened to each. kept/bottomed are subsets
    // of looked_at by card number.
    // dispositions (optional): a human label per looked_at card describing where it went / what it
    // did -- used by Soulfire Eruption to show which exiled card hit which target ("-> opponent face
    // (9)"). Parallel to looked_at_*; empty => no per-card disposition shown.
    void LogReveal(const std::string& source_name,
                   const std::vector<int>& looked_at_nums,
                   const std::vector<std::string>& looked_at_names,
                   const std::vector<int>& kept_nums,
                   const std::vector<int>& bottomed_nums,
                   const std::vector<std::string>& dispositions = {});

    // An activated ability firing (mana tap, sac, pay-life, discard-cost, etc.).
    // For mana abilities the board `tapped` rotation already conveys the visible
    // result; this records the activation event for non-tap abilities and audit.
    void LogAbility(int source_card_num, const std::string& source_card_name,
                    const std::string& ability);

    // Captures board state snapshot at the end of the current phase. `opp_battlefield` is the
    // OPPONENT's side (Forbidden Orchard tokens, scheduled spawns) so targets are visible.
    void CommitPhase(int player_life, int opp_life,
                     const std::vector<PermSnapshot>& battlefield,
                     const std::vector<int>& hand,
                     const std::vector<PermSnapshot>& opp_battlefield = {},
                     const std::vector<int>& graveyard = {},
                     const std::vector<int>& staged = {});

    // Returns true if a phase was started but not yet committed.
    bool InPhase() const { return m_in_phase; }

    // win_turn: turn the opponent reached 0 life; -1 if the game was not won.
    void EndGame(int win_turn);

    void WriteToFile(const std::filesystem::path& path) const;

private:
    struct MulliganAttempt
    {
        int                      attempt = 0;
        std::vector<int>         hand_nums;
        std::vector<std::string> hand_names;
        bool                     kept    = false;
        std::vector<int>         bottomed_nums;
        std::vector<std::string> bottomed_names;
    };

    struct Action
    {
        std::string type;
        int         card_num  = 0;
        std::string card_name;
        std::string mana_paid;
        int         chosen_x  = -1;
        std::vector<TargetDesc> targets;
        int         damage    = 0;
        int         opp_life  = 0;
        // REVEAL: cards looked at and their disposition.
        std::vector<int>         looked_at;
        std::vector<std::string> looked_at_names;
        std::vector<int>         kept;
        std::vector<int>         bottomed;
        std::vector<std::string> dispositions;   // REVEAL: per-card destination label (optional)
        // ABILITY: the ability description (source recorded in card_num/card_name).
        std::string ability;
    };

    struct PhaseEntry
    {
        int                 turn = 0;
        std::string         phase;
        std::vector<Action> actions;
        int                 player_life = 0;
        int                 opp_life    = 0;
        std::vector<PermSnapshot> battlefield;
        std::vector<PermSnapshot> opp_battlefield;
        std::vector<int>          hand;
        std::vector<int>          graveyard;
        std::vector<int>          staged;   // hand cards exiled-but-playable (Light Up / Soulfire dig)
    };

    std::string                             m_run_id;
    int                                     m_game_number = 0;
    std::string                             m_deck_id;
    uint64_t                                m_seed        = 0;
    std::map<std::string, std::vector<int>> m_numbering;
    std::vector<MulliganAttempt>            m_mulligan_sequence;
    std::vector<int>                        m_opening_hand_nums;
    std::vector<std::string>                m_opening_hand_names;
    std::vector<PhaseEntry>                 m_phases;
    PhaseEntry                              m_current;
    bool                                    m_in_phase    = false;
    int                                     m_win_turn    = -1;
};

// Thread-local logger that captures scry/dig reveals during REAL resolution. It is set
// for the duration of a logged game and PAUSED (nulled) while the search/rollout runs on
// copied states (see RevealLogPause), so only the actual game's reveals are recorded.
// Logging never mutates GameState, so this cannot affect the simulation in any way.
extern thread_local GameLogger* g_reveal_logger;

// ---- Human-play "look at the top N" resolution chooser ---------------------------------
// Scry / Surveil / Ponder-style reorder all resolve a "look at the top N, decide their
// disposition" sub-decision. Autonomously the provider heuristic (ScryKeepOnTop / KeepReorderTop)
// decides; under --claude-play the human does. This optional chooser is the hook: when set,
// ScryTop/SurveilTop/ReorderTopOrShuffle ask it instead of the heuristic. RevealLogPause nulls it
// for the duration of every search/rollout "thinking" scope, so it fires ONLY during REAL
// resolution (which is never paused) -- making the search byte-identical by construction.
struct GameState;
struct Card;
enum class LookKind { Scry, Reorder, Surveil };

// The player's chosen disposition of the looked-at top cards. `top_order` lists indices into
// the looked-at vector (look order) to place back on top, FIRST = nearest the top (drawn
// first). Indices NOT listed go elsewhere: bottom of library (Scry), graveyard (Surveil), or
// are shuffled into the library (Reorder, when `shuffle` is set -- then top_order is ignored).
struct TopDisposition
{
    std::vector<int> top_order;
    bool             shuffle = false;
};

using TopChooser = std::function<TopDisposition(const GameState& state, const std::string& source,
                                                const std::vector<Card>& looked, LookKind kind)>;

// Set for the duration of a --claude-play game; nullptr (and so inert) otherwise.
extern thread_local TopChooser* g_play_top_chooser;

// RAII: null g_reveal_logger AND g_play_top_chooser for the current scope. Placed at the top of
// every search / rollout "thinking" function so planning-time scry/dig calls are neither logged
// nor handed to the human chooser; restores the previous values on exit (so nested scopes compose).
struct RevealLogPause
{
    GameLogger* saved;
    TopChooser* saved_chooser;
    RevealLogPause() : saved(g_reveal_logger), saved_chooser(g_play_top_chooser)
    { g_reveal_logger = nullptr; g_play_top_chooser = nullptr; }
    ~RevealLogPause() { g_reveal_logger = saved; g_play_top_chooser = saved_chooser; }
    RevealLogPause(const RevealLogPause&)            = delete;
    RevealLogPause& operator=(const RevealLogPause&) = delete;
};
