import pickle, sys, re, collections
recs, _ = pickle.load(open("/workspaces/MagicDeckTester/build/scratch/gate1127_recs.pkl","rb"))
deck, cls = sys.argv[1], sys.argv[2]
pat = re.compile(sys.argv[3]) if len(sys.argv) > 3 else None
n = int(sys.argv[4]) if len(sys.argv) > 4 else 4
seen = set(); out = 0
for r in recs:
    if r["deck"] != deck or r["cls"] != cls: continue
    s = "plan={%s} | %s" % (r["plan"], r["detail"])
    if pat and not pat.search(s): continue
    k = (r["seed"], r["turn"])
    if k in seen: continue
    seen.add(k)
    print("s%s gi%s T%s m%s %s\n" % (r["seed"], r["gi"], r["turn"], r["phase"], s[:2500]))
    out += 1
    if out >= n: break
