import json, sys, glob
for tag in sys.argv[1:]:
    f = sorted(glob.glob(f"/workspaces/MagicDeckTester/logs/discard_impl/rc/melira/{tag}/*_game_0.json"))[-1]
    g = json.load(open(f))
    print("=====", tag)
    print("opening:", json.dumps(g.get("openingHand"))[:800])
    print("mull:", json.dumps(g.get("mulliganSequence"))[:800])
    print("result:", json.dumps(g.get("result"))[:400])
    for t in g["turns"]:
        if t["turn"] > 4: break
        b = t.get("boardAfter") or {}
        acts = [a.get("type") + ":" + str(a.get("cardName") or a.get("ability") or a.get("damage")) for a in t.get("actions", [])]
        print(f"T{t['turn']} {t.get('phase')}: {acts}")
        print("    board:", json.dumps(b)[:700])
