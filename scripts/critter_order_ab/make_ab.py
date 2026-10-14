#!/usr/bin/env python3
"""Paired A/B for the USER-settled CritterLifegain cast order (MTG_CRITTER_ORDER).

  python3 make_ab.py build --out DIR --arm base|new [--scale 1.0] [--hatch]
      -> DIR/manifest.json  (ONE pooled batch per binary: critter + critter2hg x 3 cells, plus every other
         suite deck at d0 / d3 b10 / shipped for the identity check; --hatch adds the new binary's
         MTG_CRITTER_ORDER=0 jobs on the first HATCH games of each critter cell)
  python3 make_ab.py analyse BASE_DIR NEW_DIR

Cells: ship = no depth / no budget key (the deck's play settings: d5 b20, value leaf + keep table via the
profile), d3 = --depth 3 --budget-ms 10, d0 = --depth 0. Seeds held out from 11200001:
  seed(game gi of deck k, cell c) = 11200001 + 100000 * k + 30000 * c + gi   (critter k=0, critter2hg k=1)
  identity decks: 11500001 + 1000 * deck_index + 300 * c + gi.
A job is a CHUNK of 50 consecutive games (seed = base + chunk start, game_index = chunk start), so every
game's identity is base + gi and chunk digests pair across arms. Unwon scores 9 (max_turns 8).
"""
import json, math, os, re, sys
from collections import defaultdict

WT = "/workspaces/MagicDeckTester/.claude/worktrees/agent-a239469ed898c5c26"
CRITTER = ("decks/CritterLifegain/CritterLifegain.cod", "decks/CritterLifegain/CritterLifegain.profile.json")
CELLS = [("ship", None, None), ("d3", 3, 10), ("d0", 0, 0)]
CELL_IX = {c[0]: i for i, c in enumerate(CELLS)}
SIZES = {"critter": {"ship": 4000, "d3": 6000, "d0": 20000},
         "critter2hg": {"ship": 2000, "d3": 3000, "d0": 10000}}
HATCH = {"ship": 400, "d3": 600, "d0": 2000}       # critter; critter2hg gets half
IDENT_GAMES = {"ship": 20, "d3": 20, "d0": 40}
CHUNK = 50


def suite_decks():
    """(key, deck, profile) for every suite deck but critter, from test/regression_cases.sh's maps."""
    txt = open(os.path.join(WT, "test/regression_cases.sh")).read()
    def amap(name):
        m = re.search(r"declare -A %s=\((.*?)\n\)" % name, txt, re.S)
        out = {}
        for line in m.group(1).splitlines():
            line = line.strip()
            mm = re.match(r'\[(\w+)\]=("?)(.*?)\2\s*$', line)
            if mm:
                out[mm.group(1)] = mm.group(3)
        return out
    files, profs = amap("DECK_FILE"), amap("DECK_PROF")
    return [(k, files[k], profs[k]) for k in files if k != "critter"]


PRIMARY = "/workspaces/MagicDeckTester"
KT = os.path.expanduser("~/.cache/mtg/keeptable")


def kt_name(canon):
    h = 1469598103934665603
    for c in canon.encode():
        h ^= c
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    stem = "".join(c if (c.isalnum() or c in "._-") else "_" for c in os.path.basename(canon))[:64]
    return "%s-%016x.keeptable" % (stem, h)


def root_for(prof):
    """The identity decks read the checkout whose keep table is already cached (the sidecars are
    byte-identical between the primary checkout and this worktree -- only three-odd .cod files differ, by
    CRLF), so a pooled batch does not spend its parse rebuilding 700 MB tables. Both arms use the SAME
    root per deck, which is all identity needs."""
    side = prof.replace(".profile.json", ".keepmodel.exhaustive.profile.json.gz")
    if os.path.exists(os.path.join(KT, kt_name(os.path.realpath(os.path.join(WT, side))))):
        return WT
    if os.path.exists(os.path.join(KT, kt_name(os.path.realpath(os.path.join(PRIMARY, side))))):
        return PRIMARY
    return WT


def job(name, deck, prof, games, seed, gi0, depth, budget, extra=None, flags=None, root=WT):
    j = {"name": name, "deck": os.path.join(root, deck), "profile": os.path.join(root, prof),
         "games": games, "seed": seed, "game_index": gi0}
    if depth is not None:
        j["depth"] = depth
        j["budget_ms"] = budget
        j["ignore_play_profile"] = True
    if extra:
        j.update(extra)
    if flags:
        j["flags"] = flags
    return j


def build(out, arm, scale, hatch):
    jobs = []
    # The slowest cells first so their tails overlap the cheap ones.
    for cname, depth, bud in CELLS:
        for k, key in enumerate(["critter", "critter2hg"]):
            extra = {"starting_life": 30, "opponent_heads": 2} if key == "critter2hg" else None
            n = max(CHUNK, int(SIZES[key][cname] * scale))
            base = 11200001 + 100000 * k + 30000 * CELL_IX[cname]
            for c in range(0, n, CHUNK):
                jobs.append(job("%s_%s_%s.c%d" % (key, cname, arm, c), CRITTER[0], CRITTER[1],
                                min(CHUNK, n - c), base + c, c, depth, bud, extra))
            if hatch:
                nh = HATCH[cname] if key == "critter" else HATCH[cname] // 2
                for c in range(0, nh, CHUNK):
                    jobs.append(job("%s_%s_hatch.c%d" % (key, cname, c), CRITTER[0], CRITTER[1],
                                    min(CHUNK, nh - c), base + c, c, depth, bud, extra,
                                    {"MTG_CRITTER_ORDER": False}))
    for di, (key, deck, prof) in enumerate(suite_decks()):
        root = root_for(prof)
        for cname, depth, bud in CELLS:
            n = IDENT_GAMES[cname]
            base = 11500001 + 1000 * di + 300 * CELL_IX[cname]
            jobs.append(job("ident-%s_%s_%s.c0" % (key, cname, arm), deck, prof, n, base, 0, depth, bud,
                            root=root))
    # The identity decks' searched jobs are the slowest single jobs: schedule them FIRST so their tails
    # overlap the many short critter chunks instead of ending the batch.
    jobs.sort(key=lambda j: (0 if j["name"].startswith("ident-") and j.get("depth", 5) > 0 else 1))
    os.makedirs(out, exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(out, "manifest.json"), "w"), indent=1)
    print("wrote %s/manifest.json: %d jobs, %d games" % (out, len(jobs), sum(j["games"] for j in jobs)))


WIN = re.compile(r"\[win\] job=(\S+) gi=(\d+) wt=(-?\d+)(?:.*digest=([0-9a-f]+))?")
JOB = re.compile(r"^(\S+): played=(\d+) avg=(\S+) digest=(\S+) ms=(\d+) units=(\d+)")


def load(d):
    wins = defaultdict(dict)      # job -> gi -> wt (9 = unwon)
    for line in open(os.path.join(d, "batch.err"), errors="ignore"):
        m = WIN.search(line)
        if m:
            wt = int(m.group(3))
            wins[m.group(1)][int(m.group(2))] = 9 if wt < 0 else wt
    jobs = {}
    for line in open(os.path.join(d, "batch.out"), errors="ignore"):
        m = JOB.match(line.strip())
        if m:
            jobs[m.group(1)] = {"played": int(m.group(2)), "avg": m.group(3), "digest": m.group(4),
                                "ms": int(m.group(5)), "units": int(m.group(6))}
    return wins, jobs


def split(name):
    # <key>_<cell>_<arm>.c<k>  /  ident-<key>_<cell>_<arm>.c0
    m = re.match(r"^(ident-)?(.+)_(ship|d3|d0)_([a-z0-9]+)\.c(\d+)$", name)
    return (m.group(1) is not None, m.group(2), m.group(3), m.group(4), int(m.group(5))) if m else None


def seed_of(key, cell, gi):
    k = 0 if key == "critter" else 1
    return 11200001 + 100000 * k + 30000 * CELL_IX[cell] + gi


def analyse(bdir, ndir):
    bw, bj = load(bdir)
    nw, nj = load(ndir)
    def collect(wins, jobs, arm):
        per = defaultdict(dict)    # (key, cell) -> gi -> wt
        cost = defaultdict(lambda: [0, 0])
        dig = {}
        for name, g in wins.items():
            sp = split(name)
            if not sp or sp[0] or sp[3] != arm:
                continue
            for gi, wt in g.items():
                per[(sp[1], sp[2])][gi] = wt
        for name, j in jobs.items():
            sp = split(name)
            if not sp or sp[3] != arm:
                continue
            if not sp[0]:
                c = cost[(sp[1], sp[2])]
                c[0] += j["ms"]; c[1] += j["units"]
            dig[(sp[0], sp[1], sp[2], sp[4])] = (j["digest"], j["avg"], j["units"])
        return per, cost, dig
    bper, bcost, bdig = collect(bw, bj, "base")
    nper, ncost, ndig = collect(nw, nj, "new")
    hper, hcost, hdig = collect(nw, nj, "hatch")
    print("A/B: new (MTG_CRITTER_ORDER default ON) vs base (ff3e205b), paired per game; unwon = 9")
    print("%-11s %-5s %6s %8s %8s %9s %7s %6s %6s %6s %9s %9s" % ("deck", "cell", "n", "base", "new", "new-base",
          "t", "faster", "slower", "net", "units x", "ms x"))
    slower, faster = [], []
    for key in ("critter", "critter2hg"):
        for cell in ("ship", "d3", "d0"):
            b, n = bper.get((key, cell), {}), nper.get((key, cell), {})
            gis = sorted(set(b) & set(n))
            if not gis:
                continue
            dd = [n[g] - b[g] for g in gis]
            N = len(dd)
            mean = sum(dd) / N
            var = sum((v - mean) ** 2 for v in dd) / max(1, N - 1)
            se = math.sqrt(var / N) if N > 1 else 0.0
            cb, cn = bcost[(key, cell)], ncost[(key, cell)]
            print("%-11s %-5s %6d %8.4f %8.4f %+9.5f %7.2f %6d %6d %+6d %9s %9s" % (
                key, cell, N, sum(b[g] for g in gis) / N, sum(n[g] for g in gis) / N, mean,
                mean / se if se > 0 else 0.0, sum(1 for v in dd if v < 0), sum(1 for v in dd if v > 0), sum(dd),
                ("%.3f" % (cn[1] / cb[1])) if cb[1] else "-", ("%.3f" % (cn[0] / cb[0])) if cb[0] else "-"))
            print("%-11s %-5s units base=%d new=%d" % ("", "", cb[1], cn[1]))
            for g in gis:
                if n[g] > b[g]:
                    slower.append((key, cell, g, b[g], n[g], seed_of(key, cell, g)))
                elif n[g] < b[g]:
                    faster.append((key, cell, g, b[g], n[g], seed_of(key, cell, g)))
    # Hatch identity: new binary with MTG_CRITTER_ORDER=0 vs base, same chunks.
    tot = same = 0
    diffs = []
    for (ident, key, cell, c), v in hdig.items():
        bv = bdig.get((False, key, cell, c))
        if bv is None:
            continue
        tot += 1
        if bv[0] == v[0]:
            same += 1
        else:
            diffs.append((key, cell, c, bv, v))
    gsame = gtot = 0
    for (key, cell), g in hper.items():
        b = bper.get((key, cell), {})
        for gi, wt in g.items():
            if gi in b:
                gtot += 1
                gsame += (b[gi] == wt)
    print("\nHATCH (new binary, MTG_CRITTER_ORDER=0) vs base: %d/%d chunk digests identical, %d/%d games same win turn"
          % (same, tot, gsame, gtot))
    for d in diffs[:20]:
        print("   HATCH DIFF", d)
    # Identity decks: per-job digest + avg.
    itot = isame = 0
    idiff = []
    ucmp = []
    for (ident, key, cell, c), v in ndig.items():
        if not ident:
            continue
        bv = bdig.get((True, key, cell, c))
        if bv is None:
            idiff.append((key, cell, "missing in base"))
            continue
        itot += 1
        if bv[0] == v[0] and bv[1] == v[1]:
            isame += 1
        else:
            idiff.append((key, cell, bv, v))
        if bv[2] != v[2]:
            ucmp.append((key, cell, bv[2], v[2]))
    print("\nIDENTITY (every other suite deck, base vs new): %d/%d jobs identical (digest + avg)" % (isame, itot))
    for d in idiff:
        print("   IDENT DIFF", d)
    for u in ucmp:
        print("   units differ (play identical?)", u)
    print("\nEVERY SLOWER game (deck cell gi base new seed):")
    for s in slower:
        print("  %-11s %-5s gi=%-6d base=%d new=%d seed=%d" % s)
    print("\nfaster: %d games" % len(faster))
    json.dump({"slower": slower, "faster": faster}, open(os.path.join(ndir, "moved.json"), "w"))


if __name__ == "__main__":
    if sys.argv[1] == "build":
        a = sys.argv[2:]
        out = a[a.index("--out") + 1]
        arm = a[a.index("--arm") + 1]
        scale = float(a[a.index("--scale") + 1]) if "--scale" in a else 1.0
        build(out, arm, scale, "--hatch" in a)
    elif sys.argv[1] == "analyse":
        analyse(sys.argv[2], sys.argv[3])
