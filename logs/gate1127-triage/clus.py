import pickle, collections, sys, re
recs, trunc = pickle.load(open("/workspaces/MagicDeckTester/build/scratch/gate1127_recs.pkl","rb"))
SKIP = set(sys.argv[2].split(",")) if len(sys.argv) > 2 else set()
ONLY = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] != "all" else None
def sig(r):
    plan = re.sub(r"land=[^+]*", "land", r["plan"]).replace(" [ordered]", "")
    acts = sorted(a.strip() for a in plan.split("+"))
    det = r["detail"]
    m = re.search(r"engine dropped \[([^\]]*)\]", det)
    drop = m.group(1) if m else ""
    rb = re.search(r"ROLLBACK (\S+):", det)
    return (r["cls"], drop, " + ".join(acts), rb.group(1) if rb else "")
ev = collections.defaultdict(list)
for r in recs:
    if ONLY and r["deck"] != ONLY: continue
    if r["deck"] in SKIP: continue
    ev[(r["deck"], r["cls"], r["seed"], int(r["turn"]), r["phase"])].append(r)
# event signature = most common (drop) + first plan
cl = collections.defaultdict(lambda: [0, set(), collections.Counter(), []])
for key, L in ev.items():
    deck, cls = key[0], key[1]
    drops = collections.Counter(sig(r)[1] for r in L)
    d = drops.most_common(1)[0][0]
    T = key[3]
    c = cl[(deck, cls, d)]
    c[0] += 1; c[1].add(key[2]); c[2]["T%d" % T] += 1
    if len(c[3]) < 3: c[3].append((key[2], L[0]["gi"], T, L[0]["plan"][:150]))
for (deck, cls, d), (n, g, ts, ex) in sorted(cl.items(), key=lambda x: -x[1][0])[:int(sys.argv[3]) if len(sys.argv)>3 else 60]:
    print("%-18s %-16s ev=%4d g=%4d drop=[%s] %s" % (deck, cls, n, len(g), d[:70], dict(ts.most_common(4))))
    for e in ex[:2]: print("      ", e)
