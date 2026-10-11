#!/usr/bin/env python3
"""Paired lever A/B from a pooled batch's MTG_DUMP_WINS lines.

usage: analyze.py <stderr.log> [--list]
Jobs are named edf_<cell>_<arm>; arm "on" = every lever at its default, "L1".."L4" = that lever =0,
"off" = all five =0. For each cell and lever: ON = arm "on", OFF = arm "<lever>"; net = sum(ON - OFF)
(negative = the lever makes games faster), faster/slower game counts, and the slower games listed.
wt 0 (unwon) is scored as max_turns + 1 = 9.
"""
import re, sys, collections, math

UNWON = 9
pat = re.compile(r'^\[win\] job=edf_(\w+?)_(on|off|L1|L2a|L2b|L3|L4) gi=(\d+) wt=(-?\d+)')

def score(wt):
    return UNWON if wt <= 0 else wt

def main():
    path = sys.argv[1]
    show = "--list" in sys.argv
    res = collections.defaultdict(dict)   # (cell, arm) -> {gi: turns}
    for line in open(path, errors="replace"):
        m = pat.match(line)
        if not m:
            continue
        cell, arm, gi, wt = m.group(1), m.group(2), int(m.group(3)), int(m.group(4))
        res[(cell, arm)][gi] = score(wt)
    cells = sorted(set(c for c, _ in res), key=lambda c: ["d0", "d3", "ship"].index(c) if c in ("d0", "d3", "ship") else 9)
    names = {"L1": "MTG_SOLVE_UNTAP_CREDIT", "L2a": "MTG_PREPAY_ENERGY_RESTORE",
             "L2b": "MTG_SOLVE_AURA_PAYER_CHECK", "L3": "MTG_EDF_ROUTE_BOARD_DEPLOY",
             "L4": "MTG_EDF_GOFF_VERIFY_UNPROJECTED", "off": "ALL FIVE (vs all off)"}
    for cell in cells:
        on = res.get((cell, "on"), {})
        print("== cell %s: on-arm games=%d  avg=%.4f" % (cell, len(on), sum(on.values()) / max(1, len(on))))
        for arm in ("L1", "L2a", "L2b", "L3", "L4", "off"):
            off = res.get((cell, arm))
            if not off:
                continue
            common = sorted(set(on) & set(off))
            d = [on[g] - off[g] for g in common]
            net = sum(d)
            fast = sum(1 for x in d if x < 0)
            slow = sum(1 for x in d if x > 0)
            n = len(d)
            mean = net / n if n else 0.0
            var = sum((x - mean) ** 2 for x in d) / (n - 1) if n > 1 else 0.0
            se = math.sqrt(var / n) if n > 1 else 0.0
            t = mean / se if se > 0 else 0.0
            print("   %-4s %-34s n=%5d  net(ON-OFF)=%+5d  faster=%4d slower=%4d  mean=%+.4f se=%.4f t=%+.2f"
                  % (arm, names[arm], n, net, fast, slow, mean, se, t))
            if show:
                sl = [(g, on[g], off[g]) for g in common if on[g] > off[g]]
                if sl:
                    print("        slower (gi on off): " + " ".join("%d:%d/%d" % s for s in sl))

if __name__ == "__main__":
    main()
