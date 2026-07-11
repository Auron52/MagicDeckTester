// Minimal, self-contained NN primitives for the dynamic (latent-rollout) policy model.
// Matrix-level manual backprop (fast; no scalar autodiff) + Adam. Float32, single-thread.
// Deliberately tiny + dependency-free: the container has no torch/numpy, and play-time
// inference must be in-process C++ anyway, so training + inference share this code.
//
// Layout: all matrices row-major, stored flat in std::vector<float>. A "batch" is a run of
// rows; layers cache their forward inputs for the backward pass. Determinism: seeded RNG,
// fixed op order (results are stable run-to-run for a fixed seed — the aggregate-stability
// regime the design doc calls for once we leave fixed-point).
#pragma once
#include <vector>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <fstream>
#include <cstdio>

namespace dyn {

using Vec = std::vector<float>;

// Deterministic Xavier/He init RNG.
struct Rng {
    std::mt19937 g;
    explicit Rng(uint64_t seed) : g(seed) {}
    float normal(float sd) { std::normal_distribution<float> d(0.0f, sd); return d(g); }
    float uniform() { std::uniform_real_distribution<float> d(0.0f, 1.0f); return d(g); }
};

// A dense (fully-connected) layer with Adam state. Row-major W is [out x in].
struct Linear {
    int in = 0, out = 0;
    Vec W, b;                 // params
    Vec gW, gb;               // grads (accumulated over a batch)
    Vec mW, vW, mb, vb;       // Adam moments

    Linear() = default;
    Linear(int in_, int out_, Rng& rng, float gain = 1.0f) { init(in_, out_, rng, gain); }
    void init(int in_, int out_, Rng& rng, float gain) {
        in = in_; out = out_;
        W.assign(out * in, 0.0f); b.assign(out, 0.0f);
        gW.assign(out * in, 0.0f); gb.assign(out, 0.0f);
        mW.assign(out * in, 0.0f); vW.assign(out * in, 0.0f);
        mb.assign(out, 0.0f); vb.assign(out, 0.0f);
        float sd = gain * std::sqrt(2.0f / (in + out));   // Xavier-ish
        for (float& w : W) w = rng.normal(sd);
    }
    // Forward one row: y[out] = W x + b.
    void forward(const Vec& x, Vec& y) const {
        y.assign(out, 0.0f);
        for (int o = 0; o < out; ++o) {
            const float* wr = &W[o * in];
            float s = b[o];
            for (int i = 0; i < in; ++i) s += wr[i] * x[i];
            y[o] = s;
        }
    }
    // Backward one row: given the forward input x and dL/dy, accumulate param grads, return dL/dx.
    // x is passed explicitly so a SHARED layer (reused across recurrent unroll steps) backprops
    // correctly with each step's own input.
    void backward(const Vec& x, const Vec& dy, Vec& dx) {
        dx.assign(in, 0.0f);
        for (int o = 0; o < out; ++o) {
            float d = dy[o];
            gb[o] += d;
            const float* wr = &W[o * in];
            float* gwr = &gW[o * in];
            for (int i = 0; i < in; ++i) {
                gwr[i] += d * x[i];
                dx[i]  += d * wr[i];
            }
        }
    }
    void zero_grad() { std::fill(gW.begin(), gW.end(), 0.0f); std::fill(gb.begin(), gb.end(), 0.0f); }
    void adam_step(float lr, float beta1, float beta2, float eps, float wd, int t) {
        auto upd = [&](Vec& p, Vec& g, Vec& m, Vec& v) {
            float bc1 = 1.0f - std::pow(beta1, (float)t);
            float bc2 = 1.0f - std::pow(beta2, (float)t);
            for (size_t k = 0; k < p.size(); ++k) {
                float gr = g[k] + wd * p[k];              // decoupled-ish weight decay
                m[k] = beta1 * m[k] + (1 - beta1) * gr;
                v[k] = beta2 * v[k] + (1 - beta2) * gr * gr;
                float mh = m[k] / bc1, vh = v[k] / bc2;
                p[k] -= lr * mh / (std::sqrt(vh) + eps);
            }
        };
        upd(W, gW, mW, vW); upd(b, gb, mb, vb);
    }
};

// ReLU with cached mask.
struct ReLU {
    Vec mask;
    void forward(const Vec& x, Vec& y) {
        y.assign(x.size(), 0.0f); mask.assign(x.size(), 0.0f);
        for (size_t i = 0; i < x.size(); ++i) { if (x[i] > 0) { y[i] = x[i]; mask[i] = 1.0f; } }
    }
    void backward(const Vec& dy, Vec& dx) {
        dx.assign(dy.size(), 0.0f);
        for (size_t i = 0; i < dy.size(); ++i) dx[i] = dy[i] * mask[i];
    }
};

// tanh with cached output (for the recurrent latent).
struct Tanh {
    Vec out;
    void forward(const Vec& x, Vec& y) {
        out.assign(x.size(), 0.0f); y.assign(x.size(), 0.0f);
        for (size_t i = 0; i < x.size(); ++i) { out[i] = std::tanh(x[i]); y[i] = out[i]; }
    }
    void backward(const Vec& dy, Vec& dx) {
        dx.assign(dy.size(), 0.0f);
        for (size_t i = 0; i < dy.size(); ++i) dx[i] = dy[i] * (1.0f - out[i] * out[i]);
    }
};

} // namespace dyn
