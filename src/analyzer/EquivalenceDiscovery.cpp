#include "EquivalenceDiscovery.h"
#include "BucketPolicy.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <ostream>
#include <thread>

#include "SlowRollouts.h"
#include "../ai/AIEngine.h"
#include "../cards/CardDatabase.h"
#include "../core/HardwareConcurrency.h"
#include "../runner/GoldFishRunner.h"

// Signature = the per-probe clairvoyant win-turn vector for one candidate card sitting in the
// probe's "test slot". Two cards with equal signatures are interchangeable for the objective.
namespace
{
// Mean |Δ| between two signatures over the probes -- the "distance" between two classes (0 == the
// engine never told them apart). Used only for the reviewer-facing near-miss list.
double SigDistance(const std::vector<int>& a, const std::vector<int>& b)
{
    if (a.size() != b.size() || a.empty()) { return 1e9; }
    long sum = 0;
    for (size_t i = 0; i < a.size(); ++i) { sum += std::abs(a[i] - b[i]); }
    return static_cast<double>(sum) / static_cast<double>(a.size());
}
}  // namespace

EquivReport DiscoverEquivalence(const Decklist& deck, const MulliganProfile& profile,
                                int probes, int depth, int budget_ms, double threshold,
                                uint64_t seed, int max_turns,
                                const BucketPolicy* policy)
{
    // Rollout fidelity mirrors the keep-model labels: keep_model cleared (the static play policy),
    // second-main search matched to the deck, same required_pieces / vial target.
    MulliganProfile rollout_profile = profile;
    rollout_profile.keep_model = KeepModel{};
    const bool second_main = GoldFishRunner::DeckUsesSecondMain(deck);

    // Distinct cards (by name), each with a representative Card instance from the deck.
    std::map<std::string, Card> distinct;
    for (const Card& c : deck.mainboard) { distinct.emplace(c.m_name.str(), c); }
    std::vector<std::string> names;
    std::vector<Card>        reps;
    for (auto& kv : distinct) { names.push_back(kv.first); reps.push_back(kv.second); }
    const int N = static_cast<int>(names.size());

    const uint64_t seed_off = seed + 11'000'000ULL;  // far from the other analyzer phases

    // Probe templates: each is a natural 7-card draw whose slot 0 is the "test slot" (dropped here,
    // refilled per candidate) and whose remaining 6 cards + 53-card library are held FIXED -> CRN.
    std::vector<GameState>         tpl(probes);
    std::vector<std::vector<Card>> base6(probes);
    for (int i = 0; i < probes; ++i)
    {
        GameState s = GoldFishRunner::SetupGame(deck, seed_off + static_cast<uint64_t>(i));
        s.m_required_pieces = &rollout_profile.required_pieces;
        s.vial_target_mv    = rollout_profile.vial_target_mv;
        s.on_the_play       = true;
        std::vector<Card> seven;
        s.ActivePlayer().library.DrawN(7, seven);          // library now holds the fixed remaining 53
        base6[i].assign(seven.begin() + 1, seven.end());   // drop slot 0 (the test slot)
        s.ActivePlayer().hand.clear();                     // hand is set per candidate below
        tpl[i] = std::move(s);
    }

    // sig[k][i] = clairvoyant win-turn with candidate k in probe i's test slot.
    //
    // The work item is ONE (probe, candidate) ROLLOUT, not a whole probe. Claiming a probe meant a
    // worker owned N serial rollouts, so the run's tail was a full probe (~N rollouts on one core while
    // every other core idled) -- a real tail before the pool, on a phase that is otherwise pure
    // fan-out. Per-rollout claiming makes the tail one rollout. Each sig[k][i] is written by exactly
    // one worker and no rollout reads another's result, so the values are unchanged by the re-graining.
    std::vector<std::vector<int>> sig(N, std::vector<int>(probes, 0));
    const long long total = static_cast<long long>(probes) * static_cast<long long>(N);
    std::atomic<long long> next_item{ 0 };
    std::atomic<long long> done_items{ 0 };
    const auto t_start = std::chrono::steady_clock::now();
    std::atomic<long long> next_report{ 30 };          // stderr heartbeat, every 30s
    auto worker = [&]()
    {
        AIEngine ai(rollout_profile, depth, budget_ms);
        ai.SetSearchPostCombat(second_main);
        for (;;)
        {
            const long long item = next_item.fetch_add(1);
            if (item >= total) { break; }
            const int i = static_cast<int>(item / N);   // probe
            const int k = static_cast<int>(item % N);   // candidate
            // Timed + streamed like every other rollout phase: a card that is degenerate to play used
            // to make discovery silently crawl, with no output at all until it finished.
            sig[k][i] = TimedRollout("discovery",
                [&]{ return "probe=" + std::to_string(i) + " candidate=" + names[k]; },
                [&]() -> int {
                    GameState s = tpl[i];               // fixed library + fixed base6 (CRN)
                    Player& ap = s.ActivePlayer();
                    ap.hand = base6[i];
                    ap.hand.push_back(reps[k]);         // hand = base6 + candidate (7 cards)
                    return ai.RolloutKeepWinTurn(s, 0, max_turns);
                });
            const long long d  = done_items.fetch_add(1) + 1;
            const double    el = std::chrono::duration<double>(
                                     std::chrono::steady_clock::now() - t_start).count();
            long long nr = next_report.load(std::memory_order_relaxed);
            if (static_cast<long long>(el) >= nr && next_report.compare_exchange_strong(nr, nr + 30))
            {
                std::cerr << "[keepgen]   discovery " << (100 * d / total) << "%  (" << d << "/" << total
                          << " rollouts, " << static_cast<long long>(el > 0 ? d / el : 0) << "/s, "
                          << static_cast<long long>(el) << "s)\n" << std::flush;
            }
        }
    };
    int nthreads = std::max(1, concurrency_util::AffinityCpuCount());
    nthreads = static_cast<int>(std::min<long long>(nthreads, std::max<long long>(1, total)));
    std::vector<std::thread> pool;
    for (int t = 0; t < nthreads; ++t) { pool.emplace_back(worker); }
    for (std::thread& th : pool) { th.join(); }

    // Cluster by SINGLE-LINKAGE at `threshold`: union any two cards whose signature distance is
    // <= threshold. threshold 0 recovers exact-match (distance is 0 only for identical signatures).
    // The empirical distance gap (merge-worthy ~0.005 vs distinct ~0.05) makes any threshold in
    // between stable and free of chaining.
    std::vector<int> parent(N);
    for (int k = 0; k < N; ++k) { parent[k] = k; }
    std::function<int(int)> find = [&](int x){ while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; } return x; };
    for (int a = 0; a < N; ++a)
    {
        for (int b = a + 1; b < N; ++b)
        {
            // The deck's keep_apart ruling VETOES the union (BucketPolicy.h). It has to act here,
            // during clustering, because the alternative -- splitting a finished class -- is not
            // well defined once single-linkage has chained cards together.
            if (policy && policy->MustKeepApart(names[a], names[b])) { continue; }
            if (SigDistance(sig[a], sig[b]) <= threshold) { parent[find(a)] = find(b); }
        }
    }
    // Merge all goldfish-inert cards BY CONSTRUCTION. They are provably game-equivalent -- the engine
    // never casts them (TurnSolver skips def.params.goldfish_inert), so any two are the same faithful
    // dead draw and belong in one bucket. The signature clustering can miss this: a dead card still
    // sits in hand and can perturb a play heuristic on a handful of the N probes, nudging the mean
    // |Δ win-turn| just over `threshold` (observed on Hinata -- three cheap inert cards merged but the
    // {X}{U}{U}{U} Distorting Wake landed just outside). Unioning them here is exact, not heuristic,
    // and shrinks K (fewer hand dimensions) for inert-heavy decks. No effect on decks without inert cards.
    int inert_root = -1;
    for (int k = 0; k < N; ++k)
    {
        auto def = CardDatabase::Instance().LookupCached(reps[k]);
        if (def && def->params.goldfish_inert)
        {
            if (inert_root < 0) { inert_root = k; }
            else { parent[find(k)] = find(inert_root); }
        }
    }
    // Merge all FETCHLANDS BY CONSTRUCTION (user rule, 2026-08-14). A fetchland is a hand slot that
    // says "a land of the right colour, one life, next land drop"; which one you hold is a difference
    // the KEEP decision does not turn on. The threshold cannot be trusted to find this: on FiveColour
    // the five-fetch cycle measured 0.0125-0.035 apart -- above the documented 0.01, below the 0.06
    // that separates genuinely different cards -- so every fetch took its own hand dimension and the
    // size-7 table came out 2.86x larger than it needed to be (5,655,953 cells vs 1,977,898). Leaving a
    // 2.9x cost difference to whether a distance lands either side of a constant is not a rule, it is a
    // coin flip; a threshold of 0.04 happens to work on this deck and that is luck.
    //
    // Unlike the inert merge above this is an APPROXIMATION, not an identity -- fetches differ in the
    // types they can find, so a deck whose fetches reach genuinely different colour sets loses that
    // distinction. It is accepted deliberately, bounded by the measured 0.035 turns, because the
    // alternative is a table nobody can afford to generate. (The rule stops at fetchlands: DUALS look
    // similar but measured ~5x further apart (0.0725-0.0775) on the same deck, and that is real signal
    // -- domain sources like Faeburrow Elder / Bloom Tender scale with the COLOURS you control, so
    // which dual you hold genuinely matters. Merging those is a 16x trap.)
    int fetch_root = -1;
    for (int k = 0; k < N; ++k)
    {
        auto def = CardDatabase::Instance().LookupCached(reps[k]);
        if (def && !def->params.fetch_land_types.empty())
        {
            if (fetch_root < 0) { fetch_root = k; }
            else { parent[find(k)] = find(fetch_root); }
        }
    }
    // The veto above blocks the DIRECT union, but two kept-apart cards can still land together by
    // chaining (a ~ x ~ b) or through a by-construction merge that asserts they are provably the
    // same dead draw. Both are real conflicts between the ruling and the engine's own evidence, so
    // say so and stop -- silently honouring one side would hide exactly the thing worth knowing.
    if (policy != nullptr)
    {
        for (int a = 0; a < N; ++a)
        {
            for (int b = a + 1; b < N; ++b)
            {
                if (find(a) != find(b))                       { continue; }
                if (!policy->MustKeepApart(names[a], names[b])) { continue; }
                throw std::runtime_error(
                    "bucket policy conflict: '" + names[a] + "' and '" + names[b] + "' are marked"
                    " keep_apart, but discovery still put them in one class -- either they were"
                    " chained together through a third card, or a by-construction rule (inert /"
                    " fetchland) merged them as provably identical. Resolve it deliberately: drop"
                    " the ruling, or name the chaining card in the same keep_apart group.");
            }
        }
    }
    EquivReport rep;
    rep.probes         = probes;
    rep.distinct_cards = N;
    std::map<int, EquivClass> by_root;
    for (int k = 0; k < N; ++k)
    {
        EquivClass& cls = by_root[find(k)];
        if (cls.members.empty()) { cls.signature = sig[k]; }   // representative signature
        cls.members.push_back(names[k]);
    }
    for (auto& kv : by_root)
    {
        std::sort(kv.second.members.begin(), kv.second.members.end());
        rep.classes.push_back(std::move(kv.second));
    }
    std::sort(rep.classes.begin(), rep.classes.end(),
              [](const EquivClass& a, const EquivClass& b)
              { return a.members.size() > b.members.size(); });
    return rep;
}

void PrintEquivReport(std::ostream& os, const EquivReport& rep)
{
    os << "=== EQUIVALENCE CLASSES (goldfish, CRN substitution over " << rep.probes
       << " probes) ===\n";
    os << rep.distinct_cards << " distinct cards -> " << rep.classes.size() << " classes\n\n";
    for (size_t c = 0; c < rep.classes.size(); ++c)
    {
        const EquivClass& cl = rep.classes[c];
        os << "[" << (c + 1) << "] ";
        for (size_t m = 0; m < cl.members.size(); ++m) { os << (m ? ", " : "") << cl.members[m]; }
        if (cl.members.size() > 1) { os << "   (" << cl.members.size() << " merged)"; }
        os << "\n";
    }

    // Near-miss list: for every class, its closest OTHER class by mean |Δ win-turn|. A small
    // non-zero distance is a near-equivalent the strict threshold kept apart (e.g. Leeching vs a
    // lord if they differ by a fraction of a turn). Sorted by distance so the boundary is on top.
    struct NM { size_t a, b; double d; };
    std::vector<NM> nm;
    for (size_t a = 0; a < rep.classes.size(); ++a)
    {
        double best = 1e9; size_t bj = a;
        for (size_t b = 0; b < rep.classes.size(); ++b)
        {
            if (a == b) { continue; }
            double d = SigDistance(rep.classes[a].signature, rep.classes[b].signature);
            if (d < best) { best = d; bj = b; }
        }
        if (bj != a) { nm.push_back({ a, bj, best }); }
    }
    std::sort(nm.begin(), nm.end(), [](const NM& x, const NM& y){ return x.d < y.d; });
    os << "\n--- nearest-neighbour distances (mean |Δ win-turn| per probe) ---\n";
    for (size_t i = 0; i < nm.size() && i < 12; ++i)
    {
        auto rep_name = [&](size_t c){ return rep.classes[c].members.front()
                                            + (rep.classes[c].members.size() > 1 ? " (+)" : ""); };
        os << "  " << rep_name(nm[i].a) << "  ~  " << rep_name(nm[i].b)
           << "   dist=" << nm[i].d << "\n";
    }
}
