#!/bin/bash
# Byte-identity spot check: the new Release binary vs the committed-tip PGO binary (src d6a760e5).
#   antilife (narrowing moved; must be identical), giants with MTG_GIANTS_ROCK_AFTER_LANDS=0 on NEW vs the
#   OLD default (must be identical), giants default on NEW (differs only where the rock rule fires).
R=/workspaces/MagicDeckTester
O=$R/logs/discard_impl/identity
mkdir -p $O
python3 - "$O" <<'EOF'
import json, sys
o = sys.argv[1]
AL = ("decks/Anti-Lifegain/Anti-Lifegain.cod", "decks/Anti-Lifegain/Anti-Lifegain.profile.json")
GI = ("decks/Giants/Giants.cod", "decks/Giants/Giants.profile.json")
def job(name, dk, games, seed, bud, depth=None, flags=None):
    j = {"name": name, "deck": dk[0], "profile": dk[1], "games": games, "seed": seed, "game_index": 0, "budget_ms": bud}
    if depth is not None: j["depth"] = depth; j["ignore_play_profile"] = True
    if flags: j["flags"] = flags
    return j
base = [job("al_d3", AL, 60, 4100001, 10, 3), job("al_ship", AL, 40, 4110001, 20)]
old = base + [job("gi_d0_off", GI, 200, 4120001, 0, 0)]
new = base + [job("gi_d0_off", GI, 200, 4120001, 0, 0, {"MTG_GIANTS_ROCK_AFTER_LANDS": False}),
              job("gi_d0_on", GI, 200, 4120001, 0, 0)]
json.dump({"jobs": old}, open(o + "/old.json", "w"), indent=1)
json.dump({"jobs": new}, open(o + "/new.json", "w"), indent=1)
EOF
nice -n 10 $R/build/PGO/mtg --batch $O/old.json --threads 3 > $O/old.out 2> $O/old.err
nice -n 10 $R/build/Release/mtg --batch $O/new.json --threads 3 > $O/new.out 2> $O/new.err
echo "== OLD (PGO, d6a760e5)"; grep -E "^(al_|gi_)" $O/old.out
echo "== NEW (Release, working tree)"; grep -E "^(al_|gi_)" $O/new.out
