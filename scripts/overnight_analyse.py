#!/usr/bin/env python3
"""Overnight analysis: what happens in the games that end on DIFFERENT win turns.

Reads logs/overnight/traces (one full decision-log JSON per game per arm) and writes a report
plus per-pair catalogues of divergent games and a stratified set of side-by-side case studies.

Every pair of arms is analysed the same way. Which card numbers differ between two arms is read
from their numbering.json, so slot groups are DERIVED, never hand-listed -- that is what stops a
mis-stated replace map from silently mislabelling an attribution (it did once, 2026-08-18).
"""
import json, glob, os, sys, statistics as st, collections, itertools
from concurrent.futures import ProcessPoolExecutor

ROOT = "/workspaces/MagicDeckTester"
TD   = f"{ROOT}/logs/overnight/traces"
DECK = f"{ROOT}/logs/deckcmp/Mirrorwing Dragon"
OUT  = f"{ROOT}/logs/overnight/reports"
MT   = 8
SEED = 980000

# ------------------------------------------------------------------ trace extraction
def extract(path):
    with open(path) as fh:
        g = json.load(fh)
    seen = {c["card"] for c in g.get("openingHand", [])}
    casts, dmg = [], {}
    for t in g["turns"]:
        tn = t["turn"]
        for a in t.get("actions", []):
            ty = a.get("type")
            if ty == "DRAW":
                seen.add(a["card"])
            elif ty == "CAST_SPELL":
                casts.append((tn, a["cardName"], a.get("manaPaid", "")))
            elif ty == "ATTACK":
                dmg[tn] = dmg.get(tn, 0) + a.get("damage", 0)
    r = (g.get("result") or {}).get("turn", -1)
    wt = MT + 1 if (r is None or r < 0 or r > MT) else r
    # A trace has ONE ENTRY PER PHASE, so several share a turn number. Keying a dict on the turn
    # keeps only the LAST phase and silently drops the main-phase casts -- accumulate instead.
    acc = collections.defaultdict(list)
    for t in g["turns"]:
        for a in t.get("actions", []):
            det = a.get("manaPaid", "") or (f"dmg={a['damage']}" if a.get("damage") is not None else "")
            acc[t["turn"]].append((a.get("type"), a.get("cardName") or "", det))
    sig = {k: tuple(v) for k, v in acc.items()}
    return dict(seen=seen, wt=wt, casts=casts, dmg=dmg, sig=sig,
                mull=max(0, len(g.get("mulliganSequence", [])) - 1),
                hand=tuple(sorted(c["card"] for c in g.get("openingHand", []))),
                mseq=[(m["attempt"], tuple(sorted(c["card"] for c in m["hand"])), m.get("kept"),
                       tuple(sorted(c["card"] for c in m.get("bottomed", []))))
                      for m in g.get("mulliganSequence", [])])

def pair_one(args):
    gi, a, b = args
    try:
        ga = extract(f"{TD}/{a}_gi{gi}.json")
        gb = extract(f"{TD}/{b}_gi{gi}.json")
    except (FileNotFoundError, json.JSONDecodeError):
        return None
    fd = None
    for t in sorted(set(ga["sig"]) | set(gb["sig"])):
        if ga["sig"].get(t) != gb["sig"].get(t):
            fd = t; break
    return dict(gi=gi, wa=ga["wt"], wb=gb["wt"], d=gb["wt"] - ga["wt"], fd=fd,
                seen=ga["seen"] | gb["seen"], mull=(ga["mull"], gb["mull"]),
                samehand=ga["hand"] == gb["hand"], mseq=(ga["mseq"], gb["mseq"]),
                casts=(ga["casts"], gb["casts"]), dmg=(ga["dmg"], gb["dmg"]),
                sig=(ga["sig"], gb["sig"]))

# ------------------------------------------------------------------ helpers
def numbering(arm):
    return json.load(open(f"{DECK}/{arm}/numbering.json"))

def slot_groups(a, b):
    """-> [(label, frozenset(numbers), a_card, b_card)] for every number where the CARD DIFFERS."""
    na, nb = numbering(a), numbering(b)
    owner = {}
    for arm, n in ((0, na), (1, nb)):
        for card, nums in n.items():
            for x in nums:
                owner.setdefault(x, [None, None])[arm] = card
    groups = collections.defaultdict(set)
    for x, (ca, cb) in owner.items():
        if ca != cb:
            groups[(ca, cb)].add(x)
    out = []
    for (ca, cb), nums in groups.items():
        lab = ",".join(str(x) for x in sorted(nums))
        out.append((lab, frozenset(nums), ca, cb))
    return sorted(out, key=lambda g: min(g[1]))

def mstat(d):
    if not d: return (0, float("nan"), float("nan"), float("nan"))
    m = st.mean(d)
    se = st.stdev(d) / len(d) ** 0.5 if len(d) > 1 else float("nan")
    return (len(d), m, se, m / se if se else float("nan"))

def row(lab, d, w=52):
    n, m, se, t = mstat(d)
    return f"| {lab:<{w}} | {n:>7,} | {m:+8.4f} | {se:7.4f} | {t:+6.2f} |"

# ------------------------------------------------------------------ per-pair analysis
def analyse(a, b, gis, W):
    W(f"\n\n## {a}  vs  {b}\n")
    with ProcessPoolExecutor(16) as ex:
        rows = [r for r in ex.map(pair_one, ((g, a, b) for g in gis), chunksize=256) if r]
    n = len(rows)
    groups = slot_groups(a, b)
    ALL = frozenset().union(*[g[1] for g in groups]) if groups else frozenset()
    d_all = [r["d"] for r in rows]
    nn, m, se, t = mstat(d_all)
    div = [r for r in rows if r["d"]]
    W(f"**paired delta = {m:+.4f} +/- {se:.4f}  (t = {t:+.2f}, n = {n:,})**   "
      f"mean win turn: {a} {st.mean(r['wa'] for r in rows):.4f}, {b} {st.mean(r['wb'] for r in rows):.4f}\n")
    W(f"\ndiverged on win turn: {len(div):,} / {n:,} = {100*len(div)/n:.1f}%\n")

    W(f"\n### slots that differ between these arms (derived from numbering.json)\n")
    W("| slot | " + f"{a} card -> {b} card" + " | numbers |\n|---|---|---|")
    for lab, nums, ca, cb in groups:
        W(f"| {lab} | {ca} -> {cb} | {len(nums)} |")

    W(f"\n### effect, isolated per substitution\n")
    W("Conditioning on which slot a game DREW selects the same games in both arms (common shuffle,")
    W("aligned numbering), so these are clean contrasts. 'only X' = that substitution and no other.\n")
    W(f"| subset | games | delta | se | t |\n|---|---:|---:|---:|---:|")
    ctrl = [r["d"] for r in rows if not (r["seen"] & ALL)]
    W(row("(control) no substituted slot drawn at all", ctrl))
    for lab, nums, ca, cb in groups:
        W(row(f"drew {ca} -> {cb} [{lab}]", [r["d"] for r in rows if r["seen"] & nums]))
    for lab, nums, ca, cb in groups:
        d = [r["d"] for r in rows if (r["seen"] & nums) and not (r["seen"] & (ALL - nums))]
        W(row(f"ONLY {ca} -> {cb} [{lab}]", d))
    if ctrl:
        cm = st.mean(ctrl); cse = st.stdev(ctrl) / len(ctrl) ** 0.5
        W(f"\n**Baseline-corrected** (subtract the control, which is the undrawn-library effect):\n")
        W(f"| substitution | delta | se | t |\n|---|---:|---:|---:|")
        for lab, nums, ca, cb in groups:
            d = [r["d"] for r in rows if (r["seen"] & nums) and not (r["seen"] & (ALL - nums))]
            if len(d) < 3: continue
            _, mm, ss, _ = mstat(d)
            dm = mm - cm; ds = (ss**2 + cse**2) ** 0.5
            W(f"| {ca} -> {cb} | {dm:+.4f} | {ds:.4f} | {dm/ds:+.2f} |")

    W(f"\n### invariant: same indexed cards => same win turn\n")
    same_cards = [r for r in rows if not (r["seen"] & ALL)]
    bad = [r for r in same_cards if r["d"]]
    W(f"- games where neither arm ever drew a substituted card: **{len(same_cards):,}**")
    W(f"- of those, diverged on win turn: **{len(bad):,}** ({100*len(bad)/max(1,len(same_cards)):.2f}%)")
    W(f"- of those, identical opening hand: {sum(1 for r in same_cards if r['samehand']):,} "
      f"({100*sum(1 for r in same_cards if r['samehand'])/max(1,len(same_cards)):.2f}%)")

    W(f"\n### apparatus: decisions on hands containing NO substituted card\n")
    tot = agree = dis = bt = bd = 0
    for r in rows:
        ma, mb = r["mseq"]
        for (at1, h1, k1, x1), (at2, h2, k2, x2) in zip(ma, mb):
            if h1 != h2: break
            if set(h1) & ALL: continue
            tot += 1
            if k1 == k2:
                agree += 1
                if k1 and (x1 or x2):
                    bt += 1; bd += (x1 != x2)
            else:
                dis += 1
    W(f"- keep/mulligan decisions compared: {tot:,};  **disagreed {dis:,} ({100*dis/max(1,tot):.2f}%)**")
    W(f"- bottoming choices compared: {bt:,};  **differed {bd:,} ({100*bd/max(1,bt):.2f}%)**")

    W(f"\n### where divergence starts\n")
    h = collections.Counter(r["fd"] for r in div)
    W("| first turn the action sequences differ | games |\n|---|---:|")
    for k in sorted(h, key=lambda x: (x is None, x)):
        W(f"| {k} | {h[k]:,} |")
    mulldiff = sum(1 for r in div if r["mull"][0] != r["mull"][1] or not r["samehand"])
    W(f"\n- divergences beginning at the mulligan (different keep or hand): "
      f"**{mulldiff:,} ({100*mulldiff/max(1,len(div)):.1f}%)**")
    W(f"- divergences where NEITHER arm drew a substituted card (contamination): "
      f"**{sum(1 for r in div if not (r['seen'] & ALL)):,} "
      f"({100*sum(1 for r in div if not (r['seen'] & ALL))/max(1,len(div)):.1f}%)**")

    # ---- catalogue every divergent game
    cat = f"{OUT}/divergent_{a}_vs_{b}.tsv"
    with open(cat, "w") as fh:
        fh.write("gi\tseed\twin_a\twin_b\tdelta\tfirst_diff_turn\tmull_a\tmull_b\tsame_hand\t"
                 "slots_drawn\tcasts_a\tcasts_b\n")
        for r in sorted(div, key=lambda r: (-abs(r["d"]), r["gi"])):
            sl = ";".join(lab for lab, nums, _, _ in groups if r["seen"] & nums)
            ca = ";".join(f"T{t}:{c}{'('+mp+')' if mp else ''}" for t, c, mp in r["casts"][0]
                          if any(c in (x[2], x[3]) for x in groups))
            cb = ";".join(f"T{t}:{c}{'('+mp+')' if mp else ''}" for t, c, mp in r["casts"][1]
                          if any(c in (x[2], x[3]) for x in groups))
            fh.write(f"{r['gi']}\t{SEED+r['gi']}\t{r['wa']}\t{r['wb']}\t{r['d']}\t{r['fd']}\t"
                     f"{r['mull'][0]}\t{r['mull'][1]}\t{int(r['samehand'])}\t{sl}\t{ca}\t{cb}\n")
    W(f"\n- full catalogue of all {len(div):,} divergent games: `{os.path.relpath(cat, ROOT)}`")

    # ---- stratified case studies
    cases = []
    for lab, nums, ca, cb in groups:                       # biggest |d| per substitution, both ways
        sub = [r for r in div if (r["seen"] & nums) and not (r["seen"] & (ALL - nums))]
        for sgn in (-1, 1):
            s = sorted([r for r in sub if r["d"]*sgn > 0], key=lambda r: -abs(r["d"]))[:2]
            cases += [(f"{ca}->{cb} ({'B faster' if sgn<0 else 'A faster'})", r) for r in s]
    cases += [("|d|=1 sample", r) for r in [r for r in div if abs(r["d"]) == 1][:6]]
    cases += [("CONTAMINATION: no substituted card drawn", r) for r in bad[:6]]
    cf = f"{OUT}/cases_{a}_vs_{b}.md"
    with open(cf, "w") as fh:
        fh.write(f"# Side-by-side case studies: {a} vs {b}\n\n")
        fh.write(f"Repro: `./build/Release/mtg \"<deck>\" --seed {SEED} --game-index <gi> --games 1"
                 f" --log-dir logs/play` then drag the JSON into `tools/replay/index.html`.\n")
        for why, r in cases:
            fh.write(f"\n\n---\n\n## gi={r['gi']} ({why})\n\n")
            fh.write(f"- **{a} wins T{r['wa']}, {b} wins T{r['wb']}** (delta {r['d']:+d}); "
                     f"first differing turn T{r['fd']}\n")
            fh.write(f"- mulligans {r['mull'][0]} vs {r['mull'][1]}; "
                     f"opening hands {'IDENTICAL' if r['samehand'] else 'DIFFER'}\n")
            fh.write(f"- substituted slots drawn: "
                     f"{', '.join(lab for lab,nums,_,_ in groups if r['seen'] & nums) or 'NONE'}\n\n")
            sa, sb = r["sig"]
            fh.write(f"| turn | {a} | {b} |\n|---|---|---|\n")
            for t in sorted(set(sa) | set(sb)):
                fa = "; ".join(f"{ty}:{c}{'('+mp+')' if mp else ''}" for ty, c, mp in sa.get(t, ()))
                fb = "; ".join(f"{ty}:{c}{'('+mp+')' if mp else ''}" for ty, c, mp in sb.get(t, ()))
                mark = "" if fa == fb else " **<-**"
                fh.write(f"| T{t}{mark} | {fa or '-'} | {fb or '-'} |\n")
    W(f"- {len(cases)} side-by-side case studies: `{os.path.relpath(cf, ROOT)}`")
    return rows


def main():
    os.makedirs(OUT, exist_ok=True)
    arms = sorted({os.path.basename(p).rsplit("_gi", 1)[0] for p in glob.glob(f"{TD}/*_gi*.json")})
    gis = sorted(int(os.path.basename(p).rsplit("_gi", 1)[1][:-5])
                 for p in glob.glob(f"{TD}/{arms[0]}_gi*.json"))
    lines = []
    W = lambda s="": lines.append(s)
    W("# Overnight Mirrorwing analysis\n")
    W(f"- arms: {', '.join(arms)}")
    W(f"- games per arm: {len(gis):,}   seed base {SEED}   max_turns {MT}")
    W(f"- apparatus: ONE pooled cell-by-arm store (keepstore.py), all arms share every cell they "
      f"can both hold\n")
    W("Delta is always (second arm - first arm) in win turns; **negative = the second arm wins "
      "sooner**.\n")
    for a, b in itertools.combinations(arms, 2):
        analyse(a, b, gis, W)
    print("\n".join(lines))

if __name__ == "__main__":
    main()
