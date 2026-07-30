#pragma once
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <atomic>
#include <map>
#include <string>
#include <utility>
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

    // Construct a DIGEST-ONLY logger: records nothing structurally (no WriteToFile output),
    // it only folds each decision into a running play digest. Cheap enough to attach to every
    // batch game so the regression suite can fingerprint a deck's exact play (see Digest()).
    explicit GameLogger(bool digest_only) : m_digest_only(digest_only) {}
    GameLogger() = default;

    // FNV-1a 64-bit fingerprint of the ordered REAL decision stream (mulligan keeps/bottoms,
    // opening hand, lands, casts + targets/mana/X, draws, discards, attacks, per-turn phase
    // markers) -- the fields the search rollout does NOT touch (m_logger is nulled in rollouts),
    // so it captures the actual game line. Excludes non-reproducible metadata (run id, wall
    // clock, board snapshots) so it is byte-stable across runs/threads for a given (seed, gi).
    // Two lines that reach the same win-turn via different plays get DIFFERENT digests -- the
    // sensitivity the coarse won/avg fingerprint lacks. Valid after EndGame.
    uint64_t Digest() const { return m_digest; }

private:
    static constexpr uint64_t FNV_OFFSET = 1469598103934665603ULL;
    static constexpr uint64_t FNV_PRIME  = 1099511628211ULL;
    void FoldByte(uint8_t b)            { m_digest ^= b; m_digest *= FNV_PRIME; }
    void FoldStr(const std::string& s)  { for (char c : s) { FoldByte(static_cast<uint8_t>(c)); } FoldByte(0); }
    void FoldInt(int64_t v)             { for (int i = 0; i < 8; ++i) { FoldByte(static_cast<uint8_t>(v & 0xff)); v >>= 8; } }

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
    bool                                    m_digest_only = false;
    uint64_t                                m_digest      = FNV_OFFSET;
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
struct Permanent;
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

// ---- Human-play target chooser (board-click target selection) --------------------------
// Damage spells (burn, Crackle, ...) target the opponent face by the heuristic. Under
// --claude-play the human may instead pick targets off the board (face / a creature). A
// ChosenTarget is an int-encoded Target (kind 0 = player at `index`, 1 = permanent at `index`)
// so this header needn't see the Target struct. CastSpellFromHand asks the chooser AFTER it has
// built the heuristic targets (those are passed as the prepopulated default); the chooser returns
// the player's set. RevealLogPause nulls it for search/rollout scopes, so it fires only for real
// casts. Inert (heuristic) unless set.
struct CardDefinition;
// kind 0 -> player_index, 1 -> permanent_index. `amount` is the damage assigned to THIS target for
// a divided-damage spell (Fiery Justice); ignored (0) for uniform-damage spells, which deal the
// spell's flat per-target damage to every chosen target.
struct ChosenTarget { int kind = 0; int index = 0; int amount = 0; };
using TargetChooser = std::function<std::vector<ChosenTarget>(
    const GameState& state, const CardDefinition& def, int controller,
    int max_targets, int per_target_damage, const std::vector<ChosenTarget>& heuristic_default)>;
extern thread_local TargetChooser* g_play_target_chooser;

// ---- Human-play Karoo bounce-land chooser (which land to return to hand) ----------------
// A Karoo bounce land (Izzet Boilerworks) ETB returns one of the controller's lands to hand.
// Autonomously BounceKarooLand picks deterministically; under --claude-play the human picks off
// the board. The chooser receives the battlefield indices of the legal lands to return and the
// heuristic's pick (an index INTO that list); it returns the chosen index into the list. Nulled
// by RevealLogPause for every search/rollout/enumeration scope, so it fires only for real ETBs.
using BounceChooser = std::function<int(const GameState& state, int controller, const std::string& source,
                                        const std::vector<int>& legal_indices, int heuristic_pick)>;
extern thread_local BounceChooser* g_play_bounce_chooser;

// ---- Human-play sacrifice-a-land chooser (Shard Volley's additional cost) -------------------
// "As an additional cost to cast this spell, sacrifice a land." Same signature/shape as the bounce
// chooser (pick a battlefield land by option index); differs only in that the land goes to the
// graveyard and the viewer says "sacrifice" not "return". Nulled by RevealLogPause for search.
extern thread_local BounceChooser* g_play_sacrifice_chooser;

// ---- Human-play ETB-dig chooser (which examined card to put into hand) ------------------
// An ETB "look at the top N, you may reveal a <type> and put it into your hand" (Acclaimed
// Contender) digs for a matching card. Autonomously PerformEtbDig takes the FIRST match; under
// --claude-play the human picks WHICH match (or declines). The chooser receives the examined
// cards, the indices (into `examined`) of the legal candidates, and the heuristic's pick (an
// index into `examined`); it returns the chosen index into `examined`, or -1 to take nothing.
// Nulled by RevealLogPause for every search/rollout/enumeration scope, so it fires only for real
// ETBs -- and only when there is at least one legal candidate. Inert (heuristic) unless set.
using DigChooser = std::function<int(const GameState& state, int controller, const std::string& source,
                                     const std::vector<Card>& examined,
                                     const std::vector<int>& legal_indices, int heuristic_pick)>;
extern thread_local DigChooser* g_play_dig_chooser;

// ---- Human-play Light-Paws tutor-attach chooser (which Aura Light-Paws fetches) -----------
// Light-Paws, Emperor's Voice: whenever an Aura you CAST resolves, search your library for an Aura
// (mana value <= the cast Aura's, a name different from every Aura you control, and whose own enchant
// restriction Light-Paws itself satisfies) and put it onto the battlefield attached to Light-Paws.
// Autonomously PerformLightPawsAttach picks the highest static-power eligible Aura; under --claude-play
// the human picks WHICH Aura (or -1 to decline -- it is a "may search"). The chooser receives the
// library Auras (library order, so the human sees the searchable pool like a real tutor), the indices
// (into `aura_pool`) that are legal fetches, and the heuristic's pick (an index into `aura_pool`); it
// returns the chosen index into `aura_pool`, or -1 to fetch nothing. Nulled by RevealLogPause for every
// search/rollout/enumeration scope -> autonomous byte-identical. Inert (heuristic) unless set.
using LightPawsChooser = std::function<int(const GameState& state, int controller, const std::string& source,
                                           const std::vector<Card>& aura_pool,
                                           const std::vector<int>& legal_indices, int heuristic_pick)>;
extern thread_local LightPawsChooser* g_play_lightpaws_chooser;

// ---- Human-play cleanup-discard chooser (which card to discard to hand size) ------------
// The cleanup step discards down to maximum hand size. Autonomously AIEngine::ChooseDiscard
// picks; under --claude-play the human picks WHICH hand card to discard (one per over-limit
// card). The chooser receives the current hand indices and the heuristic's pick (an index into
// the hand); it returns the chosen hand index. Consulted only in the REAL cleanup step (the
// search rollouts never run GameEngine::CleanupStep), so autonomous play is byte-identical.
// Inert (heuristic) unless set.
using DiscardChooser = std::function<int(const GameState& state, int controller,
                                         const std::vector<int>& hand_indices, int heuristic_pick)>;
extern thread_local DiscardChooser* g_play_discard_chooser;

// ---- Human-play Expressive Iteration chooser (top-3 split: hand / exile / bottom) ----------
// Expressive Iteration looks at the top 3 of the library: one goes to HAND (banked), one is
// EXILED and playable THIS TURN ONLY, the third goes to the BOTTOM. Autonomously the provider's
// SituationalCardRank picks the split; under --claude-play the human chooses WHICH looked card
// goes to hand and WHICH to exile (the remaining one -> bottom). The chooser receives the looked
// cards (top-first) and the heuristic's default (hand_idx, exile_idx into `looked`); it returns
// the chosen {hand_idx, exile_idx}. Nulled by RevealLogPause for every search/rollout/enumeration
// scope, so it fires only for the REAL cast and the search stays byte-identical. Inert unless set.
using EIChooser = std::function<std::pair<int,int>(const GameState& state,
                                                   const std::vector<Card>& looked,
                                                   int heur_hand_idx, int heur_exile_idx)>;
extern thread_local EIChooser* g_play_ei_chooser;

// ---- Human-play Retrace discard chooser (which land to discard as the additional cost) --------
// Retrace (Throes of Chaos) casts a spell from the graveyard by discarding a land card as an
// additional cost. Autonomously cast_from_graveyard discards the FIRST land in hand order; under
// --claude-play the human picks WHICH land to discard. The chooser receives the hand indices of the
// discardable lands and the heuristic's pick (a hand index); it returns the chosen hand index (one
// of the offered indices). Called once per land the cast must discard (retrace = 1). Nulled by
// RevealLogPause for every search/rollout/enumeration scope, so it fires only for the REAL cast and
// the autonomous heuristic (first land) stands there. Inert (heuristic) unless set.
using RetraceDiscardChooser = std::function<int(const GameState& state, int controller,
                                                const std::string& source,
                                                const std::vector<int>& hand_land_indices,
                                                int heuristic_pick)>;
extern thread_local RetraceDiscardChooser* g_play_retrace_chooser;

// ---- Human-play Replicate chooser (how many times to replicate a Sliver spell on cast) --------
// Hatchery Sliver has Replicate {1}{G} and grants replicate to every Sliver spell you cast (see
// CanReplicate). On cast you may pay the replicate cost any number of additional times, each making
// a token copy. Autonomously (and in every search rollout) the engine replicates GREEDILY -- as many
// times as leftover mana allows -- which is the heuristic default. Under --claude-play the human picks
// how many copies to make (0..max_count, default = max_count). The chooser receives the spell's name
// and the max affordable count; it returns the chosen count. Nulled by RevealLogPause for every
// search/rollout/enumeration scope, so it fires only for the REAL cast and the greedy heuristic stands
// there -> autonomous play and the search stay byte-identical. Inert (greedy) unless set.
using ReplicateChooser = std::function<int(const GameState& state, int controller,
                                           const std::string& source, int max_count)>;
extern thread_local ReplicateChooser* g_play_replicate_chooser;

// ---- Human-play firebreathing-amount chooser (#4) ----------------------------------------------
// At combat, LEFTOVER mana is spent greedily on attacker pumps (Scourge {R}:+1/+0, Lathliss {1}{R}:
// team +1/+0) by ApplyFirebreathing. Under --claude-play the human instead chooses how many pump
// ACTIVATIONS to make (0..max, default = the greedy max), so they can hold mana back. Fires at most
// once per combat (once per turn), so it rides a TURN-keyed side-channel (--firebreathe), NOT the
// positional --choices stream -> existing references (no --firebreathe) replay byte-identically as
// greedy. Returns the chosen count, or -1 for the greedy default. Nulled by RevealLogPause so every
// search/rollout pumps greedily -> autonomous byte-identical. Inert (greedy) unless set.
using FirebreatheChooser = std::function<int(const GameState& state, int controller,
    const std::vector<int>& attacker_indices, int max_activations)>;
extern thread_local FirebreatheChooser* g_play_firebreathe_chooser;

// ---- Human-play cast-ORDER chooser (#10) -------------------------------------------------------
// After the human commits a main-phase plan, they may pin the ORDER its non-sacrifice hand casts
// resolve in (e.g. cast payoff before enabler, or a specific Dragonstorm go-off sequence the
// canonical CastOrderRank batches wrong). Rather than enumerate permutations (O(k!), capped, and
// index-churning the positional --choices stream), the committed order rides a MAIN-PHASE-ORDINAL-
// keyed side-channel (--cast-order): given the ordinal of the just-chosen main-phase decision, the
// chooser returns the human's card-name order (empty => keep canonical). The executor reorders the
// chosen plan's non-sac casts to match and flags searched_order so ApplyPlanDirect honours vector
// order. Absent --cast-order => empty => canonical => existing references replay byte-identically.
// Consulted ONLY at the top-level external-chooser site (never in a rollout), so no RevealLogPause
// gating is strictly required; nulled there anyway for consistency. Inert unless set.
using CastOrderChooser = std::function<std::vector<std::string>(int main_ordinal)>;
extern thread_local CastOrderChooser* g_play_cast_order_chooser;

// ---- Human-play storage-land TAP-vs-CHARGE chooser (#6, Dwarven Hold / Mercadian Bazaar) -------
// A charged storage land either BURSTS this turn (tap it, spend counters as {R}) or CHARGES (leave it
// untapped -> +1 counter at end of turn). The clairvoyant search decides this from foresight (burst =
// payment shortfall, reserve when unneeded), but the NON-clairvoyant human -- who can't see their next
// draws -- must be able to explicitly HOLD the battery to build toward a future big burst. Fires once
// per (turn, charged storage land) at the START of the pre-combat main, BEFORE plan enumeration, so the
// offered plans reflect the hold. Returns true => hold (reserve untapped this turn); false => allow the
// normal tap/burst (the current heuristic). Keyed by (turn, land BATTLEFIELD INDEX) on a side-channel
// (--storage-hold), NOT the positional --choices stream -> existing references (no --storage-hold)
// replay byte-identically as the heuristic. Nulled by RevealLogPause; inert unless set.
using StorageHoldChooser = std::function<bool(const GameState& state, const Permanent& land, int counters)>;
extern thread_local StorageHoldChooser* g_play_storage_hold_chooser;

// ---- Human-play land-entry chooser (enter a "pay a cost, or the land enters tapped" land) ------
// Two lands present this choice as they enter: a shock land (etb_pay_life_to_untap -> pay N life to
// enter untapped) and a reveal land like Frostboil Snarl (etb_untap_reveal_subtypes -> reveal a
// matching land in hand to enter untapped). Both collapse to one binary decision: enter UNTAPPED by
// paying the cost, or enter tapped. Autonomously (and in every search rollout) the engine takes the
// heuristic (shock: pay iff mana is needed this turn; reveal: reveal iff able) -- `heuristic_untapped`
// carries that default. `pay_life` is the shock life cost (0 for a reveal land); `reveal_types` names
// the subtypes a reveal land wants (empty for a shock land, e.g. {"Island","Mountain"} for a Snarl).
// The chooser returns true to enter untapped (pay the cost), false to enter tapped. Fires only when
// there is a REAL choice (shock: affordable; reveal: a matching card is in hand) -- see
// LandEntryHasChoice. Nulled by RevealLogPause for every search/rollout/enumeration scope, so it fires
// only on the REAL land drop and the heuristic stands there -> autonomous play and the search stay
// byte-identical. Inert (heuristic) unless set.
using LandEntryChooser = std::function<bool(const GameState& state, int controller,
                                            const std::string& source, int pay_life,
                                            const std::vector<std::string>& reveal_types,
                                            bool heuristic_untapped)>;
extern thread_local LandEntryChooser* g_play_land_entry_chooser;

// ---- Human-play Soulfire own-target chooser (WHICH of your creatures the dig also targets) ------
// Soulfire Eruption targets the face + opponent creatures + (optionally) you + a SEARCHED COUNT of
// your OWN creatures (each = a deeper dig + a bigger Hinata discount, but takes a random exiled
// card's damage). The count is chosen by the search; autonomously SoulfireDig picks WHICH creatures
// Under --claude-play the human picks the FULL Soulfire target set (like Crackle): the chooser
// receives `legal` = the whole canonical target order (SoulfireTargetOrder: opponent face
// [TARGET_OPP_FACE], self [TARGET_SELF_FACE], opponent creatures, own creatures, each a sentinel or a
// battlefield index), `min_targets` = the affordability floor (the count the cast already paid the
// Hinata discount for), and the heuristic default subset. It returns the chosen subset (size in
// [min_targets, legal.size()], each an entry of `legal`); SoulfireDig re-canonicalises it and assigns
// the exiled cards positionally. Nulled by RevealLogPause for every search/rollout/enumeration scope
// (SoulfireDig runs in both the executor and the rollout), so it fires only for the REAL resolution
// and the search stays byte-identical. Inert unless set.
using SoulfireTargetChooser = std::function<std::vector<int>(const GameState& state, int controller,
                                                             const std::string& source,
                                                             const std::vector<int>& legal,
                                                             int min_targets,
                                                             const std::vector<int>& heuristic_subset)>;
extern thread_local SoulfireTargetChooser* g_play_soulfire_chooser;

// ---- Human-play Dragonstorm put chooser (WHICH library Dragons enter -- override the rule) --------
// Dragonstorm puts up to `max_puts` (the storm count, capped by Dragons left in library) Dragons from
// the library onto the battlefield. Autonomously DragonstormProvider::TutorToBattlefieldPutOrder
// SELECTS which Dragons by role (Lathliss = token engine, Scourge = pinger, Utvara = attack tokens,
// Karrthus/Kolaghan = haste) and PerformTutorToBattlefield ORDERS them by a fixed play order (Lathliss
// -> Scourges -> Utvara -> Karrthus -> Kolaghan; Lathliss/Scourge stay front, order-dependent). Under
// --claude-play the human OVERRIDES only the SELECTION: `candidates` = the library Dragon copies in
// that play order (one Card per copy, so multiplicity + the cap read naturally), `max_puts` = the cap,
// `heuristic_subset` = the indices (into `candidates`) the rule picked (the default). The chooser
// returns the chosen indices into `candidates` (size 0..max_puts); the engine keeps candidate order --
// already the rule's play order -- so the human picks WHICH, never the order. Nulled by RevealLogPause
// for every search/rollout/enumeration scope, so it fires only on the REAL resolution and the rule
// stands as the autonomous/search default -> byte-identical. Inert (rule) unless set.
using DragonChooser = std::function<std::vector<int>(const GameState& state, int controller,
                                                     const std::string& source,
                                                     const std::vector<Card>& candidates,
                                                     int max_puts,
                                                     const std::vector<int>& heuristic_subset)>;
extern thread_local DragonChooser* g_play_dragon_chooser;

// ---- Human-play draw sink (accurate per-draw reporting for the viewer history) -------------
// Under --claude-play the viewer wants to show exactly what was drawn and on which turn, rather
// than guessing from a hand diff (which can't tell duplicate copies apart or split a cantrip
// draw from the turn draw). When set, the REAL draw sites append (turn_number, card_name) here
// at the moment each card is drawn: the per-turn draw (GameEngine::DrawStep) and the cantrip
// draws executed in TurnSolver::ApplyPlanDirect (Ponder/Preordain DrawN, Treasure Hunt
// DrawUntilNonland, Light Up staged draws). Expressive Iteration is NOT recorded here -- it has
// its own decision panel that already shows the hand/exile/bottom split. Nulled by RevealLogPause
// for every search/rollout/enumeration scope, so it fires ONLY for real draws and autonomous play
// is byte-identical (appending to an external vector never touches GameState). Inert unless set.
extern thread_local std::vector<std::pair<int, std::string>>* g_play_draw_sink;

// Life-affecting-event sink for the play viewer's history (damage / lifegain enumeration). Mirrors
// g_play_draw_sink: when set, the REAL resolution sites append a PlayEvent as each combat / burn /
// lifegain-loss event happens, so the NEXT emitted decision reports exactly the events since the
// last one (the viewer renders them in the history so the user needn't recompute life by hand).
// `kind` is one of "combat" / "damage" / "lifegain" / "lifeloss" (drives the viewer icon+colour);
// `text` is the human-readable line. Nulled by RevealLogPause during every search/rollout scope, so
// only real play appends -> autonomous play is byte-identical (an external vector never touches
// GameState). Inert unless set.
struct PlayEvent { int turn; std::string kind; std::string text; };
extern thread_local std::vector<PlayEvent>* g_play_event_sink;
// Append a play event if the sink is live (real play only). Safe no-op otherwise.
inline void EmitPlayEvent(int turn, const char* kind, std::string text)
{
    if (g_play_event_sink) { g_play_event_sink->push_back({ turn, kind, std::move(text) }); }
}

// Server-truth "dropped cast" sink for the play viewer (SERVER-TRUTH RESOLUTION). When a committed
// main-phase plan is applied through TurnSolver::ApplyPlanDirect::apply_one and a declared hand cast
// CANNOT be paid, that cast is silently left in hand; the apply site appends its name here. The next
// emitted decision reports "dropped_casts" so the browser knows AUTHORITATIVELY which casts failed --
// replacing the client-side detectDropped board-diff heuristic that false-positived on working lines
// (e.g. a self-sacrificing/redrawn cast, or a mid-plan pause). Nulled by RevealLogPause during every
// search/rollout/enumeration scope (incl. CheckLine's plan_pays trial-apply), so autonomous play is
// byte-identical (appending to an external vector never touches GameState). Inert unless set.
extern thread_local std::vector<std::string>* g_play_dropped_cast_sink;

// RAII: null g_reveal_logger AND g_play_top_chooser for the current scope. Placed at the top of
// every search / rollout "thinking" function so planning-time scry/dig calls are neither logged
// nor handed to the human chooser; restores the previous values on exit (so nested scopes compose).
struct RevealLogPause
{
    GameLogger* saved;
    TopChooser* saved_chooser;
    TargetChooser* saved_tchooser;
    BounceChooser* saved_bchooser;
    DigChooser* saved_dchooser;
    DiscardChooser* saved_dischooser;
    EIChooser* saved_eichooser;
    RetraceDiscardChooser* saved_rtchooser;
    SoulfireTargetChooser* saved_sfchooser;
    std::vector<std::pair<int, std::string>>* saved_drawsink;
    std::vector<PlayEvent>* saved_evsink;
    std::vector<std::string>* saved_dropsink;
    BounceChooser* saved_sacchooser;
    ReplicateChooser* saved_repchooser;
    LandEntryChooser* saved_lechooser;
    DragonChooser* saved_dragchooser;
    LightPawsChooser* saved_lpchooser;
    FirebreatheChooser* saved_fbchooser;
    CastOrderChooser* saved_cochooser;
    StorageHoldChooser* saved_shchooser;
    RevealLogPause() : saved(g_reveal_logger), saved_chooser(g_play_top_chooser),
                       saved_tchooser(g_play_target_chooser), saved_bchooser(g_play_bounce_chooser),
                       saved_dchooser(g_play_dig_chooser), saved_dischooser(g_play_discard_chooser),
                       saved_eichooser(g_play_ei_chooser), saved_rtchooser(g_play_retrace_chooser),
                       saved_sfchooser(g_play_soulfire_chooser), saved_drawsink(g_play_draw_sink),
                       saved_evsink(g_play_event_sink), saved_dropsink(g_play_dropped_cast_sink),
                       saved_sacchooser(g_play_sacrifice_chooser),
                       saved_repchooser(g_play_replicate_chooser), saved_lechooser(g_play_land_entry_chooser),
                       saved_dragchooser(g_play_dragon_chooser), saved_lpchooser(g_play_lightpaws_chooser),
                       saved_fbchooser(g_play_firebreathe_chooser),
                       saved_cochooser(g_play_cast_order_chooser),
                       saved_shchooser(g_play_storage_hold_chooser)
    { g_reveal_logger = nullptr; g_play_top_chooser = nullptr; g_play_target_chooser = nullptr;
      g_play_bounce_chooser = nullptr; g_play_dig_chooser = nullptr; g_play_discard_chooser = nullptr;
      g_play_ei_chooser = nullptr; g_play_retrace_chooser = nullptr; g_play_soulfire_chooser = nullptr;
      g_play_draw_sink = nullptr; g_play_event_sink = nullptr; g_play_dropped_cast_sink = nullptr;
      g_play_sacrifice_chooser = nullptr;
      g_play_replicate_chooser = nullptr; g_play_land_entry_chooser = nullptr; g_play_dragon_chooser = nullptr;
      g_play_lightpaws_chooser = nullptr; g_play_firebreathe_chooser = nullptr;
      g_play_cast_order_chooser = nullptr; g_play_storage_hold_chooser = nullptr; }
    ~RevealLogPause() { g_reveal_logger = saved; g_play_top_chooser = saved_chooser;
                        g_play_target_chooser = saved_tchooser; g_play_bounce_chooser = saved_bchooser;
                        g_play_dig_chooser = saved_dchooser; g_play_discard_chooser = saved_dischooser;
                        g_play_ei_chooser = saved_eichooser; g_play_retrace_chooser = saved_rtchooser;
                        g_play_soulfire_chooser = saved_sfchooser; g_play_draw_sink = saved_drawsink;
                        g_play_event_sink = saved_evsink; g_play_dropped_cast_sink = saved_dropsink;
                        g_play_sacrifice_chooser = saved_sacchooser;
                        g_play_replicate_chooser = saved_repchooser; g_play_land_entry_chooser = saved_lechooser;
                        g_play_dragon_chooser = saved_dragchooser; g_play_lightpaws_chooser = saved_lpchooser;
                        g_play_firebreathe_chooser = saved_fbchooser;
                        g_play_cast_order_chooser = saved_cochooser;
                        g_play_storage_hold_chooser = saved_shchooser; }
    RevealLogPause(const RevealLogPause&)            = delete;
    RevealLogPause& operator=(const RevealLogPause&) = delete;
};

// ---- Human-play suppression inside the engine's clairvoyant rollouts -----------------------
// --claude-play sets MTG_HUMAN_PLAY (and MTG_UNPRUNED) for the whole process, so every place that
// reads those env vars diverges from an autonomous run. That is correct for the REAL turn (the
// human is driving, the search offers extra human-only lines), but WRONG inside the engine's own
// clairvoyant rollouts (mulligan bottoming / keep evaluation): bottoming is an ENGINE decision the
// human never makes, so it must reproduce exactly what the autonomous d5 game would keep. Without
// this the human-play apply-semantics leaked into the bottoming rollout and it kept a different
// hand than the real search -- so the "same game" wasn't the same game.
//
// HumanPlayActive() replaces the scattered `getenv("MTG_HUMAN_PLAY")` statics: it returns the env
// value EXCEPT while a HumanPlaySuppress guard is live (set only at the top of RolloutWinTurn),
// where it returns false so the rollout plays autonomously. Non-human-play runs are byte-identical
// (env is false either way). thread_local so parallel keep-model rollouts don't race.
extern thread_local bool g_human_play_suppressed;

inline bool HumanPlayActive()
{
    static const bool s_env = std::getenv("MTG_HUMAN_PLAY") != nullptr;
    return s_env && !g_human_play_suppressed;
}

// ---- Affordability audit (MEASUREMENT ONLY; MTG_AFFORD_AUDIT) --------------------------------
// Counts plan-cast payment FAILURES: a cast in an enumeration-approved plan that the payment routine
// cannot pay in its plan.actions order, so it is silently dropped (a mis-order / aggregate over-credit
// symptom). Split by path: `rollout` = search scoring (ApplyPlanDirect::apply_one, TurnSolver.cpp);
// `real` = the actually-executed move (AIEngine::CastSpellFromHand). Purely additive counters -- game
// logic and every digest are byte-identical whether or not the audit is on. Dumped to stderr at exit.
bool AffordAuditOn();
extern std::atomic<long> g_afford_rollout_fails;
extern std::atomic<long> g_afford_rollout_attempts;
extern std::atomic<long> g_afford_real_fails;
extern std::atomic<long> g_afford_real_attempts;

// STRANDED-ACCELERANT DETECTOR (part of the same audit). A dropped cast is normally BENIGN --
// enumeration is deliberately optimistic (see SameTurnReducerGenericCredit: over-crediting is safe
// *because the search discards unpayable lines*), so a suite run drops thousands. What is NOT benign
// is dropping a same-turn MANA ACCELERANT: the plan committed to a ritual/rock precisely to fund a
// later spell, so losing it strands the payoff, and unlike a normal drop the search cannot filter it
// at depth 0. That is exactly the Dragonstorm defect (Seething Song attempted before the cheap
// rituals). This records each dropped cast by name and flags the accelerant ones, so ANY deck can be
// checked for a live instance with one env var instead of re-deriving the diagnosis:
//     MTG_AFFORD_AUDIT=1 ./build/Release/mtg <deck> ... 2>&1 | grep STRANDED
// A nonzero STRANDED count is a red flag to investigate (a cast order that funds itself would have
// paid it); zero means this defect class is not live for the deck. Recorded only when the audit is
// on, so game logic and every digest stay byte-identical.
// `colour_short` splits WHY it could not be paid, which decides whether a cast ORDER can fix it:
//   COLOUR shortfall  -- enough total mana was available, the wrong colours. Ordering cannot help;
//                        this is the flat `wild`-pool enumeration approximation (a "have red" gate
//                        passes a {R}{R}{R} cost off one red source). See exact-mana-enumeration.md.
//   TOTAL shortfall   -- not enough mana at all at this point in the turn. THIS is the class a cast
//                        order can strand or save (the Dragonstorm defect).
void NoteDroppedCast(const std::string& name, bool is_accelerant, bool colour_short);

// RAII: suppress human-play (and, in a claude-play session, unpruned) semantics for the current
// scope. Placed at the top of RolloutWinTurn so the engine's clairvoyant playouts match the
// autonomous game. Restores on exit so nested scopes compose.
struct HumanPlaySuppress
{
    bool saved;
    HumanPlaySuppress() : saved(g_human_play_suppressed) { g_human_play_suppressed = true; }
    ~HumanPlaySuppress() { g_human_play_suppressed = saved; }
    HumanPlaySuppress(const HumanPlaySuppress&)            = delete;
    HumanPlaySuppress& operator=(const HumanPlaySuppress&) = delete;
};
