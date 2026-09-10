#!/usr/bin/env python3
"""THE MENU: which search shapes earn a place, decided from a multi-arm screen's per-game logs.

User, 2026-09-10: *"It would be preferable to not have more than a few options for the leaf, so 3-4 should
be the maximum. Some of these approaches are likely to outperform others in general which means the
dominated ideas can be dropped."* This is the script that drops them on evidence. Five labels -- four
candidate shapes (escalation and final-depth, each with and without the value leaf) plus the full rollout
ladder as the control:

  esc_v   ship        escalation, model leaf              (needs a trusted model)
  esc_nl  escnl_rx    escalation, leafless probe          (no model; relaxed gate alpha)
  fit_v   v_sfit      value probe -> ONE rollout pass at the deepest affordable depth
  fit_nl  nl_sfit_rx  leafless probe -> ONE rollout pass at the deepest affordable depth
  heur    heur        the full rollout ladder             (control)

A shape must hold at EVERY configuration the deck is played at (d5b20 AND d3b10 -- the mulligan generator
runs decks at d3), on BOTH axes, against the deck's shipped shape. See "Adoption rule" in
docs/design/per-deck-search-shape.md.

  SCREEN_WINS=<wins dirs, comma>  SCREEN_FILES=<batch stdout files, comma>  python3 scripts/shape_menu.py

Inputs are what `mtg --batch --game-log-dir D` writes under MTG_DUMP_UNITS=1: `D/<job>.units`
(`<game_index> <units>`) and `D/<job>.wins` (`<game_index> <win_turn> <digest>`, -1 = loss), plus the
batch's own `<job>: played=... avg=... digest=... ms=...` lines. Job names must be
`<deck>_<arm>_d<depth>b<budget>_s<seed>`.

CAVEATS THIS SCRIPT CANNOT CHECK FOR YOU (both cost a wrong conclusion once; see the design doc):
  * an arm whose job spec pins nothing is driven by the DECK'S OWN SIDECAR, not by defaults -- a control
    named `heur` is only the rollout ladder if the job actually sets `value_model: false`;
  * `value_profile: "noleaf"` is not the same play as a deck shipping `leaf: "none"` (the stand-in never
    loads the model file), and MTG_NO_BP_PREFIX_CACHE=1 changes play under a fixed budget. Comparisons
    inside one batch are safe; an ADOPTION must be re-measured the way the deck really runs.
"""
import collections, itertools, math, os, re, sys

WINS = os.environ.get("SCREEN_WINS", "").split(",")
FILES = os.environ.get("SCREEN_FILES", "").split(",") if os.environ.get("SCREEN_FILES") else []
MENU = [("esc_v",  ["ship"]),
        ("esc_nl", ["escnl_rx", "escnl"]),
        ("fit_v",  ["v_sfit", "v_sres2", "v_sres"]),
        ("fit_nl", ["nl_sfit_rx", "nl_sfit", "nl_sres2", "nl_sres"]),
        ("heur",   ["heur"])]
CFGS = ("d5b20", "d3b10")
# COST: deterministic search units; the 2% band only absorbs job-mix rounding, there is no noise here.
# QUALITY: the paired sign test z = net / sqrt(better + worse) over per-game outcomes -- arms run identical
# games (same seed, game index, opening hand), so the discordant pairs are a sign test. NOT the mean win
# turn: its standard error over a full 8 x 500-game screen is 0.004 (Hinata) to 0.011 (Melira), 8-22x the
# tolerance the rule was first written with, so on the heavy decks a small mean delta is noise. The mean is
# kept only as a MAGNITUDE guard (a shape losing a fifth of a turn is rejected whatever its z).
Z_TOL, Q_MAG, U_TOL = -2.0, 0.005, 1.02
JOB = re.compile(r"(\w+?)_((?:v_|nl_)?\w+?)_(d\d+b\d+)_s(\d+)(_rep)?: played=\d+ avg=([\d.]+) "
                 r"digest=\w+ ms=(\d+)")


def load(name, col):
    for w in WINS:
        p = os.path.join(w, name)
        if os.path.exists(p):
            d = {}
            for line in open(p):
                a = line.split()
                if len(a) > col:
                    try: d[int(a[0])] = int(a[col])
                    except ValueError: pass
            return d
    return {}


def read_runs():
    res = {}
    for f in FILES:
        if not os.path.exists(f): continue
        for line in open(f):
            m = JOB.match(line)
            if m and not m.group(5):
                res[(m.group(1), m.group(2), m.group(3), int(m.group(4)))] = (float(m.group(6)), int(m.group(7)))
    return res


def cell(res, dk, cfg, arm, base):
    seeds = sorted({k[3] for k in res if k[0] == dk and k[2] == cfg and k[1] == base
                    and (dk, arm, cfg, k[3]) in res})
    if not seeds: return None
    u = bu = ms = bms = better = worse = 0; avg = bavg = 0.0
    for s in seeds:
        avg += res[(dk, arm, cfg, s)][0]; bavg += res[(dk, base, cfg, s)][0]
        ms += res[(dk, arm, cfg, s)][1]; bms += res[(dk, base, cfg, s)][1]
        u += sum(load(f"{dk}_{arm}_{cfg}_s{s}.units", 1).values())
        bu += sum(load(f"{dk}_{base}_{cfg}_s{s}.units", 1).values())
        a = load(f"{dk}_{base}_{cfg}_s{s}.wins", 1); b = load(f"{dk}_{arm}_{cfg}_s{s}.wins", 1)
        for g in set(a) & set(b):
            # a loss (-1) is the WORST outcome, not the best -- score it beyond the turn cap
            x, y = (b[g] if b[g] > 0 else 9), (a[g] if a[g] > 0 else 9)
            if x < y: better += 1
            elif x > y: worse += 1
    n = len(seeds); disc = better + worse
    return dict(n=n, d_avg=(avg - bavg) / n, u=u / max(1, bu), w=ms / max(1, bms),
                net=better - worse, better=better, worse=worse,
                z=(better - worse) / math.sqrt(disc) if disc else 0.0)


def main():
    if not FILES or not any(WINS):
        sys.exit(__doc__.strip().split("\n\n")[-2])
    res = read_runs()
    decks = list(dict.fromkeys(k[0] for k in res))
    modelled = {dk for dk in decks if any(k[0] == dk and k[1] == "ship" for k in res)}
    print("== THE MENU: every candidate shape vs the deck's SHIPPED shape, at every configuration")
    print("   units-ratio (<1 cheaper) / d_avg (<0 wins sooner) / net games better-minus-worse / z = paired sign test.")
    print(f"   ADOPT (no worse on either axis at BOTH cfgs): z >= {Z_TOL}, d_avg <= {Q_MAG}, units <= {U_TOL}x."
          f"  BETTER: also units < 0.95x or z > +2\n")
    wins = collections.Counter(); adoptable = collections.defaultdict(list)
    for dk in decks:
        base = "ship" if dk in modelled else "heur"
        rows = []
        for label, names in MENU:
            cells = {}; is_base = False
            for cfg in CFGS:
                arm = next((n for n in names if any(k[:3] == (dk, n, cfg) for k in res)), None)
                if arm == base: is_base = True
                cells[cfg] = cell(res, dk, cfg, arm, base) if arm else None
            got = [c for c in cells.values() if c]
            if not got: continue
            ok = (len(got) == len(CFGS)
                  and all(c["z"] >= Z_TOL and c["d_avg"] <= Q_MAG and c["u"] <= U_TOL for c in got))
            strict = ok and any(c["u"] < 0.95 or c["z"] > 2.0 for c in got)
            rows.append((label, cells, ok and not is_base, strict and not is_base, is_base))
            if ok and not is_base: adoptable[label].append(dk)          # the deck's OWN shape is not an alternative
            if strict and not is_base: wins[label] += 1
        if not rows: continue
        print(f"-- {dk}  (base = {base}{'' if dk in modelled else ', no trusted model'})")
        for label, cells, ok, strict, is_base in rows:
            cs = "  ".join(f"{cfg} {cells[cfg]['u']:5.2f}x {cells[cfg]['d_avg']:+.4f} {cells[cfg]['net']:+4d}"
                           f" z{cells[cfg]['z']:+5.1f}" if cells[cfg] else f"{cfg} --" for cfg in CFGS)
            tag = "  [SHIPPED]" if is_base else (" <== BETTER" if strict else (" (equal)" if ok else ""))
            print(f"   {label:7} {cs}{tag}")
        print()
    print("== which shapes earn a place on the menu")
    for label, _ in MENU:
        d = sorted(adoptable[label])
        print(f"   {label:7} strictly better on {wins[label]:2} deck(s), no worse on {len(d):2}:"
              f" {' '.join(d) if d else '-'}")


if __name__ == "__main__":
    main()
