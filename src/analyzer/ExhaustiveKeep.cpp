#include "ExhaustiveKeep.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <fstream>
#include <map>
#include <ostream>
#include <set>
#include <thread>
#include <vector>

#include "EquivalenceDiscovery.h"
#include "../ai/AIEngine.h"
#include "../ai/MulliganProfileIO.h"
#include "../core/HardwareConcurrency.h"
#include "../runner/GoldFishRunner.h"

namespace
{
constexpr int HAND = 7;

// C(n,k) as an exact long long (n<=60, k<=7 here -> well within range).
long long Comb(int n, int k)
{
    if (k < 0 || k > n) { return 0; }
    k = std::min(k, n - k);
    long long r = 1;
    for (int i = 0; i < k; ++i) { r = r * (n - i) / (i + 1); }
    return r;
}

// FNV-1a string hash -- deterministic across machines (unlike std::hash), so it can fingerprint the
// bucket map / deck to gate cross-machine pooling.
uint64_t Fnv(const std::string& s, uint64_t h = 1469598103934665603ULL)
{
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

// Enumerate every composition of `total` over K buckets with per-bucket cap `cap`.
void EnumComps(int i, int rem, std::vector<int>& cur, const std::vector<int>& cap,
               std::vector<std::vector<int>>& out)
{
    const int K = static_cast<int>(cap.size());
    if (i == K) { if (rem == 0) { out.push_back(cur); } return; }
    const int hi = std::min(cap[i], rem);
    for (int x = 0; x <= hi; ++x) { cur[i] = x; EnumComps(i + 1, rem - x, cur, cap, out); }
    cur[i] = 0;
}

// One hand-size table: the compositions, an index for lookup, and V[idx][play/draw].
struct SizeTable
{
    std::vector<std::vector<int>>      comps;
    std::map<std::vector<int>, int>    index;
    std::vector<std::array<double, 2>> V;    // [comp][on_the_draw=0, on_the_play=1] mean win-turn
    std::vector<std::array<double, 2>> se;   // stderr of the mean (label-noise diagnostic)
};

// Optimal keep policy from fully-populated V tables. A PURE function of (tables, deck bucket counts):
// backward induction over the multivariate-hypergeometric hand distribution -> per-composition keep
// flags. Shared by the rollout path (Section 6) and the offline merge path (RunKeepMerge) so both
// serialize a BYTE-IDENTICAL policy from the same V's -- the merge tool is not a second, drifting
// implementation of the decision rule. `tables` is indexed [HAND-H]; `count[b]` is the deck copies in
// bucket b; `deck_size` = mainboard size (hypergeometric denominator). If `Dopt_out` is non-null it
// receives the per-(pd,mull) optimal mulligan values (for reporting).
ExhaustiveKeepPolicy BuildPolicyFromTables(
    const std::vector<SizeTable>& tables, const std::vector<int>& count,
    const std::vector<std::vector<std::string>>& bucket_members, int deck_size, int max_mull,
    int effective_R, std::array<std::vector<double>, 2>* Dopt_out = nullptr)
{
    const int K = static_cast<int>(count.size());
    auto keep_val = [&](const std::vector<int>& h, int m, int pd) -> double
    {
        const int target = HAND - m;
        std::vector<std::vector<int>> subs;
        std::vector<int> cur(K, 0);
        EnumComps(0, target, cur, h, subs);          // subcompositions bounded by the hand
        double best = 1e9;
        const SizeTable& t = tables[HAND - target];
        for (const std::vector<int>& s : subs)
        { auto it = t.index.find(s); if (it != t.index.end()) { best = std::min(best, t.V[it->second][pd]); } }
        return best;
    };
    const long long denom = Comb(deck_size, HAND);
    const SizeTable& H7 = tables[0];
    std::vector<double> P(H7.comps.size());
    for (std::size_t i = 0; i < H7.comps.size(); ++i)
    {
        long long num = 1;
        for (int b = 0; b < K; ++b) { num *= Comb(count[b], H7.comps[i][b]); }
        P[i] = static_cast<double>(num) / static_cast<double>(denom);
    }
    std::array<std::vector<double>, 2> Dopt_pd;
    for (int pd = 0; pd < 2; ++pd)
    {
        const int M = max_mull;
        std::vector<double> Dopt(M + 1, 0.0);
        for (std::size_t i = 0; i < H7.comps.size(); ++i) { Dopt[M] += P[i] * keep_val(H7.comps[i], M, pd); }
        for (int m = M - 1; m >= 0; --m)
            for (std::size_t i = 0; i < H7.comps.size(); ++i)
            { Dopt[m] += P[i] * std::min(keep_val(H7.comps[i], m, pd), Dopt[m + 1]); }
        Dopt_pd[pd] = std::move(Dopt);
    }
    // Optimal bottoming target: the argmin (7-m)-subcomposition (same enumeration keep_val minimises,
    // but recording WHICH subcomp wins). Shared with the keep flags so the serialized keep decision
    // and its bottoming are mutually consistent (keep_val == V of the recorded target).
    auto best_sub = [&](const std::vector<int>& h, int m, int pd) -> std::vector<int>
    {
        const int target = HAND - m;
        std::vector<std::vector<int>> subs;
        std::vector<int> cur(K, 0);
        EnumComps(0, target, cur, h, subs);
        double best = 1e9;
        std::vector<int> arg = h;                    // fallback: keep the whole hand
        const SizeTable& t = tables[HAND - target];
        for (const std::vector<int>& s : subs)
        {
            auto it = t.index.find(s);
            if (it != t.index.end() && t.V[it->second][pd] < best) { best = t.V[it->second][pd]; arg = s; }
        }
        return arg;
    };

    ExhaustiveKeepPolicy ek;
    ek.max_mull    = max_mull;
    ek.effective_R = effective_R;
    ek.buckets     = bucket_members;
    for (std::size_t i = 0; i < H7.comps.size(); ++i)
    {
        std::vector<char> flags((max_mull + 1) * 2, 1);
        std::vector<std::vector<int>> bk((max_mull + 1) * 2);
        for (int pd = 0; pd < 2; ++pd)
            for (int m = 0; m <= max_mull; ++m)
            {
                flags[m * 2 + pd] = ((m == max_mull)
                                     || (keep_val(H7.comps[i], m, pd) <= Dopt_pd[pd][m + 1])) ? 1 : 0;
                bk[m * 2 + pd] = best_sub(H7.comps[i], m, pd);   // subcomp to KEEP after bottoming m
            }
        ek.keep[H7.comps[i]]        = std::move(flags);
        ek.bottom_keep[H7.comps[i]] = std::move(bk);
    }
    ek.Index();
    if (Dopt_out) { *Dopt_out = Dopt_pd; }
    return ek;
}
}  // namespace

void RunExhaustiveKeep(std::ostream& os, const Decklist& deck, const MulliganProfile& profile,
                       const ExhaustiveKeepConfig& cfg)
{
    // ---- 1. Buckets (objective-relative equivalence) --------------------------------------------
    // Bucketing uses the FIXED equiv_seed (not the rollout seed) so the clustering is byte-identical
    // across machines -- a precondition for pooling their raw tables.
    EquivReport eq = DiscoverEquivalence(deck, profile, cfg.probes, cfg.depth, cfg.budget_ms,
                                         cfg.threshold, cfg.equiv_seed, cfg.max_turns);
    const int K = static_cast<int>(eq.classes.size());

    std::map<std::string, int> bucket_of;             // card name -> bucket index
    std::vector<int>  count(K, 0);                    // deck copies in each bucket
    std::vector<Card> rep(K);                         // a representative Card per bucket
    std::vector<std::string> label(K);
    for (int b = 0; b < K; ++b)
    {
        for (const std::string& nm : eq.classes[b].members) { bucket_of[nm] = b; }
        label[b] = eq.classes[b].members.front()
                 + (eq.classes[b].members.size() > 1
                      ? " (+" + std::to_string(eq.classes[b].members.size() - 1) + ")" : "");
    }
    for (const Card& c : deck.mainboard)
    {
        auto it = bucket_of.find(c.m_name.str());
        if (it == bucket_of.end()) { continue; }
        count[it->second]++;
        if (rep[it->second].m_name.str().empty()) { rep[it->second] = c; }
    }

    // ---- 2. Enumerate hand compositions for sizes HAND .. HAND-max_mull --------------------------
    const int min_size = std::max(1, HAND - cfg.max_mull);
    std::vector<SizeTable> tables(HAND - min_size + 1);   // tables[HAND-H] holds the size-H table
    auto TB = [&](int H) -> SizeTable& { return tables[HAND - H]; };   // vector: concurrent-safe reads
    for (int H = HAND; H >= min_size; --H)
    {
        SizeTable t;
        std::vector<int> cap(K), cur(K, 0);
        for (int b = 0; b < K; ++b) { cap[b] = std::min(count[b], H); }
        EnumComps(0, H, cur, cap, t.comps);
        for (int i = 0; i < static_cast<int>(t.comps.size()); ++i) { t.index[t.comps[i]] = i; }
        t.V.assign(t.comps.size(), { 0.0, 0.0 });
        t.se.assign(t.comps.size(), { 0.0, 0.0 });
        TB(H) = std::move(t);
    }

    os << "=== EXHAUSTIVE KEEP (deck=" << deck.mainboard.size() << " cards, " << K << " buckets) ===\n";
    for (int b = 0; b < K; ++b) { os << "  [" << b << "] " << label[b] << " x" << count[b] << "\n"; }
    os << "distinct hands:";
    long long total_hands = 0;
    for (int H = HAND; H >= min_size; --H)
    { os << " size" << H << "=" << TB(H).comps.size(); total_hands += TB(H).comps.size(); }
    os << "  (total " << total_hands << ")\n";
    os << "rollouts/hand R=" << cfg.rollouts << ", depth=" << cfg.depth
       << ", horizon=" << cfg.max_turns << "\n" << std::flush;

    // ---- 3. Evaluate V[H][comp][pd] via R reshuffled continuations (parallel over work items) -----
    MulliganProfile rollout_profile = profile;
    rollout_profile.keep_model = KeepModel{};
    const bool second_main = GoldFishRunner::DeckUsesSecondMain(deck);

    struct Work { int H; int idx; };
    std::vector<Work> work;
    for (int H = HAND; H >= min_size; --H)
        for (int i = 0; i < static_cast<int>(TB(H).comps.size()); ++i) { work.push_back({ H, i }); }

    std::atomic<int> next{0};
    const std::map<std::string, int>& bof = bucket_of;   // const ref -> concurrent-safe find()
    auto worker = [&]()
    {
        AIEngine ai(rollout_profile, cfg.depth, cfg.budget_ms);
        ai.SetSearchPostCombat(second_main);
        for (;;)
        {
            int w = next.fetch_add(1);
            if (w >= static_cast<int>(work.size())) { break; }
            const int H = work[w].H, idx = work[w].idx;
            const std::vector<int>& comp = tables[HAND - H].comps[idx];

            for (int pd = 0; pd < 2; ++pd)
            {
                double sum = 0.0, sumsq = 0.0;
                for (int r = 0; r < cfg.rollouts; ++r)
                {
                    // Fresh shuffle per rollout: pulling the top comp[b] cards of each bucket samples
                    // WHICH specific members fill the slots (proportional to their deck counts, so
                    // slightly-unequal merged cards are averaged rather than pinned to one
                    // representative) AND the library continuation -- both in one draw.
                    const uint64_t rs = cfg.seed + 21'000'000ULL
                                      + 0x9E3779B97F4A7C15ULL * (static_cast<uint64_t>(r) + 1)
                                      + 1000003ULL * static_cast<uint64_t>(w) + static_cast<uint64_t>(pd);
                    GameState s = GoldFishRunner::SetupGame(deck, rs);
                    s.m_required_pieces = &rollout_profile.required_pieces;
                    s.vial_target_mv    = rollout_profile.vial_target_mv;
                    s.on_the_play       = (pd == 1);
                    Player& ap = s.ActivePlayer();
                    ap.hand.clear();
                    for (int b = 0; b < K; ++b)
                    {
                        int need = comp[b];
                        for (std::size_t k = 0; k < ap.library.size() && need > 0; )
                        {
                            auto bit = bof.find(ap.library[k].m_name.str());
                            if (bit != bof.end() && bit->second == b)
                            { ap.hand.push_back(ap.library[k]); ap.library.erase(ap.library.begin() + k); --need; }
                            else { ++k; }
                        }
                    }
                    double wt = ai.RolloutKeepWinTurn(s, 0, cfg.max_turns);
                    sum += wt; sumsq += wt * wt;
                }
                const int R = cfg.rollouts;
                double mean = sum / R;
                double var  = R > 1 ? std::max(0.0, sumsq / R - mean * mean) : 0.0;
                tables[HAND - H].V[idx][pd]  = mean;
                tables[HAND - H].se[idx][pd] = R > 1 ? std::sqrt(var / R) : 0.0;  // stderr of the mean
            }
        }
    };
    int nthreads = std::max(1, std::min(concurrency_util::AffinityCpuCount(),
                                        static_cast<int>(work.size())));
    std::vector<std::thread> pool;
    for (int t = 0; t < nthreads; ++t) { pool.emplace_back(worker); }
    for (std::thread& th : pool) { th.join(); }

    // ---- 4. KeepVal(hand, m, pd) = best (HAND-m)-subcomposition (optimal bottoming) --------------
    auto keep_val = [&](const std::vector<int>& h, int m, int pd) -> double
    {
        const int target = HAND - m;
        std::vector<std::vector<int>> subs, empty;
        std::vector<int> cur(K, 0);
        EnumComps(0, target, cur, h, subs);   // subcompositions bounded by the hand
        double best = 1e9;
        const SizeTable& t = tables[HAND - target];
        for (const std::vector<int>& s : subs)
        {
            auto it = t.index.find(s);
            if (it != t.index.end()) { best = std::min(best, t.V[it->second][pd]); }
        }
        return best;
    };

    // ---- 5. Backward-induction mulligan value: optimal keep vs the static keep rule --------------
    // Weights: multivariate-hypergeometric P(draw this 7-card composition).
    const long long denom = Comb(static_cast<int>(deck.mainboard.size()), HAND);
    const SizeTable& H7 = tables[0];
    std::vector<double> P(H7.comps.size());
    for (std::size_t i = 0; i < H7.comps.size(); ++i)
    {
        long long num = 1;
        for (int b = 0; b < K; ++b) { num *= Comb(count[b], H7.comps[i][b]); }
        P[i] = static_cast<double>(num) / static_cast<double>(denom);
    }

    AIEngine ref_ai(profile, cfg.depth, cfg.budget_ms);   // static keep rule (ReferenceKeep = KeepHand)
    auto hand_cards = [&](const std::vector<int>& h)
    {
        std::vector<Card> cards;
        for (int b = 0; b < K; ++b) { for (int c = 0; c < h[b]; ++c) { cards.push_back(rep[b]); } }
        return cards;
    };

    os << "\n--- policy value (expected win-turn; lower is better) ---\n";
    os << "pd      D_opt   D_static   gap      keep%%_opt  keep%%_static\n";
    double opt_avg = 0, stat_avg = 0;
    std::array<std::vector<double>, 2> Dopt_pd, Dst_pd;
    for (int pd = 0; pd < 2; ++pd)
    {
        const int M = cfg.max_mull;
        std::vector<double> Dopt(M + 1, 0.0), Dst(M + 1, 0.0);
        // Terminal (forced keep at the deepest mulligan): value = KeepVal, no mull option.
        for (std::size_t i = 0; i < H7.comps.size(); ++i)
        {
            double kv = keep_val(H7.comps[i], M, pd);
            Dopt[M] += P[i] * kv;
            Dst[M]  += P[i] * kv;
        }
        long long kept_opt = 0, kept_st = 0;
        for (int m = M - 1; m >= 0; --m)
        {
            for (std::size_t i = 0; i < H7.comps.size(); ++i)
            {
                double kv = keep_val(H7.comps[i], m, pd);
                Dopt[m] += P[i] * std::min(kv, Dopt[m + 1]);
                bool sk = ref_ai.ReferenceKeep(hand_cards(H7.comps[i]), m, pd == 1);
                Dst[m]  += P[i] * (sk ? kv : Dst[m + 1]);
                if (m == 0)
                {
                    if (kv <= Dopt[1]) { kept_opt++; }
                    if (sk)            { kept_st++;  }
                }
            }
        }
        // keep% at m=0 by hand-count (unweighted) for a quick read of aggressiveness.
        double kopt = 100.0 * static_cast<double>(kept_opt) / static_cast<double>(H7.comps.size());
        double kst  = 100.0 * static_cast<double>(kept_st)  / static_cast<double>(H7.comps.size());
        os << (pd == 1 ? "play  " : "draw  ")
           << "  " << Dopt[0] << "   " << Dst[0]
           << "   " << (Dst[0] - Dopt[0]) << "    " << kopt << "      " << kst << "\n";
        opt_avg += Dopt[0] / 2.0; stat_avg += Dst[0] / 2.0;
        Dopt_pd[pd] = std::move(Dopt); Dst_pd[pd] = std::move(Dst);
    }
    os << "avg     D_opt=" << opt_avg << "  D_static=" << stat_avg
       << "  gap=" << (stat_avg - opt_avg) << " turns (optimal keep decisions vs static)\n";

    // ---- 5b. Full disagreement set: every m=0 hand where optimal != static, ranked by the ------
    // win-turn cost it contributes (P(hand) * |V_keep - mull threshold|). The agreeing hands add 0,
    // so these rows EXACTLY decompose the D_static-D_opt gap. On the play (pd=1).
    auto fmt_hand = [&](const std::vector<int>& h)
    {
        std::string s; bool first = true;
        for (int b = 0; b < K; ++b)
        { if (h[b] > 0) { if (!first) { s += ", "; } first = false;
                          s += std::to_string(h[b]) + "x " + label[b]; } }
        return s;
    };
    struct Diff { std::vector<int> h; double P, kv, thr, weighted; bool opt_keep, st_keep; };
    std::vector<Diff> diffs;
    double mass = 0.0, gap = 0.0, mass_overkeep = 0.0, mass_overmull = 0.0;
    const std::vector<double>& Do = Dopt_pd[1];
    for (std::size_t i = 0; i < H7.comps.size(); ++i)
    {
        double kv = keep_val(H7.comps[i], 0, 1);
        bool ok = kv <= Do[1];
        bool sk = ref_ai.ReferenceKeep(hand_cards(H7.comps[i]), 0, true);
        if (ok == sk) { continue; }
        double regret = std::abs(kv - Do[1]);
        diffs.push_back({ H7.comps[i], P[i], kv, Do[1], P[i] * regret, ok, sk });
        mass += P[i]; gap += P[i] * regret;
        if (sk && !ok) { mass_overkeep += P[i]; } else { mass_overmull += P[i]; }
    }
    std::sort(diffs.begin(), diffs.end(),
              [](const Diff& a, const Diff& b){ return a.weighted > b.weighted; });
    os << "\n--- static vs optimal DISAGREEMENTS (on the play, mull 0) ---\n";
    os << diffs.size() << " hand-types disagree; total draw-prob " << (100.0 * mass)
       << "% ; win-turn cost " << gap << " (static over-keeps " << (100.0 * mass_overkeep)
       << "%, over-mulls " << (100.0 * mass_overmull) << "%)\n";
    os << "rank  P%      Vkeep  thr    static->opt   cost    cum%   hand\n";
    double cum = 0.0;
    for (std::size_t i = 0; i < diffs.size() && i < 25; ++i)
    {
        const Diff& d = diffs[i];
        cum += d.weighted;
        os << "  " << (i + 1) << "   " << (100.0 * d.P) << "   " << d.kv << "  " << d.thr
           << "   " << (d.st_keep ? "KEEP->MULL" : "MULL->KEEP")
           << "   " << d.weighted << "   " << (gap > 0 ? 100.0 * cum / gap : 0.0)
           << "   " << fmt_hand(d.h) << "\n";
    }

    // ---- 6. Decisions on notable marginal hands -------------------------------------------------
    auto decide = [&](const std::vector<std::string>& names)
    {
        std::vector<int> h(K, 0);
        bool ok = true;
        for (const std::string& nm : names)
        {
            auto it = bucket_of.find(nm);
            if (it == bucket_of.end()) { ok = false; break; }
            h[it->second]++;
        }
        if (!ok || (int)names.size() != HAND) { os << "  (skip: " << names.size() << " cards)\n"; return; }
        // recompute Dopt[1]/Dst[1] on the play for the threshold (pd=1).
        const int pd = 1, M = cfg.max_mull;
        std::vector<double> Dopt(M + 1, 0.0), Dst(M + 1, 0.0);
        for (std::size_t i = 0; i < H7.comps.size(); ++i)
        { double kv = keep_val(H7.comps[i], M, pd); Dopt[M] += P[i]*kv; Dst[M] += P[i]*kv; }
        for (int m = M - 1; m >= 0; --m)
            for (std::size_t i = 0; i < H7.comps.size(); ++i)
            { double kv = keep_val(H7.comps[i], m, pd);
              Dopt[m] += P[i]*std::min(kv, Dopt[m+1]);
              Dst[m]  += P[i]*(ref_ai.ReferenceKeep(hand_cards(H7.comps[i]), m, true) ? kv : Dst[m+1]); }
        double kv0 = keep_val(h, 0, pd);
        bool opt_keep = kv0 <= Dopt[1];
        bool st_keep  = ref_ai.ReferenceKeep(hand_cards(h), 0, true);
        os << "  optimal=" << (opt_keep ? "KEEP" : "MULL")
           << " (V=" << kv0 << " vs mull " << Dopt[1] << ")   static="
           << (st_keep ? "KEEP" : "MULL") << "   [";
        for (std::size_t i = 0; i < names.size(); ++i) { os << (i ? ", " : "") << names[i]; }
        os << "]\n";
    };
    // ---- 5c. Label-noise diagnostic: how much draw-probability mass sits within k*stderr of the ---
    // keep/mull threshold (where R's noise can flip the decision). Large mass here => need more R.
    {
        const double thr = Dopt_pd[1][1];   // on-the-play mull-0 threshold
        double se_mass = 0.0, amb1 = 0.0, amb2 = 0.0, noise_regret = 0.0; long long amb2n = 0;
        for (std::size_t i = 0; i < H7.comps.size(); ++i)
        {
            const double se = H7.se[i][1];
            const double v  = H7.V[i][1];    // KeepVal(h,0) = V[7][h] for m=0
            const double d  = std::abs(v - thr);
            se_mass += P[i] * se;
            if (se > 0 && d < se)       { amb1 += P[i]; }
            if (se > 0 && d < 2.0 * se) { amb2 += P[i]; ++amb2n; }
            // Expected win-turn lost to noise on this hand: probability the noisy estimate lands on
            // the wrong side of the threshold, times the cost of being wrong (= the true gap d).
            // Flips only happen where d is small, so each term -- and the total -- is marginal.
            const double flip = (se > 0) ? 0.5 * std::erfc(d / (se * 1.4142135623730951))
                                         : (d == 0 ? 0.5 : 0.0);
            noise_regret += P[i] * d * flip;
        }
        os << "\n--- label noise (R=" << cfg.rollouts << ", on the play, mull 0) ---\n";
        os << "prob-weighted per-hand stderr = " << se_mass << " turns\n";
        os << "draw-prob mass within 1 stderr of threshold = " << (100.0 * amb1)
           << "% ; within 2 stderr = " << (100.0 * amb2) << "% (" << amb2n << " hand-types)\n";
        os << "EST. win-turn lost to label noise ~= " << noise_regret
           << " (flips only near-ties -> shortfall stays marginal; raise R to shrink)\n";
        // Project the noise-regret at other R from this run's per-hand variance (stderr ~ 1/sqrt(R)).
        // Shows how low R can go before the policy degrades -- the lever for higher-combination decks.
        os << "projected regret vs R (extrapolated): ";
        for (int Rt : { 5, 10, 20, 50, 100, 200, 500 })
        {
            double reg = 0.0;
            for (std::size_t i = 0; i < H7.comps.size(); ++i)
            {
                const double seR = H7.se[i][1] * std::sqrt(static_cast<double>(cfg.rollouts) / Rt);
                const double d   = std::abs(H7.V[i][1] - thr);
                const double flip = (seR > 0) ? 0.5 * std::erfc(d / (seR * 1.4142135623730951))
                                              : (d == 0 ? 0.5 : 0.0);
                reg += P[i] * d * flip;
            }
            os << "R=" << Rt << ":~" << reg << "  ";
        }
        os << "\n";
    }

    // ---- 6. Serialize the keep policy (bucket map + per-composition keep decisions) --------------
    // Built via the shared BuildPolicyFromTables so the in-run policy is byte-identical to what the
    // offline merge tool (RunKeepMerge) produces from the same pooled V's.
    if (!cfg.out_profile.empty())
    {
        std::vector<std::vector<std::string>> bmembers;
        for (int b = 0; b < K; ++b) { bmembers.push_back(eq.classes[b].members); }
        ExhaustiveKeepPolicy ek = BuildPolicyFromTables(
            tables, count, bmembers, static_cast<int>(deck.mainboard.size()),
            cfg.max_mull, cfg.rollouts);
        ek.commit = cfg.commit;
        MulliganProfile out = profile;
        out.exhaustive_keep = std::move(ek);
        if (SaveDeckProfile(cfg.out_profile, out))
        { os << "\nexhaustive keep policy written to " << cfg.out_profile << "\n"; }
        else
        { os << "\nWARNING: failed to write " << cfg.out_profile << "\n"; }
    }

    // ---- 6b. Poolable RAW sidecar: per-(size,composition,pd) rollout sum + count, plus a fingerprint
    // gating cross-machine merges (same commit + bucket map + deck; disjoint seed_base). count == R
    // here but is stored per entry so a merge is a plain element-wise sum.
    if (!cfg.out_raw.empty())
    {
        using json = nlohmann::json;
        // Fingerprints: bucket map (sorted member lists) and deck (sorted mainboard names).
        uint64_t bucket_fp = 1469598103934665603ULL;
        for (int b = 0; b < K; ++b)
        { bucket_fp = Fnv("|", bucket_fp); for (const std::string& n : eq.classes[b].members) bucket_fp = Fnv(n + ",", bucket_fp); }
        std::vector<std::string> deck_names;
        for (const Card& c : deck.mainboard) { deck_names.push_back(c.m_name.str()); }
        std::sort(deck_names.begin(), deck_names.end());
        uint64_t deck_fp = 1469598103934665603ULL;
        for (const std::string& n : deck_names) { deck_fp = Fnv(n + ",", deck_fp); }

        json root;
        root["meta"] = {
            { "commit", cfg.commit }, { "bucket_fp", bucket_fp }, { "deck_fp", deck_fp },
            { "seed_base", cfg.seed }, { "R", cfg.rollouts }, { "max_mull", cfg.max_mull },
            { "probes", cfg.probes }, { "threshold", cfg.threshold }, { "K", K }, { "equiv_seed", cfg.equiv_seed }
        };
        json buckets = json::array();
        for (int b = 0; b < K; ++b) { buckets.push_back(eq.classes[b].members); }
        root["buckets"] = buckets;
        json sizes = json::array();
        for (int H = HAND; H >= min_size; --H)
        {
            const SizeTable& t = tables[HAND - H];
            json entries = json::array();
            for (std::size_t i = 0; i < t.comps.size(); ++i)
            {
                json je;
                je["comp"]  = t.comps[i];
                je["sum"]   = { t.V[i][0] * cfg.rollouts, t.V[i][1] * cfg.rollouts };
                je["count"] = { cfg.rollouts, cfg.rollouts };
                entries.push_back(je);
            }
            sizes.push_back({ { "H", H }, { "entries", entries } });
        }
        root["sizes"] = sizes;
        std::ofstream f(cfg.out_raw);
        if (f) { f << root.dump(); os << "raw poolable table written to " << cfg.out_raw << "\n"; }
        else   { os << "WARNING: failed to write " << cfg.out_raw << "\n"; }
    }

    os << "\n--- notable hands (on the play) ---\n";
    decide({ "Unclaimed Territory", "Ancient Ziggurat", "Aether Vial", "Aether Vial",
             "Aether Vial", "Aether Vial", "Predatory Sliver" });          // the 4-Vial trap
    decide({ "Unclaimed Territory", "Ancient Ziggurat", "Aether Vial", "Predatory Sliver",
             "Galerider Sliver", "Striking Sliver", "Sinew Sliver" });      // 2 land + 1 Vial + slivers
    decide({ "Unclaimed Territory", "Cavern of Souls", "Sliver Hive", "Predatory Sliver",
             "Galerider Sliver", "Muscle Sliver", "Striking Sliver" });     // good curve
    os << std::flush;
}

// ---- Offline merge: pool raw sidecars from multiple runs/machines into one policy -----------------
// Each sidecar carries per-(size,composition,pd) rollout sum+count plus a fingerprint {commit,
// bucket_fp, deck_fp, K, max_mull, equiv_seed} and its seed_base(s). Pooling is a plain element-wise
// sum, valid ONLY when every input shares the same play-logic (commit), bucket map and deck, and the
// seed_bases are DISJOINT (else continuations overlap and R is double-counted). We reject any
// mismatch or overlap, sum the compatible ones, rebuild V=sum/count, and run the SAME
// BuildPolicyFromTables used in-run -> an identical serialized policy at the pooled R.
void RunKeepMerge(std::ostream& os, const Decklist& deck, const MulliganProfile& profile,
                  const std::vector<std::string>& raw_paths,
                  const std::string& out_profile, const std::string& out_raw)
{
    using json = nlohmann::json;
    if (raw_paths.empty()) { os << "merge: no raw sidecars given (set MTG_MERGE_INPUTS)\n"; return; }

    struct Acc { double sum[2] = { 0, 0 }; long long cnt[2] = { 0, 0 }; };
    std::map<int, std::map<std::vector<int>, Acc>> pooled;   // H -> comp -> summed sum/count
    std::vector<std::vector<std::string>> buckets;
    std::string commit;
    uint64_t bucket_fp = 0, deck_fp = 0, equiv_seed = 0;
    int K = -1, max_mull = -1;
    std::set<uint64_t> seed_bases;                           // every base already folded in (overlap guard)
    bool first = true;
    long long files_ok = 0;

    os << "=== KEEP MERGE (" << raw_paths.size() << " sidecar(s)) ===\n";
    for (const std::string& path : raw_paths)
    {
        std::ifstream f(path);
        if (!f) { os << "  SKIP (cannot open): " << path << "\n"; continue; }
        json root;
        try { f >> root; }
        catch (const std::exception& e) { os << "  SKIP (bad json: " << e.what() << "): " << path << "\n"; continue; }
        if (!root.contains("meta") || !root.contains("sizes")) { os << "  SKIP (not a raw sidecar): " << path << "\n"; continue; }
        const json& m = root["meta"];
        const std::string c   = m.value("commit", std::string());
        const uint64_t bfp = m.value("bucket_fp", 0ULL);
        const uint64_t dfp = m.value("deck_fp", 0ULL);
        const uint64_t es  = m.value("equiv_seed", 0ULL);
        const int      k   = m.value("K", -1);
        const int      mm  = m.value("max_mull", -1);
        // A file's own bases: an already-merged sidecar lists all its pooled bases; a raw run lists one.
        std::vector<uint64_t> file_bases;
        if (m.contains("pooled_seed_bases")) { file_bases = m["pooled_seed_bases"].get<std::vector<uint64_t>>(); }
        else { file_bases.push_back(m.value("seed_base", 0ULL)); }

        if (first)
        {
            commit = c; bucket_fp = bfp; deck_fp = dfp; equiv_seed = es; K = k; max_mull = mm;
            buckets = root["buckets"].get<std::vector<std::vector<std::string>>>();
            first = false;
        }
        else if (c != commit || bfp != bucket_fp || dfp != deck_fp || es != equiv_seed
                 || k != K || mm != max_mull)
        {
            os << "  REJECT (fingerprint mismatch): " << path << "\n"
               << "    commit "     << c   << " vs " << commit
               << " | bucket_fp "   << bfp << " vs " << bucket_fp
               << " | deck_fp "     << dfp << " vs " << deck_fp
               << " | equiv_seed "  << es  << " vs " << equiv_seed
               << " | K " << k << "/" << K << " | max_mull " << mm << "/" << max_mull << "\n";
            continue;
        }
        bool overlap = false;
        for (uint64_t b : file_bases) { if (seed_bases.count(b)) { overlap = true; break; } }
        if (overlap)
        { os << "  REJECT (seed_base overlap -> would double-count): " << path << "\n"; continue; }
        for (uint64_t b : file_bases) { seed_bases.insert(b); }

        for (const json& sz : root["sizes"])
        {
            const int H = sz.value("H", 0);
            for (const json& e : sz["entries"])
            {
                std::vector<int> comp = e["comp"].get<std::vector<int>>();
                Acc& a = pooled[H][comp];
                a.sum[0] += e["sum"][0].get<double>();     a.sum[1] += e["sum"][1].get<double>();
                a.cnt[0] += e["count"][0].get<long long>(); a.cnt[1] += e["count"][1].get<long long>();
            }
        }
        os << "  + " << path << "  (seed_base " << file_bases.front()
           << (file_bases.size() > 1 ? "+" + std::to_string(file_bases.size() - 1) : "")
           << ", R=" << m.value("R", 0) << ")\n";
        ++files_ok;
    }

    if (files_ok == 0) { os << "merge: no compatible sidecars pooled; nothing written\n"; return; }

    // The deck on the command line must be the one the sidecars were built on (its counts drive the
    // hypergeometric hand weights). Verify the fingerprint; warn loudly on mismatch rather than
    // silently weight with the wrong counts.
    std::vector<std::string> dn;
    for (const Card& c : deck.mainboard) { dn.push_back(c.m_name.str()); }
    std::sort(dn.begin(), dn.end());
    uint64_t my_deck_fp = 1469598103934665603ULL;
    for (const std::string& n : dn) { my_deck_fp = Fnv(n + ",", my_deck_fp); }
    if (my_deck_fp != deck_fp)
    { os << "WARNING: command-line deck fp " << my_deck_fp << " != pooled deck_fp " << deck_fp
         << " -- hand weights may be wrong. Pass the SAME deck the sidecars were built on.\n"; }

    std::map<std::string, int> bucket_of;
    for (int b = 0; b < static_cast<int>(buckets.size()); ++b)
        for (const std::string& nm : buckets[b]) { bucket_of[nm] = b; }
    std::vector<int> count(buckets.size(), 0);
    for (const Card& c : deck.mainboard)
    { auto it = bucket_of.find(c.m_name.str()); if (it != bucket_of.end()) { count[it->second]++; } }

    const int min_size = std::max(1, HAND - max_mull);
    std::vector<SizeTable> tables(HAND - min_size + 1);
    int effective_R = 0;
    for (int H = HAND; H >= min_size; --H)
    {
        SizeTable t;
        auto pit = pooled.find(H);
        if (pit != pooled.end())
        {
            for (const auto& kv : pit->second)
            {
                const int i = static_cast<int>(t.comps.size());
                t.comps.push_back(kv.first);
                t.index[kv.first] = i;
                std::array<double, 2> V = {
                    kv.second.cnt[0] ? kv.second.sum[0] / kv.second.cnt[0] : 0.0,
                    kv.second.cnt[1] ? kv.second.sum[1] / kv.second.cnt[1] : 0.0 };
                t.V.push_back(V);
                t.se.push_back({ 0.0, 0.0 });
                if (H == HAND) { effective_R = std::max<long long>(effective_R, kv.second.cnt[1]); }
            }
        }
        else { os << "WARNING: pooled table missing size " << H << "\n"; }
        tables[HAND - H] = std::move(t);
    }

    os << "pooled " << files_ok << " file(s); " << seed_bases.size()
       << " distinct seed_base(s); effective R=" << effective_R << " per hand (per pd)\n";
    for (int b = 0; b < static_cast<int>(buckets.size()); ++b)
    {
        os << "  [" << b << "] " << buckets[b].front()
           << (buckets[b].size() > 1 ? " (+" + std::to_string(buckets[b].size() - 1) + ")" : "")
           << " x" << count[b] << "\n";
    }

    std::array<std::vector<double>, 2> Dopt;
    ExhaustiveKeepPolicy ek = BuildPolicyFromTables(
        tables, count, buckets, static_cast<int>(deck.mainboard.size()), max_mull, effective_R, &Dopt);
    ek.commit = commit;
    os << "merged policy: D_opt(draw)=" << Dopt[0][0] << "  D_opt(play)=" << Dopt[1][0]
       << "  (expected win-turn, optimal keep)\n";

    if (!out_profile.empty())
    {
        MulliganProfile out = profile;
        out.exhaustive_keep = ek;
        if (SaveDeckProfile(out_profile, out)) { os << "merged keep policy -> " << out_profile << "\n"; }
        else { os << "WARNING: failed to write " << out_profile << "\n"; }
    }

    // Re-emit the pooled table as a sidecar so merges chain (merge of merges). It lists ALL folded-in
    // seed_bases so a downstream merge still detects any overlap.
    if (!out_raw.empty())
    {
        std::vector<uint64_t> bases(seed_bases.begin(), seed_bases.end());
        json root;
        root["meta"] = {
            { "commit", commit }, { "bucket_fp", bucket_fp }, { "deck_fp", deck_fp },
            { "seed_base", bases.empty() ? 0ULL : bases.front() },
            { "pooled_seed_bases", bases }, { "pooled_files", files_ok },
            { "R", effective_R }, { "max_mull", max_mull }, { "K", K }, { "equiv_seed", equiv_seed }
        };
        json jb = json::array();
        for (const auto& mem : buckets) { jb.push_back(mem); }
        root["buckets"] = jb;
        json sizes = json::array();
        for (int H = HAND; H >= min_size; --H)
        {
            const SizeTable& t = tables[HAND - H];
            json entries = json::array();
            for (std::size_t i = 0; i < t.comps.size(); ++i)
            {
                const Acc& a = pooled[H][t.comps[i]];
                json je;
                je["comp"]  = t.comps[i];
                je["sum"]   = { a.sum[0], a.sum[1] };
                je["count"] = { a.cnt[0], a.cnt[1] };
                entries.push_back(je);
            }
            sizes.push_back({ { "H", H }, { "entries", entries } });
        }
        root["sizes"] = sizes;
        std::ofstream f(out_raw);
        if (f) { f << root.dump(); os << "merged raw table -> " << out_raw << "\n"; }
        else   { os << "WARNING: failed to write " << out_raw << "\n"; }
    }
    os << std::flush;
}
