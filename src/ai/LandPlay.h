#pragma once
// THE land drop (backlog C1, unification unit 6): put one land from hand onto the battlefield,
// resolving fetch / MDFC face / enters-tapped / depletion / ETB scry-surveil-bounce / Forbidden
// Orchard. This existed THREE times -- AIEngine::TryPlaySpecificLand (the executor's searched
// drop), the play_land_iter lambda inside AIEngine::TryPlayLand (the executor's greedy drop), and
// TurnSolver's PlayLandByName (the rollout) -- kept in sync by comment discipline, and the
// historical divergences were real bugs (the rollout dropped the permanent's m_number so a bounced
// Karoo land came back unnumbered; the staged-copy preference desynced the burn committed line).
//
// The three copies were NOT identical. Every way they differed is now an explicit option below
// rather than a silent difference buried in duplicated code, and each caller passes exactly what
// it did before -- so this is byte-identical, and any future convergence is a one-flag change with
// its own measurement. See docs/design/rollout-executor-lockstep.md.
#include "../core/GameState.h"
#include <cstddef>
#include <string>

class CardDefinition;
class GameLogger;

struct LandPlayOptions
{
    // Fetchland: the searched fetch choice; "" lets PerformFetch use its own heuristic pick.
    std::string fetch_target;
    // Modal double-faced land (Pathway): "back" enters as the back face's identity (locks its
    // colour). "" / "front" / any non-MDFC land keeps the front face.
    std::string land_face;
    // May the land pay its shock life to enter untapped? The autonomous engine always may; human
    // play declines when the turn spends no mana (see ApplyPlanDirect).
    bool allow_shock_pay = true;
    // Consult g_play_land_entry_chooser for the enters-tapped decision when the land offers a real
    // choice. ONLY the rollout does this today -- the executor's real land drop never asks, so
    // under --claude-play the human's land-entry choice is not applied to the realised drop.
    bool honor_entry_chooser = false;
    // Pass the land's own name to ScryTop / SurveilTop as the look SOURCE (the executor does; the
    // rollout leaves the default "Scry"/"Surveil"). Affects the reveal log and the claude-play
    // prompt label only -- the autonomous heuristic ignores the source.
    bool label_look_source = false;
    // Forbidden Orchard's on-play Spirit for the opponent. The executor's SEARCHED drop spawns it,
    // its GREEDY drop does NOT (a live divergence, see the doc), and the rollout spawns it unless
    // MTG_LEGACY_SEARCH restores the old model.
    bool spawn_orchard_spirit = true;
    // Execution-trace: record that this land was played (executor only).
    bool record_touch = false;
    // Game log (executor only; null in every rollout).
    GameLogger* logger = nullptr;
};

// Play ap.hand[hand_index] -- which must be `def`'s card and a land -- as the active player's land
// drop. Returns false only if the index is out of range; the CALLER is responsible for the land-drop
// availability check and for choosing WHICH card to play (the selection heuristics are deliberately
// not unified here: the executor's four-pass ranker and the rollout's two-pass fallback are
// different heuristics, not twins). Erases the card from hand, so any caller-held iterator into
// the hand is invalidated.
bool PlayLandFromHand(GameState& state, std::size_t hand_index, const CardDefinition& def,
                      const LandPlayOptions& opts);

// THE greedy land ranker: the hand index of the land the greedy heuristic plays this turn, or -1
// for none. Reliquary-Tower pre-pass (a no_max_hand_size land first when a Treasure Hunt is in hand
// OR the hand is already flooding past max size), then the four-pass priority -- untapped before
// tapped, multi-colour before single-colour within each -- with the closing-window sub-order
// (MTG_LAND_CLOSING_WINDOW) inside each pass and DYNAMIC tapped-ness (a fastland / shock / reveal
// land carries enters_tapped == false yet may come down tapped). Skips Apex-exiled lands and a Karoo
// bounce land with no other land in play (its bounce would return itself).
//
// Extracted because it existed twice: AIEngine::TryPlayLand (which PLAYS the land) and TurnSolver's
// `greedy_land_name` lambda (which PREDICTS it, as the search's last-resort plan-ordering tiebreak,
// and documents itself as mirroring TryPlayLand). The mirror had silently drifted in three ways --
// no flooding clause in the pre-pass, no Apex-exiled skip in the pre-pass, and no Karoo self-bounce
// skip in the four-pass -- so the search's tiebreak defaulted to a land the executor would not play.
// Returns an INDEX, not a name, so the caller plays exactly the copy the ranker chose.
int GreedyLandChoiceIndex(const GameState& state);
