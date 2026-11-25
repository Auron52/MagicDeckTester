#!/usr/bin/env python3
# trace batch: every changed/slower game, play setting (b20, profile depth) with traces; plus d8 b0 for slower ones.
import json
SETS = []  # tag, deck, seedbase, extra, gis
def big(row):
    out = []
    for ln in open("../big/changed_base_new.txt"):
        a = ln.split()
        if a[0] == row: out.append(int(a[1]))
    return out
L2 = {"starting_life": 30, "opponent_heads": 2}
SETS += [("m", "Morph", 9930000, {}, [29, 857, 1021, 1520]),
         ("md", "Morph", 9930000, {"shuffle_salt_search": 1}, [354, 1021]),
         ("b", "Modern RW Burn", 9930000, {}, [1627, 1419, 1421]),
         ("bd", "Modern RW Burn", 9930000, {"shuffle_salt_search": 1}, [1619, 1627, 1421]),
         ("M", "Morph", 14200000, {}, big("morph_play")),
         ("B", "Modern RW Burn", 14200000, {}, big("modernrwburn_play")),
         ("Md", "Morph", 14250000, {"shuffle_salt_search": 1}, big("morph_dec")),
         ("h", "Morph", 14220000, L2, big("morph2hg_play")),
         ("bh", "Modern RW Burn", 14220000, L2, big("modernrwburn2hg_play"))]
slow = set()
for ln in open("../big/changed_base_new.txt"):
    a = ln.split()
    if len(a) == 4 or a[-1] == "lost": slow.add((a[0], int(a[1])))
rowof = {"M": "morph_play", "B": "modernrwburn_play", "h": "morph2hg_play", "bh": "modernrwburn2hg_play", "Md": "morph_dec"}
oldslow = {("m", g) for g in [29, 857, 1021, 1520]} | {("md", g) for g in [354, 1021]} | {("b", g) for g in [1627, 1419, 1421]} | {("bd", g) for g in [1619, 1627, 1421]}
play, d8 = [], []
for tag, deck, sb, ex, gis in SETS:
    for gi in gis:
        j = {"name": f"{tag}_gi{gi}", "deck": f"decks/{deck}/{deck}.cod", "profile": f"decks/{deck}/{deck}.profile.json",
             "games": 1, "seed": sb + gi, "game_index": gi, "budget_ms": 20}
        j.update(ex); play.append(j)
        if (tag, gi) in oldslow or (rowof.get(tag), gi) in slow:
            k = dict(j); k["name"] = f"{tag}_gi{gi}_d8"; k.update({"budget_ms": 0, "depth": 8, "ignore_play_profile": True, "abandon_units": 20000000})
            d8.append(k)
json.dump({"jobs": d8 + play}, open("m_all.json", "w"), indent=1)
json.dump({"jobs": play}, open("m_play.json", "w"), indent=1)
print(len(play), "play jobs", len(d8), "d8 jobs")
