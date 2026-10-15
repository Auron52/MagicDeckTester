#!/usr/bin/env python3
"""The follow-up's closing batch, built the moment the follow-up (new2) lands: ONE pooled manifest of
  * rec.*  -- stage-1 / stage-2 recovery jobs for every new2 slower game the follow-up did not already
              recover (its predicted jobs cover the first A/B's slower games; this covers the rest);
  * idc.*  -- the first two chunks of every new2 arm/cell again, for the HEAD binary: HEAD's src differs
              from mtg.new2's only by a cost-only rewrite of KillTriggerComboCredit and comments, so every
              digest must match new2's.
  python3 final_prep.py BASE_DIR NEW2_DIR OUT_DIR"""
import json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_ab

dbase, dnew, out = sys.argv[1:4]
make_ab.analyse(dbase, dnew)                       # writes NEW2_DIR/analysis.txt + slower.json
done = set()
for line in open(os.path.join(dnew, "batch.out"), errors="ignore"):
    m = make_ab.RES.match(line)
    if m:
        done.add(m.group(1))
jobs = [j for j in make_ab.recovery_jobs(dnew) if j["name"] not in done]
nrec = len(jobs)
man = json.load(open(os.path.join(dnew, "manifest.json")))
seen = {}
for j in man["jobs"]:
    p = j["name"].split(".")
    if p[0] in ("rec", "id"):
        continue
    k = tuple(p[:3])
    if seen.get(k, 0) >= 2:
        continue
    seen[k] = seen.get(k, 0) + 1
    jj = dict(j)
    jj["name"] = "idc." + j["name"]
    jobs.append(jj)
os.makedirs(out, exist_ok=True)
json.dump({"jobs": jobs}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
print("wrote %s/manifest.json: %d recovery jobs, %d identity chunks" % (out, nrec, len(jobs) - nrec))
