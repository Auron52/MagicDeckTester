import sys, glob, statistics
# aggregate_ensemble.py <outdir>  -- per-game win-prob & mean win-turn over the salt ensemble
outdir=sys.argv[1]
jobs={}  # jobname -> {gi -> [win_turn per salt]}
for wf in sorted(glob.glob(f'{outdir}/salt*/*.wins')):
    job=wf.split('/')[-1].replace('.wins','')
    for ln in open(wf):
        p=ln.split()
        if len(p)>=2 and p[0].lstrip('-').isdigit():
            gi=int(p[0]); wt=int(p[1])
            jobs.setdefault(job,{}).setdefault(gi,[]).append(wt)
for job,games in jobs.items():
    K=max(len(v) for v in games.values())
    winprob=[]; meanwt=[]; swingy=0
    for gi,wts in games.items():
        wins=[w for w in wts if w>0]
        wp=len(wins)/len(wts)
        winprob.append(wp)
        if wins: meanwt.append(statistics.mean(wins))
        # a "swingy" game: outcome depends on the shuffle (won in some salts, lost in others)
        if 0<len(wins)<len(wts): swingy+=1
    n=len(games)
    print(f'[{job}] games={n} salts={K}')
    print(f'  ensemble win%%  = {100*statistics.mean(winprob):.2f}%%  (per-game win-prob averaged)')
    print(f'  mean win turn  = {statistics.mean(meanwt):.3f}')
    print(f'  SHUFFLE-SWINGY games (won some salts / lost others) = {swingy}/{n} ({100*swingy/n:.1f}%%) '
          f'-- these are where the shuffle DECISION actually matters')
