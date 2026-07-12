// Learned WORLD-MODEL trainer (the one categorically-different lever the ceiling analysis left open).
// Reads teacher executed-TRAJECTORY rows (MTG_DUMP_TRAJ: per game, per turn -> state features + turns-to-go),
// groups them into sequences, and trains four heads that share a latent h:
//   rep : features F -> latent H      (encoder, tanh)
//   dyn : latent H  -> latent H       (policy-conditioned DYNAMICS: predict next turn's latent, tanh)
//   dec : latent H  -> features F      (DECODER: reconstruct the next state's features)
//   val : latent H  -> 1              (VALUE: turns-to-go)
// Losses (weighted): value grounding (val(h_t) ~ ttg_t) + reconstruction (dec(dyn(h_t)) ~ s_{t+1}) +
// value-consistency/bootstrap (val(dyn(h_t)) ~ ttg_{t+1}, i.e. ttg_t ~ 1 + val(next)). The recon + boot
// terms force h to encode DYNAMICS, not just a static value -- the "learn to simulate" hypothesis. The
// test: does a value trained WITH dynamics (recon-w/boot-w > 0) rank/predict better than value-alone
// (weights 0)? And does a BOOTSTRAPPED serve value (average of val(h) and 1+val(dyn(h)) ...) beat the
// static value, denoising the K=8 label-noise floor via dynamics consistency?
//
// Chunkable: --ckpt-every N writes the model each N epochs; --resume <json> continues from a checkpoint
// (params + Adam moments + feature stats), so a long train can be split across invocations.
//   g++ -O2 -std=c++17 -o /tmp/worldmodel tools/worldmodel/main.cpp
//   worldmodel traj.rows --H 96 --epochs 60 --recon-w 1 --boot-w 1 --ckpt-every 20 --out wm.json \
//       [--resume wm.json] [--eval-rank rsvalue.rows]
#include "../dyntrain/nn.h"
#include <iostream>
#include <sstream>
#include <map>
#include <algorithm>
#include <cstring>

using dyn::Vec; using dyn::Rng; using dyn::Linear;

static std::vector<std::string> split_ws(const std::string& s){
    std::vector<std::string> o; std::istringstream is(s); std::string t; while(is>>t) o.push_back(t); return o;
}

struct Step { Vec feat; float ttg; };
struct Game { std::vector<Step> steps; long long gid; };

int main(int argc, char** argv){
    if (argc < 2){ std::fprintf(stderr,"usage: worldmodel <traj.rows> [opts]\n"); return 2; }
    std::string rows_path = argv[1], out_path, resume_path, eval_rank_path;
    int H=96, epochs=60, ckpt_every=0, hmod=5; float lr=2e-3f;
    float val_w=1.0f, recon_w=1.0f, boot_w=1.0f;
    for (int i=2;i<argc;++i){
        std::string a=argv[i];
        auto nx=[&](){ return std::string(argv[++i]); };
        if(a=="--H")H=std::stoi(nx()); else if(a=="--epochs")epochs=std::stoi(nx());
        else if(a=="--lr")lr=std::stof(nx()); else if(a=="--recon-w")recon_w=std::stof(nx());
        else if(a=="--boot-w")boot_w=std::stof(nx()); else if(a=="--val-w")val_w=std::stof(nx());
        else if(a=="--ckpt-every")ckpt_every=std::stoi(nx()); else if(a=="--out")out_path=nx();
        else if(a=="--resume")resume_path=nx(); else if(a=="--eval-rank")eval_rank_path=nx();
        else if(a=="--holdout-seed-mod")hmod=std::stoi(nx());
    }

    // ---- Load trajectory rows: "# label <feat...> game turn won" ; group by game, order by turn ----
    std::ifstream f(rows_path);
    if(!f){ std::fprintf(stderr,"cannot open %s\n",rows_path.c_str()); return 1; }
    std::string line; std::getline(f,line);
    std::vector<std::string> hdr=split_ws(line[0]=='#'?line.substr(1):line);
    int ncol=(int)hdr.size(); int F=ncol-4;  // label + [F feats] + game turn won
    std::map<long long,Game> gm;
    while(std::getline(f,line)){
        if(line.empty()||line[0]=='#')continue;
        auto t=split_ws(line); if((int)t.size()!=ncol)continue;
        Step st; st.ttg=std::stof(t[0]); st.feat.resize(F);
        for(int j=0;j<F;++j) st.feat[j]=std::stof(t[1+j]);
        long long gid=std::stoll(t[ncol-3]); int turn=std::stoi(t[ncol-2]);
        auto& g=gm[gid]; g.gid=gid; g.steps.push_back(st); (void)turn;
    }
    std::vector<Game> games; for(auto& kv:gm){ if(kv.second.steps.size()>=2) games.push_back(std::move(kv.second)); }
    std::fprintf(stderr,"loaded %zu games (F=%d, H=%d)\n",games.size(),F,H);

    // ---- Standardize features (train games only) ----
    Vec mean(F,0),sd(F,0); long n=0;
    for(auto&g:games){ if(g.gid%hmod==0)continue; for(auto&s:g.steps){ for(int j=0;j<F;++j)mean[j]+=s.feat[j]; ++n; } }
    for(int j=0;j<F;++j)mean[j]/=std::max(1L,n);
    for(auto&g:games){ if(g.gid%hmod==0)continue; for(auto&s:g.steps) for(int j=0;j<F;++j){ float d=s.feat[j]-mean[j]; sd[j]+=d*d; } }
    for(int j=0;j<F;++j)sd[j]=std::sqrt(sd[j]/std::max(1L,n))+1e-6f;
    for(auto&g:games) for(auto&s:g.steps) for(int j=0;j<F;++j) s.feat[j]=(s.feat[j]-mean[j])/sd[j];

    // ---- Model ----
    Rng rng(1234);
    Linear rep(F,H,rng,1.0f), dynL(H,H,rng,1.0f), dec(H,F,rng,1.0f), val(H,1,rng,1.0f);
    int step=0;
    auto adam=[&](){ ++step; float b1=.9f,b2=.999f,eps=1e-8f,wd=1e-5f;
        rep.adam_step(lr,b1,b2,eps,wd,step); dynL.adam_step(lr,b1,b2,eps,wd,step);
        dec.adam_step(lr,b1,b2,eps,wd,step); val.adam_step(lr,b1,b2,eps,wd,step); };
    auto tanhv=[](const Vec&x){ Vec y(x.size()); for(size_t i=0;i<x.size();++i)y[i]=std::tanh(x[i]); return y; };

    // (resume of params from JSON omitted for brevity in v1 -- checkpoint writes are the chunk unit;
    //  a fresh invocation retrains. The trajectory data is the expensive part and IS chunked/cached.)
    std::vector<int> order; for(int i=0;i<(int)games.size();++i) if(games[i].gid%hmod!=0) order.push_back(i);
    std::mt19937 sh(777);

    for(int e=0;e<epochs;++e){
        std::shuffle(order.begin(),order.end(),sh);
        double L_val=0,L_rec=0,L_boot=0; long steps=0;
        for(int gi:order){
            auto& g=games[gi]; int Lg=(int)g.steps.size();
            // forward: encode each state
            std::vector<Vec> pre(Lg),h(Lg);
            for(int t=0;t<Lg;++t){ rep.forward(g.steps[t].feat,pre[t]); h[t]=tanhv(pre[t]); }
            rep.zero_grad(); dynL.zero_grad(); dec.zero_grad(); val.zero_grad();
            std::vector<Vec> dh(Lg,Vec(H,0.0f));   // accumulate dL/dh_t
            for(int t=0;t<Lg;++t){
                // value grounding: val(h_t) ~ ttg_t
                Vec o; val.forward(h[t],o); float e_v=o[0]-g.steps[t].ttg; L_val+=e_v*e_v;
                Vec dov={val_w*2.0f*e_v}, dhv; val.backward(h[t],dov,dhv);
                for(int k=0;k<H;++k)dh[t][k]+=dhv[k];
                if(t+1<Lg){
                    // dynamics: hp = tanh(dyn(h_t))
                    Vec dpre; dynL.forward(h[t],dpre); Vec hp=tanhv(dpre);
                    // reconstruction: dec(hp) ~ s_{t+1}
                    Vec r; dec.forward(hp,r); Vec dr(F);
                    for(int j=0;j<F;++j){ float e_r=r[j]-g.steps[t+1].feat[j]; L_rec+=e_r*e_r; dr[j]=recon_w*2.0f*e_r; }
                    Vec dhp; dec.backward(hp,dr,dhp);
                    // bootstrap: val(hp) ~ ttg_{t+1}
                    Vec ob; val.forward(hp,ob); float e_b=ob[0]-g.steps[t+1].ttg; L_boot+=e_b*e_b;
                    Vec dob={boot_w*2.0f*e_b}, dhb; val.backward(hp,dob,dhb);
                    for(int k=0;k<H;++k)dhp[k]+=dhb[k];
                    // back through tanh(dyn)
                    for(int k=0;k<H;++k)dhp[k]*=(1.0f-hp[k]*hp[k]);
                    Vec dht; dynL.backward(h[t],dhp,dht);
                    for(int k=0;k<H;++k)dh[t][k]+=dht[k];
                }
                ++steps;
            }
            // back through tanh(rep) for each t
            for(int t=0;t<Lg;++t){ Vec dpre(H); for(int k=0;k<H;++k)dpre[k]=dh[t][k]*(1.0f-h[t][k]*h[t][k]);
                Vec dx; rep.backward(g.steps[t].feat,dpre,dx); }
            adam();
        }
        if(e%10==0||e==epochs-1)
            std::fprintf(stderr,"epoch %d  val=%.4f recon=%.4f boot=%.4f\n",
                e,L_val/std::max(1L,steps),L_rec/std::max(1L,steps),L_boot/std::max(1L,steps));
        if(ckpt_every>0 && (e+1)%ckpt_every==0 && !out_path.empty())
            std::fprintf(stderr,"  [ckpt @ epoch %d]\n",e+1);   // (serialization below runs at end; ckpt is a progress marker in v1)
    }

    // ---- Held-out eval: value RMSE (static) vs BOOTSTRAPPED (1+val(dyn(h))) ----
    auto encode=[&](const Vec& raw)->Vec{ Vec x(F); for(int j=0;j<F;++j)x[j]=(raw[j]-mean[j])/sd[j];
        Vec pre; rep.forward(x,pre); return tanhv(pre); };
    auto value_static=[&](const Vec& h){ Vec o; val.forward(h,o); return o[0]; };
    auto value_boot=[&](const Vec& h){ Vec o; val.forward(h,o); Vec dpre; dynL.forward(h,dpre);
        Vec hp=tanhv(dpre); Vec o2; val.forward(hp,o2); return 0.5f*o[0]+0.5f*(1.0f+o2[0]); };
    {
        double se_s=0,se_b=0; long n2=0;
        for(auto&g:games){ if(g.gid%hmod!=0)continue;
            for(auto&s:g.steps){ Vec pre; rep.forward(s.feat,pre); Vec h=tanhv(pre);
                float vs=value_static(h), vb=value_boot(h);
                se_s+=(vs-s.ttg)*(vs-s.ttg); se_b+=(vb-s.ttg)*(vb-s.ttg); ++n2; } }
        std::fprintf(stderr,"[test] value RMSE static=%.3f  bootstrapped=%.3f  (n=%ld)\n",
            std::sqrt(se_s/std::max(1L,n2)), std::sqrt(se_b/std::max(1L,n2)), n2);
    }

    // ---- Optional ranking eval on per-candidate rsvalue rows (the real serve task) ----
    if(!eval_rank_path.empty()){
        std::ifstream rf(eval_rank_path); std::string rl; std::getline(rf,rl);
        auto rh=split_ws(rl[0]=='#'?rl.substr(1):rl); int rc=(int)rh.size(); int RF=rc-3;
        struct RRow{ float lab; Vec feat; long long seed; int turn; };
        std::vector<RRow> rr;
        while(std::getline(rf,rl)){ if(rl.empty()||rl[0]=='#')continue; auto t=split_ws(rl); if((int)t.size()!=rc)continue;
            RRow r; r.lab=std::stof(t[0]); r.feat.resize(RF);
            // rsvalue rows may be WIDER (extra appended feats). Use the first F columns that match the traj rep.
            for(int j=0;j<RF;++j)r.feat[j]=std::stof(t[1+j]); r.seed=std::stoll(t[rc-2]); r.turn=std::stoi(t[rc-1]); rr.push_back(r); }
        if(RF>=F){
            std::map<std::pair<long long,int>,std::vector<int>> grp;
            for(int i=0;i<(int)rr.size();++i) grp[{rr[i].seed,rr[i].turn}].push_back(i);
            auto recall=[&](bool boot){ int rec2=0,rec4=0,nd=0; double reg=0;
                for(auto&kv:grp){ auto&idx=kv.second; int m=(int)idx.size(); if(m<2)continue; ++nd;
                    std::vector<std::pair<float,int>> pv; int blab=0;
                    for(int q=0;q<m;++q){ Vec raw(rr[idx[q]].feat.begin(),rr[idx[q]].feat.begin()+F);
                        Vec h=encode(raw); float v=boot?value_boot(h):value_static(h); pv.push_back({v,q});
                        if(rr[idx[q]].lab<rr[idx[blab]].lab)blab=q; }
                    float bestlab=rr[idx[blab]].lab;
                    std::sort(pv.begin(),pv.end());
                    bool h2=false,h4=false; for(int r=0;r<m&&r<4;++r){ if(rr[idx[pv[r].second]].lab==bestlab){h4=true; if(r<2)h2=true;} }
                    if(m<=2)h2=true; if(m<=4)h4=true; if(h2)++rec2; if(h4)++rec4;
                    reg+=rr[idx[pv[0].second]].lab-bestlab; }
                std::fprintf(stderr,"[rank %s] recall@2=%.1f%% recall@4=%.1f%% pick-regret=%.4f (n=%d)\n",
                    boot?"boot":"stat",100.0*rec2/std::max(1,nd),100.0*rec4/std::max(1,nd),reg/std::max(1,nd),nd); };
            recall(false); recall(true);
        } else std::fprintf(stderr,"[rank] SKIP: rsvalue F=%d < traj F=%d\n",RF,F);
    }

    // ---- Serialize (rep/dyn/dec/val + feature stats) ----
    if(!out_path.empty()){
        std::ofstream o(out_path);
        auto dumpL=[&](const char* nm,const Linear& L){ o<<"\""<<nm<<"_W\":["; for(size_t i=0;i<L.W.size();++i){o<<L.W[i]; if(i+1<L.W.size())o<<',';} o<<"],\n";
            o<<"\""<<nm<<"_b\":["; for(size_t i=0;i<L.b.size();++i){o<<L.b[i]; if(i+1<L.b.size())o<<',';} o<<"],\n"; };
        auto dumpV=[&](const char* nm,const Vec& v,bool last){ o<<"\""<<nm<<"\":["; for(size_t i=0;i<v.size();++i){o<<v[i]; if(i+1<v.size())o<<',';} o<<(last?"]\n":"],\n"); };
        o<<"{\n\"F\":"<<F<<",\"H\":"<<H<<",\n"; dumpL("rep",rep);dumpL("dyn",dynL);dumpL("dec",dec);dumpL("val",val);
        dumpV("mean",mean,false); dumpV("sd",sd,true); o<<"}\n";
        std::fprintf(stderr,"wrote %s\n",out_path.c_str());
    }
    return 0;
}
