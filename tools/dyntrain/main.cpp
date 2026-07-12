// Dynamic (latent-rollout) policy trainer — the d0-replacement experiment.
//
// Reads NC-teacher EVAL rows (per-candidate: label = honest reshuffle-avg win turn, + features),
// groups by decision, and trains a model that ranks candidate plans. The model does an internal
// T-step latent rollout (the "dynamic" part): h0=f(state); h_{t+1}=tanh(dyn([h_t; a])); pred=val(h_T),
// where the plan is injected at step 0. T=0 collapses to a plain MLP baseline over [state;plan].
//
// Go/no-go metric: does the dynamic model (T>=1) beat the static plateau (GBDT) on held-out
// PICK-REGRET (avg extra win turns from following the model's argmin vs the teacher's best plan)?
//
//   dyntrain <rows> [--T 2] [--H 32] [--epochs 40] [--lr 3e-3] [--holdout-seed-mod 5]
#include "nn.h"
#include <algorithm>
#include <cstring>
#include <map>
#include <array>
#include <sstream>

using dyn::Vec; using dyn::Linear; using dyn::Rng;

struct Row { float label; Vec sfeat, pfeat; long long seed; int turn; };
struct Group { std::vector<int> idx; };   // row indices sharing a decision

static std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> t; std::istringstream is(s); std::string w;
    while (is >> w) t.push_back(w); return t;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: dyntrain <rows> [--T n] [--H n] [--epochs n] [--lr f]\n"); return 2; }
    std::string rows_path = argv[1];
    int T = 2, H = 32, epochs = 40; float lr = 3e-3f; int hmod = 5; std::string out_path;
    float gamma = 0.0f;   // policy cross-entropy weight (train the argmin-of-pred onto the teacher's best plan)
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--T") && i+1<argc) T = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--H") && i+1<argc) H = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--epochs") && i+1<argc) epochs = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--lr") && i+1<argc) lr = (float)std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--holdout-seed-mod") && i+1<argc) hmod = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--gamma") && i+1<argc) gamma = (float)std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--out") && i+1<argc) out_path = argv[++i];
    }

    // ---- Load header + rows, split state/plan feature columns by name ----
    std::ifstream f(rows_path);
    if (!f) { std::fprintf(stderr, "cannot open %s\n", rows_path.c_str()); return 1; }
    std::string line; std::getline(f, line);
    if (!line.empty() && line[0] == '#') line = line.substr(1);
    std::vector<std::string> hdr = split_ws(line);   // label feat... seed turn
    int ncol = (int)hdr.size();
    int nfeat = ncol - 3;                            // minus label, seed, turn
    std::vector<int> is_plan(nfeat, 0);
    int nstate = 0, nplan = 0, base_pidx = -1;   // base_pidx = index of plan_baseline_eval within pfeat
    std::vector<std::string> state_names, plan_names;   // names in sfeat / pfeat order (for failure analysis)
    for (int j = 0; j < nfeat; ++j) {
        const std::string& nm = hdr[1 + j];
        if (nm.rfind("plan_", 0) == 0) { if (nm == "plan_baseline_eval") base_pidx = nplan; is_plan[j] = 1; ++nplan; plan_names.push_back(nm); }
        else { ++nstate; state_names.push_back(nm); }
    }

    std::vector<Row> rows;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> t = split_ws(line);
        if ((int)t.size() != ncol) continue;
        Row r; r.label = std::stof(t[0]);
        r.seed = std::stoll(t[ncol-2]); r.turn = std::stoi(t[ncol-1]);
        for (int j = 0; j < nfeat; ++j) {
            float v = std::stof(t[1 + j]);
            if (is_plan[j]) r.pfeat.push_back(v); else r.sfeat.push_back(v);
        }
        rows.push_back(std::move(r));
    }
    std::fprintf(stderr, "loaded %zu rows  state=%d plan=%d  T=%d H=%d\n",
                 rows.size(), nstate, nplan, T, H);

    // ---- Standardize features (train-set stats); keep the stats for serialization ----
    Vec mean_s, sd_s, mean_p, sd_p;
    auto standardize = [&](int dim, bool plan, Vec& mean, Vec& sd) {
        mean.assign(dim, 0.0f); sd.assign(dim, 0.0f); long n = 0;
        for (const Row& r : rows) { if (r.seed % hmod == 0) continue; const Vec& v = plan?r.pfeat:r.sfeat;
            for (int j = 0; j < dim; ++j) mean[j] += v[j]; ++n; }
        for (int j = 0; j < dim; ++j) mean[j] /= std::max(1L, n);
        for (const Row& r : rows) { if (r.seed % hmod == 0) continue; const Vec& v = plan?r.pfeat:r.sfeat;
            for (int j = 0; j < dim; ++j) { float d = v[j]-mean[j]; sd[j] += d*d; } }
        for (int j = 0; j < dim; ++j) sd[j] = std::sqrt(sd[j]/std::max(1L,n)) + 1e-6f;
        for (Row& r : rows) { Vec& v = plan?r.pfeat:r.sfeat;
            for (int j = 0; j < dim; ++j) v[j] = (v[j]-mean[j])/sd[j]; }
    };
    standardize(nstate, false, mean_s, sd_s); standardize(nplan, true, mean_p, sd_p);

    // ---- Group rows into decisions (seed,turn); split train/holdout ----
    std::map<std::pair<long long,int>, Group> gm;
    for (int i = 0; i < (int)rows.size(); ++i) gm[{rows[i].seed, rows[i].turn}].idx.push_back(i);
    std::vector<Group> train, test;
    for (auto& kv : gm) { if (kv.second.idx.size() < 2) continue;
        (kv.first.first % hmod == 0 ? test : train).push_back(kv.second); }
    std::fprintf(stderr, "decisions: train=%zu test=%zu\n", train.size(), test.size());

    // ---- Model ----
    Rng rng(1234);
    int rep_in = (T == 0) ? (nstate + nplan) : nstate;
    Linear rep(rep_in, H, rng, 1.0f);
    Linear plan_enc, dyn_;
    if (T >= 1) { plan_enc.init(nplan, H, rng, 1.0f); dyn_.init(2*H, H, rng, 1.0f); }
    Linear val(H, 1, rng, 1.0f);

    struct Cache { Vec x0; std::vector<Vec> hs; Vec a; float pred; };
    auto tanhv = [](const Vec& x){ Vec y(x.size()); for (size_t i=0;i<x.size();++i) y[i]=std::tanh(x[i]); return y; };

    auto forward = [&](const Row& r, Cache& c) {
        if (T == 0) {
            c.x0.clear(); c.x0.insert(c.x0.end(), r.sfeat.begin(), r.sfeat.end());
            c.x0.insert(c.x0.end(), r.pfeat.begin(), r.pfeat.end());
            Vec pre; rep.forward(c.x0, pre); Vec h = tanhv(pre);
            c.hs = { h };
            Vec o; val.forward(h, o); c.pred = o[0];
        } else {
            c.x0 = r.sfeat;
            Vec pre; rep.forward(r.sfeat, pre); Vec h = tanhv(pre);
            plan_enc.forward(r.pfeat, c.a);
            c.hs.clear(); c.hs.push_back(h);
            for (int t = 0; t < T; ++t) {
                Vec in(2*H); for (int k=0;k<H;++k) in[k]=c.hs[t][k];
                for (int k=0;k<H;++k) in[H+k] = (t==0? c.a[k] : 0.0f);
                Vec dpre; dyn_.forward(in, dpre); c.hs.push_back(tanhv(dpre));
            }
            Vec o; val.forward(c.hs.back(), o); c.pred = o[0];
        }
        return c.pred;
    };

    // backward: given dL/dpred, accumulate grads through the (cached) forward.
    auto backward = [&](const Row& r, const Cache& c, float dpred) {
        Vec dy = { dpred };
        if (T == 0) {
            Vec dh; val.backward(c.hs[0], dy, dh);
            Vec dpre(H); for (int k=0;k<H;++k) dpre[k] = dh[k]*(1.0f - c.hs[0][k]*c.hs[0][k]);
            Vec dx; rep.backward(c.x0, dpre, dx);
        } else {
            Vec dhT; val.backward(c.hs[T], dy, dhT);
            Vec dh = dhT, da(H, 0.0f);
            for (int t = T-1; t >= 0; --t) {
                const Vec& hnext = c.hs[t+1];
                Vec dpre(H); for (int k=0;k<H;++k) dpre[k] = dh[k]*(1.0f - hnext[k]*hnext[k]);
                Vec in(2*H); for (int k=0;k<H;++k) in[k]=c.hs[t][k];
                for (int k=0;k<H;++k) in[H+k] = (t==0? c.a[k] : 0.0f);
                Vec din; dyn_.backward(in, dpre, din);
                dh.assign(H,0.0f); for (int k=0;k<H;++k) dh[k]=din[k];
                if (t==0) for (int k=0;k<H;++k) da[k]=din[H+k];
            }
            Vec dp; plan_enc.backward(r.pfeat, da, dp);
            Vec dpre0(H); for (int k=0;k<H;++k) dpre0[k] = dh[k]*(1.0f - c.hs[0][k]*c.hs[0][k]);
            Vec dx; rep.backward(r.sfeat, dpre0, dx);
        }
    };

    auto zero_grads = [&]{ rep.zero_grad(); val.zero_grad(); if (T>=1){ plan_enc.zero_grad(); dyn_.zero_grad(); } };
    auto adam = [&](int t){ float b1=0.9f,b2=0.999f,eps=1e-8f,wd=1e-5f;
        rep.adam_step(lr,b1,b2,eps,wd,t); val.adam_step(lr,b1,b2,eps,wd,t);
        if (T>=1){ plan_enc.adam_step(lr,b1,b2,eps,wd,t); dyn_.adam_step(lr,b1,b2,eps,wd,t);} };

    // ---- Train (per-decision minibatch: MSE + pairwise rank) ----
    float alpha = 0.3f, beta = 1.0f;   // MSE weight, pairwise weight
    std::mt19937 sh(777);
    int step = 0;
    for (int e = 0; e < epochs; ++e) {
        std::shuffle(train.begin(), train.end(), sh);
        double eloss = 0; int recount = 0;
        for (const Group& g : train) {
            int m = (int)g.idx.size();
            std::vector<Cache> cs(m); Vec pred(m), dpred(m, 0.0f);
            for (int i = 0; i < m; ++i) pred[i] = forward(rows[g.idx[i]], cs[i]);
            // MSE
            for (int i = 0; i < m; ++i) { float e2 = pred[i]-rows[g.idx[i]].label; dpred[i]+=alpha*2.0f*e2; eloss+=alpha*e2*e2; }
            // pairwise: for each better(i)/worse(j), push pred[i] below pred[j]
            for (int i = 0; i < m; ++i) for (int j = 0; j < m; ++j) {
                float yi=rows[g.idx[i]].label, yj=rows[g.idx[j]].label;
                if (yi >= yj) continue;
                float w = (yj - yi);
                float s = 1.0f/(1.0f+std::exp(-(pred[i]-pred[j])));   // sigmoid(diff)
                eloss += beta*w*std::log(1.0f+std::exp(pred[i]-pred[j]));
                dpred[i] += beta*w*s; dpred[j] -= beta*w*s;
            }
            // POLICY cross-entropy (--gamma): train the argMIN of pred onto the teacher's best plan(s).
            // The prior only needs the teacher's pick in its top-M -- a listwise target, NOT accurate
            // win-turns -- so put a softmax POLICY over -pred (lower pred = higher prob) and pull mass
            // onto the min-label plan(s). Soft target over label-ties (uniform on all plans at the best
            // label). dL/dpred_i = gamma*(p_i - t_i). Off (gamma=0) => byte-identical to MSE+pairwise.
            if (gamma > 0.0f && m > 1) {
                float ymin = rows[g.idx[0]].label; int nties = 0;
                for (int i = 0; i < m; ++i) ymin = std::min(ymin, rows[g.idx[i]].label);
                for (int i = 0; i < m; ++i) if (rows[g.idx[i]].label == ymin) ++nties;
                float smax = -pred[0]; for (int i=0;i<m;++i) smax = std::max(smax, -pred[i]);
                float Z = 0.0f; std::vector<float> p(m);
                for (int i = 0; i < m; ++i) { p[i] = std::exp(-pred[i]-smax); Z += p[i]; }
                for (int i = 0; i < m; ++i) p[i] /= Z;
                for (int i = 0; i < m; ++i) {
                    float t = (rows[g.idx[i]].label == ymin) ? (1.0f/nties) : 0.0f;
                    if (t > 0.0f) eloss += -gamma * t * std::log(std::max(1e-9f, p[i]));
                    dpred[i] += gamma * (p[i] - t);
                }
            }
            zero_grads();
            for (int i = 0; i < m; ++i) backward(rows[g.idx[i]], cs[i], dpred[i]);
            ++step; adam(step); ++recount;
        }
        if (e % 5 == 0 || e == epochs-1)
            std::fprintf(stderr, "epoch %d  loss/dec=%.4f\n", e, eloss/std::max(1,recount));
    }

    // ---- Eval on held-out: RMSE, top-1 pick accuracy, pick-regret ----
    // per_turn: turn -> {sum model-regret, sum heur-regret, count, sum #candidates} to see WHERE the model
    // ranks badly (the user's question: is the gap concentrated on late/combo turns while setup is near-optimal?)
    auto eval = [&](std::vector<Group>& gs, const char* tag, bool byturn=false) {
        double se=0; long n=0; int top1=0, ndec=0, rec2=0, rec4=0;
        double regret=0, heur_regret=0, rand_regret=0;
        std::map<int, std::array<double,4>> per_turn;   // [modelreg, heurreg, count, cand]
        for (Group& g : gs) {
            int m=(int)g.idx.size();
            Vec pred(m); Cache c;
            int bpred=0, blabel=0, bheur=0; double sumlab=0;
            for (int i=0;i<m;++i){ pred[i]=forward(rows[g.idx[i]], c);
                float e2=pred[i]-rows[g.idx[i]].label; se+=e2*e2; ++n; sumlab+=rows[g.idx[i]].label;
                if (pred[i]<pred[bpred]) bpred=i;
                if (rows[g.idx[i]].label<rows[g.idx[blabel]].label) blabel=i;
                // heuristic pick = highest plan_baseline_eval (standardized keeps argmax)
                if (base_pidx>=0 && rows[g.idx[i]].pfeat[base_pidx] > rows[g.idx[bheur]].pfeat[base_pidx]) bheur=i; }
            if (bpred==blabel) ++top1;
            float bestlab = rows[g.idx[blabel]].label;
            // top-M RECALL: does the model's top-M (lowest pred) contain a teacher-best plan (label==bestlab)?
            // This -- not top1 or RMSE -- is what predicts the MTG_NC_TOPM prior's quality at M=2/4.
            { std::vector<int> ord(m); for (int i=0;i<m;++i) ord[i]=i;
              std::sort(ord.begin(), ord.end(), [&](int a,int b){ return pred[a]<pred[b]; });
              bool h2=false,h4=false;
              for (int r=0;r<m && r<4;++r){ if (rows[g.idx[ord[r]]].label==bestlab){ h4=true; if(r<2) h2=true; } }
              if (m<=2) h2=true; if (m<=4) h4=true;   // trivially recalled when <=M candidates
              if (h2) ++rec2; if (h4) ++rec4; }
            double mreg = rows[g.idx[bpred]].label - bestlab;
            double hreg = rows[g.idx[bheur]].label - bestlab;
            regret += mreg; heur_regret += hreg; rand_regret += sumlab/m - bestlab; ++ndec;
            if (byturn) { auto& a = per_turn[rows[g.idx[0]].turn]; a[0]+=mreg; a[1]+=hreg; a[2]+=1; a[3]+=m; }
        }
        std::fprintf(stderr,
            "[%s] RMSE=%.3f  top1=%.1f%%  recall@2=%.1f%%  recall@4=%.1f%%  pick-regret=%.4f  | heur=%.4f  random=%.4f  (n=%d)\n",
            tag, std::sqrt(se/std::max(1L,n)), 100.0*top1/std::max(1,ndec),
            100.0*rec2/std::max(1,ndec), 100.0*rec4/std::max(1,ndec),
            regret/std::max(1,ndec), heur_regret/std::max(1,ndec), rand_regret/std::max(1,ndec), ndec);
        if (byturn) {
            std::fprintf(stderr, "  per-turn regret (model | heur | #decisions | avg#cands):\n");
            for (auto& kv : per_turn) { double* a = kv.second.data();
                std::fprintf(stderr, "    turn %d:  model=%.4f  heur=%.4f  (n=%.0f, cands=%.1f)\n",
                    kv.first, a[0]/std::max(1.0,a[2]), a[1]/std::max(1.0,a[2]), a[2], a[3]/std::max(1.0,a[2])); }
        }
    };
    eval(train, "train");
    eval(test,  "test ", true);

    // ---- FAILURE ANALYSIS: why does the model mis-rank? (aliasing vs mis-weighting) ----
    // For every held-out decision the model gets meaningfully wrong (its pick's win-turn is >0.5 worse
    // than the teacher's best), measure the STANDARDIZED state-feature distance between the model's pick
    // and the teacher's best. SMALL distance => the two resulting boards look ~identical to the features
    // yet play out differently = ALIASING (the representation is the wall; add features). LARGE distance
    // => the features DO separate them and the model just weights them wrong (loss/capacity -- which we've
    // shown doesn't move). Also report which state features differ MOST on failures (the axes to enrich).
    {
        double dist_fail = 0, dist_ok = 0; int nfail = 0, nok = 0, aliased = 0;
        const double ALIAS = 1.0;   // L2 over standardized state feats; <1 ~= "indistinguishable"
        std::vector<double> featdiff(nstate, 0.0);
        for (Group& g : test) {
            int m = (int)g.idx.size(); if (m < 2) continue;
            Vec pred(m); Cache c;
            int bpred = 0, blabel = 0;
            for (int i = 0; i < m; ++i) { pred[i] = forward(rows[g.idx[i]], c);
                if (pred[i] < pred[bpred]) bpred = i;
                if (rows[g.idx[i]].label < rows[g.idx[blabel]].label) blabel = i; }
            const Vec& a = rows[g.idx[bpred]].sfeat;   // model's pick resulting board
            const Vec& b = rows[g.idx[blabel]].sfeat;  // teacher's best resulting board
            double d2 = 0; for (int j = 0; j < nstate; ++j) { double d = a[j]-b[j]; d2 += d*d; }
            double d = std::sqrt(d2);
            double mreg = rows[g.idx[bpred]].label - rows[g.idx[blabel]].label;
            if (mreg > 0.5) { dist_fail += d; ++nfail; if (d < ALIAS) ++aliased;
                for (int j = 0; j < nstate; ++j) featdiff[j] += std::fabs(a[j]-b[j]); }
            else { dist_ok += d; ++nok; }
        }
        std::fprintf(stderr,
            "[fail-analysis] failures(mreg>0.5)=%d  aliased(dist<%.1f)=%d (%.0f%%)  "
            "mean state-dist: fail=%.2f  ok=%.2f\n",
            nfail, ALIAS, aliased, 100.0*aliased/std::max(1,nfail),
            dist_fail/std::max(1,nfail), dist_ok/std::max(1,nok));
        std::vector<int> ord(nstate); for (int j = 0; j < nstate; ++j) ord[j] = j;
        std::sort(ord.begin(), ord.end(), [&](int x, int y){ return featdiff[x] > featdiff[y]; });
        std::fprintf(stderr, "  top state feats differing on failures (mean|diff| standardized):\n");
        for (int r = 0; r < 10 && r < nstate; ++r)
            std::fprintf(stderr, "    %-22s %.3f\n", state_names[ord[r]].c_str(),
                         featdiff[ord[r]]/std::max(1,nfail));
    }

    // ---- Serialize the model (dependency-free JSON) for in-engine inference ----
    if (!out_path.empty()) {
        std::ofstream o(out_path);
        auto arr = [&](const char* name, const Vec& v, bool last=false) {
            o << "  \"" << name << "\": [";
            for (size_t i = 0; i < v.size(); ++i) { o << v[i]; if (i+1<v.size()) o << ','; }
            o << (last ? "]\n" : "],\n");
        };
        auto iarr = [&](const char* name, const std::vector<int>& v) {
            o << "  \"" << name << "\": [";
            for (size_t i = 0; i < v.size(); ++i) { o << v[i]; if (i+1<v.size()) o << ','; }
            o << "],\n";
        };
        o << "{\n  \"type\": \"dyn_latent_rollout\",\n";
        o << "  \"T\": " << T << ", \"H\": " << H
          << ", \"nstate\": " << nstate << ", \"nplan\": " << nplan << ",\n";
        iarr("bucket", is_plan);            // per feature (enum order): 0=state, 1=plan
        arr("mean_s", mean_s); arr("sd_s", sd_s);
        arr("mean_p", mean_p); arr("sd_p", sd_p);
        arr("rep_W", rep.W); arr("rep_b", rep.b);
        if (T >= 1) { arr("plan_W", plan_enc.W); arr("plan_b", plan_enc.b);
                      arr("dyn_W", dyn_.W); arr("dyn_b", dyn_.b); }
        arr("val_W", val.W); arr("val_b", val.b, true);
        o << "}\n";
        std::fprintf(stderr, "wrote model -> %s (T=%d H=%d)\n", out_path.c_str(), T, H);
    }
    return 0;
}
