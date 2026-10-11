#!/usr/bin/env bash
# Byte-identity of the fix (default ON) vs ccacda4e on decks holding NO discount_targets_scale_x card.
WT=/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8
R=$WT/logs/rs_crackle
python3 - <<'EOF'
import json, os
WT="/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8"
jobs=[]
for key,deck,prof in [("burn","decks/burn/burn.txt","decks/burn/burn.profile.json"),
                      ("dragonstorm","decks/Dragonstorm/Dragonstorm.cod","decks/Dragonstorm/Dragonstorm.profile.json"),
                      ("stompy","decks/StompySurprise/StompySurprise.cod","decks/StompySurprise/StompySurprise.profile.json")]:
    for cell,depth,bud,g in [("d0",0,0,50),("d3",3,10,20)]:
        jobs.append({"name":"%s_%s_x.c0"%(key,cell),"deck":WT+"/"+deck,"profile":WT+"/"+prof,"games":g,
                     "seed":7950001,"game_index":0,"budget_ms":bud,"depth":depth,"ignore_play_profile":True})
for d in ("other_base","other_fix"):
    os.makedirs(WT+"/logs/rs_crackle/"+d, exist_ok=True)
    json.dump({"jobs":jobs}, open(WT+"/logs/rs_crackle/"+d+"/manifest.json","w"), indent=1)
EOF
bash "$R/run_batch.sh" "$R/mtg_base_ccacda4e" "$R/other_base" 3 &
bash "$R/run_batch.sh" "$R/mtg_fix1" "$R/other_fix" 3 &
wait
grep "played=" "$R/other_base/batch.out" | sort > "$R/other_base/d.txt"
grep "played=" "$R/other_fix/batch.out" | sort > "$R/other_fix/d.txt"
paste -d'|' "$R/other_base/d.txt" "$R/other_fix/d.txt" | awk -F'|' '{split($1,a," "); split($2,b," "); print a[1], a[3], a[4], b[4], (a[4]==b[4]?"SAME":"DIFF")}'
