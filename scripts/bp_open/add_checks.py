#!/usr/bin/env python3
"""Copy named jobs from one manifest into another under a chk_ prefix (cross-binary digest checks).
  add_checks.py <target-manifest> <source-manifest> <job-name>..."""
import json, sys

tgt = json.load(open(sys.argv[1]))
src = {j["name"]: j for j in json.load(open(sys.argv[2]))["jobs"]}
for nm in sys.argv[3:]:
    j = dict(src[nm])
    j["name"] = "chk_" + nm
    tgt["jobs"].insert(0, j)
json.dump(tgt, open(sys.argv[1], "w"), indent=1)
print(len(tgt["jobs"]))
