#!/usr/bin/env python3
import json, glob, os, sys, collections
O = os.path.dirname(os.path.abspath(__file__))
def load(arm, mode, name, gi):
    f = f"{O}/{arm}.{mode}.tr/{name}_gi{gi}.json"
    return json.load(open(f)) if os.path.exists(f) else None
def turn(d):
    if d is None: return None
    r = d["result"]; return r["turn"] if r.get("winner") == "player" else -1
def names(d):
    n = {}
    for c in d["openingHand"]: n[c["card"]] = c["cardName"]
    for t in d["turns"]:
        for x in t.get("actions", []):
            if "card" in x: n[x["card"]] = x.get("cardName")
    return n
def div(a, b):
    prev = None
    for ta, tb in zip(a["turns"], b["turns"]):
        if ta.get("actions") != tb.get("actions"):
            f = lambda t: [x.get("type", "")[:4] + ":" + str(x.get("cardName", "")) for x in t.get("actions", [])]
            na = names(a)
            hand = prev["boardAfter"]["hand"] if prev else [c["card"] for c in a["openingHand"]]
            g = lambda t: [x.get("cardName","") for x in t.get("actions", []) if x.get("type")=="PLAY_LAND"] or [x.get("type","")[:4]+":"+str(x.get("cardName","")) for x in t.get("actions", [])][:2]
            return f"T{ta['turn']} {ta['phase'][:6]} base={g(ta)} new={g(tb)}"
        prev = ta
    return "no action diff"
rows = json.load(open(f"{O}/m_play.json"))["jobs"]
for j in rows:
    name, gi = j["name"].rsplit("_gi", 1)[0], j["game_index"]
    nm = j["name"]
    bc, nc_ = load("base", "cp", nm, gi), load("new", "cp", nm, gi)
    t = lambda arm, mode, n=nm: turn(load(arm, mode, n, gi))
    line = f"{nm:12s} cp {t('base','cp')}/{t('new','cp')}  nc {t('base','nc')}/{t('new','nc')}  d8 {t('base','cp',nm+'_d8')}/{t('new','cp',nm+'_d8')}"
    if bc and nc_: line += "  | " + div(bc, nc_)
    print(line)
