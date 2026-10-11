#!/usr/bin/env python3
"""Find the first turn/phase entry whose actions differ between two trace JSONs and print both."""
import json
import sys


def load(p):
    return json.load(open(p))


def key(t):
    return (t["turn"], t["phase"])


def acts(t):
    return [(a.get("type"), a.get("cardName"), json.dumps({k: v for k, v in a.items() if k not in ("type", "card", "cardName")}, sort_keys=True)) for a in t.get("actions", [])]


def main(a, b):
    da, db = load(a), load(b)
    if da.get("mulliganSequence") != db.get("mulliganSequence"):
        print("MULLIGAN DIFFERS")
    for ta, tb in zip(da["turns"], db["turns"]):
        if key(ta) != key(tb) or acts(ta) != acts(tb) or ta.get("boardAfter", {}).get("hand") != tb.get("boardAfter", {}).get("hand"):
            print("first divergence at", key(ta), key(tb))
            print(" OFF:", [(x[0], x[1]) for x in acts(ta) if x[0] != "UNTAP_SOURCES"])
            print(" ON :", [(x[0], x[1]) for x in acts(tb) if x[0] != "UNTAP_SOURCES"])
            return
    print("no divergence in common prefix")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
