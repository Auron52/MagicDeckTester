# mkd8.py <cmp.txt> <out.json> <sbase> <d0base> <armsjson>  : d8 b0 replay of every 'slower' game, every arm
import json,sys
rd=json.load(open('/workspaces/MagicDeckTester/build/scratch/claude_rock_cpu/rock_decks.json'))
cmp,out,sb,db,arms=sys.argv[1],sys.argv[2],int(sys.argv[3]),int(sys.argv[4]),json.loads(sys.argv[5])
jobs=[]
for l in open(cmp):
    f=l.split()
    if not f or f[0]!='slower': continue
    deck,mode,gi=f[1],f[2],int(f[3]); d,p,_=rd[deck]; base=sb if mode=='s' else db
    for a,fl in arms.items():
        jobs.append({"name":f"{a}__{deck}__{mode}__g{gi}","deck":d,"profile":p,"games":1,"seed":base+gi,"game_index":gi,
                     "depth":8,"budget_ms":0,"ignore_play_profile":True,"flags":fl})
json.dump({"jobs":jobs},open(out,'w'),indent=1); print(len(jobs),'jobs')
