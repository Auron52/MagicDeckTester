#pragma once
#include "Player.h"
#include "Permanent.h"
#include "ManaPool.h"
#include "DiscardPolicy.h"
#include <array>
#include <map>
#include <string>
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

// What a deck is able to OBSERVE about a graveyard (GameState::deck_gy_readers). Each bit is a
// distinct reader in the engine, and dominance folds only the corresponding projection -- so a
// graveyard's irrelevant cards never block a comparison. SOUNDNESS-CRITICAL: a reader with no bit
// here is a reader dominance cannot see, so any new graveyard-reading path must add one.
enum GyReader : std::uint32_t
{
    GyR_None            = 0,
    GyR_AllNames        = 1u << 0,  // Regrowth / Garth: ANY card can return to hand -> full multiset
    GyR_RetraceNames    = 1u << 1,  // retrace cards are castable FROM the graveyard (Throes, Flame Jab)
    GyR_SelfCopyNames   = 1u << 2,  // "per copy of THIS card in a graveyard" (Ancestral Anger, Rite of Flame)
    GyR_MulticolorNames = 1u << 3,  // Jared Carthalion -6: highest-MV MULTICOLORED card returns
    GyR_LandsEdgeNames  = 1u << 4,  // Land's Edge definition lookup falls through to the graveyard
    GyR_TypeCounts      = 1u << 5,  // Deathrite: COUNTS of land / instant-or-sorcery / creature cards
    GyR_ColorDemand     = 1u << 6,  // ChosenFloatColorCandidates: coloured pips over NONLAND gy cards
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
    bool                bestow         = false;     // BESTOW (Gnarled Scarhide): this cast is the AURA mode.
                                        // Stamped from Action::bestow at the cast site; the resolver then
                                        // enters the DB's synthesized "<name> (Bestowed)" aura face instead
                                        // of the creature. false => the ordinary creature cast.
    // ---- Real-stack trigger machinery (BreachingDragonstorm onboarding, 2026-09-03) ----
    // Cast/enter triggers are their own LIFO entries so nested chains (cascade -> free cast ->
    // cascade...) resolve in true stack order instead of ad-hoc inline special cases.
    enum class TriggerKind
    {
        None,              // not a trigger entry
        Cascade,           // one per cascade instance; pushed ABOVE the casting spell at cast
                           // time (CR 601.2i), so the exile walk + free cast resolve first
        EtbExileFreeCast,  // Breaching Dragonstorm's enter trigger (exile until nonland,
                           // may free-cast if MV <= etb_exile_free_cast_max_mv, else to hand)
        Demonstrate        // Creative Technique's cast trigger (may copy the spell; the copy
                           // is pushed above the original and resolves first)
    };
    TriggerKind         trigger_kind = TriggerKind::None;  // meaningful on EntryType::Triggered
    bool                is_copy     = false;  // demonstrate copy: resolves its payload but was
                                        // never CAST -- no cast triggers fired for it and it
                                        // ceases to exist on resolution (MoveToGraveyard skips).
                                        // (Sakashima's Protege's copy-SOURCE pick reuses
                                        // enchant_target above -- the aura/trick precedent:
                                        // m_number, -1 = searched decline, 0 = heuristic.)
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
    // Was the opponent DEALT a library (and an opening hand)? See core/OpponentDeck.h. Stamped by
    // GoldFishRunner::StampDeckTraits from the decklist: false for every deck that cannot touch the
    // opponent's library or hand, which is what keeps those decks byte-identical.
    //
    // THIS FLAG IS THE DECK-OUT GATE, and it is not optional. `players[1].library` is EMPTY, not
    // absent, on an ungated deck -- so the obvious test `Opponent().library.empty()` is TRUE on turn
    // 0 and would hand out a spurious instant win in every game of every deck. Never test emptiness
    // without this flag; use OpponentHasLost() below, which does it for you.
    bool                     opponent_library_dealt       = false;
    // The opponent was asked to draw from an empty library (CR 104.3c). Set by the simulated
    // opponent draw at the END of our turn -- which is also WHY the win turn for a deck-out is OUR
    // turn number and not the next one: the opponent never gets a main phase in this model, so on
    // the user's instruction (2026-09-02) the game is over as of our last turn.
    bool                     opponent_decked              = false;
    // "Infinite life" win (USER feature, 2026-09-04, Melira Pod): a demonstrated unbounded
    // lifegain loop counts as a WIN, reported under its own kind -- "most decks can't win when
    // you have a massive amount of life ... only losing to taking them out that turn."
    // EXECUTION-VERIFIED, never pattern-matched: set inside ApplySacCreatureOutlet (both worlds,
    // lockstep) the moment a FREE sac outlet sacrifices a persist creature with an ETB lifegain
    // and the body RETURNS CLEAN (no -1/-1 counter -- Melira/Vizier active). That one iteration
    // changed nothing but our life total, so it can repeat without bound. Gated behind
    // InfLifeWinEnabled() (MTG_INFLIFE_WIN, default ON; =0 = pure-kill measurement arm) at the
    // set site; false for every deck without the loop -> byte-identical elsewhere.
    bool                     infinite_life_win            = false;
    // Turn the active player last STACKED the top of their library with a tutor-to-top
    // (Worldly Tutor). -1 = never. The USER's intentionality gate for top-consumer models
    // (MTG_TOP_RESOLVE, 2026-08-21): a consumer decision may read the top ONLY when the stack
    // was deliberate ("I'm not particularly interested in having it take advantage of
    // clairvoyance ... when it was not put there intentionally"). Turn-stamped rather than a
    // cleared bool so no draw/shuffle site needs bookkeeping -- readers verify the top card
    // still matches what they need, so a consumed/buried stack self-invalidates. Written
    // unconditionally at PerformTutor's to-top placement; every reader is lever-gated, so the
    // field is inert (byte-identical) with the lever off.
    int                      top_stacked_turn             = -1;
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
    // Summed MANA VALUE of the spells behind spells_cast_this_turn (Call Forth the Tempest's
    // damage clause: "equal to the total mana value of other spells you've cast this turn" --
    // the reader subtracts its own MV once). Incremented at EXACTLY the five sites that ++ the
    // storm counter (CastSpellFromHand, apply_one, PushFreeCast, the Garth copy-cast, and
    // CastOffSuspend) and reset at the same two turn starts, so the pair can never desync.
    // Folded into state keys ONLY when deck_reads_mv_cast (below) -- an unconditional fold
    // would shift every storm-deck key and give up byte-identity to record a field that is
    // read by nothing in those decks (same guard rationale as opponent_library_dealt).
    int                      mv_cast_this_turn = 0;
    // Deck-level stamp (GoldFishRunner::SetupGame): true iff some card in the deck carries
    // damage_opp_creatures_mv_cast, i.e. something actually READS mv_cast_this_turn. Gates the
    // key folds only; the accumulator itself always runs (cheap, and lockstep is simpler than
    // a conditional increment at five sites).
    bool                     deck_reads_mv_cast = false;
    // "You can cast only one more spell this turn" (Irencrag Feat, CardParams::max_casts_after) enforced
    // at EXECUTION time: -1 = no restrictor active (unlimited); otherwise the number of ADDITIONAL spells
    // still castable this turn. Installed when a max_casts_after spell is cast, decremented at every later
    // cast (hand OR staged, e.g. an Apex-of-Power-exiled Dragonstorm), and blocks a cast at 0. The static
    // subset checks in EnumeratePlans/consider only APPROXIMATE this (rank-based + blind to staged casts),
    // so this is the authoritative enforcement. Turn-scoped: reset to -1 at every turn start alongside
    // spells_cast_this_turn. Read by nothing else and folded into NO state key -> byte-identical for every
    // deck without a max_casts_after card (stays -1, so the guard and update below are inert).
    int                      casts_remaining_this_turn = -1;
    // POST-COMBAT PRODUCTIVITY markers (USER 2026-08-19: "limit the search to productive options
    // and skip it for unproductive ones"). Stamped by SimulateCombat / the executor's combat phase
    // with the active player's hand and battlefield sizes as combat BEGINS, so the post-combat main
    // can ask the one question that decides whether it has anything to do that main 1 could not:
    // did combat CREATE a resource? A card drawn mid-combat (Armored Skyhunter's put firing
    // Puresteel Paladin) grows the hand; an Equipment put onto the battlefield mid-combat grows the
    // battlefield. Two ints rather than a hand snapshot on purpose -- GameState is copied on every
    // plan application (12.5% of one profile), so this must not allocate. -1 = not stamped (no
    // combat yet this turn), which every consumer treats as "assume productive".
    int                      hand_size_at_combat       = -1;
    int                      battlefield_at_combat     = -1;
    uint64_t                 game_seed             = 0;   // seed used to set up this game; used for mulligan reshuffles
    uint64_t                 search_count          = 0;   // # library SEARCHES (fetch/tutor) this game; seeds the
                                                          // deterministic mid-game shuffle (ShuffleAfterSearch).
                                                          // Copied with state so the search rollout reproduces the
                                                          // same shuffle the executor will -> lockstep. LIVE by
                                                          // default; MTG_NO_SEARCH_SHUFFLE opts out.
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
    // Does this game PLAY a post-combat main at all (GoldFishRunner::DeckUsesSecondMain, stamped
    // by SetupGame)? The main-phase classifier is gated on it STRUCTURALLY: deferring a cast to
    // main 2 on a game with no main 2 would DELETE the cast (the "never suite-wide" hazard the
    // battery manifest used to warn about -- now impossible by construction). Default FALSE =
    // classifier inactive for any state not built through SetupGame (conservative: a filter that
    // silently narrows nothing beats one that silently deletes casts).
    bool                     uses_second_main      = false;
    // ORDER-CONDEMNATION snapshot (USER model, 2026-08-19: the search decides what to cast,
    // "limited to what our order allows" -- a Main1-classified card the pre-combat decision
    // passed on is CONDEMNED for the rest of the turn; "main 2 should not re-litigate the whole
    // hand ... it should continue with the same condemnation list"; newly drawn/acquired cards
    // exempt). Card numbers in hand at THIS turn's pre-combat main decision, stamped by BOTH
    // worlds at the same logical point (AIEngine::TakeTurn pre-combat entry + ApplyPlanDirect
    // pre-combat entry -- lockstep pair, TurnSolver::StampM1Hand). Consumed by the post-combat
    // CollectActions condemnation filter (provider opt-in, CondemnsPassedMainPhase).
    // m1_hand_turn guards staleness: a consumer only honours a snapshot stamped THIS turn, so no
    // clearing pass is needed. POD array, not a vector: GameState deep-copy is a measured
    // hotspot and this must stay allocation-free; a hand longer than the cap leaves the
    // overflow un-condemnable (a missed prune -- the safe direction).
    static constexpr int          kM1HandCap = 16;
    std::array<int, kM1HandCap>   m1_hand{};
    std::uint8_t                  m1_hand_n    = 0;
    int                           m1_hand_turn = -1;
    // CARD-DEPENDENCY-MAP pulls (GoldFishRunner::DeriveDependencyPulls, stamped by SetupGame; see
    // docs/design/card-dependency-map.md). Closure over the deck's dependency edges: an ENABLER
    // (lifegain_to_loss) must be considerable in the phase of its opponent-lifegain payloads, and a
    // CAST-PAYOFF card (verse_damage) wants to resolve before the instant/sorcery casts that feed
    // it -- when the feeding side classifies Main1, these pull to Main1 too. Default FALSE = no
    // pull = prior classifier behaviour for any state not built through SetupGame.
    bool                     dep_enabler_main1     = false;
    bool                     dep_castpayoff_main1  = false;
    // Deck-level input to EOT DOMINANCE: WHICH PROJECTION of a graveyard this deck can observe
    // (GoldFishRunner::DeckGraveyardReaders, stamped by SetupGame; bits from GyReader below).
    // Conditional like the storm counter -- on for the cards/decks that can read it, ignored
    // otherwise (USER, 2026-08-15) -- but a MASK rather than a bool, because what matters is the
    // TYPE of cards in the graveyard, not the whole zone (USER, 2026-08-15: "is there Throes of
    // Chaos (retrace) in TH, or fetchlands (for DRS) in fivecolour?"). Dominance folds only the
    // projection these bits select, so two graveyards differing only in cards nothing can read
    // still compare equal. Default = every bit set = fail closed (fold everything) for any state
    // not built through SetupGame.
    std::uint32_t            deck_gy_readers       = 0xFFFFFFFFu;

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
    // Searched AETHER VIAL UPKEEP CHARGE (Plan::vial_charge_choice): 0 = hold, 1 = charge, -1 = the
    // provider heuristic (WantVialCharge). Same state-pin shape as scripted_discard_choice and for
    // the same reason, one turn further out: the charge fires at the NEXT turn's upkeep, long after
    // the plan apply has returned, so a scoped guard would be gone. Consumed by the FIRST vial of
    // that upkeep (later vials of the same upkeep use the heuristic) and cleared there, so it cannot
    // leak into a later turn. NOTE it must survive the turn boundary -- do NOT clear it alongside
    // scripted_cheat_choice in SimulateEndAndStartNextTurn, which runs BEFORE the upkeep that reads it.
    int                      scripted_vial_charge    = -1;
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
    // Non-owning pointer to the deck's LEARNED per-card marginals (MulliganProfile::card_scores),
    // stamped beside m_required_pieces in AIEngine::HandleMulligan and by every analyzer rollout
    // harness, and propagated through every deep copy. card_scores[name][k] is the measured win-turn
    // improvement of the (k+1)-th copy IN THE OPENING HAND -- see AnalyzerEngine::ComputeCardScores.
    //
    // READ THE UNITS BEFORE USING THIS. It is an unadjusted group-mean difference over opening
    // hands, so it is (a) an opening-hand quantity, not a "value of playing this card on turn 5",
    // and (b) confounded with castability -- a hand holding a five-drop holds one fewer cheap card,
    // which is why AIEngine's own consumer clamps the negative half to zero and calls it selection
    // bias. It is a keep/bottom feature. Any use as a mid-game VALUE term is a hypothesis to be
    // measured, not a free upgrade; docs/design/minotaur-discard-policy-proposal.md records one
    // such measurement. nullptr -> the consumer's own fallback (matches a raw GameState).
    const std::map<std::string, std::vector<double>>* m_card_scores = nullptr;
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
    // Which MidGameFeature indices the attached models can BRANCH on, as a bitmask -- stamped
    // beside the two pointers above and carried through every deep copy. EOT dominance needs it to
    // decide whether the graveyard / exile zones are observable (ai/Dominance.h), and it must be a
    // stamped VALUE rather than something derived on demand from the pointers: a thread_local memo
    // keyed on pointer identity is an ABA hazard -- a freed profile and a newly-allocated one can
    // land on the same address, return a STALE mask, and make the search nondeterministic (measured
    // 2026-08-15: prune-arm smoke gave 3/4/5 failures across identical runs). 0 = no model attached.
    std::uint64_t m_model_feat_mask = 0;

    Player&       ActivePlayer()       { return players[active_player_index]; }
    const Player& ActivePlayer() const { return players[active_player_index]; }
    Player&       Opponent()           { return players[1 - active_player_index]; }
    const Player& Opponent()     const { return players[1 - active_player_index]; }
};

// THE win predicate. Every "have we won?" test in BOTH worlds goes through here.
//
// It exists because the check was scattered: the executor asked GameEngine::CheckWinCondition
// (== Opponent().HasLost()) while the rollout open-coded `Opponent().life <= 0` at ~40 sites. Add a
// non-damage win condition to only one of those and you get the worst possible failure -- an
// executor that RECOGNISES the win and a search that never PURSUES it, so the line is played only
// by accident. That is the exact shape of the Dragonstorm go-off that executed as a kill zero times
// (docs/design/, dragonstorm-goff-lethal-bug), and deck-out would have repeated it.
//
// `opponent_decked` is false unless the deck was stamped opponent_library_dealt, so this is
// byte-identical to the old `life <= 0` for every deck that cannot mill.
inline bool OpponentHasLost(const GameState& s)
{
    // infinite_life_win is OUR alternate win (not an opponent loss), but every win consumer --
    // search projection, executor, fd-oracle -- reads this one predicate, so folding it here is
    // what keeps all of them agreeing about the same game (the opponent_decked precedent).
    return s.Opponent().HasLost() || s.opponent_decked || s.infinite_life_win;
}
