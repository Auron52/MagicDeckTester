#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>
#include "../deck/DeckLoader.h"
#include "../ai/MulliganProfile.h"

// Objective-relative card equivalence, discovered by CRN substitution in the goldfish engine.
//
// Two cards are "equivalent" iff swapping one for the other, in a battery of probe hands that
// share a FIXED library (common random numbers), never changes the clairvoyant win-turn. Because
// the library is fixed per probe the rollout is deterministic, so behaviourally-identical cards
// yield byte-identical signatures. This measures equivalence UNDER THE OBJECTIVE (goldfish, no
// opponent) rather than by matching card parameters -- so the three Sliver lords and Leeching
// Sliver merge (each ~"+1 to the clock per attacking Sliver"), even though their params differ,
// while a real-game distinction the goldfish doesn't model (toughness surviving burn/combat) does
// not separate them. The output is the review artifact: the merged classes plus the pairwise
// distance that exposes near-equivalents at the strict/loose boundary.

struct EquivClass
{
    std::vector<std::string> members;    // card names in this class (sorted)
    std::vector<int>         signature;  // per-probe clairvoyant win-turn (length == probes)
};

struct EquivReport
{
    int                     probes = 0;
    int                     distinct_cards = 0;
    std::vector<EquivClass> classes;     // largest class first
};

// Discover equivalence classes for `deck`. `profile` supplies rollout fidelity (vial_target_mv /
// required_pieces / play style) exactly as the keep-model labels use it. probes = number of CRN
// contexts; depth/budget_ms = rollout search strength (match the keep-model: depth 5, budget 20).
//
// Cards are clustered by SINGLE-LINKAGE at `threshold` = the max mean |Δ win-turn| per probe two
// cards may differ and still merge. threshold 0 == exact-match, which fragments as probes grow (a
// rare probe eventually separates near-identical cards); a small threshold in the empirical gap
// between merge-worthy (~0.005) and distinct (~0.05) pairs is the stable criterion.
EquivReport DiscoverEquivalence(const Decklist& deck, const MulliganProfile& profile,
                                int probes, int depth, int budget_ms, double threshold,
                                uint64_t seed, int max_turns);

// Human-readable dump for review: the classes, then a near-miss list (closest other class + the
// mean |Δ win-turn| per probe) so the strict-vs-loose merge boundary is visible.
void PrintEquivReport(std::ostream& os, const EquivReport& rep);
