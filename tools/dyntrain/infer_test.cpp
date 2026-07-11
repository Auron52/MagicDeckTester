// Parity test: does DynModel.h (engine inference) reproduce the trainer's held-out pick accuracy?
// Reads the SAME eval rows (raw int features) and scores with PredictWinTurn. If top-1 / pick-regret
// match the trainer's report, inference is correct and the play collapse is covariate shift; if not,
// there is a train/serve bug in DynModel.h.
//   infer_test <model.json> <rows> [--holdout-seed-mod 5]
#include "../../src/ai/DynModel.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <cmath>

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: infer_test <model.json> <rows>\n"); return 2; }
    int hmod = 5;
    for (int i = 3; i < argc; ++i) if (!std::strcmp(argv[i], "--holdout-seed-mod") && i+1<argc) hmod = std::atoi(argv[++i]);
    DynModel dm;
    if (!dm.Load(argv[1])) { std::fprintf(stderr, "load failed\n"); return 1; }
    std::fprintf(stderr, "loaded T=%d H=%d nstate=%d nplan=%d bucket=%zu\n", dm.T, dm.H, dm.nstate, dm.nplan, dm.bucket.size());

    std::ifstream f(argv[2]); std::string line; std::getline(f, line); // header
    struct R { float label; std::vector<int> feat; long long seed; int turn; };
    std::vector<R> rows;
    while (std::getline(f, line)) {
        if (line.empty() || line[0]=='#') continue;
        std::istringstream is(line); std::vector<std::string> t; std::string w;
        while (is >> w) t.push_back(w);
        R r; r.label = std::stof(t[0]); r.seed = std::stoll(t[t.size()-2]); r.turn = std::stoi(t.back());
        for (size_t j = 1; j+2 < t.size(); ++j) r.feat.push_back((int)std::lround(std::stof(t[j])));
        rows.push_back(std::move(r));
    }
    std::map<std::pair<long long,int>, std::vector<int>> gm;
    for (int i = 0; i < (int)rows.size(); ++i) gm[{rows[i].seed, rows[i].turn}].push_back(i);

    int ndec=0, top1=0; double regret=0;
    for (auto& kv : gm) {
        auto& idx = kv.second; if (idx.size() < 2) continue;
        if (kv.first.first % hmod != 0) continue;   // held-out only
        int bpred=idx[0], blabel=idx[0]; float bp = dm.PredictWinTurn(rows[idx[0]].feat);
        for (int i : idx) {
            float p = dm.PredictWinTurn(rows[i].feat);
            if (p < bp) { bp = p; bpred = i; }
            if (rows[i].label < rows[blabel].label) blabel = i;
        }
        if (bpred==blabel) ++top1;
        regret += rows[bpred].label - rows[blabel].label; ++ndec;
    }
    std::fprintf(stderr, "[infer] held-out decisions=%d  top1=%.1f%%  pick-regret=%.4f\n",
                 ndec, 100.0*top1/std::max(1,ndec), regret/std::max(1,ndec));
    return 0;
}
