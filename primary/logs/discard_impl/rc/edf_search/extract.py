"""Extract the fs-sim rollouts under one root plan of the LAST real T<turn> root decision.
usage: extract.py <err file> <cost marker e.g. 'T3 pass=1 done win=6 cost=8554'> <plan substring>"""
import sys
path, marker, plan = sys.argv[1], sys.argv[2], sys.argv[3]
lines = open(path, errors="replace").read().split("\n")
end = max(i for i, l in enumerate(lines) if marker in l)
# start = previous [search] line
start = max(i for i in range(end) if lines[i].startswith("[search]"))
blk = lines[start:end + 1]
out, on = [], False
for l in blk:
    if l.startswith("[fs-root] scan"):
        on = "bp=-1" in l and l.endswith("(defer): " + plan)
    if on:
        out.append(l)
    if l.startswith("[fs-root] tail") and on:
        on = False
print("\n".join(out))
