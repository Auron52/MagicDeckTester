#pragma once
#include "../core/Card.h"
#include "KeepModel.h"
#include "ExhaustiveKeepPolicy.h"
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>

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

// Per-deck PLAY policy (`value_play` block in <deck>.value.json). Keeps all value-leaf play settings in one
// self-contained place so play can be driven by the deck's file instead of a global --depth/--budget-ms.
// GOVERNANCE: a block DRIVES play ONLY when `enabled==true`, which is set by a deliberate recommend->accept
// exercise (the agent recommends target_depth/budget from the measured table; the user accepts). An UNENABLED
// block (enabled==false, the default the writer emits) is a pure RECOMMENDATION: it records the numbers but
// does NOT drive or lock play at all -- ResolvePlaySettings ignores it, so writing recommendations for every
// deck is byte-identical. Once enabled it LOCKS: an explicit --depth/--budget-ms (or a batch case's explicit
// depth) is a conflict ERROR unless --ignore-play-profile. present()==false (target_depth==0) => no block.
// See docs/design/value-leaf-fallback-table.md "SETTLED DESIGN".
struct ValuePlay
{
    int    target_depth = 0;              // 0 => absent (present()==false)
    int    budget_ms    = 0;
    bool   enabled      = false;          // true => the ADOPTED policy: drives + locks play. false => recorded
                                          //         recommendation only (does NOT affect play; byte-identical).
    double escalation_fresh_frac = -1.0;  // budget renewal, per-deck; -1 = off (legacy shared budget)
    // Escalation value-guided beam (per-deck; see docs/design/escalation-beam-verify.md). The heuristic
    // escalation reorders each node's plans by the probe's recorded value ranking and expands only the top
    // `beam_width` at nodes within `beam_leafdepth` plies of the leaf -- cutting the deep rollout frontier
    // while the top plies (the committed play) keep full exploration. 0 => off (byte-identical, full frontier).
    int    beam_width     = 0;             // 0 => off; >0 => keep top-N value-ranked plans near the leaf
    int    beam_leafdepth = 2;             // beam only nodes at remaining depth <= this (protect the top plies)
    // Informative only (not read at runtime): how fast the leaf / heuristic search is at each depth, and a
    // human-readable regime tag. Kept so target_depth can be re-derived if the budget changes.
    std::string         regime;           // "light" | "heavy"
    std::vector<double> leaf_cost_ms;     // indexed by depth (index 0 unused)
    std::vector<double> heur_cost_ms;
    bool present() const { return target_depth > 0; }
    bool drives()  const { return target_depth > 0 && enabled; }   // actually steers play (adopted)
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
    // SHARED (shared_ptr<const>): the table is large (antilife ~1GB / 366k entries) and read-only after
    // build, so every profile/AIEngine copy shares ONE instance instead of deep-copying it. This is what
    // keeps a THREADS=N goldfish batch from holding N copies (the per-thread AIEngine copies the profile);
    // sharing turns N*1GB into 1*1GB. Null (or ->empty()) => no policy; use HasExhaustiveKeep() to test.
    std::shared_ptr<const ExhaustiveKeepPolicy> exhaustive_keep;
    bool HasExhaustiveKeep() const { return exhaustive_keep && !exhaustive_keep->empty(); }

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

    // Per-model "trust depth" for the value leaf (from the `value_trust_depth` key in <deck>.value.json).
    // The raw value leaf is a WEAK-but-cheap evaluator: measured, it reaches converged-heuristic quality only
    // at ~d5, and on an UNVERIFIED committed line below this depth it plays materially worse than the heuristic
    // (see docs/design/learned-d0-policy.md, 2026-07-11). The hybrid keeps the value-leaf line without the
    // (clairvoyant) heuristic escalation when its committed depth >= this; below it (and not a verified win) it
    // escalates to one heuristic search on the remaining budget. 0 (unset) => always eligible to escalate.
    // Decks whose value-leaf matches the heuristic at d5 (verified-win-dominated: knights/slivers) set 5.
    int value_trust_depth = 0;

    // Per-model "never fall back to the value-leaf line" flag (`value_no_fallback` key in <deck>.value.json).
    // The hybrid escalates an unverified line to the heuristic, then TAKES that result only if it clears the
    // take-crossover (heuristic reached deep enough to beat the value-leaf line, ~ committed - kValueTrustOffset);
    // otherwise it FALLS BACK to the value-leaf line. For a deck whose leaf is weaker than the uniform crossover
    // assumes (Hinata: measured LP 6.0250 with fall-back vs 5.9917 always-take, s1001), that fall-back keeps a
    // worse line ~1/3 of escalations. Setting this true makes the deck ALWAYS take the escalation (never fall
    // back), at ANY depth. Default false => the crossover rule is unchanged (byte-identical). Decks whose leaf
    // IS good at d5 (knights/slivers) must leave this false. See hinata-escalation-budget-restore FOLLOW-UP 13.
    bool value_no_fallback = false;

    // Table-driven take-crossover (`value_fallback_crossover` in <deck>.value.json), DERIVED from the measured
    // value-leaf x heuristic depth table: value_fallback_take_at[c] = the shallowest heuristic depth hc* that
    // BEATS the leaf committed at depth c (min{hc: H_hc < V_c}). At runtime, after escalating a leaf line
    // committed at depth c, the hybrid TAKES the heuristic iff hcommitted >= value_fallback_take_at[c], else it
    // keeps the leaf. This REPLACES the uniform offset proxy (kValueTrustOffset assumed every c falls back at
    // c-offset) with the per-committed-depth measured crossover -- weak-leaf decks fall back sooner, strong-leaf
    // decks keep the leaf longer. Indexed by committed depth (index 0 unused); clamp c to [1, size-1]. A value >
    // value_fallback_max_depth means "never fall back at this c" (leaf >= any heuristic). Empty => no table =>
    // use the legacy uniform offset + value_no_fallback. See value-leaf-table-in-metadata; docs learned-d0-policy.
    std::vector<int> value_fallback_take_at;   // [committed depth c] -> hc*; empty = no table
    int value_fallback_max_depth = 0;          // table covers depths <= this (clamp beyond)

    // Per-deck play policy (see the ValuePlay comment above). Absent (present()==false) by default => the
    // resolver falls back to BuiltinDefaultPlay(). Parsed from <deck>.value.json by AttachValueSidecar.
    ValuePlay value_play;

    // The universal fallback play policy for a fully-bare run with no ENABLED value_play: exactly the current
    // d5 gate setting (depth 5, budget 20 virtual-ms). Not a block (never stored in a profile), just the
    // resolver's default. See ResolvePlaySettings below.
    static ValuePlay BuiltinDefaultPlay()
    {
        ValuePlay v;
        v.target_depth = 5;
        v.budget_ms    = 20;
        v.enabled      = false;
        return v;
    }

    // Clamp `committed` to the measured range and return hc* (the heuristic depth at/above which to take the
    // escalation). Precondition: !value_fallback_take_at.empty().
    int FallbackTakeAt(int committed) const
    {
        const int hi = static_cast<int>(value_fallback_take_at.size()) - 1;   // max measured committed depth
        int c = committed < 1 ? 1 : (committed > hi ? hi : committed);
        return value_fallback_take_at[c];
    }

    static MulliganProfile DefaultProfile() { return {}; }
};

// Effective play settings resolved from an explicitly-requested depth/budget and the profile's value_play.
// `source` is a human-readable tag ("cli" | "value_play(derived)" | "value_play(default)" |
// "cli(--ignore-play-profile)") that callers PRINT so the run's play settings are never hidden.
struct PlaySettings
{
    int         depth     = 0;
    int         budget_ms = 0;
    const char* source    = "cli";
};

// Resolve the effective (depth, budget_ms) from an explicit request and the deck's value_play.
//   req_depth / req_budget < 0  => "not given" (no --depth / no --budget-ms / manifest omitted the key).
//   ignore_play                 => bypass an ENABLED block entirely (the --ignore-play-profile escape hatch).
// Only an ENABLED value_play (drives()==true) participates -- an unenabled block is a pure recommendation and
// is ignored here (so recording recommendations is byte-identical).
//
// DEPTH and BUDGET are treated ASYMMETRICALLY, because they are different kinds of thing:
//   * DEPTH is the PLAY POLICY (how deep the deck thinks). An enabled block OWNS it -- overriding it with
//     --depth is the ambiguous/guarded case => ERROR unless --ignore-play-profile.
//   * BUDGET is a RESOURCE KNOB (how long it may think), independent of the policy. --budget-ms freely
//     OVERRIDES the block's budget while keeping the profile's depth -- e.g. the overnight deep bug-net runs
//     the SHIPPED depth under a generous budget (burn d6 / budget-80). No error, and the [play] line shows
//     the per-field source.
// With no enabled block (or --ignore-play-profile), each field falls to the explicit CLI value or its normal
// default (0), byte-identical to today; a fully bare run with no enabled block => the built-in d5/20 default.
// See the truth table in docs/design/value-leaf-fallback-table.md "SETTLED DESIGN".
inline PlaySettings ResolvePlaySettings(const MulliganProfile& p,
                                        int req_depth, int req_budget, bool ignore_play)
{
    const bool depth_given   = (req_depth  >= 0);
    const bool budget_given  = (req_budget >= 0);
    const bool enabled_block = p.value_play.drives();   // adopted policy only; recommendations are ignored

    if (enabled_block && !ignore_play)
    {
        if (depth_given)
        {
            throw std::runtime_error(
                "value_play depth is ENABLED for this deck (target_depth=" +
                std::to_string(p.value_play.target_depth) + "); omit --depth to use it (pass --budget-ms "
                "alone to override just the budget), or --ignore-play-profile to override the depth too.");
        }
        // Profile depth (the policy); budget from the block unless --budget-ms overrides the resource knob.
        const int budget = budget_given ? req_budget : p.value_play.budget_ms;
        return { p.value_play.target_depth, budget,
                 budget_given ? "value_play(depth)+cli(budget)" : "value_play" };
    }
    if (depth_given || budget_given)
    {
        // No enabled block, or --ignore-play-profile: explicit CLI wins; omitted half -> normal default (0).
        return { depth_given ? req_depth : 0, budget_given ? req_budget : 0,
                 ignore_play ? "cli(--ignore-play-profile)" : "cli" };
    }
    // Fully bare run, no enabled block: the built-in d5/20 default.
    const ValuePlay d = MulliganProfile::BuiltinDefaultPlay();
    return { d.target_depth, d.budget_ms, "default" };
}
