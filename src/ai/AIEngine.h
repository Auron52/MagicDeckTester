#pragma once
#include "../core/GameState.h"
#include "../core/GameLogger.h"
#include "../core/ManaPool.h"
#include "../cards/CardDatabase.h"
#include "MulliganProfile.h"
#include "TurnSolver.h"
#include "TranspositionTable.h"
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <vector>

class AIEngine
{
public:
    // lookahead_depth: 0 = single-turn heuristic (fast, for the runner);
    //                  N = N-turn lookahead via game simulation (for the analyzer).
    // budget_ms:       per-decision deterministic search budget in "virtual ms"
    //                  (maps to rollout work units via SearchBudget; see that
    //                  header). 0 = unlimited. Replaces the old wall-clock timeout
    //                  so results are reproducible and machine-independent.
    explicit AIEngine(MulliganProfile profile = MulliganProfile::DefaultProfile(),
                      int lookahead_depth = 0,
                      int budget_ms = 0);

    // London mulligan: draw 7, keep or mulligan, bottom N cards on keep after N mulligans.
    // max_turns bounds the rollout horizon used by lookahead bottoming (see below).
    void HandleMulligan(GameState& state, int max_turns = 20);

    // Called once by GameEngine::RunGame after a real game finishes (not for
    // rollouts, which bypass RunGame). Drives the full-depth fidelity oracle
    // (MTG_FD_ORACLE): flags when the realised win turn is worse than the earliest
    // win any committed line predicted this game — a genuine commit-line divergence,
    // compared at game end so it never fires on games that win on time via combat.
    void OnGameEnd(const GameState& state, int win_turn);

    // Returns the card names of the hand kept after the most recent HandleMulligan call.
    const std::vector<std::string>& GetKeptOpeningHand() const { return m_kept_opening_hand; }

    // Mulligan reproducibility (see docs/design/claude-play-mulligan-reproducibility.md).
    // The per-mulligan reshuffle is seeded by game_seed + mulligan_count, so the pre-bottom hand at
    // each depth is fixed by the seed alone -- only the keep count and the bottomed cards are
    // heuristic. Recording those two lets a replay reconstruct the exact opening hand + library on
    // ANY engine version, independent of the keep model / bottoming heuristic.
    //   * After HandleMulligan, these report what THIS game did (for recording into a reference).
    int LastMulliganCount() const { return m_last_mulligan_count; }
    const std::vector<int>& LastBottomedNumbers() const { return m_last_bottomed_numbers; }
    //   * When a forced directive is set, HandleMulligan keeps at exactly `count` mulligans and
    //     bottoms exactly `bottom_numbers` (by card m_number, in order), ignoring both heuristics.
    void SetForcedMulligan(int count, std::vector<int> bottom_numbers)
    { m_forced_mull_active = true; m_forced_mull_count = count; m_forced_bottom_numbers = std::move(bottom_numbers); }

    // Called each main phase. Plays lands, casts spells, activates abilities.
    //
    // resolve_stack: if supplied, called after EACH individual cast so the stack
    // resolves before the next spell is cast. We don't model priority, so this is how
    // we reproduce legal sequential play within a main phase: a creature is on the
    // battlefield (lords/prowess see it) and a damage spell has dealt its damage
    // (spectacle is unlocked) before the following spell is cast — matching the
    // lookahead rollout, which resolves each cast immediately. With no callback the
    // casts are batched (legacy behaviour). Returns true if a draw-engine spell
    // (DrawUntilNonland / cascade) was cast, so the caller can give the AI a second
    // pass to play the newly drawn cards.
    bool TakeTurn(GameState& state, bool is_pre_combat_main,
                  const std::function<void(GameState&)>& resolve_stack = {});

    // Returns pointers to battlefield permanents that will attack this turn.
    std::vector<Permanent*> DeclareAttackers(GameState& state);

    // Firebreathing (Scourge {R}:+1/+0 self, Lathliss {1}{R}: Dragons +1/+0 team): spend the
    // active player's LEFTOVER combat mana on attacker pumps, converting mana into face damage.
    // Called from GameEngine::CombatPhase after attackers are finalized and before combat damage.
    // Builds the leftover pool via BuildAvailableMana (byte-identical to the rollout's BuildPool)
    // and applies the shared ApplyFirebreathing so the executor and rollout pump identically.
    // Inert (no attacker carries a firebreathing param) for every non-Dragonstorm deck.
    void Firebreathe(GameState& state, const std::vector<int>& attacker_indices);

    // Returns a pointer to a card in the active player's hand to discard.
    Card* ChooseDiscard(GameState& state);

    void SetLogger(GameLogger* logger) { m_logger = logger; }

    // Discard all lands from hand to each Land's Edge permanent the active player
    // controls, dealing 2 damage per land per Land's Edge.  Called by GameEngine
    // after ResolveStack so Treasure Hunt has already resolved and filled the hand.
    void ActivateLandsEdge(GameState& state);

    // Lookahead bottoming evaluates each candidate removal with a full clairvoyant
    // game rollout and bottoms the card whose removal preserves the earliest win
    // (heuristic breaks win-turn ties). More accurate, ~2x slower. It is DERIVED
    // FROM DEPTH -- ON exactly when the engine searches (depth > 0), OFF for the
    // depth-0 greedy baseline (whose greedy rollout cannot discriminate on a deep
    // mulligan and would bottom the payoff). There is intentionally no flag: tying
    // it to depth removes the search/executor and single-deck/batch config-mismatch
    // footguns that an independent toggle caused.
    bool LookaheadBottoming() const { return m_lookahead_depth > 0; }

    // Sets the max-turns horizon used by ActivateLandsEdge rollouts (depth > 0).
    // Called by GameEngine::PlayOut so the value tracks each game's actual limit.
    void SetMaxTurns(int max_turns) { m_max_turns = max_turns; }

    // Marks the second (post-combat) main phase as RELEVANT for this deck. OFF by
    // default, in which case NOTHING happens in the second main: in a clairvoyant
    // goldfish combat reveals nothing and creates no new resources, so everything
    // is castable in the first main, and both the real game and the lookahead
    // rollout skip the second main entirely (kept consistent on purpose).
    //
    // Turn it on only for decks whose COMBAT enables genuine second-main plays:
    //   - lands untapped during combat (e.g. Bear Umbra, Hidden Strings),
    //   - spectacle costs unlocked by combat damage (the finisher is cast after
    //     the attack), and similar combat-damage-gated effects.
    // When on, the second main is SEARCHED at lookahead depths (depth > 0) and
    // played GREEDILY at depth 0 (the fast runner has no search to run).
    //
    // Long-term this should be auto-set by detecting such cards in the deck rather
    // than a manual toggle. NOTE: the SEARCHED (depth > 0) path is not yet fully
    // wired — before relying on it, (a) the lookahead's inline first-turn sim must
    // be made phase-aware (it always simulates a combat step, so a post-combat
    // invocation double-counts combat), and (b) the rollout must model the second
    // main under this same gate so the search actually values those plays.
    void SetSearchPostCombat(bool enabled) { m_search_post_combat = enabled; }

    // External main-phase decision provider (Claude-play / human-play prototype).
    // Signature: (state, legal_plans, is_pre_combat_main) -> chosen plan index, or -1
    // to pass (cast nothing). When set, TakeTurn offers the SAME candidate plans the
    // solver would search and executes the chosen one instead of searching -- so an
    // external player drives the main phases while combat/discard stay on the engine
    // heuristics. Inert when unset (normal AI path). See TurnSolver::EnumerateMainPlans.
    using ExternalChooser =
        std::function<int(const GameState&, const std::vector<TurnSolver::Plan>&, bool)>;
    void SetExternalChooser(ExternalChooser chooser) { m_external_chooser = std::move(chooser); }

    // Aether Vial upkeep charge decision (Vial-as-a-choice). The DEFAULT is the current
    // heuristic (charge up to the deck's dominant creature MV, vial_target_mv), so the
    // normal AI and its ground truth are unchanged. When an external vial chooser is set
    // (claude-play / human-play), it decides instead -- so a different controller can
    // hold the Vial at a chosen count. Its bool arg is the heuristic's default choice.
    using ExternalVialChooser =
        std::function<bool(const GameState&, const Permanent&, bool)>;
    void SetExternalVialChooser(ExternalVialChooser c) { m_external_vial_chooser = std::move(c); }

    // Mulligan keep/mulligan decision (claude-play / human-play). The DEFAULT is the engine's own
    // KeepHand (so autonomous play and its ground truth are unchanged). When set, the external
    // controller decides instead, per London-mulligan attempt. Args: (current 7-card hand,
    // mulligan_count so far, on_the_play, ai_keep) where ai_keep is what the engine would do;
    // returns true to KEEP this hand, false to mulligan again. Never consulted during --force-mulligan
    // replay (that reconstructs an exact recorded hand). Inert when unset.
    using ExternalMulliganChooser =
        std::function<bool(const std::vector<Card>&, int, bool, bool)>;
    void SetExternalMulliganChooser(ExternalMulliganChooser c) { m_external_mulligan_chooser = std::move(c); }

    // London bottoming decision (claude-play / human-play). The DEFAULT is the engine's own bottom
    // pick (HeuristicBottomPick over the lookahead-win-optimal removals). When set, the external
    // controller picks which card to put on the bottom, one card per step. Args: (current hand,
    // ai_pick = the hand index the engine would bottom, win_optimal = per-index flags marking the
    // removals that preserve the earliest clairvoyant win — all-1 at depth 0, step = 0-based bottom
    // step, total = cards to bottom this mulligan); returns the chosen hand index. Never consulted
    // during --force-mulligan replay. Inert when unset.
    using ExternalBottomChooser =
        std::function<int(const std::vector<Card>&, int, const std::vector<char>&, int, int)>;
    void SetExternalBottomChooser(ExternalBottomChooser c) { m_external_bottom_chooser = std::move(c); }

    // True if a charge counter should be added to `vial` this upkeep (called by
    // GameEngine). Defaults to the heuristic; consults the external vial chooser if set.
    bool DecideVialCharge(const GameState& state, const Permanent& vial) const;

    // Mulligan keep-model GENERATOR hook (analyzer-only): clairvoyant "keep value" of an
    // opening hand. Bottoms `mulligan_count` cards using the SAME lookahead bottoming that
    // real play uses (depth > 0), then rolls the game out and returns the win turn
    // (max_turns + 1 = no win within the horizon). `trial` must be a freshly set-up state
    // with a 7-card hand and on_the_play / required_pieces already set on it. Used by
    // BuildKeepModel to label hands keep-vs-mulligan against the rollout oracle, so the
    // label uses exactly the same bottoming + rollout the deck is actually played with.
    // out_hit (execution-trace): if non-null AND a touch index has been set (SetTouchIndex), each
    // index whose card's effect ran during this rollout is set to 1 (caller sizes+zeroes it). Default
    // null => no instrumentation, byte-identical.
    // lands_out (diagnostic): if non-null, receives the number of lands player 0 controls at the moment
    // the rollout ends (win or horizon). Default null => no effect, byte-identical.
    int RolloutKeepWinTurn(GameState trial, int mulligan_count, int max_turns,
                           std::vector<char>* out_hit = nullptr, int* lands_out = nullptr);

    // Execution-trace instrumentation: point at a card-name -> compact-index map (non-owning). When set
    // and RolloutKeepWinTurn is given an out_hit vector, the rollout records which cards' effects ran.
    void SetTouchIndex(const std::map<std::string, int>* idx) { m_touch_index = idx; }

    // Analyzer-only: the reference (static-profile) keep decision, exposed so BuildKeepModel can
    // BOOTSTRAP its policy-simulated mulligan baseline from the current static policy. Routes through
    // the same KeepHand the runtime uses; with an empty keep_model in the profile (as the rollout
    // profile is) this is exactly the legacy static keep path -- a blind (outcome-independent)
    // function of the hand, which is what the unbiased policy simulation requires.
    bool ReferenceKeep(const std::vector<Card>& hand, int mulligan_count, bool on_the_play) const
    { return KeepHand(hand, mulligan_count, on_the_play); }

private:
    MulliganProfile          m_profile;
    int                      m_lookahead_depth   = 0;
    int                      m_budget_ms         = 0;   // virtual-ms search budget (see SearchBudget)
    int                      m_max_turns         = 20;  // rollout horizon; kept in sync by SetMaxTurns
    bool                     m_search_post_combat  = false;
    bool                     m_in_rollout          = false; // prevents recursive LE search in rollouts
    const std::map<std::string, int>* m_touch_index = nullptr;  // execution-trace card index (non-owning)
    std::vector<std::string> m_kept_opening_hand;
    // Mulligan reproducibility (see the public accessors above).
    int                      m_last_mulligan_count = 0;
    std::vector<int>         m_last_bottomed_numbers;
    bool                     m_forced_mull_active  = false;
    int                      m_forced_mull_count   = 0;
    std::vector<int>         m_forced_bottom_numbers;
    GameLogger*              m_logger            = nullptr;
    ExternalChooser          m_external_chooser;          // unset => normal AI path
    ExternalVialChooser      m_external_vial_chooser;     // unset => heuristic charge
    ExternalMulliganChooser  m_external_mulligan_chooser; // unset => engine KeepHand
    ExternalBottomChooser    m_external_bottom_chooser;   // unset => engine bottom pick

    // Shared transposition table for the clairvoyant bottoming loop (BottomCards).
    // Non-null only for the duration of that loop; nullptr during the real game and
    // the in-search rollout, so every other SolveWithLookahead keeps its own
    // per-decision local table (behaviour byte-identical to before). Bottoming runs
    // a full lookahead PlayOut per candidate removal over a FIXED library, and each
    // of those playouts re-rolls the same overlapping late-game turns; pointing every
    // TakeTurn at one shared table lets later turns/candidates reuse memoised exact
    // win turns instead of rebuilding a table from scratch ~(count*hand_size) times.
    // Lossless: the TT stores only exact real win turns keyed by SimulateToEnd's
    // inputs. The key folds library size+front but not the tail, so two candidate
    // states that bury a DIFFERENT card share a key — benign here because the bottomed
    // card sits at the library bottom, far below the max_turns (=20) draw horizon, so
    // no rollout can ever draw the differing card. Validated by a byte-identical
    // regression check. See project-cross-turn-reuse / project-search-optimizations.
    TranspositionTable*      m_shared_tt           = nullptr;

    // Game-persistent LEAF cache (MTG_LEAF_CACHE, opt-in; default off => byte-identical).
    // Shares SimulateToEnd rollout results across a game's decisions (not just per-call).
    // Cleared per game in HandleMulligan; used only on the real top-level path (not the
    // bottoming loop / rollout). A prior build produced stale cross-decision hits (hinata
    // work rose) -- the MTG_LEAF_VERIFY harness recomputes each hit to find/confirm the
    // key-completeness bug. See docs/design/escalation-interior-reuse.md.
    TranspositionTable       m_leaf_cache;
    const bool               m_leaf_cache_enabled  = std::getenv("MTG_LEAF_CACHE") != nullptr;

    // --- Full-depth commit-the-line (env-gated by MTG_FULL_DEPTH) ---
    // The remaining phases of the optimal line found by the last FullSearchLine, in
    // execution order (pre-combat, then second main when used, across the searched
    // turns). When set, TakeTurn REPLAYS the front phase instead of re-searching, so
    // the realised win matches the searched win (no per-turn re-deciding drift).
    // Recomputed at a pre-combat main once exhausted; reset per game in HandleMulligan
    // and saved/restored around rollouts in RolloutWinTurn (the rollout PlayOut shares
    // this AIEngine by reference, so its play must not consume the real game's line).
    std::deque<TurnSolver::PhasePlan> m_committed_line;

    // Oracle (MTG_FD_ORACLE): earliest searched win predicted this game and the turn
    // it was predicted, to flag when a later recompute degrades below it (divergence).
    int m_fd_best_win  = 21;
    int m_fd_best_turn = 0;

    // --- Non-convergence detector (env-gated by MTG_FLAG_NONCONV; read-only) ---
    // Tracks, per game, the EARLIEST exhaustively-verified win turn the search has
    // proved so far (m_nonconv_best_win) and the real turn it was proved on
    // (m_nonconv_best_turn). Reset each game in HandleMulligan. A win is
    // "exhaustively verified" iff the committing pass ran at full depth
    // (sub_depth == m_lookahead_depth - 1), the win is within the horizon, and the
    // win sits inside that pass's branched lookahead (win - turn <= sub_depth). When
    // a later turn verifies a win that EXCEEDS an earlier verified win, the search
    // failed to converge to the earlier proof — a bug we want to surface, not paper
    // over. See project-cross-turn-reuse.
    int                      m_nonconv_best_win    = 0;
    int                      m_nonconv_best_turn   = 0;

    // Reports a non-convergence (later verified win > earlier verified win) for the
    // committed decision to stderr. No-op unless MTG_FLAG_NONCONV is set.
    void FlagNonConvergence(const GameState& state, const TurnSolver::Plan& plan,
                            int committed_win, int committed_sub_depth);

    // --- Mulligan helpers ---
    // on_the_play: true if the active player is on the play (skips the turn-1 draw). Fed to the
    // analyzer-generated keep model (KeepModel) as a feature; ignored by the legacy keep path.
    bool KeepHand(const std::vector<Card>& hand, int mulligan_count, bool on_the_play) const;
    void BottomCards(GameState& state, int count, int max_turns);

    // Picks the index of the card the curve/castability heuristic would bottom,
    // considering only cards whose allowed[i] is non-zero. Returns -1 if none.
    int HeuristicBottomPick(const std::vector<Card>& hand,
                            const std::vector<char>& allowed) const;

    // Per-card "keep value" derived from the deck analysis (card_scores): the
    // empirical marginal win-turn improvement of the (copy_index+1)-th copy in
    // the opening hand, clamped to >= 0. copy_index is 0-based: index 0 is the
    // first copy's marginal, index 1 the second copy's (typically smaller —
    // diminishing returns), and so on. Indices past the recorded vector clamp to
    // its last entry. Lords and other payload cards score high on the first copy;
    // lands and support cards ~0. Used purely as a bottoming tiebreak — among
    // removals the lookahead judged win-equal, prefer to bottom the copy with the
    // lowest marginal (keep the payload; bottom a redundant 2nd copy before a
    // unique card). Returns 0.0 when the profile carries no scores, so the
    // tiebreak is inert (bottoming byte-identical) for decks analysed without it.
    double CardScore(const std::string& name, int copy_index) const;

    // Plays a full clairvoyant game from a (post-mulligan) trial state and returns
    // the win turn, or max_turns + 1 if no win. Suppresses logging during the rollout.
    int RolloutWinTurn(GameState trial, int max_turns, int* lands_out = nullptr);

    // Discards up to `count` lands from hand to Land's Edge at `rate` damage each.
    // Used by ActivateLandsEdge for both the real game path and rollout comparisons.
    // `log` is false for throwaway trial copies so their projected pings do not leak
    // phantom ATTACK entries into the real game log.
    void DoActivateLandsEdge(GameState& state, int count, int rate, bool log = true);

    // --- Turn helpers ---

    // Play a land from hand if a land drop is available.
    bool TryPlayLand(GameState& state);

    // Play a specific land (by name) from hand. Used by the land search in TakeTurn
    // to apply a chosen candidate to the real state or a search copy. fetch_target names
    // the searched fetchland target (Plan::fetch_target); empty -> heuristic top pick.
    bool TryPlaySpecificLand(GameState& state, const std::string& name,
                             const std::string& fetch_target = "");

    // "Dig when stuck" land abilities: cycling (e.g. Lonely Sandbar) and sacrifice-to-draw
    // (e.g. Fiery Islet) to draw toward Treasure Hunt. Gated by ShouldConsiderDig (no draw
    // engine in hand, no retrace in yard, >=2 lands, Land's Edge not already lethal). Used
    // on the depth-0 / develop-when-stuck paths; full-depth committed turns instead replay
    // the search's recorded dig (PerformDig). Pre-combat only.
    void UseSurplusLandAbilities(GameState& state);

    // Mechanically perform one dig: cycle the named land from hand (is_sacrifice=false) or
    // {cost},{T},Sacrifice it from the battlefield (is_sacrifice=true), pay the cost, and
    // draw a card. Returns whether the drawn card was a LAND (so the reactive caller may
    // keep digging through lands toward action); false also if the dig could not be
    // performed. The post-draw casts on a committed line are replayed separately by the
    // caller (the DigDraw's nested breakpoint_casts).
    bool PerformDig(GameState& state, const std::string& source, bool is_sacrifice);

    // Animate untapped animatable lands (e.g. Mutavault) if mana is available.
    void AnimateLands(GameState& state, ManaPool& available);

    // Activate tap-and-pay token abilities (e.g. Sliver Hive) with spare mana.
    void ActivateTapTokens(GameState& state, ManaPool& available);

    // Build a ManaPool from all currently untapped mana sources the active player controls.
    ManaPool BuildAvailableMana(const GameState& state) const;

    // Tap permanents to pay the cost, updating the available pool in place.
    // for_creature: if false, skip creature-only mana sources (e.g. Ancient Ziggurat).
    // Returns false if the cost cannot be paid (leaves state unchanged on failure).
    bool TapForCost(GameState& state, const ManaCost& cost, ManaPool& available,
                    bool for_creature = true);

    // One payment attempt with a set of special sources HELD (reserved_mask = active-player
    // battlefield indices never tapped). TapForCost wraps this: reserved attempt first, then a
    // normal (reserved_mask=0) attempt. See ReserveEnabled / ReservableSpecialMask.
    bool TapForCostOnce(GameState& state, const ManaCost& cost, ManaPool& available,
                        bool for_creature, std::uint64_t reserved_mask);

    // Remove a spell from hand, tap sources to pay, and push a StackEntry.
    // alt_lifegain > 0 casts via an alternative cost (Invigorate / Skyshroud Cutter / Reverent
    // Silence): pay no mana and instead make the opponent gain alt_lifegain life.
    void CastSpellFromHand(GameState& state, Card& hand_card, ManaPool& available,
                           int alt_lifegain = 0, const std::string& tutor_target = "",
                           int chosen_x = 0, int own_targets = 0, int ponder_keep = -1,
                           int crackle_targets = -1,    // -1 = legacy auto-max discount
                           int splice_count = 0,        // Desperate Ritual splice count k (0 = plain)
                           const std::string& chosen_float_color = "", // Apex of Power: searched float colour
                           int enchant_target = 0);     // Aura: searched creature to enchant (0 = none)

    // Returns the battlefield index of the first creature the opponent controls, or -1.
    int FindOpponentCreature(const GameState& state) const;

    // Returns the mana cost to pay for this card this turn (spectacle cost if eligible).
    // copies (default 1) = Desperate Ritual splice multiplier (splice_count+1): scales the RAW cost
    // before the single-floor Medallion/Hinata reductions. 1 for every non-spliced cast.
    ManaCost EffectiveCost(const CardDefinition& def, const GameState& state, int copies = 1) const;
};
