#!/usr/bin/env python3
"""Write the jobs of a manifest that no batch.out under the given dirs has finished, sorted
most-expensive-first. drop_done.py <in-manifest> <out-dir> <done-dir>..."""
import json, os, re, sys

src, out = sys.argv[1], sys.argv[2]
done = set()
for d in sys.argv[3:]:
    p = os.path.join(d, "batch.out")
    if not os.path.exists(p):
        continue
    for line in open(p, errors="ignore"):
        m = re.match(r"^(\S+): played=", line)
        if m:
            done.add(m.group(1))
m = json.load(open(src))
keep = [j for j in m["jobs"] if j["name"] not in done]
os.makedirs(out, exist_ok=True)
json.dump({"jobs": keep}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
print("%d of %d jobs left, %d games" % (len(keep), len(m["jobs"]), sum(j["games"] for j in keep)))
