#!/usr/bin/env python3
"""The draft's probe (docs/design/cast-order-drafts-2026-10-14/burn.md section 2: 600 games at the deck's play
settings, seeds 9001..9600 with game_index = gi) re-run OLD vs NEW as ONE pooled batch with full game traces.
OLD = the new binary with every lever off (byte-identical to the base -- the A/B's "off" arm checks that).
  python3 make_probe.py --out DIR      -> DIR/manifest.json; run with --game-trace-dir DIR/trace
  python3 make_probe.py --scan DIR     -> the probe's symptom counts per arm (scan.txt)
"""
import argparse, glob, json, os, re, sys
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
O, M2, CR, BF = "MTG_BURN_ORDER", "MTG_BURN_M2_RULE", "MTG_SPECTACLE_PLAN_CREDIT", "MTG_BURN_BLOOD_FIRST"
SO = "MTG_SPECTACLE_CREDIT_SEARCH_ONLY"
ARMS = [("off", {O: False, M2: False, CR: False, BF: False, SO: False}),
        ("o1", {O: True, M2: False, CR: False, BF: False, SO: False}),
        ("o123", {O: True, M2: True, CR: True, BF: False, SO: False}),
        ("o123b", {O: True, M2: True, CR: True, BF: True, SO: False}),
        ("o123s", {O: True, M2: True, CR: True, BF: False, SO: True})]
N, CH = 600, 25


def build(out):
    jobs = []
    for c in range(0, N, CH):
        for arm, fl in ARMS:
            jobs.append({"name": "probe.%s.c%d" % (arm, c), "deck": os.path.join(ROOT, "decks/burn/burn.txt"),
                         "profile": os.path.join(ROOT, "decks/burn/burn.profile.json"), "games": CH,
                         "seed": 9001 + c, "game_index": c, "budget_ms": 20, "flags": fl})
    os.makedirs(out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    print("wrote %d jobs" % len(jobs))


BURN = {"Lightning Bolt", "Shard Volley", "Searing Blaze", "Searing Blood", "Skullcrack"}


def scan(d):
    c = defaultdict(Counter)
    for f in glob.glob(os.path.join(d, "trace", "*.json")):
        m = re.search(r"probe\.([a-z0-9]+)\.c\d+_gi(\d+)\.json$", os.path.basename(f))
        if not m:
            continue
        arm = m.group(1)
        g = json.load(open(f))
        for ph in g.get("turns", []):
            main = ph.get("phase")
            if main not in ("MAIN_1", "MAIN_2"):
                continue
            seq = []
            for a in ph.get("actions", []):
                if a.get("type") == "PLAY_LAND":
                    seq.append(("LAND", None))
                elif a.get("type") == "CAST_SPELL":
                    seq.append((a.get("cardName"), a.get("manaPaid")))
            names = [s for s, _ in seq]
            for i, (nm, paid) in enumerate(seq):
                if nm == "Light Up the Stage":
                    c[arm]["LUtS %s %s" % (main, "{R}" if paid == "{R}" else "full")] += 1
                if main == "MAIN_2" and nm not in (None, "LAND", "Light Up the Stage",
                                                   "Eidolon of the Great Revel", "Searing Blaze"):
                    c[arm]["main-2 cast of %s" % nm] += 1
                if nm == "Shard Volley" and "Eidolon of the Great Revel" in names[:i]:
                    c[arm]["Volley after Eidolon (same main)"] += 1
                if nm == "Eidolon of the Great Revel" and any(x not in ("LAND",) for x in names[i + 1:]):
                    c[arm]["a cast after Eidolon (same main)"] += 1
                if nm == "Monastery Swiftspear" and any(x in BURN or x == "Light Up the Stage" for x in names[:i]):
                    c[arm]["Swiftspear after a noncreature (same main)"] += 1
                if nm == "Searing Blaze" and "LAND" in names[i + 1:]:
                    c[arm]["Blaze then the land (same main)"] += 1
    out = []
    for arm, _ in ARMS:
        out.append("== %s" % arm)
        for k in sorted(c[arm]):
            out.append("  %-50s %d" % (k, c[arm][k]))
    text = "\n".join(out)
    print(text)
    open(os.path.join(d, "scan.txt"), "w").write(text + "\n")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out")
    ap.add_argument("--scan")
    a = ap.parse_args()
    if a.scan:
        scan(a.scan)
    else:
        build(a.out)
