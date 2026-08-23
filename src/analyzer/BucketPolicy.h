#pragma once
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// ---- Per-deck BUCKET POLICY -------------------------------------------------------------------
//
// The human ruling about which cards equivalence discovery may and may not put in one bucket. It
// lives beside the deck as `<stem>.buckets.json` and NO TOOL EVER WRITES IT. That is the whole
// point: it is the one bucketing input that survives a regeneration, a threshold change, a card-data
// edit and a play change, so a decision made once stays made.
//
// WHY IT EXISTS, concretely. Discovery clusters cards by the mean |Δ win-turn| per probe. The win
// turn is an INTEGER and the horizon is 8 turns, so the entire scale is "how many of the N probes
// moved by a whole turn" -- the shipped 0.01 threshold is literally "at most 4 probes out of 400".
// Measured on KittyEquipment, Colossus Hammer (equip {8}, +10/+10, needs a Puresteel-metalcraft or
// Balan cheat to attach at all) and O-Naginata (equip {2}, +3/+0 trample, attaches only to power 3+)
// differ on FIVE probes out of 400 -- and not even consistently: three favour the Hammer, two favour
// the Naginata, so the signed effect is ~0.0025 while the unsigned distance is 0.0175. The metric is
// not reporting that these cards are alike. It is reporting that it cannot see them: at one-turn
// granularity a card that changes the clock only in the games where its enabler shows up is
// invisible. Deciding such a pair by moving a constant is choosing between a count of 5 and a count
// of 4, which is noise. It is a judgement, so it belongs in a file with a reason attached.
//
// (The deeper fix -- a continuous objective that can resolve sub-turn differences -- would change
// bucketing for every deck and is a separate project. This makes the judgement expressible today,
// and would still be wanted afterwards: no metric decides whether two cards are the SAME CARD for
// the purpose of a keep decision.)
//
// FORMAT (both lists optional; `why` is mandatory on every group -- a ruling with no reason is a
// landmine for whoever reads it in six months):
//
//   {
//     "keep_apart": [
//       { "cards": ["Colossus Hammer", "O-Naginata"],
//         "why":   "Opposite dependency structures ..." }
//     ],
//     "merge": [
//       { "cards": ["Flooded Strand", "Windswept Heath"],
//         "why":   "..." }
//     ]
//   }
//
// `keep_apart` is a constraint on the CLUSTERING (a veto on the union), because a split cannot be
// applied afterwards: single-linkage may have chained the pair through a third card, and there is no
// well-defined way to undo that. `merge` is applied after clustering, through the same path the
// MTG_EQUIV_FORCE_MERGE env override uses, so the two agree by construction.
//
// Both are folded into the discovery cache key and into bucket_fp, so changing the policy forces
// re-discovery and a chunk generated under one policy can never pool with a chunk generated under
// another.

struct BucketPolicy
{
    struct Group
    {
        std::vector<std::string> cards;
        std::string              why;
    };
    std::vector<Group> keep_apart;
    std::vector<Group> merge;
    std::string        path;    // where it was loaded from; empty when the deck has no policy file

    bool Empty() const { return keep_apart.empty() && merge.empty(); }

    // Must these two cards land in DIFFERENT buckets? True when any keep_apart group names both.
    bool MustKeepApart(const std::string& a, const std::string& b) const
    {
        if (a == b) { return false; }
        for (const Group& g : keep_apart)
        {
            const bool ha = std::find(g.cards.begin(), g.cards.end(), a) != g.cards.end();
            if (!ha) { continue; }
            if (std::find(g.cards.begin(), g.cards.end(), b) != g.cards.end()) { return true; }
        }
        return false;
    }

    // Order-insensitive text for fingerprinting and display: cards sorted within a group, groups
    // sorted, so reformatting the file or reordering its entries is not a policy change. `why` is
    // deliberately EXCLUDED -- editing a comment must not invalidate hours of generated cells.
    std::string Canonical() const
    {
        auto emit = [](const std::vector<Group>& gs, const char* tag)
        {
            std::vector<std::string> lines;
            for (const Group& g : gs)
            {
                std::vector<std::string> c = g.cards;
                std::sort(c.begin(), c.end());
                std::string s = tag;
                for (const std::string& n : c) { s += "|" + n; }
                lines.push_back(std::move(s));
            }
            std::sort(lines.begin(), lines.end());
            std::string out;
            for (const std::string& l : lines) { out += l + "\n"; }
            return out;
        };
        return emit(keep_apart, "apart") + emit(merge, "merge");
    }

    // The `merge` groups in MTG_EQUIV_FORCE_MERGE's wire format ("a,b;c,d"), so they flow through
    // the existing, tested force-merge path instead of a second implementation of the same thing.
    std::string MergeSpec() const
    {
        std::string out;
        for (const Group& g : merge)
        {
            if (g.cards.empty()) { continue; }
            if (!out.empty()) { out += ";"; }
            for (std::size_t i = 0; i < g.cards.size(); ++i)
            { out += (i ? "," : "") + g.cards[i]; }
        }
        return out;
    }
};

// `<stem>.buckets.json` beside the deck. Absent file == empty policy (every deck that has not made
// a ruling behaves exactly as before). A malformed file is a hard error rather than a silent empty
// policy: a typo must not quietly discard a decision the user made deliberately.
inline BucketPolicy LoadBucketPolicy(const std::filesystem::path& deck_path)
{
    BucketPolicy pol;
    const std::filesystem::path p =
        deck_path.parent_path() / (deck_path.stem().string() + ".buckets.json");
    if (!std::filesystem::exists(p)) { return pol; }
    pol.path = p.string();

    nlohmann::json j;
    { std::ifstream f(p); f >> j; }
    auto read = [&](const char* key, std::vector<BucketPolicy::Group>& into)
    {
        if (!j.contains(key)) { return; }
        if (!j[key].is_array())
        { throw std::runtime_error("bucket policy: '" + std::string(key) + "' must be an array (" + pol.path + ")"); }
        for (const auto& e : j[key])
        {
            BucketPolicy::Group g;
            g.cards = e.value("cards", std::vector<std::string>{});
            g.why   = e.value("why", std::string());
            if (g.cards.size() < 2)
            {
                throw std::runtime_error("bucket policy: every '" + std::string(key)
                                         + "' group needs at least 2 cards (" + pol.path + ")");
            }
            if (g.why.empty())
            {
                throw std::runtime_error("bucket policy: group [" + g.cards[0] + ", ...] has no 'why'."
                                         " Every ruling must record its reason (" + pol.path + ")");
            }
            into.push_back(std::move(g));
        }
    };
    read("keep_apart", pol.keep_apart);
    read("merge",      pol.merge);
    return pol;
}

// Reject a policy that names a card the deck does not contain -- otherwise a rename or a decklist
// edit silently turns a ruling into a no-op, which is the failure mode this file exists to prevent.
// `deck_cards` is the deck's DISTINCT card names.
inline void ValidateBucketPolicy(const BucketPolicy& pol, const std::vector<std::string>& deck_cards)
{
    if (pol.Empty()) { return; }
    auto known = [&](const std::string& n)
    { return std::find(deck_cards.begin(), deck_cards.end(), n) != deck_cards.end(); };
    for (const std::vector<BucketPolicy::Group>* gs : { &pol.keep_apart, &pol.merge })
    {
        for (const BucketPolicy::Group& g : *gs)
        {
            for (const std::string& n : g.cards)
            {
                if (!known(n))
                {
                    throw std::runtime_error("bucket policy names a card this deck does not contain: '"
                                             + n + "' (" + pol.path + "). Fix the name or remove the"
                                             " group -- a ruling that matches nothing is silently no"
                                             " ruling at all.");
                }
            }
        }
    }
}

inline void PrintBucketPolicy(std::ostream& os, const BucketPolicy& pol)
{
    if (pol.Empty()) { return; }
    os << "  bucket policy   : " << pol.path << "\n";
    for (const BucketPolicy::Group& g : pol.keep_apart)
    {
        os << "     keep apart   :";
        for (std::size_t i = 0; i < g.cards.size(); ++i) { os << (i ? " / " : " ") << g.cards[i]; }
        os << "\n                     (" << g.why << ")\n";
    }
    for (const BucketPolicy::Group& g : pol.merge)
    {
        os << "     merge        :";
        for (std::size_t i = 0; i < g.cards.size(); ++i) { os << (i ? " / " : " ") << g.cards[i]; }
        os << "\n                     (" << g.why << ")\n";
    }
}
