#!/usr/bin/env python3
import json, os, collections
O = os.path.dirname(os.path.abspath(__file__))
def T(arm, mode, nm, gi):
    f = f"{O}/{arm}.{mode}.tr/{nm}_gi{gi}.json"
    if not os.path.exists(f): return None
    r = json.load(open(f))["result"]; return r["turn"] if r.get("winner") == "player" else 99
agg = collections.defaultdict(collections.Counter)
for j in json.load(open(f"{O}/m_play.json"))["jobs"]:
    nm, gi = j["name"], j["game_index"]; tag = nm.split("_gi")[0]
    bc, nc, bn, nn = T("base","cp",nm,gi), T("new","cp",nm,gi), T("base","nc",nm,gi), T("new","nc",nm,gi)
    d8b, d8n = T("base","cp",nm+"_d8",gi), T("new","cp",nm+"_d8",gi)
    if None in (bc, nc, bn, nn): continue
    a = agg[tag]
    a["nc_net"] += (nn - bn) if max(nn, bn) < 99 else 0
    a["nc_lost"] += (nn == 99 and bn < 99); a["nc_gain"] += (bn == 99 and nn < 99)
    if nc > bc:
        a["slow"] += 1
        if nn <= bc: k = "R"
        elif bn > bc and nn <= bn: k = "C"
        else: k = "V"
        a[k] += 1
        if d8b is not None: a["d8_rec"] += (d8n is not None and d8n <= bc); a["d8_n"] += 1
        print(f"{nm:12s} cp {bc}/{nc} nc {bn}/{nn} d8 {d8b}/{d8n} -> {k}")
    elif nc < bc: a["fast"] += 1
for t, a in agg.items(): print(t, dict(a))
