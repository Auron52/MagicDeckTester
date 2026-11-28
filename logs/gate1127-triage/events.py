import pickle, collections, sys, re
recs, trunc = pickle.load(open("/workspaces/MagicDeckTester/build/scratch/gate1127_recs.pkl","rb"))
ev = collections.defaultdict(list)
for r in recs:
    ev[(r["deck"], r["cls"], r["seed"], int(r["turn"]), r["phase"])].append(r)
tab = collections.defaultdict(lambda: [0,0,set()])
for (deck, cls, seed, t, p), L in ev.items():
    x = tab[(deck, cls)]; x[0] += len(L); x[1] += 1; x[2].add(seed)
print("%-20s %-18s %7s %7s %6s" % ("deck","cls","lines","events","games"))
for (deck, cls), (l, e, g) in sorted(tab.items()):
    print("%-20s %-18s %7d %7d %6d" % (deck, cls, l, e, len(g)))
