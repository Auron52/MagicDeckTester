#!/usr/bin/env python3
"""dfeat.py <dcls.txt> <dc_dir>: per Elves game of dcls.sh, the class (phantom = the lever arm's chosen set read lethal
under the lever and the game did not end that turn) and the payment features of the lever arm's diverging main 1:
creatures untapped at the draw (not cast this turn) that the payment tapped, lands left untapped, Lodge / colourless."""
import collections, glob, json, re, sys
txt, dc = sys.argv[1:3]
COLOURLESS = {"Wirewood Lodge", "Cavern of Souls", "Ancient Tomb", "Mishra's Workshop", "Gaea's Cradle"}
cnt = collections.Counter()
for ln in open(txt):
    m = re.match(r"Elves (\d+) T(\d+) .*?\| on casts=(\[.*?\]) dealt=(\d+) read\(on\)=(\S+)", ln)
    if not m: continue
    gi, T, rd = int(m.group(1)), int(m.group(2)), m.group(5)
    g = json.load(open(glob.glob(f"{dc}/Elves_{gi}/b/*.json")[0]))
    ts = [t for t in g["turns"] if t["turn"] == T]
    draw = next(t for t in ts if t["phase"] == "DRAW")["boardAfter"]["battlefield"]
    m1 = next(t for t in ts if t["phase"] == "MAIN_1")["boardAfter"]["battlefield"]
    pre = {p["card"]: p for p in draw}
    tapped_atk = [p["cardName"] for p in m1 if not p.get("isLand") and p["card"] in pre and not pre[p["card"]].get("tapped")
                  and p.get("tapped")]
    open_lands = [p["cardName"] for p in m1 if p.get("isLand") and not p.get("tapped")]
    names = {p["cardName"] for p in draw}
    won = g["result"].get("turn") == T
    cls = "phantom" if rd.endswith("/W") and not won else ("unmatched" if rd == "none" else "nonlethal")
    cnt[cls] += 1
    if tapped_atk: cnt[cls + ":tapped_atk"] += 1
    if tapped_atk and open_lands: cnt[cls + ":tapped_atk+open_land"] += 1
    if "Wirewood Lodge" in names: cnt[cls + ":lodge"] += 1
    if names & COLOURLESS - {"Wirewood Lodge"}: cnt[cls + ":other_colourless"] += 1
    print(f"{gi} T{T} {cls} read={rd} dealt={m.group(4)} tapped_atk={tapped_atk} open_lands={open_lands} "
          f"lodge={'Wirewood Lodge' in names}")
for k, v in sorted(cnt.items()): print(f"  {k}: {v}")
