#!/usr/bin/env python3
"""Does the FAST standalone model rank card-swap A/B DELTAS like the NC teacher?

The clairvoyance-abuse A/B goal never needs absolute LP — only the SIGN/ORDER of LP(variant)-LP(base).
A constant model bias (~0.4-0.7 LP) cancels in that delta. This measures, over a family of legal
within-deck swaps, whether the cheap policies' delta-ranking tracks the teacher's:
  - heuristic d0            (control: zero model)
  - land-fold DYN d0        (the best standalone model, FIXED = trained once on the BASE deck)
  - NC teacher K16 d2       (ground truth ranking)

  scripts/ab_delta.py gen                 # write variant .cod files
  scripts/ab_delta.py run [--games N]     # resumable; append LP per (variant,policy,seed)
  scripts/ab_delta.py analyze             # Spearman / sign-agreement / top-pick vs teacher

Model is FIXED across variants on purpose: the realistic screen is "I have a model for my deck, I tweak
one card, can the cheap model tell me if the tweak helped WITHOUT retraining or running the slow teacher?"
"""
import argparse, json, os, re, subprocess, sys, time
from collections import OrderedDict

ROOT = "/workspaces/MagicDeckTester2"
MTG = f"{ROOT}/build/Release/mtg"
MT = 8
SEEDS = [11111, 22222, 33333]

# ---- deck registry: each deck = (dir, fixed-model, basic-land, BASE counts, VARIANTS) --------------
ANTILIFE_BASE = OrderedDict([
    ("Swords to Plowshares", 3), ("Invigorate", 4), ("Skyshroud Cutter", 4), ("Tainted Remedy", 4),
    ("Forest", 1), ("Temple Garden", 1), ("Overgrown Tomb", 1), ("Windswept Heath", 4),
    ("Birds of Paradise", 4), ("Marsh Flats", 2), ("Bloodstained Mire", 3), ("Reverent Silence", 4),
    ("Aria of Flame", 4), ("Grove of the Burnwillows", 2), ("Stomping Ground", 1), ("Blood Crypt", 1),
    ("Fiery Justice", 2), ("Idyllic Tutor", 2), ("Wooded Foothills", 3), ("Godless Shrine", 1),
    ("Plague Drone", 4), ("Ignoble Hierarch", 4), ("Enlightened Tutor", 1),
])
ANTILIFE_VARIANTS = OrderedDict([
    ("v00_base", {}),
    ("v01_flood", {"Forest": +4, "Idyllic Tutor": -2, "Fiery Justice": -1, "Enlightened Tutor": -1}),
    ("v02_lean", {"Windswept Heath": -1, "Bloodstained Mire": -1, "Wooded Foothills": -1,
                  "Swords to Plowshares": +1, "Fiery Justice": +1, "Idyllic Tutor": +1}),
    ("v03_no_birds", {"Birds of Paradise": -4, "Forest": +4}),
    ("v04_half_hierarch", {"Ignoble Hierarch": -2, "Forest": +2}),
    ("v05_more_tutors", {"Idyllic Tutor": +2, "Enlightened Tutor": +2, "Reverent Silence": -4}),
    ("v06_no_reverent", {"Reverent Silence": -4, "Forest": +4}),
    ("v07_no_cutter", {"Skyshroud Cutter": -4, "Forest": +4}),
    ("v08_max_removal", {"Swords to Plowshares": +1, "Fiery Justice": +2,
                         "Reverent Silence": -1, "Skyshroud Cutter": -2}),
    ("v09_double_enl", {"Enlightened Tutor": +3, "Reverent Silence": -3}),
    ("v10_no_drone", {"Plague Drone": -4, "Forest": +4}),
])
TH_BASE = OrderedDict([
    ("Reliquary Tower", 4), ("Island", 4), ("Saprazzan Skerry", 4), ("Sandstone Needle", 4),
    ("Cascade Bluffs", 4), ("Fiery Islet", 4), ("Temple of Epiphany", 4), ("Frostboil Snarl", 4),
    ("Steam Vents", 4), ("Ferrous Lake", 4), ("Lonely Sandbar", 4), ("Forgotten Cave", 4),
    ("Thundering Falls", 4), ("Remote Isle", 1), ("Treasure Hunt", 4), ("Land's Edge", 2),
    ("Throes of Chaos", 1),
])
TH_VARIANTS = OrderedDict([
    ("v00_base", {}),
    ("v01_no_tower", {"Reliquary Tower": -4, "Island": +4}),          # lose no-max-hand for big draws
    ("v02_max_edge", {"Land's Edge": +2, "Lonely Sandbar": -2}),      # more reach/wincon
    ("v03_no_edge", {"Land's Edge": -2, "Island": +2}),               # drop the wincon
    ("v04_max_throes", {"Throes of Chaos": +3, "Ferrous Lake": -3}),  # more spells, fewer lands
    ("v05_no_cyclers", {"Lonely Sandbar": -4, "Forgotten Cave": -4, "Island": +8}),  # lose card-filter
    ("v06_fewer_tower", {"Reliquary Tower": -2, "Island": +2}),       # milder tower cut
    ("v07_edge_throes", {"Land's Edge": +2, "Throes of Chaos": +1, "Steam Vents": -3}),
    ("v08_dual_to_island", {"Frostboil Snarl": -4, "Island": +4}),   # fixing->mono-U (near-neutral test)
    ("v09_edge_over_cave", {"Land's Edge": +2, "Forgotten Cave": -2}),
    ("v10_more_islands", {"Sandstone Needle": -4, "Island": +4}),     # cut a sac-land for basics
])
DECKS = {
    "antilife": dict(dir=f"{ROOT}/logs/model_improve/ab", model="/tmp/antilife_base.dyn",
                     basic="Forest", base=ANTILIFE_BASE, variants=ANTILIFE_VARIANTS),
    "TH": dict(dir=f"{ROOT}/logs/model_improve/ab_TH", model="/tmp/TH_base.dyn",
               basic="Island", base=TH_BASE, variants=TH_VARIANTS),
}
# module globals bound by configure(); antilife defaults keep the in-flight run's paths byte-identical.
ABDIR = DECKS["antilife"]["dir"]
CACHE = f"{ABDIR}/results.json"
MODEL = DECKS["antilife"]["model"]
BASIC = "Forest"
BASE = ANTILIFE_BASE
VARIANTS = ANTILIFE_VARIANTS
POLICIES = OrderedDict()


def configure(deck):
    global ABDIR, CACHE, MODEL, BASIC, BASE, VARIANTS, POLICIES
    d = DECKS[deck]
    ABDIR = d["dir"]; CACHE = f"{ABDIR}/results.json"; MODEL = d["model"]
    BASIC = d["basic"]; BASE = d["base"]; VARIANTS = d["variants"]
    POLICIES = OrderedDict([
        ("heuristic", (0, {})),
        ("model",     (0, {"MTG_D0_LANDFOLD": "1", "MTG_D0LF_K": "16", "MTG_DYN_MODEL": MODEL})),
        ("teacher",   (1, {"MTG_NC_SEARCH": "1", "MTG_NC_K": "16", "MTG_NC_DEPTH": "2"})),
    ])


def build_counts(name):
    c = OrderedDict(BASE)
    for k, d in VARIANTS[name].items():
        c[k] = c[k] + d
    assert sum(c.values()) == 60, (name, sum(c.values()))
    for k, v in c.items():
        assert v >= 0, (name, k, v)
        assert v <= 4 or k == BASIC, (name, k, v)  # only the basic land may exceed 4
    return c


def gen():
    os.makedirs(ABDIR, exist_ok=True)
    for name in VARIANTS:
        c = build_counts(name)
        rows = "\n".join(f'        <card number="{n}" name="{k}"/>' for k, n in c.items() if n > 0)
        xml = ('<?xml version="1.0" encoding="UTF-8"?>\n<cockatrice_deck version="1">\n'
               f'    <deckname>{name}</deckname>\n    <comments></comments>\n'
               f'    <zone name="main">\n{rows}\n    </zone>\n</cockatrice_deck>\n')
        with open(f"{ABDIR}/{name}.cod", "w") as f:
            f.write(xml)
        d = ", ".join(f"{k} {'+' if v>0 else ''}{v}" for k, v in VARIANTS[name].items()) or "(base)"
        print(f"  {name:20s} {d}")
    print(f"wrote {len(VARIANTS)} variants -> {ABDIR}")


def load_cache():
    if os.path.exists(CACHE):
        return json.load(open(CACHE))
    return {}


def run_one(deck, depth, extra, games, seed):
    env = {k: v for k, v in os.environ.items() if not k.startswith("MTG_")}
    env.update(extra)
    out = subprocess.run([MTG, deck, "--games", str(games), "--seed", str(seed), "--depth", str(depth),
                          "--max-turns", str(MT), "--threads", "12"],
                         capture_output=True, text=True, env=env).stdout
    p = int(re.search(r"played\s*:\s*(\d+)", out).group(1))
    w = int(re.search(r"won\s*:\s*(\d+)", out).group(1))
    m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out)
    a = float(m.group(1)) if m else 0.0
    return {"p": p, "w": w, "a": a, "lp": (w * a + (p - w) * (MT + 1)) / p}


def run(games):
    cache = load_cache()
    total = len(VARIANTS) * len(POLICIES) * len(SEEDS)
    done = 0
    t0 = time.time()
    for vname in VARIANTS:
        deck = f"{ABDIR}/{vname}.cod"
        for pname, (depth, extra) in POLICIES.items():
            for seed in SEEDS:
                key = f"{vname}|{pname}|{seed}|{games}"
                if key in cache:
                    done += 1
                    continue
                r = run_one(deck, depth, extra, games, seed)
                cache[key] = r
                json.dump(cache, open(CACHE, "w"))
                done += 1
                el = time.time() - t0
                print(f"[{done:3d}/{total}] {vname:18s} {pname:9s} s{seed} "
                      f"LP={r['lp']:.3f} ({r['w']}/{r['p']})  {el:.0f}s", flush=True)
    print(f"RUN DONE {time.time()-t0:.0f}s")


def per_seed(cache, vname, pname, games):
    """dict seed->LP for this (variant,policy)."""
    out = {}
    for s in SEEDS:
        k = f"{vname}|{pname}|{s}|{games}"
        if k in cache:
            out[s] = cache[k]["lp"]
    return out


def agg(cache, vname, pname, games):
    ps = per_seed(cache, vname, pname, games)
    return sum(ps.values()) / len(ps) if ps else None


def mean_se(xs):
    n = len(xs)
    m = sum(xs) / n
    if n < 2:
        return m, 0.0
    var = sum((x - m) ** 2 for x in xs) / (n - 1)
    return m, (var / n) ** 0.5  # standard error of the mean


def spearman(xs, ys):
    def ranks(v):
        order = sorted(range(len(v)), key=lambda i: v[i])
        r = [0.0] * len(v)
        i = 0
        while i < len(v):
            j = i
            while j + 1 < len(v) and v[order[j + 1]] == v[order[i]]:
                j += 1
            avg = (i + j) / 2.0 + 1
            for k in range(i, j + 1):
                r[order[k]] = avg
            i = j + 1
        return r
    rx, ry = ranks(xs), ranks(ys)
    n = len(xs)
    mx, my = sum(rx) / n, sum(ry) / n
    cov = sum((rx[i] - mx) * (ry[i] - my) for i in range(n))
    vx = sum((rx[i] - mx) ** 2 for i in range(n)) ** 0.5
    vy = sum((ry[i] - my) ** 2 for i in range(n)) ** 0.5
    return cov / (vx * vy) if vx and vy else float("nan")


def analyze(games, noise):
    cache = load_cache()
    names = list(VARIANTS)
    variants = [v for v in names if v != "v00_base"]
    # per-seed LP: lp[policy][variant][seed]
    lp = {p: {v: per_seed(cache, v, p, games) for v in names} for p in POLICIES}
    miss = [(v, p) for p in POLICIES for v in names if len(lp[p][v]) < len(SEEDS)]
    if miss:
        got = sum(len(lp[p][v]) for p in POLICIES for v in names)
        print(f"incomplete cache ({got}/{len(POLICIES)*len(names)*len(SEEDS)} cells) -- run more first")
        print("missing:", miss[:8], "..." if len(miss) > 8 else "")
        return
    absmean = {p: {v: agg(cache, v, p, games) for v in names} for p in POLICIES}

    # PAIRED-by-seed delta vs base: per seed (variant - base), then mean+SE across seeds.
    delta = {p: {} for p in POLICIES}      # v -> (mean, se)
    for p in POLICIES:
        for v in variants:
            ds = [lp[p][v][s] - lp[p]["v00_base"][s] for s in SEEDS]
            delta[p][v] = mean_se(ds)

    # significance threshold from the TEACHER's own per-seed spread (paired delta SE), floored.
    tse = max([delta["teacher"][v][1] for v in variants] + [0.0])
    thr = max(noise, 2 * tse)

    print(f"\n===== A/B DELTA AGREEMENT  (games/seed={games}, seeds={SEEDS}) =====")
    print(f"significance threshold |dLP_teacher| > {thr:.3f}  (max teacher paired-SE={tse:.3f})")
    print("\nabsolute LP (lower=faster):")
    print(f"  {'variant':20s} {'heuristic':>10s} {'model':>10s} {'teacher':>10s}")
    for v in names:
        print(f"  {v:20s} {absmean['heuristic'][v]:10.3f} {absmean['model'][v]:10.3f} {absmean['teacher'][v]:10.3f}")

    print("\nPAIRED delta vs base (dLP +/-SE; +=worse):")
    print(f"  {'variant':20s} {'heuristic':>13s} {'model':>13s} {'teacher':>13s}  sig?")
    for v in variants:
        dh, dm, dt = delta["heuristic"][v], delta["model"][v], delta["teacher"][v]
        sig = "***" if abs(dt[0]) > thr else "   "
        print(f"  {v:20s} {dh[0]:+7.3f}+-{dh[1]:.3f} {dm[0]:+7.3f}+-{dm[1]:.3f} "
              f"{dt[0]:+7.3f}+-{dt[1]:.3f}  {sig}")

    dteach = [delta["teacher"][v][0] for v in variants]

    def report(name):
        dvec = [delta[name][v][0] for v in variants]
        rho = spearman(dvec, dteach)
        sig_idx = [i for i, v in enumerate(variants) if abs(dteach[i]) > thr]
        sgn = sum(1 for i in sig_idx if (dvec[i] > 0) == (dteach[i] > 0))
        # pairwise order over ALL variant pairs where teacher gap is significant
        alln = names
        pv = [absmean[name][v] for v in alln]
        pt = [absmean["teacher"][v] for v in alln]
        agree = tot = 0
        for i in range(len(alln)):
            for j in range(i + 1, len(alln)):
                if abs(pt[i] - pt[j]) <= thr:
                    continue
                tot += 1
                if (pv[i] < pv[j]) == (pt[i] < pt[j]):
                    agree += 1
        best_p = min(alln, key=lambda v: absmean[name][v])
        best_t = min(alln, key=lambda v: absmean["teacher"][v])
        print(f"\n  {name.upper()} vs teacher:")
        print(f"    Spearman rho(delta)              = {rho:+.3f}")
        print(f"    sign-agreement (|dteacher|>{thr:.2f})  = {sgn}/{len(sig_idx)}"
              + ("" if sig_idx else "  (no significant variants!)"))
        if tot:
            print(f"    pairwise-order agreement         = {agree}/{tot} = {agree/tot*100:.0f}%")
        print(f"    best pick: {name}->{best_p}  teacher->{best_t}  {'MATCH' if best_p==best_t else 'MISS'}")

    report("heuristic")
    report("model")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["gen", "run", "analyze"])
    ap.add_argument("--deck", default="antilife", choices=list(DECKS))
    ap.add_argument("--games", type=int, default=80)
    ap.add_argument("--noise", type=float, default=0.06)
    a = ap.parse_args()
    configure(a.deck)
    if a.cmd == "gen":
        gen()
    elif a.cmd == "run":
        run(a.games)
    else:
        analyze(a.games, a.noise)
