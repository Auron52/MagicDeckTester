# usage: mk.py out.json searched_base d0_base n_searched n_niv_searched n_d0 'ARMS_JSON' [decks,comma]
# ARMS_JSON: {"arm": {flags...}, ...}
import json,sys
S="/workspaces/MagicDeckTester/build/scratch/claude_rock_cpu"
rd=json.load(open(S+"/rock_decks.json")); rd.pop('snow',None)
out=sys.argv[1]; sb=int(sys.argv[2]); db=int(sys.argv[3]); ns=int(sys.argv[4]); nn=int(sys.argv[5]); nd=int(sys.argv[6])
arms=json.loads(sys.argv[7]); only=set(sys.argv[8].split(',')) if len(sys.argv)>8 and sys.argv[8] else None
jobs=[]
for k,(deck,prof,_) in sorted(rd.items()):
    if only and k not in only: continue
    for mode,base,n in (("s",sb,nn if k=='niv' else ns),("d0",db,nd)):
        CH=25 if mode=="s" else 100
        for off in range(0,n,CH):
            for arm,fl in arms.items():
                j={"name":f"{arm}__{k}__{mode}__o{off}","deck":deck,"profile":prof,"games":min(CH,n-off),
                   "seed":base+off,"game_index":off,"flags":fl}
                if mode=="d0": j.update({"depth":0,"budget_ms":0,"ignore_play_profile":True})
                jobs.append(j)
json.dump({"jobs":jobs},open(out,"w"),indent=1)
print(len(jobs),"jobs",sum(j['games'] for j in jobs),"games")
