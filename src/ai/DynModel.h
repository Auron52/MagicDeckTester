// Inference-only loader + forward pass for the dynamic (latent-rollout) d0 policy model
// trained by tools/dyntrain. Mirrors that trainer's forward EXACTLY (train/serve parity):
//   T==0 : pred = val( tanh( rep([state;plan]) ) )                         (plain MLP baseline)
//   T>=1 : h0 = tanh(rep(state)); a = plan_enc(plan);
//          h_{t+1} = tanh(dyn([h_t; (t==0? a : 0)]));  pred = val(h_T)      (dynamic rollout)
// Features arrive as the engine's ExtractMidGameFeatures(state, plan_summary) int vector (enum order);
// `bucket` routes each into the state/plan sub-vectors (in order), then per-bucket standardization.
//
// Float NN => NOT byte-identical (the aggregate-stability regime, per learned-d0-policy.md). Gated by
// MTG_DYN_MODEL; empty model => never consulted (byte-identical default).
#pragma once
#include <vector>
#include <string>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>

struct DynModel {
    int T = 0, H = 0, nstate = 0, nplan = 0;
    std::vector<int>   bucket;                 // per feature (enum order): 0=state, 1=plan
    std::vector<float> mean_s, sd_s, mean_p, sd_p;
    std::vector<float> repW, repb, planW, planb, dynW, dynb, valW, valb;
    int rep_in = 0;

    bool empty() const { return H == 0 || valW.empty(); }

    static std::vector<float> f(const nlohmann::json& j, const char* k) {
        std::vector<float> v; if (j.contains(k)) for (const auto& x : j[k]) v.push_back(x.get<float>()); return v;
    }

    // Load from a tools/dyntrain --out JSON. Returns false (empty) on any problem.
    bool Load(const std::string& path) {
        std::ifstream in(path); if (!in) return false;
        nlohmann::json j; try { in >> j; } catch (...) { return false; }
        if (!j.value("type", std::string()).size()) return false;
        T = j.value("T", 0); H = j.value("H", 0);
        nstate = j.value("nstate", 0); nplan = j.value("nplan", 0);
        for (const auto& x : j["bucket"]) bucket.push_back(x.get<int>());
        mean_s = f(j,"mean_s"); sd_s = f(j,"sd_s"); mean_p = f(j,"mean_p"); sd_p = f(j,"sd_p");
        repW = f(j,"rep_W"); repb = f(j,"rep_b");
        planW = f(j,"plan_W"); planb = f(j,"plan_b");
        dynW = f(j,"dyn_W"); dynb = f(j,"dyn_b");
        valW = f(j,"val_W"); valb = f(j,"val_b");
        rep_in = H > 0 ? (int)repW.size() / H : 0;
        return !empty();
    }

    static void matvec(const std::vector<float>& W, const std::vector<float>& b,
                       const std::vector<float>& x, int out, int in, std::vector<float>& y) {
        y.assign(out, 0.0f);
        for (int o = 0; o < out; ++o) { float s = b[o]; const float* wr = &W[(size_t)o*in];
            for (int i = 0; i < in; ++i) s += wr[i]*x[i]; y[o] = s; }
    }
    static void tanhv(std::vector<float>& v) { for (float& z : v) z = std::tanh(z); }

    // Predicted (de-clairvoyed) win turn from this (state, plan). Lower = better plan.
    float PredictWinTurn(const std::vector<int>& feats) const {
        std::vector<float> st, pl;
        for (size_t i = 0; i < bucket.size() && i < feats.size(); ++i)
            (bucket[i] ? pl : st).push_back((float)feats[i]);
        for (int k = 0; k < nstate && k < (int)st.size(); ++k) st[k] = (st[k]-mean_s[k])/sd_s[k];
        for (int k = 0; k < nplan  && k < (int)pl.size(); ++k) pl[k] = (pl[k]-mean_p[k])/sd_p[k];
        std::vector<float> h, o;
        if (T == 0) {
            std::vector<float> x = st; x.insert(x.end(), pl.begin(), pl.end());
            matvec(repW, repb, x, H, rep_in, h); tanhv(h);
        } else {
            matvec(repW, repb, st, H, nstate, h); tanhv(h);
            std::vector<float> a; matvec(planW, planb, pl, H, nplan, a);
            for (int t = 0; t < T; ++t) {
                std::vector<float> in(2*H);
                for (int k = 0; k < H; ++k) in[k] = h[k];
                for (int k = 0; k < H; ++k) in[H+k] = (t==0 ? a[k] : 0.0f);
                matvec(dynW, dynb, in, H, 2*H, h); tanhv(h);
            }
        }
        matvec(valW, valb, h, 1, H, o);
        return o[0];
    }

    // Ranking key: higher = better plan. The engine compares plans by (wins, value, mask); value is
    // ordinal, so we return -win_turn scaled to int range. Lethal plans bypass this (exact check wins).
    long long ScoreHigherBetter(const std::vector<int>& feats) const {
        float pred = PredictWinTurn(feats);
        long long s = (long long)std::llround(-pred * 1000.0f);
        const long long kLo = -1000000000LL, kHi = 1000000000LL;
        return s < kLo ? kLo : (s > kHi ? kHi : s);
    }
};
