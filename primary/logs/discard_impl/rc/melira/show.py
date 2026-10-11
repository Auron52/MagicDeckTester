import json, sys, glob
for tag in sys.argv[1:]:
    f = sorted(glob.glob(f"/workspaces/MagicDeckTester/logs/discard_impl/rc/melira/{tag}/*_game_0.json"))[-1]
    g = json.load(open(f))
    print("=====", tag, f)
    keys = list(g.keys())
    print("keys:", keys)
    turns = g.get("turns") or g.get("Turns")
    for t in turns:
        print("-- turn", t.get("turn"), "draw:", t.get("draw") or t.get("drawn"), "keys:", list(t.keys()))
        for a in t.get("actions", []):
            s = {k: v for k, v in a.items() if k not in ("boardAfter",)}
            print("   ", json.dumps(s)[:300])
            if a.get("type") == "DISCARD" or "DISCARD" in json.dumps(s):
                print("      boardAfter:", json.dumps(a.get("boardAfter"))[:600])
