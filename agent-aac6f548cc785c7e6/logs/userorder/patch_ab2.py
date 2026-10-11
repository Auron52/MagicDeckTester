p='/workspaces/MagicDeckTester/.claude/worktrees/agent-aac6f548cc785c7e6/logs/userorder/make_ab2.py'
s=open(p).read()
old='''    "cyc": (WT + "/decks/Unpredictable Cyclone/Unpredictable Cyclone.cod",
            WT + "/decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json"),
}'''
new='''    "cyc": (WT + "/decks/Unpredictable Cyclone/Unpredictable Cyclone.cod",
            WT + "/decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json"),
}
for _k, _n in (("mw", "Mirrorwing Dragon"), ("dragons", "Dragons"), ("giants", "Giants"), ("snow", "Snow"),
               ("wk", "WhiteKnights"), ("angels", "Angels"), ("bruna", "Bruna"), ("kitty", "KittyEquipment"),
               ("stompy", "StompySurprise"), ("five", "FiveColour")):
    DECKS[_k] = (WT + "/decks/%s/%s.cod" % (_n, _n), WT + "/decks/%s/%s.profile.json" % (_n, _n))'''
assert old in s; s=s.replace(old,new)
old2='''    for di, dk in enumerate(decks):
        deck, prof = DECKS[dk]
        for cname, off, games, depth, bud in (("d0", 0, 300, 0, 0), ("d3", 50000, 100, 3, 10)):'''
new2='''    for di, dks in enumerate(decks):
        dk, _, only = dks.partition("@")   # "key@d0" -> the d0 cell only
        deck, prof = DECKS[dk]
        for cname, off, games, depth, bud in (("d0", 0, 300, 0, 0), ("d3", 50000, 100, 3, 10)):
            if only and cname != only:
                continue'''
assert old2 in s; s=s.replace(old2,new2)
old3='''    for di, dk in enumerate(meta["decks"]):
        for cell, off in (("d0", 0), ("d3", 50000)):'''
new3='''    for di, dks in enumerate(meta["decks"]):
        dk = dks.partition("@")[0]
        for cell, off in (("d0", 0), ("d3", 50000)):'''
assert old3 in s; s=s.replace(old3,new3)
old4='''            for k in sl:
                print("   SLOWER %s %s gi=%d seed=%d: on %d off %d" % (dk, cell, k[2], seed0 + k[2], res[k]["on"], res[k]["off"]))'''
new4='''            for k in sl:
                print("   SLOWER %s %s gi=%d seed=%d: on %d off %d" % (dk, cell, k[2], seed0 + k[2], res[k]["on"], res[k]["off"]))
            for k in fa:
                print("   faster %s %s gi=%d seed=%d: on %d off %d" % (dk, cell, k[2], seed0 + k[2], res[k]["on"], res[k]["off"]))'''
assert old4 in s; s=s.replace(old4,new4)
open(p,'w').write(s)
print('ok')
