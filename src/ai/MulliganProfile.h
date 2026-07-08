#pragma once
#include "../core/Card.h"
#include "KeepModel.h"
#include "ExhaustiveKeepPolicy.h"
#include <map>
#include <string>
#include <vector>

// Priority order for the two castability metrics when choosing which excess land to bottom.
enum class BottomOrder
{
    CountFirst, // minimise uncastable spell count first, then total deficit (default)
    TotalFirst, // minimise total deficit first, then uncastable count
};

// How aggressively to check for early plays when evaluating an opening hand.
// Applies until the hand has been mulliganed twice (after that, kept unconditionally).
enum class CurveCheck
{
    None,       // no check — decks with no required early play (control, all-land, etc.)
    TwoDrop,    // need at least one spell with MV ≤ 2 and ≥ 2 lands (default, generic fair decks)
    OneDrop,    // need at least one spell with MV ≤ 1 (aggressive decks that must act turn 1)
    OneAndTwo,  // need a turn-1 play (MV ≤ 1) AND at least one additional play by turn 2
                // (≥ 2 total MV ≤ 2 spells with ≥ 2 lands) — ultra-aggressive curve requirements
};

struct MulliganProfile
{
    int min_lands = 1;
    int max_lands = 5;

    // Mulligan unless at least one of these card names is in the opening hand (OR logic).
    // For AND requirements, encode as a custom KeepHand override or flag the deck for review.
    std::vector<std::string> required_pieces;

    // Minimum number of permanent mana sources (lands, mana dorks) for each color that
    // must appear in the opening hand. Only colors present in the deck are populated.
    // Spell-based mana sources (Dark Ritual, Lotus Bloom, etc.) are not counted here —
    // those require separate handling per card.
    std::map<Color, int> min_color_sources;

    // Minimum number of non-land cards in the opening hand that are castable given the lands
    // also in hand (each card evaluated independently against the full land base in hand).
    // Catches color-mismatch hands (e.g. Swamps + green cards in a Golgari deck) and
    // mana-quantity mismatches (e.g. 1 land + all 2-drops). 0 = disabled.
    int min_playable = 0;

    // Priority order when scoring excess lands for bottoming. See BottomOrder enum above.
    BottomOrder bottom_order = BottomOrder::CountFirst;

    // Curve requirement for the opening hand. See CurveCheck enum above.
    // None and TwoDrop are the most common; OneDrop and OneAndTwo exist for decks
    // whose analysis requires distinguishing T1 presence from T1+T2 coverage.
    CurveCheck curve_check = CurveCheck::TwoDrop;

    // Keep unconditionally once hand reaches this size (London mulligan stop point).
    int stop_at = 4;

    // Aether Vial target: the charge-counter value at which the Vial should stop advancing.
    // Computed by the deck analyzer from the deck's creature curve. 0 = not set.
    int vial_target_mv = 0;

    // Per-card marginal win-turn improvement for the opening hand scorer.
    // card_scores[name][0] = improvement from having the first copy in hand,
    // card_scores[name][1] = additional improvement from a second copy, etc.
    // Positive values mean the card improves the expected win turn.
    // Computed by the deck analyzer from empirical game data; empty = scoring disabled.
    std::map<std::string, std::vector<double>> card_scores;

    // Minimum hand score to keep without mulliganing (after hard filters pass).
    // hand_score = sum of marginal values for all cards in hand.
    // 0.0 with empty card_scores = scoring disabled.
    double hand_score_threshold = 0.0;

    // Analyzer-generated interpretable mulligan-KEEP model (decision tree over named features incl.
    // on-the-play and mulligan depth). When non-empty it REPLACES the static-filter + linear-score
    // keep path above (AIEngine::KeepHand); empty => legacy path (so existing decks are unchanged
    // until regenerated). Serialized in the profile JSON. See KeepModel.h / mulligan-model-direction.
    KeepModel keep_model;

    // Exhaustive bucketed keep policy (optional). When present and a hand resolves to a tabled
    // bucket composition, it OVERRIDES keep_model/static for the keep decision (see AIEngine::KeepHand).
    ExhaustiveKeepPolicy exhaustive_keep;

    // Durable human-authored keep constraints, loaded from a SEPARATE sibling file
    // (<deck>.constraints.json) -- NOT part of the profile JSON, so regenerating the profile never
    // clobbers them. Applied as a hard guard wrapping keep_model at runtime. Empty => no constraints.
    KeepConstraints keep_constraints;

    // Analyzer-trained mid-game PLAY evaluator (learned d0 replacement): ranks non-lethal turn-plans
    // by predicted expected win-turn inside TurnSolver::Solve. Loaded from the per-deck eval sidecar
    // (decks/<name>.eval.json) via AttachEvalSidecar; gated by MTG_EVAL_MODEL. Empty => heuristic
    // ranking (byte-identical). See KeepModel.h / docs/design/learned-d0-policy.md. Play only.
    MidGameEvaluator eval_model;

    // Analyzer-trained leaf VALUE model: predicts the deep-search win turn from a position, replacing
    // the search's horizon rollout (FSLineWin's SimulateToEnd) with an O(1) estimate. Loaded from the
    // per-deck value sidecar (decks/<name>.value.json) via AttachValueSidecar; gated by MTG_VALUE_MODEL.
    // Empty => the exact rollout (byte-identical). Its Score() is a WIN TURN (lower = better).
    MidGameEvaluator value_model;

    static MulliganProfile DefaultProfile() { return {}; }
};
