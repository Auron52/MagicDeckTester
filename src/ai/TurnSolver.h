#pragma once
#include "../core/GameState.h"
#include "../cards/CardDatabase.h"
#include "SearchBudget.h"
#include <chrono>
#include <string>
#include <vector>

class TranspositionTable;  // per-decision SimulateToEnd memo (see TranspositionTable.h)

// A single atomic play the active player can make in a main phase. Unifies the
// formerly-separate action sources (hand cast, Aether Vial activation, Land's Edge
// discard, graveyard retrace) so enumeration, evaluation and execution each handle
// one collection rather than several parallel special cases.
//
// The valuation scalars (eval, direct_damage, ...) are populated by CollectActions
// at enumeration time and read by the subset evaluators; the apply/execute paths
// re-derive costs and effects from the card definition, so they ignore those fields.
struct Action
{
    enum class Kind
    {
        CastFromHand,        // cast a spell from hand
        CastFromGraveyard,   // Retrace: cast from graveyard, discarding a land as an additional cost
        ActivateVial,        // Aether Vial: put a creature from hand onto the battlefield
        DiscardToLandsEdge,  // discard `discard_lands` lands from hand to a Land's Edge for damage
        PlayLand,            // play `card_name` as the turn's land drop (recorded in a draw
                             // breakpoint so commit-the-line replay reproduces a land revealed
                             // and played post-draw, e.g. a Light Up the Stage land)
        DigDraw,             // activate a surplus-land card-draw ability to dig toward action
                             // (Treasure Hunt): cycle a land from hand (dig_sacrifice=false) or
                             // {cost},{T},Sacrifice a land in play (dig_sacrifice=true). Draws one
                             // card, then re-solves the phase exactly like a DrawUntilNonland draw
                             // engine, so a dug Treasure Hunt is cast the same turn.
    };

    Kind        kind           = Kind::CastFromHand;
    std::string card_name;             // source card (creature name for ActivateVial)
    int         hand_index     = -1;   // hand index of the source/creature at enumeration time
    ManaCost    cost;                  // effective mana cost (enumeration feasibility only)
    bool        sacrifice_land = false;// additional cost: sacrifice a land (e.g. Shard Volley)
    int         discard_lands  = 0;    // Retrace = 1; for DiscardToLandsEdge = lands to discard
    int         vial_bf_index  = -1;   // ActivateVial: battlefield index of the tapped Vial
    bool        dig_sacrifice  = false;// DigDraw: true = sacrifice a land in play (sac-draw, e.g.
                                       // Fiery Islet); false = cycle a land from hand (e.g. Lonely
                                       // Sandbar). card_name = the source land; cost = its
                                       // cycling_cost / sacrifice_draw_cost.
    bool        alt_cost       = false;// CastFromHand via an alternative cost (Invigorate /
                                       // Skyshroud Cutter / Reverent Silence): pay no mana and
                                       // instead make the opponent gain alt_lifegain life (-> that
                                       // much damage with Tainted Remedy). cost is empty.
    int         alt_lifegain   = 0;    // opponent lifegain paid as the alt cost (see alt_cost)
    std::string tutor_target;          // CastFromHand of a tutor: the specific library card to
                                       // fetch. When the heuristic is UNSURE, CollectActions emits
                                       // one variant per candidate (same hand_index -> mutually
                                       // exclusive) so the search picks; one variant when it is
                                       // sure. Empty for non-tutors (PerformTutor falls back to
                                       // the heuristic's top pick).
    int         chosen_x       = 0;    // CastFromHand of an {X} spell: the X value chosen at
                                       // enumeration. The provider (XCandidates) narrows the X
                                       // range; CollectActions emits one variant per candidate
                                       // (sharing hand_index -> mutually exclusive) so the search
                                       // picks. cost already includes the X generic; chosen_x is
                                       // carried so the cast (rollout AND executor) scales the
                                       // effect (X damage) identically. 0 = not an X spell.

    // Valuation / win-check scalars (mirror the former per-function Candidate fields).
    int  eval                  = 0;
    int  direct_damage         = 0;
    bool is_noncreature        = true;
    int  card_mv               = 0;
    int  vial_attack_power     = 0;    // power this turn if a hasted Vial drop (wins_this_turn)
    bool is_draw               = false;// DrawSpell / DrawX (Plan-B draw-early variants)
    bool has_spectacle         = false;// has a spectacle alternate cost (Plan-B)
    bool is_draw_until_nonland = false;// Treasure Hunt (Solve's LE/TH combo valuation)
    int  discard_land_damage   = 0;    // if this card IS a Land's Edge being cast (Solve)

    // Commit-the-line (MTG_FULL_DEPTH) faithful replay of DYNAMIC draw turns: the
    // exact casts the search's draw-breakpoint re-solve made right AFTER this card's
    // resolution revealed new cards (DrawSpell staging / DrawUntilNonland / the
    // cascade target it free-cast). Recorded by ApplyPlanDirect when building the
    // committed line, in execution order, nested (a recorded cast that is itself a
    // draw engine carries its own breakpoint_casts). AIEngine replays this script
    // verbatim instead of re-solving, so the realised turn matches the searched one
    // (the re-solve diverged on land-drop/mana state -> phantom wins). Empty for
    // non-draw cards and for decks/turns with no breakpoint. See
    // project-full-depth-search (TH oracle class).
    std::vector<Action> breakpoint_casts;
};

// Finds the optimal set of spells to cast in one main phase by exhaustive
// subset enumeration (O(2^|hand|), tractable for hand sizes up to ~15).
//
// Evaluation accounts for tempo: creatures are valued at power * expected
// remaining attacks, not just immediate damage. This prevents the solver
// from always preferring burn over board development.
//
// Sacrifice-land spells are always placed last in the execution order so
// that other spells have already tapped their lands before the sacrifice
// fires, minimising the real cost of the additional cost.
class TurnSolver
{
public:
    struct Plan
    {
        // The set of plays to execute this main phase. Execution order is canonical
        // (ActivateVial -> hand casts -> sacrifice-land casts -> graveyard casts ->
        // Land's Edge discards), applied by ApplyPlanDirect / AIEngine::TakeTurn.
        std::vector<Action> actions;
        int  value          = -1;   // -1 = nothing castable
        bool wins_this_turn = false;

        // Cast-ordering search (C): when true, ApplyPlanDirect executes the non-sacrifice
        // hand casts in `actions` VECTOR ORDER instead of the canonical enabler-first
        // bucketing -- so the search can explore orderings the canonical heuristic batches
        // wrong (e.g. enabler/destroy-all-payload interleaving: Tainted Remedy -> Reverent
        // Silence -> Tainted Remedy -> Reverent Silence, where casting both Remedies first
        // lets the first Reverent wipe both). Default false => canonical order (byte-
        // identical). Set only by the gated ordering enumeration (MTG_SEARCH_ORDER /
        // MTG_UNPRUNED), which dedups orderings by end-of-phase state.
        bool searched_order = false;

        // Land drop folded into the plan (searched alongside spells). When
        // land_decided is true the executor plays exactly land_to_play this turn
        // ("" == a deliberate defer / no land available); when false the land was
        // not searched (depth-0 static Solve plans) and the executor falls back to
        // the greedy land heuristic. Folding the land into the plan keeps the land
        // choice consistent between the real game and the lookahead rollout — the
        // rollout re-searches lands every turn exactly as the real game does.
        bool        land_decided = false;
        std::string land_to_play;

        // Fetchland search target (Pass 2 of the real-fetch model): when land_to_play is a
        // fetchland and FetchCandidates returned MORE THAN ONE legal target, the land
        // enumeration emits one Plan variant per candidate, each carrying the chosen target
        // here so the rollout (PlayLandByName -> PerformFetch) and the realised game
        // (TryPlaySpecificLand) fetch the SAME land. Empty == use the heuristic's top pick
        // (the single-candidate / Pass-1 case). Parallels Action::tutor_target for the
        // [[heuristic-then-search]] "heuristic narrows, search decides" land choice.
        std::string fetch_target;

        // Commit-the-line (MTG_FULL_DEPTH): the casts the search's draw-breakpoint
        // re-solve(s) made this phase, after a main `actions` draw engine revealed new
        // cards. Top-level (triggered by the main plan); each entry nests its own
        // breakpoint_casts. Populated by ApplyPlanDirect's out_breakpoint only when
        // building the committed line; AIEngine replays it verbatim (no re-solve) so
        // the realised turn matches the search. Empty for static turns. See
        // Action::breakpoint_casts and project-full-depth-search.
        std::vector<Action> breakpoint_actions;

        bool empty() const { return actions.empty(); }
    };

    // Returns the highest-value feasible plan for one main phase.
    // Uses a static evaluation function (no lookahead).
    static Plan Solve(const GameState& state, bool is_pre_combat);

    // Enable per-pass per-candidate trace output for top-level T1 decisions.
    static void SetTraceSolve(bool enable);
    static bool GetTraceSolve();

    // --- External-controller hooks (Claude-play / human-play prototype) ---------
    // Expose the same candidate enumeration and plan application the solver uses, so
    // an external decision provider can be offered the legal main-phase plans and have
    // its chosen plan executed identically to a searched one. EnumerateMainPlans folds
    // the land choice for a pre-combat main (each Plan carries its land_to_play), just
    // like the search; ApplyPlan runs the canonical execution order (land, casts, Land's
    // Edge) via the same path the rollouts use. Not used by the normal AI path.
    static std::vector<Plan> EnumerateMainPlans(const GameState& state, bool is_pre_combat);
    static void              ApplyPlan(GameState& state, const Plan& plan, bool is_pre_combat);

    // Returns the plan that leads to the earliest win, evaluated by simulating
    // the rest of the game for each candidate play at this turn.
    // depth=0 falls back to Solve.  depth=1 simulates one turn ahead using Solve
    // for all subsequent decisions; depth=2 uses depth=1 for subsequent decisions;
    // and so on.
    //
    // budget: deterministic work budget (see SearchBudget). All rollout work is
    // counted against it; nullptr means unlimited. enforce_budget governs whether
    // THIS invocation may stop iterative deepening when the budget runs out:
    //   - true  (top-level decision): applies the start-gate / overrun-guard and
    //            commits the deepest fully-completed pass.
    //   - false (rollout sub-search):  runs every pass to completion regardless,
    //            only consuming from the shared budget (preserves rollout
    //            fidelity, mirroring the old time_point::max() deadline).
    //
    // second_main: when true, the simulation plays a post-combat (second) main
    // phase each turn (greedy in the rollout), and a top-level is_pre_combat=false
    // call is treated as a real second-main decision (no combat is re-simulated).
    // Off for most decks; on only for ones whose combat enables second-main plays
    // (spectacle unlocked by combat damage, lands untapped in combat). See
    // AIEngine::SetSearchPostCombat.
    //
    // tt: per-decision transposition table memoizing SimulateToEnd. The enforcing
    // top-level call creates one when none is supplied and threads it through the
    // whole recursion; rollout sub-searches forward the table they were given.
    //
    // When is_pre_combat is true and the active player still has a land drop, the
    // land choice is folded into the candidate enumeration (each candidate carries
    // its land_to_play) and searched alongside the spells. The same fold runs in
    // the rollout, so the land decision is consistent between real game and rollout.
    // out_committed_win / out_committed_sub_depth (optional): report the committed
    // pass's exact win turn and the rollout sub_depth that proved it, so the caller
    // can detect non-convergence (a later turn's verified win exceeding an earlier
    // one). A win-this-turn reports (turn, depth-1); an empty / depth<=0 decision
    // reports (max_turns+1, 0) i.e. "no verified win".
    static Plan SolveWithLookahead(const GameState& state, bool is_pre_combat,
                                   int depth, int max_turns = 20,
                                   SearchBudget* budget = nullptr,
                                   bool enforce_budget = true,
                                   bool second_main = false,
                                   TranspositionTable* tt = nullptr,
                                   int* out_committed_win = nullptr,
                                   int* out_committed_sub_depth = nullptr);

    // One committed phase of a full-depth line: the plan to execute and whether it
    // is the pre-combat (true) or post-combat second main (false) of its turn.
    struct PhasePlan
    {
        bool is_pre_combat = true;
        Plan plan;
    };

    // The optimal line found by a full-depth search: the win turn it achieves and
    // the exact sequence of per-turn phase plans (pre-combat, then second main when
    // the deck uses one) over the fully-searched turns. Empty `phases` means no
    // play was searched (depth 0 or no candidates).
    struct SearchLine
    {
        int win_turn = 0;
        std::vector<PhasePlan> phases;
    };

    // FULL-DEPTH search (experimental, env-gated via MTG_FULL_DEPTH). Unlike
    // SolveWithLookahead — which iterative-deepens the PRE-COMBAT decision and
    // approximates every future turn with a reduced-depth rollout plus a GREEDY
    // second main — this fully searches `depth` COMPLETE turns: at every turn it
    // branches over both the pre-combat plans (EnumeratePlansWithLand) and, when
    // second_main is set, the post-combat plans (EnumeratePlans), advancing the
    // turn and recursing. Beyond `depth` turns a greedy rollout (SimulateToEnd at
    // depth 0) estimates the tail. The objective is the EARLIEST win turn, with
    // branch-and-bound pruning: a plan that wins the current turn is the hard
    // floor, and any branch that cannot beat the best win found so far is abandoned.
    // Deterministic (no RNG), so thread-invariant.
    //
    // Returns the WHOLE optimal line (commit-the-line), not just the next plan, so
    // the caller can REPLAY the exact searched sequence instead of re-deciding each
    // turn. Re-deciding makes the realised win drift below the searched win (the
    // search idles on an optimistic continuation its turn-by-turn policy never
    // reproduces); replaying the committed line makes realised == searched within
    // the horizon. `state` must be positioned at the start of a pre-combat main.
    //
    // `tt` memoizes the greedy tail rollouts (leaf SimulateToEnd) across the whole
    // branch-and-bound tree, exactly as SolveWithLookahead does — the deep search
    // revisits the same leaf states many times. When null, a per-call local table
    // is created; the result is byte-identical either way (SimulateToEnd is a pure
    // deterministic function of its key), the table only avoids recompute.
    //
    // `budget` drives iterative deepening: the search runs passes of 1..depth complete
    // turns and a deterministic start gate skips a pass that won't fit the remaining
    // budget, committing the deepest pass that did fit. It also stops early at the
    // first pass that finds a win VERIFIED within its horizon (a deeper pass can only
    // push the win later), which is lossless. nullptr (or a generous budget) still
    // commits a verified win at the shallowest pass that finds it; with no verified
    // win it runs every pass and commits depth -- byte-identical to a single search.
    //
    // out_committed_depth (optional) receives the depth actually searched for the
    // committed line (= the last pass run). The caller needs it to tell a VERIFIED
    // win (win_turn <= turn + committed_depth - 1) from a greedy-tail ESTIMATE: the
    // start gate can commit a pass shallower than `depth`, so the nominal depth would
    // misjudge a shallow estimate as verified.
    static SearchLine FullSearchLine(const GameState& state, int depth,
                                     int max_turns, bool second_main,
                                     TranspositionTable* tt = nullptr,
                                     SearchBudget* budget = nullptr,
                                     int* out_committed_depth = nullptr);

    // ---- Rule-miner: enumerate-all-earliest-wins (offline diagnostic) -------------------
    // For the CURRENT pre-combat main, score EVERY candidate top-level play (the same
    // EnumeratePlansWithLand candidates the search ranks -- run with MTG_SEARCH_ORDER=1 to
    // also expand cast ORDERINGS) by the EARLIEST full-game win turn it leads to: apply the
    // play, run its combat, then full B&B-search the rest of the game (no cross-plan pruning,
    // so each candidate gets its TRUE earliest win, not the first one the search commits).
    // Emitting all candidates -- and especially the set tied at the minimum win turn -- lets
    // the analyzer mine the COMMON structure of optimal lines (cast order, which land, which
    // target) to ground ordering/targeting heuristics. EXPENSIVE (a full rollout per
    // candidate); single-game offline use only, never in the hot search. See analyze-deck 5g.
    struct EarliestWinCandidate
    {
        std::vector<std::string> cast_order;   // non-sacrifice hand casts in execution order
        std::vector<std::string> sac_casts;    // sacrifice-land casts (Shard Volley) -- after
        std::string land;                      // land played this turn ("" = none)
        std::string fetch;                     // fetch target if land is a fetchland ("" = n/a)
        bool        searched_order = false;    // true => cast_order is a searched permutation
        int         win_turn       = 0;        // earliest full-game win if this play is committed
    };
    struct EarliestWinReport
    {
        int turn     = 0;                      // the decision turn
        int earliest = 0;                      // min win_turn over all candidates
        std::vector<EarliestWinCandidate> candidates;
    };
    static EarliestWinReport EnumerateEarliestWins(const GameState& state, int max_turns,
                                                   bool second_main);
};
