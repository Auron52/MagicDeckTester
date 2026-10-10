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
    // "You may have this land enter tapped. If you do, you get N rad counters." (Mariposa Military
    // Base, etb_optional_tapped_rad). -1 == the heuristic default (DECLINE); 0 / 1 == the SEARCH
    // picked, one Plan variant per mode (Plan::rad_mode). A searched decision rather than a
    // hardcoded one on the user's instruction, 2026-09-02: "We probably shouldn't always decline the
    // rad counters, since it draws more cheaply with them out."
    //
    // It is a genuine trade, which is exactly why it is searched and not assumed either way:
    // entering tapped costs a turn of the land's mana and takes on the rad MILL, against a
    // {1}-per-counter discount on its own draw ability. The mill is modelled (see ApplyRadMill), so
    // the mode carries its real cost -- without it, accepting would be strictly-upside and the
    // search would take it every time, which is the over-acceptance class that shows up as
    // [fd-diverge].
    int rad_mode = -1;
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
    // The land comes from the library TOP (Courser of Kruphix), not the hand. Set by
    // PlayLandFromLibraryTop itself; read only by the log (the PLAY_LAND line names its source).
    bool from_library_top = false;
};

// Play ap.hand[hand_index] -- which must be `def`'s card and a land -- as the active player's land
// drop. Returns false only if the index is out of range; the CALLER is responsible for the land-drop
// availability check and for choosing WHICH card to play (the selection heuristics are deliberately
// not unified here: the executor's four-pass ranker and the rollout's two-pass fallback are
// different heuristics, not twins). Erases the card from hand, so any caller-held iterator into
// the hand is invalidated.
bool PlayLandFromHand(GameState& state, std::size_t hand_index, const CardDefinition& def,
                      const LandPlayOptions& opts);

// Play the active player's library TOP as the land drop (Courser of Kruphix: "You may play lands from
// the top of your library"). `def` must be the top card's definition. THE SAME BODY as
// PlayLandFromHand (one templated core) -- fetch, MDFC face, enters-tapped, depletion, every land
// ETB, the Karoo bounce and the landfall dispatch -- so a land played from the top is
// indistinguishable from one played from hand once it is on the battlefield. The card is popped
// from the library first (a raw pop, not a draw). Returns false only on an empty library; the CALLER
// checks the drop and the permission (PlayableTopLandNumber, SpellEffects.h), exactly as a hand
// caller checks the drop and picks the copy.
bool PlayLandFromLibraryTop(GameState& state, const CardDefinition& def, const LandPlayOptions& opts);

// THE greedy land ranker: the hand index of the land the greedy heuristic plays this turn, or -1
// for none. Reliquary-Tower pre-pass (a no_max_hand_size land first when a Treasure Hunt is in hand
// OR the hand is already flooding past max size), then the four-pass priority -- untapped before
// tapped, multi-colour before single-colour within each -- with the closing-window sub-order
// (MTG_LAND_CLOSING_WINDOW) inside each pass and DYNAMIC tapped-ness (a fastland / shock / reveal
// land carries enters_tapped == false yet may come down tapped). Skips Apex-exiled lands and a Karoo
// bounce land with no other land in play (its bounce would return itself).
//
// MTG_LAND_IDLE_TAPPED_FIRST (default OFF, measuring) swaps the untapped and tapped halves of the
// four passes on the FIRST drop of the game when the untapped land's mana provably cannot be spent
// this turn -- deferring it is free then, and it buys an extra mana on every later turn. Scoped to
// the empty board because that is the only state where "no mana sink exists" is cheap to prove.
//
// Extracted because it existed twice: AIEngine::TryPlayLand (which PLAYS the land) and TurnSolver's
// `greedy_land_name` lambda (which PREDICTS it, as the search's last-resort plan-ordering tiebreak,
// and documents itself as mirroring TryPlayLand). The mirror had silently drifted in three ways --
// no flooding clause in the pre-pass, no Apex-exiled skip in the pre-pass, and no Karoo self-bounce
// skip in the four-pass -- so the search's tiebreak defaulted to a land the executor would not play.
// Returns an INDEX, not a name, so the caller plays exactly the copy the ranker chose.
int GreedyLandChoiceIndex(const GameState& state);

// THE GREEDY LAND RANKER OVER BOTH SOURCES (Courser of Kruphix, 2026-10-15). GreedyLandChoiceIndex
// ranks the HAND; with a play-from-top permanent out, the library top is one more candidate, judged
// by the very same four passes (untapped before tapped, multi before single -- and the same Karoo
// self-bounce skip), and it wins a tie WITHIN a pass when the provider says so
// (DecisionProvider::TopLandSourceFirstOnTie, default true: a top play keeps the hand copy, so it
// nets a card). {hand_index, false} for a hand land, {-1, true} for the top, {-1, false} for none.
// Used ONLY at the permitted greedy sites -- the depth-0 runner (AIEngine::TryPlayLand) and the
// beyond-horizon playout (SimulateLandPlay) -- and the enumeration's last-resort tie-break. With no
// playable top it returns exactly GreedyLandChoiceIndex's pick, so every other deck is unchanged.
struct GreedyLandPick { int hand_index = -1; bool from_top = false; };
GreedyLandPick GreedyLandChoice(const GameState& state);

// THE ACTIVATION-COLOUR NEED (MTG_ROLLOUT_LAND_ACT_COLOR -- the rollout's land drop; see the call
// site in TurnSolver's SimulateLandPlay). Bit i (i = Color: W U B R G C) is set when a permanent the
// active player controls has a battlefield activated ability whose EFFECTIVE cost carries a hard
// pip of that colour, and no permanent the player controls can produce it (BoardCanProduceColor:
// lands, rocks, dorks, and land Auras -- whose "any colour" is never {C}). Hybrid and Phyrexian
// pips are not hard needs and are excluded. 0 = no unmet need.
int UnmetActivationColorMask(const GameState& state);

// The hand index of the land that supplies an unmet activation colour, or -1 when there is no
// unmet need or no land in hand supplies one. If `preferred` (a hand index another ranker already
// chose) supplies one, it is returned unchanged; otherwise the pick is the first such land in the
// rollout's legacy two-pass order (multi-colour first, then any, hand order), under the same skips
// (Apex-exiled, Karoo with no other land). Because that order is the legacy order restricted to the
// supplying lands, a legacy pick that already supplies the need is exactly what this returns.
int ActivationColorLandIndex(const GameState& state, int preferred = -1);
