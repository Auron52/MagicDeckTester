import re, os, sys, collections, json, pickle
S = "/workspaces/MagicDeckTester/build/wt/land-1128/scratch/mana_clean/claude-queue-20261127a"
ISSUE = {"ENUM-GAP","APPLY-DROP","APPLY-DROP-ORDER","DOMINATED-CREATURE","DOMINATED-ONESHOT","DOMINATED-HASTE","DOMINATED-OTHER","ORACLE-GAP","UNMODELED","CAPPED","CAPPED-DECISION"}
DIG = re.compile(r"^(?P<job>[^:\s]+): played=(\d+) avg=([0-9.]+) digest=([0-9a-f]+)")
LINE = re.compile(r"^\[mana-oracle\] (?P<cls>[A-Z-]+) seed=(?P<seed>\d+) job=(?P<job>\S+) gi=(?P<gi>-?\d+) T(?P<turn>\d+) m(?P<phase>\d) plan=\{(?P<plan>.*?)\} \| (?P<detail>.*)$")
CNT = re.compile(r"^\[mana-oracle\] (?P<cls>[A-Z-]+) seed=(?P<seed>\d+) job=(?P<job>\S+)")
owner = {}
k = 0
while os.path.exists(f"{S}/on.{k}.out"):
    for ln in open(f"{S}/on.{k}.out", errors="replace"):
        m = DIG.match(ln.strip())
        if m: owner.setdefault(m.group("job"), k)
    k += 1
N = k
recs = []
trunc = collections.Counter()
for k in range(N):
    for ln in open(f"{S}/on.{k}.err", errors="replace"):
        if not ln.startswith("[mana-oracle] "): continue
        m = CNT.match(ln)
        if not m or m.group("cls") not in ISSUE: continue
        if owner.get(m.group("job")) != k: continue
        deck = m.group("job").split("__")[0]
        if m.group("seed") == "0":
            trunc[(deck, m.group("cls"))] += 1; continue
        m2 = LINE.match(ln.rstrip("\n"))
        if not m2:
            trunc[(deck, m.group("cls"), "nomatch")] += 1; continue
        d = m2.groupdict(); d["deck"] = deck; d["att"] = k
        recs.append(d)
pickle.dump((recs, trunc), open(sys.argv[1], "wb"))
print(len(recs), trunc)
