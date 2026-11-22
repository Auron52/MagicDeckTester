# mk2.py out.json sbase dbase 'NS_JSON {deck:n}' nd 'ARMS_JSON'  -- per-deck searched sizes; d0 nd per deck
import json,sys
S="/workspaces/MagicDeckTester/build/scratch/claude_rock_cpu"
rd=json.load(open(S+"/rock_decks.json"))
out,sb,db,ns,nd,arms=sys.argv[1],int(sys.argv[2]),int(sys.argv[3]),json.loads(sys.argv[4]),int(sys.argv[5]),json.loads(sys.argv[6])
jobs=[]
for k in sorted(ns):
    deck,prof,_=rd[k]
    for mode,base,n in (("s",sb,ns[k]),("d0",db,nd)):
        CH=25 if mode=="s" else 100
        for off in range(0,n,CH):
            for arm,fl in arms.items():
                j={"name":f"{arm}__{k}__{mode}__o{off}","deck":deck,"profile":prof,"games":min(CH,n-off),
                   "seed":base+off,"game_index":off,"flags":fl}
                if mode=="d0": j.update({"depth":0,"budget_ms":0,"ignore_play_profile":True})
                jobs.append(j)
json.dump({"jobs":jobs},open(out,"w"),indent=1)
print(len(jobs),"jobs",sum(j['games'] for j in jobs),"games")
