#!/usr/bin/env python3
"""Audit the USER's Dragonstorm cast order against MTG_USER_ORDER_TRACE output.

    python3 audit.py <stderr-file> [<stderr-file> ...]

Settings are told apart by game-seed range (SETTINGS). Reads [uo-cast] (every REAL executor cast, in order,
paid or not) and [uo-order] (every executor cast-order decision) lines. Counts are DISTINCT PHASES
(game seed, turn, main) showing the class at least once; pre/post = before / after an Apex of Power
resolved in that phase.

The user's order (docs/design/dragonstorm-search-pruning.md + the 2026-10-10 rulings):
  Ruby Medallion (earliest payable slot) < Rite < Pyretic < Desperate (no splice) < Seething
  < Desperate (spliced) < Apex (enabler) < hard-cast Dragon < Irencrag (second-to-last, before
  Dragonstorm) < Dragonstorm (last).
A plain Desperate cast AFTER a spliced one in the same segment is the copy the splice revealed (it must
stay in hand), so it ranks with the spliced casts -- "splice-legal", not a violation.
Pairs inside one segment are split into SAME decision (one decided order) and a LATER decision (a d0
second pass / a re-solve after a failed cast added the later card).
"""
import re, sys
from collections import defaultdict, Counter

SETTINGS = [("d0", 7000001, 7099999), ("d3b10", 7100001, 7199999), ("ship", 7200001, 7299999)]
DRAGONS = {"Scourge of Valkas", "Dragonlord Kolaghan", "Karrthus, Tyrant of Jund", "Utvara Hellkite",
           "Lathliss, Dragon Queen"}


def setting_of(gs):
    for name, lo, hi in SETTINGS:
        if lo <= gs <= hi:
            return name
    return "other"


def cls(card, splice):
    if card == "Ruby Medallion": return "M"
    if card == "Rite of Flame": return "RITE"
    if card == "Pyretic Ritual": return "PYR"
    if card == "Desperate Ritual": return "DESPS" if splice > 0 else "DESP0"
    if card == "Seething Song": return "SEETH"
    if card == "Apex of Power": return "APEX"
    if card == "Irencrag Feat": return "IREN"
    if card == "Dragonstorm": return "STORM"
    if card in DRAGONS: return "DRAGON"
    return "OTHER"


UO = {"M": 0, "RITE": 1, "PYR": 2, "DESP0": 3, "SEETH": 4, "DESPS": 5, "DESPL": 5.5, "APEX": 6, "DRAGON": 7,
      "IREN": 8, "STORM": 9}
RIT = {"RITE", "PYR", "DESP0", "SEETH", "DESPS", "DESPL"}

cast_re = re.compile(r"\[uo-cast\] gs=(\d+) t=(\d+) m=(\d) d=(-?\d+) card=(.+?) num=(-?\d+) staged=(\d) "
                     r"splice=(\d+) paid=(\d) pool=(-?\d+)")
order_re = re.compile(r"\[uo-order\] gs=(\d+) t=(\d+) m=(\d) route=(\S+) pool=(-?\d+) n=(\d+) pays=(-?\d+) "
                      r"ppays=(-?\d+) mpos=(-?\d+) mearl=(-?\d+) seq=(.*)$")


def parse(paths):
    ev = defaultdict(list)
    for p in paths:
        with open(p, errors="replace") as f:
            for line in f:
                m = cast_re.search(line)
                if m:
                    key = (int(m.group(1)), int(m.group(2)), int(m.group(3)))
                    ev[key].append({"t": "cast", "card": m.group(5), "staged": int(m.group(7)),
                                    "splice": int(m.group(8)), "paid": int(m.group(9))})
                    continue
                m = order_re.search(line)
                if m:
                    key = (int(m.group(1)), int(m.group(2)), int(m.group(3)))
                    seq = []
                    for tok in m.group(11).split("|"):
                        if not tok:
                            continue
                        parts = tok.split(":")
                        sp = 0
                        for x in parts[1:]:
                            if x.startswith("s"):
                                sp = int(x[1:])
                        seq.append((parts[0], sp))
                    ev[key].append({"t": "order", "route": m.group(4), "pays": int(m.group(7)),
                                    "mpos": int(m.group(9)), "mearl": int(m.group(10)), "seq": seq,
                                    "n": int(m.group(6))})
    return ev


def seq_violations(items):
    """items: list of (class, segment, decision). Returns {(class, post)}."""
    out = set()
    n = len(items)
    # splice-legal late plain Desperates
    items = list(items)
    for i in range(n):
        c, sg, dc = items[i]
        if c == "DESP0" and any(items[j][0] == "DESPS" and items[j][1] == sg for j in range(i)):
            items[i] = ("DESPL", sg, dc)
            out.add(("info: plain Desperate after the spliced one (splice-legal)", sg > 0))
        if c == "DESP0" and any(items[j][0] == "DESPS" and items[j][1] == sg for j in range(i + 1, n)):
            out.add(("info: plain Desperate BEFORE a spliced one (legal only with a spare copy in hand)", sg > 0))
    for i in range(n):
        ci, si, di = items[i]
        for j in range(i + 1, n):
            cj, sj, dj = items[j]
            if ci == "OTHER" or cj == "OTHER":
                continue
            post = sj > 0
            if si == sj:
                where = "same decision" if di == dj else "later decision"
                if ci in RIT and cj in RIT and UO[ci] > UO[cj]:
                    out.add(("ritual order not per USER list [%s]" % where, post))
                if ci == "DRAGON" and cj in RIT:
                    out.add(("hard-cast Dragon before a ritual [%s]" % where, post))
                if ci == "DRAGON" and cj == "APEX":
                    out.add(("hard-cast Dragon before Apex [%s]" % where, post))
                if ci in RIT | {"APEX", "DRAGON", "IREN", "STORM"} and cj == "M":
                    out.add(("Medallion after a %s [%s] (info: see earliest-payable)" % (ci.lower(), where), post))
                if ci == "STORM" and cj != "STORM":
                    out.add(("Dragonstorm not last [%s]" % where, post))
            if ci == "IREN" and cj == "APEX":
                out.add(("Irencrag before Apex (trap)", post))
    for i in range(n):
        ci, si, di = items[i]
        if ci == "IREN":
            nxt = items[i + 1][0] if i + 1 < n else None
            post = si > 0
            if nxt is None:
                out.add(("Irencrag last (no spell after)", post))
            elif nxt == "DRAGON":
                out.add(("Irencrag -> Dragon (allowed, USER rule 2)", post))
            elif nxt != "STORM":
                out.add(("Irencrag -> non-closer spell", post))
    return out


def main(paths):
    ev = parse(paths)
    res = defaultdict(lambda: defaultdict(lambda: [set(), set()]))
    ex = defaultdict(lambda: defaultdict(list))
    phases = defaultdict(int)
    phases_bp = defaultdict(int)
    games = defaultdict(set)
    routes = defaultdict(Counter)
    spin = defaultdict(int)

    def hit(st, c, post, key):
        s = res[st][c][1 if post else 0]
        if key in s:
            return
        s.add(key)
        if len(ex[st][c]) < 5 and key not in ex[st][c]:
            ex[st][c].append(key)

    for key, evs in ev.items():
        gs, t, mm = key
        st = setting_of(gs)
        games[st].add(gs)
        casts = [e for e in evs if e["t"] == "cast"]
        if not casts:
            continue
        phases[st] += 1
        if sum(1 for e in casts if not e["paid"]) >= 10:
            spin[st] += 1
        seg = 0
        dec = -1
        last_order = None
        pre_plan = Counter()
        pre_cast = Counter()
        items = []
        any_bp = False
        for e in evs:
            if e["t"] == "order":
                dec += 1
                last_order = e
                if e["n"] > 0:
                    routes[st][e["route"]] += 1
                if seg == 0 and not pre_plan:
                    pre_plan = Counter(n for n, _ in e["seq"])
                if e["mpos"] >= 0 and e["mearl"] >= 0 and e["mpos"] > e["mearl"]:
                    hit(st, "Medallion later than its earliest payable slot [%s]" % e["route"], seg > 0, key)
                if e["mpos"] >= 0 and e["mearl"] >= 0 and e["mpos"] < e["mearl"]:
                    hit(st, "Medallion EARLIER than payable (drops the line) [%s]" % e["route"], seg > 0, key)
                continue
            c = cls(e["card"], e["splice"])
            if not e["paid"]:
                pays = last_order["pays"] if last_order else None
                tag = "unpaid cast, decided order projected payable" if pays == -1 \
                    else "unpaid cast, decided order cannot pay"
                hit(st, tag, seg > 0, key)
                continue
            if seg > 0 and not e["staged"] and c in RIT | {"M"}:
                kind = "in pre-Apex plan" if pre_plan[e["card"]] > pre_cast[e["card"]] else "not in pre-Apex plan"
                hit(st, "hand %s cast after Apex (%s)" % ("Medallion" if c == "M" else "ritual", kind), True, key)
            if seg == 0:
                pre_cast[e["card"]] += 1
            items.append((c, seg, dec))
            if e["card"] == "Apex of Power":
                seg += 1
                any_bp = True
        if any_bp:
            phases_bp[st] += 1
        for c, post in seq_violations(items):
            hit(st, c, post, key)

    for st, _, _ in SETTINGS:
        if st not in phases:
            continue
        print("=" * 120)
        print("setting %s: games=%d, phases with a cast=%d (Apex breakpoint in %d); non-empty decided orders by route %s;"
              " phases with >=10 failed casts (re-solve spin) %d"
              % (st, len(games[st]), phases[st], phases_bp[st], dict(routes[st]), spin[st]))
        print("  %-72s %5s %5s   examples (game seed, turn, main)" % ("class", "pre", "post"))
        for c in sorted(res[st]):
            pre, post = res[st][c]
            print("  %-72s %5d %5d   %s" % (c, len(pre), len(post), ex[st][c][:4]))


if __name__ == "__main__":
    main(sys.argv[1:])
