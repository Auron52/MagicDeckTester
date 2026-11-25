#!/usr/bin/env python3
# heath-morph big A/B: origin 05e2e0030 (base) vs wip/land-heath 886231bd9 (new). Paired on seed=BASE+so+off, game_index=off.
import json
BASE=14300000; CHUNK=100
ROWS=[("morph2hg_play","Morph",20,8000,{"starting_life":30,"opponent_heads":2}),
      ("morph2hg_dec","Morph",20,8000,{"starting_life":30,"opponent_heads":2,"shuffle_salt_search":1,"_so":50000}),
      ("modernrwburn2hg_dec","Modern RW Burn",20,4000,{"starting_life":30,"opponent_heads":2,"shuffle_salt_search":1,"_so":50000})]
def jobs(arm):
    out=[]
    for name,deck,b,n,ex in ROWS:
        so=ex.get("_so",0)
        for off in range(0,n,CHUNK):
            j={"name":f"{name}_{arm}_o{off}","deck":f"decks/{deck}/{deck}.cod","profile":f"decks/{deck}/{deck}.profile.json",
               "games":CHUNK,"seed":BASE+so+off,"game_index":off,"budget_ms":b}
            j.update({k:v for k,v in ex.items() if not k.startswith("_")}); out.append(j)
    return out
for arm in ("base","new"): json.dump({"jobs":jobs(arm)},open(f"m_{arm}.json","w"),indent=1)
json.dump([r[0] for r in ROWS],open("rows.json","w"))
