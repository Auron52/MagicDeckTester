// Learned WORLD-MODEL trainer -- the categorically-different lever the ceiling analysis left open.
// PRIMARY task: rank per-candidate rsvalue rows (value head on the candidate distribution -> good ranking,
// matches the ~0.17 baseline). AUXILIARY task (shared encoder): learn DYNAMICS from teacher executed-
// trajectory rows -- predict the next state (reconstruction) and the next value (bootstrap). Hypothesis:
// a value regularized by dynamics, and a BOOTSTRAPPED serve value V(s)=0.5 val(h)+0.5(1+val(dyn(h))),
// denoises the K=8 label-noise floor (0.12) by averaging consistent estimates across the trajectory --
// the one mechanism that can beat a per-state static value. A/B via --recon-w/--boot-w = 0 (value only)
// vs > 0 (world-model). Chunkable: the expensive TRAJECTORY data is dumped/cached separately (MTG_DUMP_TRAJ).
//   g++ -O2 -std=c++17 -o /tmp/worldmodel tools/worldmodel/main.cpp
//   worldmodel <rsvalue.rows> --traj <traj.rows> --H 96 --epochs 80 --recon-w 1 --boot-w 1 --out wm.json
#include "../dyntrain/nn.h"
#include <iostream>
#include <sstream>
#include <map>
#include <algorithm>
#include <cstring>

using dyn::Vec; using dyn::Rng; using dyn::Linear;
static std::vector<std::string> ws(const std::string& s){ std::vector<std::string> o; std::istringstream is(s); std::string t; while(is>>t)o.push_back(t); return o; }
static Vec tanhv(const Vec&x){ Vec y(x.size()); for(size_t i=0;i<x.size();++i)y[i]=std::tanh(x[i]); return y; }

int main(int argc,char**argv){
    if(argc<2){ std::fprintf(stderr,"usage: worldmodel <rsvalue.rows> --traj <traj.rows> [opts]\n"); return 2; }
    std::string rank_path=argv[1], traj_path, out_path;
    int H=96,epochs=80,hmod=5; float lr=2e-3f, recon_w=1.f, boot_w=1.f, val_w=1.f;
    for(int i=2;i<argc;++i){ std::string a=argv[i]; auto nx=[&]{return std::string(argv[++i]);};
        if(a=="--traj")traj_path=nx(); else if(a=="--H")H=std::stoi(nx()); else if(a=="--epochs")epochs=std::stoi(nx());
        else if(a=="--lr")lr=std::stof(nx()); else if(a=="--recon-w")recon_w=std::stof(nx());
        else if(a=="--boot-w")boot_w=std::stof(nx()); else if(a=="--val-w")val_w=std::stof(nx());
        else if(a=="--out")out_path=nx(); else if(a=="--holdout-seed-mod")hmod=std::stoi(nx()); }

    // ---- rsvalue rows: "# label <feat...> seed turn" (the STATE feats are the non-plan_ columns) ----
    struct RRow{ float lab; Vec feat; long long seed; int turn; };
    std::vector<RRow> rr; int F=0;
    { std::ifstream f(rank_path); if(!f){std::fprintf(stderr,"open %s\n",rank_path.c_str());return 1;}
      std::string ln; std::getline(f,ln); auto h=ws(ln[0]=='#'?ln.substr(1):ln); int nc=(int)h.size();
      std::vector<int> keep; for(int j=1;j<nc-2;++j) if(h[j].rfind("plan_",0)!=0) keep.push_back(j);  // STATE cols only
      F=(int)keep.size();
      while(std::getline(f,ln)){ if(ln.empty()||ln[0]=='#')continue; auto t=ws(ln); if((int)t.size()!=nc)continue;
        RRow r; r.lab=std::stof(t[0]); r.seed=std::stoll(t[nc-2]); r.turn=std::stoi(t[nc-1]);
        for(int j:keep) r.feat.push_back(std::stof(t[j])); rr.push_back(std::move(r)); } }
    std::fprintf(stderr,"rsvalue: %zu rows, F=%d\n",rr.size(),F);

    // ---- trajectory rows (aux): sequences of the SAME F state feats + turns-to-go ----
    struct Step{ Vec feat; float ttg; }; struct Game{ std::vector<Step> steps; long long gid; };
    std::vector<Game> games;
    if(!traj_path.empty()){ std::ifstream f(traj_path); std::string ln; std::getline(f,ln);
      auto h=ws(ln[0]=='#'?ln.substr(1):ln); int nc=(int)h.size();
      std::vector<int> keep; for(int j=1;j<nc-3;++j) if(h[j].rfind("plan_",0)!=0) keep.push_back(j);
      std::map<long long,Game> gm;
      while(std::getline(f,ln)){ if(ln.empty()||ln[0]=='#')continue; auto t=ws(ln); if((int)t.size()!=nc)continue;
        Step s; s.ttg=std::stof(t[0]); for(int j:keep)s.feat.push_back(std::stof(t[j]));
        long long gid=std::stoll(t[nc-3]); gm[gid].gid=gid; gm[gid].steps.push_back(std::move(s)); }
      for(auto&kv:gm) if(kv.second.steps.size()>=2) games.push_back(std::move(kv.second));
      std::fprintf(stderr,"traj: %zu games\n",games.size());
      if((int)games[0].steps[0].feat.size()!=F){ std::fprintf(stderr,"FEATURE MISMATCH traj%d vs rank%d\n",(int)games[0].steps[0].feat.size(),F); return 1; } }

    // ---- standardize on rsvalue TRAIN rows; apply to both ----
    Vec mean(F,0),sd(F,0); long n=0;
    for(auto&r:rr){ if(r.seed%hmod==0)continue; for(int j=0;j<F;++j)mean[j]+=r.feat[j]; ++n; }
    for(int j=0;j<F;++j)mean[j]/=std::max(1L,n);
    for(auto&r:rr){ if(r.seed%hmod==0)continue; for(int j=0;j<F;++j){float d=r.feat[j]-mean[j]; sd[j]+=d*d;} }
    for(int j=0;j<F;++j)sd[j]=std::sqrt(sd[j]/std::max(1L,n))+1e-6f;
    auto norm=[&](Vec&v){ for(int j=0;j<F;++j)v[j]=(v[j]-mean[j])/sd[j]; };
    for(auto&r:rr)norm(r.feat); for(auto&g:games)for(auto&s:g.steps)norm(s.feat);

    // group rsvalue by decision
    std::map<std::pair<long long,int>,std::vector<int>> dec;
    for(int i=0;i<(int)rr.size();++i)dec[{rr[i].seed,rr[i].turn}].push_back(i);
    std::vector<std::vector<int>> train_dec, test_dec;
    for(auto&kv:dec){ if(kv.second.size()<2)continue; (kv.first.first%hmod==0?test_dec:train_dec).push_back(kv.second); }

    // ---- model ----
    Rng rng(1234);
    Linear rep(F,H,rng), dynL(H,H,rng), dec_(H,F,rng), val(H,1,rng);
    int step=0; auto adam=[&]{ ++step; float b1=.9f,b2=.999f,e=1e-8f,wd=1e-5f;
        rep.adam_step(lr,b1,b2,e,wd,step);dynL.adam_step(lr,b1,b2,e,wd,step);dec_.adam_step(lr,b1,b2,e,wd,step);val.adam_step(lr,b1,b2,e,wd,step); };
    auto enc=[&](const Vec&x,Vec&pre){ rep.forward(x,pre); return tanhv(pre); };

    std::mt19937 sh(777);
    std::vector<int> gorder; for(int i=0;i<(int)games.size();++i) if(games[i].gid%hmod!=0)gorder.push_back(i);

    for(int e=0;e<epochs;++e){
        // ---- PRIMARY: value + pairwise rank on rsvalue decisions ----
        std::shuffle(train_dec.begin(),train_dec.end(),sh);
        double Lv=0; long nv=0;
        for(auto&idx:train_dec){ int m=(int)idx.size();
            std::vector<Vec> pre(m),h(m); Vec pred(m),dh_sum;
            std::vector<Vec> dh(m,Vec(H,0.f));
            rep.zero_grad();dynL.zero_grad();dec_.zero_grad();val.zero_grad();
            for(int q=0;q<m;++q){ h[q]=enc(rr[idx[q]].feat,pre[q]); Vec o; val.forward(h[q],o); pred[q]=o[0]; }
            // MSE to ttg label + pairwise rank
            for(int q=0;q<m;++q){ float ev=pred[q]-rr[idx[q]].lab; Lv+=ev*ev; ++nv;
                Vec dov={val_w*(2.f*ev)},dhv; val.backward(h[q],dov,dhv); for(int k=0;k<H;++k)dh[q][k]+=dhv[k]; }
            for(int q=0;q<m;++q)for(int p=0;p<m;++p){ float yi=rr[idx[q]].lab,yj=rr[idx[p]].lab; if(yi>=yj)continue;
                float w=yj-yi, s=1.f/(1.f+std::exp(-(pred[q]-pred[p])));
                Vec dq={w*s},dp={-w*s},dhq,dhp; val.backward(h[q],dq,dhq); val.backward(h[p],dp,dhp);
                for(int k=0;k<H;++k){dh[q][k]+=dhq[k];dh[p][k]+=dhp[k];} }
            for(int q=0;q<m;++q){ Vec dpre(H); for(int k=0;k<H;++k)dpre[k]=dh[q][k]*(1.f-h[q][k]*h[q][k]); Vec dx; rep.backward(rr[idx[q]].feat,dpre,dx); }
            adam();
        }
        // ---- AUX: dynamics/recon/bootstrap on trajectories (shared encoder) ----
        double Lr=0,Lb=0; long na=0;
        if(recon_w>0||boot_w>0){ std::shuffle(gorder.begin(),gorder.end(),sh);
          for(int gi:gorder){ auto&g=games[gi]; int Lg=(int)g.steps.size();
            std::vector<Vec> pre(Lg),h(Lg); for(int t=0;t<Lg;++t)h[t]=enc(g.steps[t].feat,pre[t]);
            rep.zero_grad();dynL.zero_grad();dec_.zero_grad();val.zero_grad();
            std::vector<Vec> dh(Lg,Vec(H,0.f));
            for(int t=0;t+1<Lg;++t){ Vec dpre; dynL.forward(h[t],dpre); Vec hp=tanhv(dpre);
                Vec r; dec_.forward(hp,r); Vec dr(F,0.f);
                for(int j=0;j<F;++j){ float er=r[j]-g.steps[t+1].feat[j]; Lr+=er*er; dr[j]=recon_w*2.f*er; } ++na;
                Vec dhp; dec_.backward(hp,dr,dhp);
                Vec ob; val.forward(hp,ob); float eb=ob[0]-g.steps[t+1].ttg; Lb+=eb*eb;
                Vec dob={boot_w*2.f*eb},dhb; val.backward(hp,dob,dhb); for(int k=0;k<H;++k)dhp[k]+=dhb[k];
                for(int k=0;k<H;++k)dhp[k]*=(1.f-hp[k]*hp[k]);
                Vec dht; dynL.backward(h[t],dhp,dht); for(int k=0;k<H;++k)dh[t][k]+=dht[k]; }
            for(int t=0;t<Lg;++t){ Vec dpre(H); for(int k=0;k<H;++k)dpre[k]=dh[t][k]*(1.f-h[t][k]*h[t][k]); Vec dx; rep.backward(g.steps[t].feat,dpre,dx); }
            adam();
          } }
        if(e%10==0||e==epochs-1) std::fprintf(stderr,"epoch %d  val=%.4f recon=%.4f boot=%.4f\n",e,Lv/std::max(1L,nv),Lr/std::max(1L,na),Lb/std::max(1L,na));
    }

    // ---- eval ranking on held-out rsvalue: static vs bootstrapped value ----
    auto vstat=[&](const Vec&h){ Vec o; val.forward(h,o); return o[0]; };
    auto vboot=[&](const Vec&h){ Vec o; val.forward(h,o); Vec dp; dynL.forward(h,dp); Vec hp=tanhv(dp); Vec o2; val.forward(hp,o2); return 0.5f*o[0]+0.5f*(1.f+o2[0]); };
    auto rankeval=[&](bool boot){ int r2=0,r4=0,nd=0; double reg=0;
        for(auto&idx:test_dec){ int m=(int)idx.size(); ++nd; std::vector<std::pair<float,int>> pv; int bl=0;
            for(int q=0;q<m;++q){ Vec pre; Vec h=enc(rr[idx[q]].feat,pre); float v=boot?vboot(h):vstat(h); pv.push_back({v,q}); if(rr[idx[q]].lab<rr[idx[bl]].lab)bl=q; }
            float bestlab=rr[idx[bl]].lab; std::sort(pv.begin(),pv.end()); bool h2=false,h4=false;
            for(int r=0;r<m&&r<4;++r){ if(rr[idx[pv[r].second]].lab==bestlab){h4=true; if(r<2)h2=true;} } if(m<=2)h2=true; if(m<=4)h4=true;
            if(h2)++r2; if(h4)++r4; reg+=rr[idx[pv[0].second]].lab-bestlab; }
        std::fprintf(stderr,"[rank %s] recall@2=%.1f%% recall@4=%.1f%% pick-regret=%.4f (n=%d)\n",boot?"BOOT":"stat",100.0*r2/std::max(1,nd),100.0*r4/std::max(1,nd),reg/std::max(1,nd),nd); };
    rankeval(false); rankeval(true);
    return 0;
}
