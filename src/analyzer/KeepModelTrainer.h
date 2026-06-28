#pragma once
#include "../ai/KeepModel.h"
#include "../ai/MulliganProfile.h"
#include "../deck/DeckLoader.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// ============================================================================
// Mulligan KEEP-model generator (Phase 2 of the analyzer-generated keep model).
//
// Produces an interpretable decision tree (KeepModel -- a serialized tree over the
// NAMED integer features in ComputeKeepFeatures, including mulligan depth and
// on-the-play) that decides keep-vs-mulligan, REPLACING the static-filter + linear
// score keep path. It is fit OFFLINE by the analyzer, entirely from this engine's own
// clairvoyant rollout oracle -- no external ML dependency, no hand-authored rules.
//
// Method (see KeepModelTrainer.cpp for detail):
//   1. Sample fresh opening hands (one per seed). For each hand x {play,draw} x
//      mulligan-depth, compute its clairvoyant KEEP VALUE (bottom that many cards via
//      the deck's real lookahead bottoming, then roll the game out) = win turn.
//   2. Backward-induct the value of MULLIGANING at each depth (expected value of the
//      optimal keep/mulligan policy over fresh hands), then LABEL each sampled hand
//      keep iff its keep value beats the value of mulliganing once more.
//   3. Fit a shallow CART over the shared ComputeKeepFeatures vector, and pick the
//      shallowest depth whose held-out REGRET (expected win-turn cost vs the oracle
//      action, measured in turns -- not just classification accuracy) is within a small
//      margin of an unconstrained deep tree. This is the accuracy bar: the readable form
//      may not pay a meaningful win-turn cost over a strong baseline.
//
// Featurization is the SHARED ComputeKeepFeatures (KeepModel.cpp), the exact function the
// runtime evaluates, so the model is trained on and scored against identical features.
// ============================================================================

struct KeepModelTrainConfig
{
    int      depth     = 5;     // lookahead depth the rollout oracle plays at (= ANALYSIS_DEPTH)
    int      budget_ms = 20;    // per-decision virtual-ms node budget (= ANALYSIS_BUDGET)
    int      max_turns = 8;     // rollout horizon (a "win" past this is a de-facto loss)
    int      games     = 2000;  // # opening hands sampled (caller passes a Scaled() count)
    uint64_t seed      = 0;     // base seed for the sampling offset
};

// Builds the keep model for a deck. base_profile supplies the provider/required-piece
// context the rollouts use (and stop_at, which bounds the depths the model is consulted
// at); card_scores helps pick key pieces when the profile has no required_pieces. Returns
// an EMPTY model (so the runtime keeps the legacy path) if there is not enough signal to
// fit one. All progress + the fitted rules + the accuracy-bar verdict are logged to stderr.
// When MTG_KEEP_SPLIT=both and out_alt is non-null, the GINI model is returned and the REGRET
// model is written through out_alt (both fitted from the one shared kv table -- cheap second tree).
KeepModel BuildKeepModel(const Decklist& deck,
                         const MulliganProfile& base_profile,
                         const std::map<std::string, std::vector<double>>& card_scores,
                         const KeepModelTrainConfig& cfg,
                         KeepModel* out_alt = nullptr);
