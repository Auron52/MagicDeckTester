#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>
#include "../deck/DeckLoader.h"
#include "../ai/MulliganProfile.h"

// Exhaustive keep/bottom policy over the bucketed hand space.
//
// Once equivalent cards are merged (see EquivalenceDiscovery), an opening hand is just a
// composition over buckets, and that composition is a SUFFICIENT STATISTIC for the keep decision.
// The distinct-hand space collapses (slivers: 386M card-hands -> ~7.8k bucket-hands), so instead of
// fitting a model we EVALUATE EVERY hand: V[H][comp] = expected goldfish win-turn of keeping exactly
// that H-card composition, averaged over R library continuations. From the V tables:
//   - optimal bottoming at mulligan m = the (7-m)-subcomposition with the lowest win-turn;
//   - the mulligan policy = exact backward induction over the hypergeometric-weighted hand
//     distribution (keep iff KeepVal(hand,m) <= value-of-mulliganing-on).
// The result is the optimal keep+bottom policy for the objective, with zero model/generalization
// error -- the only approximation is the Monte-Carlo precision of the per-hand labels (R).

struct ExhaustiveKeepConfig
{
    int      probes    = 400;   // equivalence-discovery probes (bucket resolution)
    double   threshold = 0.01;  // equivalence single-linkage merge distance
    int      depth     = 5;     // rollout search depth (match keep-model labels)
    int      budget_ms = 20;
    int      rollouts  = 100;   // R_max: the per-cell rollout CAP (label precision ceiling)
    // Adaptive (confidence-driven) sampling. When r_floor < rollouts the generator samples every cell
    // at r_floor first, then adds rollouts ONLY to cells whose argmin value could still flip a keep
    // decision (flip-prob > flip_eps against the provisional threshold), up to `rollouts`. Lossless by
    // construction (a cell stops only once its decision can't flip); r_floor==rollouts => uniform R =
    // byte-identical to the pre-adaptive path (the free unpruned A/B). Per-cell actual counts are
    // stored in the raw sidecar, so pooling (which reads counts per-entry) is unaffected.
    int      r_floor   = 0;     // R_0: initial rollouts for every cell (0 => = rollouts, i.e. uniform)
    int      r_batch   = 16;    // rollouts added per refine wave to a still-ambiguous cell
    double   flip_eps  = 0.02;  // stop refining a cell once P(decision flips vs threshold) < this
    double   se_prior  = 8.0;   // pseudo-count for shrinking a cell's sample variance toward the global
                               // (pooled) variance in the STOP gate only. Guards against a low-R cell's
                               // sample variance being spuriously small (win-turn is right-skewed, so a
                               // sample that misses the tail looks both better AND tighter -> fake
                               // confidence -> over-keep). Shrinkage vanishes as R grows and never
                               // touches the stored V/counts/policy. 0 disables (raw se).
    int      max_mull  = 3;     // deepest mulligan (sizes 7 .. 7-max_mull evaluated)
    uint64_t seed      = 0;     // ROLLOUT seed base (the "run id"); vary per run/machine for disjoint
                               // continuation streams that pool cleanly.
    uint64_t equiv_seed = 20260701ULL;  // BUCKETING seed -- FIXED across machines so the equivalence
                                        // clustering is byte-identical everywhere (decoupled from the
                                        // rollout seed, so pooled runs share buckets but not rollouts).
    int      max_turns = 8;
    bool     bottoming_enabled = false;  // bake into the written profile: use blind exhaustive bottoming
                                         // at runtime? default OFF (low-R bottoming is noise-limited and
                                         // loses to lookahead) -- set true only for a validated high-R
                                         // profile. From MTG_KEEP_BOTTOMING. Keep is always on regardless.
    std::string out_profile;    // if set, write the serialized keep policy (base profile + table) here
    std::string out_raw;        // if set, write the poolable raw sum+count sidecar (for cross-machine merge)
    std::string commit;         // play-logic identity stamped into the raw sidecar (from MTG_COMMIT)
};

// Build the exhaustive policy and print a diagnostic report (bucket list, per-size hand counts,
// exact optimal-policy expected win-turn vs the static keep rule, keep rates, and the decisions on
// notable marginal hands) to `os`. Returns nothing structured yet -- this is the validation pass
// before the policy is serialized into a profile for the in-game A/B.
void RunExhaustiveKeep(std::ostream& os, const Decklist& deck, const MulliganProfile& profile,
                       const ExhaustiveKeepConfig& cfg);

// Offline pooling: read the poolable raw sidecars written by RunExhaustiveKeep (out_raw), reject any
// that disagree on play-logic/bucket-map/deck or reuse a seed_base, element-wise-sum the compatible
// ones, and rebuild the exact keep policy at the pooled R via the SAME code the in-run path uses.
// Writes `out_profile` (runtime policy) and `out_raw` (the re-poolable merged sidecar) if set. Enables
// combining a second machine's rollouts with this one's. Driven by MTG_KEEP_MERGE / MTG_MERGE_INPUTS.
void RunKeepMerge(std::ostream& os, const Decklist& deck, const MulliganProfile& profile,
                  const std::vector<std::string>& raw_paths,
                  const std::string& out_profile, const std::string& out_raw,
                  bool bottoming_enabled = false);
