#pragma once
#include <algorithm>
#include <map>
#include <string>
#include <vector>

// Runtime keep policy produced by the exhaustive bucketed evaluation (see analyzer/ExhaustiveKeep).
//
// A hand's keep/mull decision is a pure function of its BUCKET COMPOSITION (equivalent cards merged),
// so this stores the decision for every distinct 7-card composition at each mulligan depth x
// play/draw. At runtime KeepHand maps the hand to its composition and looks the decision up; a hand
// whose composition is somehow absent (or contains an unbucketed card) yields present=false so the
// caller falls back to the static/model keep. Empty() => no exhaustive policy loaded.
struct ExhaustiveKeepPolicy
{
    std::vector<std::vector<std::string>>         buckets;   // bucket index -> member card names
    int                                           max_mull = 0;
    // composition (K bucket counts summing to 7) -> keep flags, length (max_mull+1)*2,
    // indexed [mull*2 + (on_play ? 1 : 0)]; 1 = keep, 0 = mulligan.
    std::map<std::vector<int>, std::vector<char>> keep;
    // Optimal bottoming (phase 2): composition -> for each [mull*2 + pd] the K-vector of the
    // subcomposition to KEEP after bottoming `mull` cards (sum = 7-mull). Empty => no bottoming table
    // (fall back to the heuristic). Populated only for mull >= 1.
    std::map<std::vector<int>, std::vector<std::vector<int>>> bottom_keep;
    // Whether this profile's blind exhaustive bottoming should be USED at runtime (vs. falling through
    // to lookahead/heuristic bottoming). Baked into the artifact: default OFF, because low-R bottoming
    // is noise-limited (the argmin mis-ranks near-tie subhands) and loses to lookahead -- only a
    // validated high-R profile sets this true. Overridable at play time by MTG_EXHAUSTIVE_BOTTOM
    // (unset = follow this flag; 0 = force off; 1 = force on) for A/B. Keep is always presence-gated
    // and independent of this flag.
    bool        bottoming_enabled = false;
    // Provenance (audit/merge only; unused at decision time).
    std::string commit;
    int         effective_R = 0;

    // name -> bucket index; rebuilt by Index() after buckets are populated (loader/analyzer call it).
    std::map<std::string, int> name_to_bucket;

    bool empty() const { return keep.empty(); }

    void Index()
    {
        name_to_bucket.clear();
        for (int b = 0; b < static_cast<int>(buckets.size()); ++b)
            for (const std::string& n : buckets[b]) { name_to_bucket[n] = b; }
    }

    // Keep decision for a hand given by card name. Read-only (thread-safe after Index()); sets
    // present=false when the hand can't be resolved to a tabled composition (caller falls back).
    bool Decide(const std::vector<std::string>& hand, int mull, bool on_play, bool& present) const
    {
        const int K = static_cast<int>(buckets.size());
        std::vector<int> comp(K, 0);
        for (const std::string& n : hand)
        {
            auto it = name_to_bucket.find(n);
            if (it == name_to_bucket.end()) { present = false; return false; }
            comp[it->second]++;
        }
        auto it = keep.find(comp);
        if (it == keep.end()) { present = false; return false; }
        const int idx = std::min(mull, max_mull) * 2 + (on_play ? 1 : 0);
        if (idx < 0 || idx >= static_cast<int>(it->second.size())) { present = false; return false; }
        present = true;
        return it->second[idx] != 0;
    }

    // Optimal-bottoming target: the K-vector subcomposition to KEEP after bottoming `count` cards from
    // `hand` (the blind expected-over-continuations argmin). Returns false (caller falls back to the
    // lookahead/heuristic bottoming) when there is no bottoming table, the hand contains an unbucketed
    // card, or the composition isn't tabled. Read-only / thread-safe after Index().
    bool DecideBottom(const std::vector<std::string>& hand, int count, bool on_play,
                      std::vector<int>& target) const
    {
        if (bottom_keep.empty()) { return false; }
        const int K = static_cast<int>(buckets.size());
        std::vector<int> comp(K, 0);
        for (const std::string& n : hand)
        {
            auto it = name_to_bucket.find(n);
            if (it == name_to_bucket.end()) { return false; }
            comp[it->second]++;
        }
        auto it = bottom_keep.find(comp);
        if (it == bottom_keep.end()) { return false; }
        const int idx = std::min(count, max_mull) * 2 + (on_play ? 1 : 0);
        if (idx < 0 || idx >= static_cast<int>(it->second.size())) { return false; }
        if (static_cast<int>(it->second[idx].size()) != K) { return false; }
        target = it->second[idx];
        return true;
    }
};
