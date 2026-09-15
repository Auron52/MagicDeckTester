#pragma once
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "KeepTable.h"

// Runtime keep policy produced by the exhaustive bucketed evaluation (see analyzer/ExhaustiveKeep).
//
// A hand's keep/mull decision is a pure function of its BUCKET COMPOSITION (equivalent cards merged),
// so this stores the decision for every distinct 7-card composition at each mulligan depth x
// play/draw. At runtime KeepHand maps the hand to its composition and looks the decision up; a hand
// whose composition is somehow absent (or contains an unbucketed card) yields present=false so the
// caller falls back to the static/model keep. Empty() => no exhaustive policy loaded.
//
// TWO BACKINGS, one decision. The generator, the merge tool and the JSON emitters work on the two
// in-memory maps below. PLAY loads instead attach an on-disk table (`disk`, see KeepTable.h) and leave
// the maps EMPTY: a 2M-composition table is ~5 GB as maps and is consulted a few times per game, so
// resident maps were the memory ceiling on a 10 GB box. Decide/DecideBottom consult whichever backing
// is present and return identical answers for identical tables -- the table's clear-bit rows are
// exactly the maps' missing/empty rows (test/unit/test_keep_table.cpp checks every key both ways).
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
    // On-disk backing for play loads (null when the maps are the backing). Read-only, shared by every
    // profile/engine that attaches this policy; lookups are mutex-serialised inside the table.
    std::shared_ptr<const keeptable::Table> disk;
    // Whether this profile's blind exhaustive bottoming should be USED at runtime (vs. falling through
    // to lookahead/heuristic bottoming). Generation ALWAYS bakes this true and there is NO off switch,
    // so in practice every shipped table has it set; the JSON loader likewise defaults it ON for a
    // present-but-keyless block. This member's `false` is only the default for a DEFAULT-CONSTRUCTED
    // (empty) policy, which ek.empty() gates upstream -- it is not a per-deck choice.
    //
    // (It once WAS a choice: "default OFF, because low-R bottoming is noise-limited... only a validated
    // high-R profile sets this true." That policy is dead -- see the SUPERSEDED banner in
    // docs/design/exhaustive-keep-policy.md. A bad confounded bottoming A/B now means raise R or fix
    // the heuristic, never ship bottoming off.)
    //
    // Overridable at play time by MTG_EXHAUSTIVE_BOTTOM (unset = follow this flag; 0 = force off;
    // 1 = force on) -- that override exists FOR test/keepmodel_exhaustive_ab.sh ONLY, which uses it to
    // isolate the halves. Keep is always presence-gated and independent of this flag.
    bool        bottoming_enabled = false;
    // Provenance (audit/merge only; unused at decision time).
    std::string commit;         // source revision the sidecar was built at (advisory once play_digest exists)
    std::string play_digest;    // rollout-config play fingerprint (depth 5 / budget 20) -- the real
                                // pooling identity: a doc/other-deck/GUI commit leaves it unchanged, so
                                // sidecars from those commits stay poolable (see RunKeepMerge).
    int         effective_R = 0;

    // name -> bucket index; rebuilt by Index() after buckets are populated (loader/analyzer call it).
    std::map<std::string, int> name_to_bucket;

    bool empty() const { return keep.empty() && !(disk && disk->size() > 0); }
    // Number of tabled compositions, whichever backing holds them.
    std::size_t table_size() const { return disk ? static_cast<std::size_t>(disk->size()) : keep.size(); }

    void Index()
    {
        name_to_bucket.clear();
        for (int b = 0; b < static_cast<int>(buckets.size()); ++b)
            for (const std::string& n : buckets[b]) { name_to_bucket[n] = b; }
    }

    // Hand -> bucket composition; false if any card is unbucketed.
    bool Composition(const std::vector<std::string>& hand, std::vector<int>& comp) const
    {
        comp.assign(buckets.size(), 0);
        for (const std::string& n : hand)
        {
            auto it = name_to_bucket.find(n);
            if (it == name_to_bucket.end()) { return false; }
            comp[it->second]++;
        }
        return true;
    }

    // Keep decision for a hand given by card name. Read-only (thread-safe after Index()); sets
    // present=false when the hand can't be resolved to a tabled composition (caller falls back).
    bool Decide(const std::vector<std::string>& hand, int mull, bool on_play, bool& present) const
    {
        std::vector<int> comp;
        if (!Composition(hand, comp)) { present = false; return false; }
        const int idx = std::min(mull, max_mull) * 2 + (on_play ? 1 : 0);
        if (disk)
        {
            std::vector<char> rec;
            if (!disk->Find(comp, rec)) { present = false; return false; }
            if (idx < 0 || idx >= static_cast<int>(disk->header().F_keep)) { present = false; return false; }
            present = true;
            return disk->KeepFlag(rec, idx);
        }
        auto it = keep.find(comp);
        if (it == keep.end()) { present = false; return false; }
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
        const int K = static_cast<int>(buckets.size());
        if (disk)
        {
            if (disk->header().F_bot == 0) { return false; }          // no bottoming table at all
            std::vector<int> comp;
            if (!Composition(hand, comp)) { return false; }
            std::vector<char> rec;
            if (!disk->Find(comp, rec)) { return false; }
            const int idx = std::min(count, max_mull) * 2 + (on_play ? 1 : 0);
            if (idx < 0 || idx >= static_cast<int>(disk->header().F_bot)) { return false; }
            if (!disk->HasRow(rec, idx)) { return false; }             // absent / empty / non-K row
            disk->Row(rec, idx, target);
            return true;
        }
        if (bottom_keep.empty()) { return false; }
        std::vector<int> comp;
        if (!Composition(hand, comp)) { return false; }
        auto it = bottom_keep.find(comp);
        if (it == bottom_keep.end()) { return false; }
        const int idx = std::min(count, max_mull) * 2 + (on_play ? 1 : 0);
        if (idx < 0 || idx >= static_cast<int>(it->second.size())) { return false; }
        if (static_cast<int>(it->second[idx].size()) != K) { return false; }
        target = it->second[idx];
        return true;
    }
};
