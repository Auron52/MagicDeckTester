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
    int      depth     = 5;     // LABEL-ROLLOUT search depth (match keep-model labels). A pure COST knob:
    int      budget_ms = 20;    // it sets how hard each cell's rollouts search, nothing else.
    // DISCOVERY (equivalence-bucketing) search settings -- deliberately SEPARATE from the label-rollout
    // pair above, and sourced from the deck's SHIPPED PLAY settings rather than from mull_gen_*.
    //
    // Why they must not be the same knob (measured 2026-08-15): bucketing asks "are these two cards
    // interchangeable for THIS deck?", which is a property of the deck AS PLAYED -- it has no business
    // moving when we pick a cheaper label-rollout setting. When they were one field, lowering the gen
    // depth silently re-bucketed the deck, and because hand count grows as C(K+6,7) a WEAKER setting
    // could cost far MORE: on slivers, K went 10 -> 13 -> 11 for gen d3/d2/d1, i.e. 14,117 -> 61,001
    // distinct hands (4.3x) at d2. It also made `mull_gen_*` silently non-comparable across machines
    // and made any gen-depth A/B measure two things at once.
    //
    // Keeping them separate is what makes `mull_gen_depth` / `mull_gen_budget_ms` a pure cost knob, and
    // what lets the documented MTG_EQUIV_DEPTH / MTG_EQUIV_BUDGET pins actually work on the recipe path.
    int      equiv_depth     = 5;
    int      equiv_budget_ms = 20;
    int      rollouts  = 100;   // R_max: the per-cell rollout CAP (label precision ceiling). Must be >= 2:
                                // a cap of 1 leaves no room for a floor below it, and the generator has
                                // only the adaptive/continuous path (a cap of 1 is rejected, not downgraded).
    // Adaptive (confidence-driven) sampling -- the ONLY execution path. The generator samples every cell
    // at r_floor first, then adds rollouts ONLY to cells whose argmin value could still flip a keep
    // decision (flip-prob > flip_eps against the provisional threshold), up to `rollouts`. Lossless by
    // construction (a cell stops only once its decision can't flip). Per-cell actual counts are stored in
    // the raw sidecar, so pooling (which reads counts per-entry) is unaffected.
    // r_floor is DERIVED, not configured: RunExhaustiveKeep clamps it into [1, rollouts-1] (0 => 2). The
    // old "r_floor >= rollouts => uniform R" escape is deleted -- it took the continuous pool, the journal
    // and the slow/projection reports with it (docs/design/keepgen-no-off-switches.md).
    int      r_floor   = 0;     // R_0: initial rollouts for every cell (0 => the default floor of 2)
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
    bool     adaptive_bottom = false;    // EXPERIMENT (MTG_KEEP_ADAPTIVE_BOTTOM): let sub-tables be
                                         // adaptively sampled even with bottoming_enabled (instead of
                                         // forcing every sub-cell to the cap), and restrict the bottoming
                                         // argmin (best_sub) to REFINED cells (cnt > floor) so a floor-R
                                         // cell can't win by optimizer's-curse noise. Recovers the
                                         // keep-only rollout savings for bottoming-on profiles. Off => the
                                         // current full-R-sub-table behaviour, byte-identical.
    bool     bottoming_enabled = false;  // bake into the written profile: use blind exhaustive bottoming
                                         // at runtime. main.cpp ALWAYS sets this true (no generation-time
                                         // off switch -- shipping a bottoming-off profile is a footgun no
                                         // agent should reach; the confounded A/B has consistently shown
                                         // blind >= clairvoyant lookahead). The struct default stays false
                                         // only so an unset/legacy config is inert. Keep is always on too.
    bool     recommend_only = false;  // --gen-mulligan recommend: after the cheap floor pass, project the
                                     // full-gen wall-clock (vs an overnight window) and STOP -- no refine
                                     // waves, no profile written. A cost-scouting probe, ~1/(cap/floor) the
                                     // price of a full gen, so the user can pick fast/complete/another machine.
                                     // ALSO writes its R=1 floor pass to <out_raw>.probe (a poolable "probe
                                     // chunk") that a later real gen reuses automatically (see the probe-carry note below).
    // (Probe carry -- reusing a COMPLETE recommend-probe chunk `<out_raw>.probe` as this gen's r=0 slice --
    // is UNCONDITIONAL and has no flag. The probe sampled every cell once at the same seed/depth/budget/
    // bucketing, so (the rollout seed being a pure fn of (seed_base,r,w,pd)) its r=0 IS byte-identically
    // this gen's r=0: load it and roll only r>=1, skipping one rollout per cell. It is BYTE-IDENTICAL to a
    // from-scratch run, not a lossy pool, and it is gated on matching play_digest + fingerprints +
    // floor_complete, so a mismatch is silently ignored rather than misapplied. There is nothing to opt
    // into or out of -- the old MTG_KEEP_PROBE_CARRY / MTG_KEEP_NO_PROBE_CARRY pair only let a resumed or
    // scouted run repay work it had already banked.)
    std::string out_profile;    // if set, write the serialized keep policy (base profile + table) here
    std::string out_raw;        // if set, write the poolable raw sum+count sidecar (for cross-machine merge)
    std::string gen_cache;      // fingerprint-gated cache of the EQUIVALENCE CLASSES (the minutes-long
                                // bucketing head). Defaulted to <deck-stem>.keepmodel.gencache.json by the
                                // CLI so a resume never re-derives buckets; MTG_EQUIV_CACHE overrides the
                                // path. Keyed on deck/params/equiv_seed AND the play digest, so a changed
                                // engine, card or value sidecar can never produce a stale hit.
                                // records this deck's EXPECTED bucket count. First generation records it
                                // (for the user to confirm); every later run must MATCH or the generator
                                // REFUSES. See the lock note in BuildEquivalenceClasses.
    std::string commit;         // play-logic identity stamped into the raw sidecar (from MTG_COMMIT)
    std::string force_merge;    // MANUAL bucket override (MTG_EQUIV_FORCE_MERGE): ";"-separated groups,
                                // each a ","-separated list of card names, unioned into ONE bucket AFTER
                                // discovery. For deliberately merging near-equivalents the strict distance
                                // threshold keeps apart (e.g. the four fetchlands) to shrink the hand space.
                                // Gameplay is untouched -- cards keep real identities; only the keep/bottom
                                // bucketing merges. Baked into bucket_fp, so pooled chunks MUST use the same
                                // spec (a different merge => different fingerprint => won't pool).
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
