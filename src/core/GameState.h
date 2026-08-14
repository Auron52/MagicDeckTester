#pragma once
#include "Player.h"
#include "Permanent.h"
#include "ManaPool.h"
#include "DiscardPolicy.h"
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

// Honest-teacher label instrument (ANALYSIS ONLY): true while generating a full-strength
// non-clairvoyant TEACHER label -- a depth>0 rollout continuation whose per-turn lookahead is
// DECOUPLED from the real draw order. g_shuffle_eval only decouples mid-game shuffle EVENTS
// (SearchShuffleSeed / Gamble); the BASE draw order (opening shuffle) has no such event, so a
// depth>0 rollout continuation would otherwise read the real future (clairvoyant). When this is
// set, SimulateToEndImpl reshuffles the unseen library into a random future BEFORE each turn's
// SolveWithLookahead (the plan is chosen against that random future, then RESOLVED against the true
// order) -- so the continuation is a genuine "mental sim under uncertainty", not a clairvoyant peek.
// Averaged over K by the outer dump loop (which varies GameState::shuffle_salt_search per k, folded
// into the per-turn reshuffle salt). Defaults false -> byte-identical to normal play. RAII guard.
inline thread_local bool g_honest_teacher = false;
struct HonestTeacherGuard
{
    bool prev;
    explicit HonestTeacherGuard(bool v) : prev(g_honest_teacher) { g_honest_teacher = v; }
    ~HonestTeacherGuard() { g_honest_teacher = prev; }
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
    std::optional<int>  splice_count;          // Desperate Ritual "Splice onto Arcane": the searched #
                                               // of OTHER copies spliced onto this base cast. The
                                               // executor stamps it (CastSpellFromHand) so EffectHandler's
                                               // ApplyRitualFloat floats (splice_count+1)*{R}{R}{R},
                                               // matching the cost CastSpellFromHand already scaled by
                                               // (k+1) and the rollout's apply_one (lockstep). Unset
                                               // (== 0 splices) for every non-splice spell.
    std::string         tutor_target;   // for a tutor spell: the specific library card to fetch
                                        // (searched choice). Empty -> PerformTutor uses the
                                        // heuristic's top pick.
    std::string         chosen_float_color;        // Apex of Power (+ Lotus SacForMana on its own Action):
                                        // the searched single colour whose N mana is floated on resolution
                                        // (AddChosenColorFloat). Empty -> wild / no colour choice. Carried
                                        // from Action::chosen_float_color at the cast site (CastSpellFromHand).
    bool                cast_from_hand = true;      // Apex of Power gate: was this spell cast FROM HAND
                                        // (vs off another Apex's staged exile)? Stamped = !hand_card.m_is_staged
                                        // at the cast site; Apex adds its 10-of-one-colour float ONLY when
                                        // true. True/unused for every non-impulse spell.
    int                 enchant_target = 0;         // Aura cast: the card.m_number of the creature this Aura
                                        // enters attached to (a SEARCH decision -- one plan variant per legal
                                        // creature). Set at the cast site from Action::enchant_target; read at
                                        // resolution (EffectHandler default case) to set the aura permanent's
                                        // aura_attached_to. 0 => not an aura / heuristic fallback pick.
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
    // Per-copy id for TOKENS (Stage 6 directive, analysis-Mirrorwing Dragon.md): deck cards carry
    // 1..60 from deck setup; tokens draw unique ids from this counter (base 1000 keeps provenance
    // obvious) at every shared creation helper. Identity is what lets a token be a trick TARGET
    // (enchant_target rides m_number), a distinct sac source (multiple Treasures crackable in one
    // plan -- the old shared id 0 collapsed them to one), and an addressable viewer choice. The
    // counter lives on the state so search branches fork it: the same line assigns the same ids in
    // the rollout and the executor (lockstep by construction). Opponent pseudo-spawns stay id 0
    // (never targeted, never our sac sources).
    int                      next_token_number     = 1000;
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
    // STORM counter: number of spells cast THIS TURN (by the active player -- the only caster in
    // the goldfish). Incremented by exactly 1 at every shared cast site (AIEngine::CastSpellFromHand,
    // TurnSolver::apply_one, and the off-suspend CastOffSuspend -- a Lotus Bloom arrival IS a cast),
    // reset to 0 at the start of each turn (GameEngine::UntapStep + the rollout's
    // SimulateEndAndStartNextTurn) in lockstep. Read ONLY by Dragonstorm's tutor_to_battlefield
    // resolution (put min(spells_cast_this_turn, Dragons-left) Dragons -- the counter already counts
    // Dragonstorm's own cast, so it equals storm copies + the original). Turn-scoped and consumed
    // WITHIN a single first-main plan application (rituals -> Dragonstorm), exactly like floating_mana
    // above; therefore -- like floating_mana -- it is NEVER folded into BuildSimKey (it is 0 at every
    // first-main dedup boundary and the combo never spans one). Byte-identical for every deck without
    // a tutor_to_battlefield card: the field is written but read by nothing else and folded nowhere.
    int                      spells_cast_this_turn = 0;
    // "You can cast only one more spell this turn" (Irencrag Feat, CardParams::max_casts_after) enforced
    // at EXECUTION time: -1 = no restrictor active (unlimited); otherwise the number of ADDITIONAL spells
    // still castable this turn. Installed when a max_casts_after spell is cast, decremented at every later
    // cast (hand OR staged, e.g. an Apex-of-Power-exiled Dragonstorm), and blocks a cast at 0. The static
    // subset checks in EnumeratePlans/consider only APPROXIMATE this (rank-based + blind to staged casts),
    // so this is the authoritative enforcement. Turn-scoped: reset to -1 at every turn start alongside
    // spells_cast_this_turn. Read by nothing else and folded into NO state key -> byte-identical for every
    // deck without a max_casts_after card (stays -1, so the guard and update below are inert).
    int                      casts_remaining_this_turn = -1;
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
    // Deck-level input to the main-phase classifier (GoldFishRunner::DeckFeedsCombat, stamped by
    // SetupGame): does ANY card in the deck feed the attack when cast pre-combat? False collapses
    // the classifier's BOTH classes and Main1-by-doubt default to Main2 (USER rule: a deck with no
    // main-1 effects casts EVERYTHING second main, draws included). Default TRUE = the wide/safe
    // reading for any state not built through SetupGame.
    bool                     deck_feeds_combat     = true;

    // SEARCHED Goblin Lackey put (Plan::lackey_choice): an index into the provider's ranked
    // CombatCheatCandidates list, or -1 for the provider's top pick. Unlike the scry/ETB-dig pins
    // this lives on the STATE rather than a scoped thread_local, because the decision is made in the
    // main phase but consumed in the COMBAT-DAMAGE step -- a scoped guard around the plan apply
    // would be destroyed before the trigger ever fires. Living on the state also means it rides
    // every rollout deep-copy for free, so each plan variant carries its own pick.
    // Consumed by the first cheat trigger and reset at the start of each turn, so it cannot leak
    // into a later combat.
    int                      scripted_cheat_choice = -1;
    // Searched CLEANUP DISCARD (Plan::discard_choice): which candidate of the provider's ranked
    // CleanupDiscardCandidates this turn's FIRST cleanup shed takes. Same state-pin shape as
    // scripted_cheat_choice and for the same reason -- the decision belongs to the turn's plan but
    // fires at END of turn, after the plan apply has returned, so a scoped guard would be gone.
    // Consumed by the first shed (later sheds of the same cleanup use the ranked default) and
    // cleared there, so it cannot leak into a later turn. -1 == the provider's top pick.
    int                      scripted_discard_choice = -1;
    // Maelstrom Archangel free-cast BANK (user-approved 2026-08-06): each copy that deals combat
    // damage to the player increments this (Combat.cpp ResolveCombatDamage, the shared combat
    // core), and the post-combat main may cast that many hand spells without paying mana (a
    // free_cast plan variant per hand card; spent in apply_one via the cascade_free mechanism /
    // the executor's CastSpellFromHand skip). Reset at the start of every turn in BOTH worlds.
    // CAUTION (user): banking is only safe because the free cast carries nothing across a phase
    // boundary -- a future card producing MANA mid-combat must NOT reuse this pattern blindly.
    int                      free_casts_available = 0;
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
    // How widely m_required_pieces is protected from the cleanup discard (profile field
    // mulligan.discard_protect). A plain value member, so every deep copy / rollout trial carries
    // it exactly like m_required_pieces. Default All = the established behaviour, so a raw
    // GameState and every deck that does not set the field are unchanged. See DiscardPolicy.h.
    DiscardProtectScope m_discard_protect = DiscardProtectScope::All;
    // Non-owning pointer to the deck's learned mid-game PLAY evaluator (MulliganProfile::eval_model),
    // stamped in AIEngine::HandleMulligan and propagated through every deep copy. Ranks NON-lethal
    // turn-plans in TurnSolver::Solve (the d0 decision + every rollout leaf) when MTG_EVAL_MODEL is
    // set; nullptr / empty / flag-off -> the heuristic EvalCard ranking (byte-identical). NEVER folded
    // into BuildSimKey (a per-deck constant, like m_provider). Mid-game play only -- not mulligan.
    const MidGameEvaluator* m_evaluator = nullptr;
    // Non-owning pointer to the deck's learned leaf VALUE model (MulliganProfile::value_model), stamped
    // in AIEngine::HandleMulligan and propagated through every deep copy. When set + MTG_VALUE_MODEL is
    // on, it REPLACES the search's horizon rollout (FSLineWin's SimulateToEnd) with a direct predicted
    // win turn -- distilling the deep search into an O(1) leaf estimate (the rollout is the weak link:
    // greedy, not searched). Its Score() is read as a WIN TURN (lower = better), unlike m_evaluator
    // whose Score is higher=better. nullptr / empty / flag-off -> the exact rollout (byte-identical).
    // NEVER folded into BuildSimKey (a per-deck constant). See docs/design/learned-d0-policy.md.
    const MidGameEvaluator* m_value_model = nullptr;

    Player&       ActivePlayer()       { return players[active_player_index]; }
    const Player& ActivePlayer() const { return players[active_player_index]; }
    Player&       Opponent()           { return players[1 - active_player_index]; }
    const Player& Opponent()     const { return players[1 - active_player_index]; }
};
