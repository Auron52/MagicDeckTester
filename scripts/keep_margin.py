#!/usr/bin/env python3
"""How much would a keep table's cell values have to MOVE before the comparison it referees changes?

Screening plays every arm under ONE keep table, fit to the base deck. Reweighting
(`deck_compare.py`) reproduces the *weighting* half of that fit exactly and for free, but leaves the
per-cell rollout values estimated on the base deck's library. Bounding that second half by
regenerating each arm's own table is not affordable: six of the nine shipped tables were themselves
generated below R=40 (Hinata2 at R=22 over 431k cells), so a "high-R bracket" would mean generating a
BETTER table than the one we ship, once per arm, per screen.

It does not need generation. A misfit cell only costs anything if it changes a DECISION, and the two
decisions the table makes are read off the same enumeration (`KeepVal` / `ArgminSub`):

  keep/mull   threshold   regret of a flip = |KeepVal(h,m) - Dopt[m+1]|
  bottoming   argmin      regret of a flip = V(picked) - V(true best)

Both have the same asymmetry: a cell far from its threshold needs a large move to flip and won't; a
cell near it flips easily and costs nearly nothing when it does. So if no cell value moves by more
than d,

    bias(d)  <=  SUM  w(h,m) * |margin(h,m)|    over decisions with |margin| < d

which is an EXACT upper bound, computable from artifacts already committed (the raw sidecar stores
per-cell count/sum/sumsq, so V and its se are direct). Seconds per deck, no play, no rollouts, and
the cost does not grow with K.

`d` is reported in units of each cell's own sampling se as well as in turns, which makes the first
pass calibration-free: d = 1 se is the movement the shipped table ALREADY has from its own Monte
Carlo noise, so bias(1 se) is misfit we demonstrably tolerate, and an edit has to perturb values by
more than that before its bias is worse than what we ship with.

    python3 scripts/keep_margin.py decks/burn              # one deck
    python3 scripts/keep_margin.py --all                   # every deck with a committed raw
    python3 scripts/keep_margin.py decks/burn --no-verify  # skip the shipped-table check (faster)

VERIFICATION IS THE POINT OF THE FIRST HALF OF THIS FILE. `BuildPolicyFromTables` is shared by the
rollout path and the merge tool precisely so there is ONE decision rule and not two that drift; this
script is a third. So it rebuilds the shipped policy from the raw and refuses to report a bound
unless every keep flag matches (`--no-verify` to override, which you should not).
"""
import argparse
import glob
import gzip
import json
import math
import os
from array import array

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HAND = 7
SE_PRIOR = 8.0          # ExhaustiveKeepConfig::se_prior -- matches the C++ flip gate's shrink


# ---- artifacts ---------------------------------------------------------------------------------

def read_json(path):
    op = gzip.open if path.endswith(".gz") else open
    with op(path, "rt") as f:
        return json.load(f)


def deck_paths(deck_dir):
    """(decklist, raw sidecar, shipped profile) for a per-deck folder."""
    stem = os.path.basename(os.path.abspath(deck_dir))
    raw = os.path.join(deck_dir, f"{stem}.keepmodel.exhaustive.raw.json.gz")
    prof = os.path.join(deck_dir, f"{stem}.keepmodel.exhaustive.profile.json.gz")
    lst = None
    for ext in (".txt", ".cod"):
        if os.path.exists(os.path.join(deck_dir, stem + ext)):
            lst = os.path.join(deck_dir, stem + ext)
            break
    return lst, raw, prof


def deck_counts(list_path, buckets):
    """Per-bucket copy counts and mainboard size, from the decklist the raw was generated against."""
    # The sideboard must NOT reach the hypergeometric denominator. In .cod the cards carry no `zone`
    # attribute -- the zone is the enclosing <zone name="main"|"side"> element -- and in .txt the
    # sideboard follows a marker line. Getting either wrong inflates deck_size and silently rewrites
    # every hand weight (it put Knights at 68 cards and broke 46% of its replicated keep flags).
    per = {}
    if list_path.endswith(".cod"):
        import xml.etree.ElementTree as ET
        for zone in ET.parse(list_path).getroot().iter("zone"):
            if (zone.get("name") or "main") != "main":
                continue
            for card in zone.iter("card"):
                per[card.get("name")] = per.get(card.get("name"), 0) + int(card.get("number", 1))
    else:
        for line in open(list_path):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if line.lower().startswith(("sideboard", "// sideboard")):
                break
            n, name = line.split(" ", 1)
            per[name.strip()] = per.get(name.strip(), 0) + int(n)
    counts = [sum(per.get(m, 0) for m in members) for members in buckets]
    return counts, sum(per.values())


# ---- the decision rule (mirrors ExhaustiveKeep.cpp -- verified against the shipped table) --------

class SizeTable:
    """One hand-size table: composition -> (V, cnt, se) per pd. V = sum/cnt, exactly as the merge builds it."""

    def __init__(self, entries):
        self.V, self.cnt, self.se, self.index = [], [], [], {}
        # Per-size pooled variance (the shrink target the C++ flip gate uses), over cells with cnt > 1.
        vg, ng = [0.0, 0.0], [0, 0]
        for e in entries:
            for pd in (0, 1):
                c = e["count"][pd]
                if c > 1:
                    mn = e["sum"][pd] / c
                    vg[pd] += max(0.0, e["sumsq"][pd] / c - mn * mn)
                    ng[pd] += 1
        vg = [vg[pd] / ng[pd] if ng[pd] else 0.0 for pd in (0, 1)]
        self.vg = vg
        for i, e in enumerate(entries):
            comp = tuple(e["comp"])
            self.index[comp] = i
            v, cn, se = [0.0, 0.0], [0, 0], [0.0, 0.0]
            for pd in (0, 1):
                c = e["count"][pd]
                cn[pd] = c
                if c:
                    v[pd] = e["sum"][pd] / c
                    vc = max(0.0, e["sumsq"][pd] / c - v[pd] * v[pd]) if c > 1 else vg[pd]
                    # se_prior shrink: an unlucky-small sample variance must not fake confidence.
                    se[pd] = math.sqrt(max(0.0, (c * vc + SE_PRIOR * vg[pd]) / (c + SE_PRIOR)) / c)
            self.V.append(v)
            self.cnt.append(cn)
            self.se.append(se)


def subcomps(hand, target):
    """Subcompositions of `hand` of size `target`. Enumerates only the hand's NONZERO buckets (at most
    7 of them), which is what keeps this cheap at K=21 -- the C++ EnumComps walks all K."""
    nz = [(b, hand[b]) for b in range(len(hand)) if hand[b]]
    out, K = [], len(hand)

    def rec(i, left, picked):
        if left == 0:
            comp = [0] * K
            for b, x in picked:
                comp[b] = x
            out.append(tuple(comp))
            return
        if i == len(nz):
            return
        b, cap = nz[i]
        rest = sum(c for _, c in nz[i + 1:])
        for x in range(min(cap, left), -1, -1):
            if left - x <= rest:
                rec(i + 1, left - x, picked + ([(b, x)] if x else []))

    rec(0, target, [])
    return out


def keep_val(tables, hand, m, pd, subs=None):
    """min over size-(7-m) subcompositions present in the table. Returns (value, argmin_index)."""
    t = tables[m]
    best, arg = 1e9, -1
    for s in (subs if subs is not None else subcomps(hand, HAND - m)):
        i = t.index.get(s)
        if i is not None and t.V[i][pd] < best:
            best, arg = t.V[i][pd], i
    return best, arg


def keep_val_two(tables, hand, m, pd, subs=None):
    """(best, second_best, argmin_index) -- the runner-up is the bottoming argmin's flip cost."""
    t = tables[m]
    b1, b2, arg = 1e9, 1e9, -1
    for s in (subs if subs is not None else subcomps(hand, HAND - m)):
        i = t.index.get(s)
        if i is None:
            continue
        v = t.V[i][pd]
        if v < b1:
            b1, b2, arg = v, b1, i
        elif v < b2:
            b2 = v
    return b1, b2, arg


def hand_weights(tables, counts, deck_size):
    denom = math.comb(deck_size, HAND)
    P = []
    for comp in tables[0].index:
        num = 1
        for b, x in enumerate(comp):
            num *= math.comb(counts[b], x)
        P.append(num / denom)
    return P


def compute_dopt(P, max_mull, pd, kv):
    """Backward induction. Dopt[m] = expected win turn of the optimal keep policy at mull level m."""
    M = max_mull
    Dopt = [0.0] * (M + 1)
    Dopt[M] = sum(p * v for p, v in zip(P, kv[M][pd]))
    for m in range(M - 1, -1, -1):
        thr = Dopt[m + 1]
        Dopt[m] = sum(p * (v if v < thr else thr) for p, v in zip(P, kv[m][pd]))
    return Dopt


def corresponds(raw_path, prof_path):
    """Does this raw sidecar actually build this shipped profile?

    Nothing enforced it. Anti-Lifegain ships a raw at `max_mull=3` / commit 9d9f654 holding only sizes
    7-4 beside a profile at `max_mull=6` / commit 3276862 -- the raw cannot rebuild it, because the
    size-3/2/1 tables the deeper mulligans need are not in the file. Every consumer of the raw
    (reweighting, merging, this script) is silently building a DIFFERENT policy than the deck plays."""
    raw, prof = read_json(raw_path), read_json(prof_path)
    ek = prof if "buckets" in prof else prof["exhaustive_keep"]
    pm, rm = ek["max_mull"], raw["meta"]["max_mull"]
    have = {s["H"] for s in raw["sizes"]}
    need = {HAND - m for m in range(pm + 1)}
    why = []
    if rm != pm:
        why.append(f"max_mull raw={rm} profile={pm}")
    if need - have:
        why.append(f"raw lacks hand sizes {sorted(need - have)}")
    if raw["buckets"] != ek["buckets"]:
        why.append("bucketing differs")
    return (not why), "; ".join(why), raw["meta"].get("commit"), ek.get("commit")


def build(raw_path, list_path=None, counts=None, deck_size=None, max_mull=None):
    """Policy state for a deck's raw table.

    `counts`/`deck_size` may be given INSTEAD of a decklist, which is how a screen bounds an ARM: the
    shared table's cell values with the arm's hand weights, exactly the combination the arm plays
    under. That is the same substitution reweighting makes, so the two agree by construction."""
    raw = read_json(raw_path)
    buckets = raw["buckets"]
    max_mull = raw["meta"]["max_mull"] if max_mull is None else max_mull
    tables = [None] * (max_mull + 1)
    for s in raw["sizes"]:
        ti = HAND - s["H"]
        if ti <= max_mull:
            tables[ti] = SizeTable(s["entries"])
    for s in raw["sizes"]:      # SizeTable has copied what it needs; the parsed entries are the bulk
        s["entries"] = None
    if counts is None:
        counts, deck_size = deck_counts(list_path, buckets)
    comps = list(tables[0].index)
    P = hand_weights(tables, counts, deck_size)
    # One enumeration per (hand, mull level), shared by both pd -- half the work of the C++ ordering.
    # Held as flat per-(m,pd) arrays rather than a dict keyed on (i,m,pd): at Goblins' 417k size-7
    # cells the dict form is ~3.8 GB, the arrays ~120 MB.
    n = len(comps)
    kv = [[array("d", bytes(8 * n)), array("d", bytes(8 * n))] for _ in range(max_mull + 1)]
    sec = [[array("d", bytes(8 * n)), array("d", bytes(8 * n))] for _ in range(max_mull + 1)]
    arg = [[array("i", bytes(4 * n)), array("i", bytes(4 * n))] for _ in range(max_mull + 1)]
    for i, h in enumerate(comps):
        for m in range(max_mull + 1):
            subs = subcomps(h, HAND - m)
            for pd in (0, 1):
                b1, b2, a = keep_val_two(tables, h, m, pd, subs)
                kv[m][pd][i], sec[m][pd][i], arg[m][pd][i] = b1, b2, a
    Dopt = {pd: compute_dopt(P, max_mull, pd, kv) for pd in (0, 1)}
    return dict(raw=raw, buckets=buckets, tables=tables, counts=counts, deck_size=deck_size,
                comps=comps, P=P, kv=kv, sec=sec, arg=arg, Dopt=Dopt, max_mull=max_mull)


# ---- verification: this must reproduce the shipped policy exactly --------------------------------

def verify(st, prof_path):
    """Rebuild the shipped keep flags from the raw. Any mismatch means this script has drifted from
    `BuildPolicyFromTables` and its bound is describing a decision rule the engine does not run."""
    prof = read_json(prof_path)
    ek = prof if "buckets" in prof else prof["exhaustive_keep"]
    shipped = {tuple(e["comp"]): e["keep"] for e in ek["entries"]}
    M, Dopt, kv = st["max_mull"], st["Dopt"], st["kv"]
    bad = missing = 0
    for i, h in enumerate(st["comps"]):
        want = shipped.get(h)
        if want is None:
            missing += 1
            continue
        for pd in (0, 1):
            for m in range(M + 1):
                got = 1 if (m == M or kv[m][pd][i] <= Dopt[pd][m + 1]) else 0
                if got != want[m * 2 + pd]:
                    bad += 1
    return dict(cells=len(st["comps"]), shipped_cells=len(shipped), mismatched_flags=bad,
                missing=missing, R=ek.get("effective_R"), bottoming=ek.get("bottoming_enabled"))


# ---- the bound ----------------------------------------------------------------------------------

class Curve:
    """Streaming accumulator for bias(d) = SUM w*|margin| over decisions with |margin| < d.

    Accumulated as the decisions are enumerated rather than collected first: at Goblins' scale there
    are 5.8M of them and a list of tuples costs GBs, while the histogram is a few floats. `se_grid`
    thresholds at d*se(cell) (calibration-free -- the cell's own sampling noise is the unit);
    `turn_grid` thresholds at a flat d (the unit a screen's effect size is measured in)."""

    def __init__(self, se_grid, turn_grid):
        self.se_grid, self.turn_grid = se_grid, turn_grid
        self.se_bias, self.se_mass = [0.0] * len(se_grid), [0.0] * len(se_grid)
        self.t_bias, self.t_mass = [0.0] * len(turn_grid), [0.0] * len(turn_grid)
        self.total = 0.0
        self.n = 0

    def add(self, w, marg, se):
        self.total += w
        self.n += 1
        wm = w * marg
        for j, d in enumerate(self.se_grid):
            if marg < d * se:
                self.se_bias[j] += wm
                self.se_mass[j] += w
        for j, d in enumerate(self.turn_grid):
            if marg < d:
                self.t_bias[j] += wm
                self.t_mass[j] += w

    def rows(self):
        tot = self.total or 1.0
        return ([(d, b, m / tot) for d, b, m in zip(self.se_grid, self.se_bias, self.se_mass)],
                [(d, b, m / tot) for d, b, m in zip(self.turn_grid, self.t_bias, self.t_mass)])


def margins(st, pd, se_grid, turn_grid):
    """Every decision the table makes, with its probability of being reached and its flip cost.

    Reach probability: at mull level m the hand is redrawn from the whole deck, so the composition
    distribution is the same at every level and reach[m+1] = reach[m] * (mulligan rate at m).

    Two decision families:
      keep   -- taken at every level reached;      flip cost |KeepVal - Dopt[m+1]|
      bottom -- taken only when the hand is KEPT;  flip cost (runner-up - best) among subcomps
    """
    M, Dopt, P = st["max_mull"], st["Dopt"][pd], st["P"]
    tables, KV, SEC, ARG = st["tables"], st["kv"], st["sec"], st["arg"]
    keep_dec, bot_dec = Curve(se_grid, turn_grid), Curve(se_grid, turn_grid)
    reach = 1.0
    for m in range(M + 1):
        if reach <= 0.0:
            break
        kvm, secm, argm, se_m, thr = KV[m][pd], SEC[m][pd], ARG[m][pd], tables[m].se, Dopt[m + 1] if m < M else None
        mull_mass = 0.0
        for i in range(len(st["comps"])):
            kv, second, arg = kvm[i], secm[i], argm[i]
            w = P[i] * reach
            kept = (m == M) or (kv <= thr)
            if m < M:
                keep_dec.add(w, abs(kv - thr), se_m[arg][pd] if arg >= 0 else 0.0)
                if not kept:
                    mull_mass += P[i]
            if kept and m > 0 and arg >= 0 and second < 1e9:
                # A bottoming choice only exists once cards must go on the bottom (m > 0).
                bot_dec.add(w, second - kv, se_m[arg][pd])
        reach *= mull_mass
    return keep_dec, bot_dec


SE_GRID = (0.5, 1.0, 2.0, 3.0)
TURN_GRID = (0.01, 0.02, 0.05, 0.10, 0.25)


def bound_for(raw_path, counts, deck_size, deltas=TURN_GRID, max_mull=None):
    """-> {delta: total bias bound in turns} for a deck (or arm) with these bucket counts.

    The public entry point for callers that already know the counts -- `deck_compare.py` uses it to
    print a screen's rollout-half bound beside its reweight floor. Sums the keep and bottoming halves
    and takes the worse of play/draw, which is the conservative reading."""
    st = build(raw_path, counts=counts, deck_size=deck_size, max_mull=max_mull)
    out = {}
    for pd in (0, 1):
        keep_dec, bot_dec = margins(st, pd, SE_GRID, deltas)
        _, kt = keep_dec.rows()
        _, bt = bot_dec.rows()
        for (d, kb, _), (_, bb, _) in zip(kt, bt):
            out[d] = max(out.get(d, 0.0), kb + bb)
    return out


def report(deck_dir, do_verify=True):
    lst, raw_path, prof_path = deck_paths(deck_dir)
    name = os.path.basename(os.path.abspath(deck_dir))
    if not (lst and os.path.exists(raw_path)):
        print(f"{name}: no committed raw sidecar -- skipped")
        return None
    ok, why, rc, pc = corresponds(raw_path, prof_path)
    if not ok:
        print(f"\n=== {name} ===  ARTIFACT MISMATCH: the committed raw does not build the committed "
              f"profile\n  {why}\n  raw commit {rc}, profile commit {pc}\n"
              f"  Every consumer of this raw -- reweighting, merging, this script -- builds a "
              f"policy the deck does not play.")
        return None
    st = build(raw_path, lst)
    K, R = len(st["buckets"]), st["raw"]["meta"].get("R")
    print(f"\n=== {name} ===  K={K}  R={R}  cells(size-7)={len(st['comps']):,}  "
          f"deck={st['deck_size']}  max_mull={st['max_mull']}")
    if do_verify:
        v = verify(st, prof_path)
        ok = v["mismatched_flags"] == 0 and v["missing"] == 0
        print(f"  policy replication vs shipped table: "
              f"{'EXACT' if ok else 'MISMATCH'} "
              f"({v['cells']:,} cells, {v['mismatched_flags']} flag diffs, {v['missing']} missing)")
        if not ok:
            print("  REFUSING to report a bound -- this script does not reproduce the engine's rule.")
            return None
    rows = {}
    for pd, lbl in ((1, "play"), (0, "draw")):
        keep_dec, bot_dec = margins(st, pd, SE_GRID, TURN_GRID)
        keep_se, keep_t = keep_dec.rows()
        bot_se, bot_t = bot_dec.rows()
        rows[lbl] = dict(keep_se=keep_se, keep_t=keep_t, bot_se=bot_se, bot_t=bot_t,
                         dopt=st["Dopt"][pd][0], n_keep=keep_dec.n, n_bot=bot_dec.n)
    for lbl in ("play", "draw"):
        r = rows[lbl]
        print(f"\n  [{lbl}]  D_opt={r['dopt']:.4f}")
        print(f"    {'perturbation':>14s} | {'keep/mull bound':>16s} {'mass':>7s} | "
              f"{'bottoming bound':>16s} {'mass':>7s}")
        for (d, kb, km), (_, bb, bm) in zip(r["keep_se"], r["bot_se"]):
            print(f"    {d:11.1f} se | {kb:16.5f} {km:6.1%} | {bb:16.5f} {bm:6.1%}")
        for (d, kb, km), (_, bb, bm) in zip(r["keep_t"], r["bot_t"]):
            print(f"    {d:11.3f} t  | {kb:16.5f} {km:6.1%} | {bb:16.5f} {bm:6.1%}")
    return dict(deck=name, K=K, R=R, cells=len(st["comps"]), rows=rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("decks", nargs="*")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--no-verify", action="store_true")
    ap.add_argument("--json", help="write the curves here")
    a = ap.parse_args()
    dirs = a.decks
    if a.all:
        dirs = sorted(os.path.dirname(p) for p in
                      glob.glob(os.path.join(ROOT, "decks/*/*.keepmodel.exhaustive.raw.json.gz")))
    if not dirs:
        ap.error("name a deck folder or pass --all")
    out = [r for r in (report(d, not a.no_verify) for d in dirs) if r]
    if a.json:
        json.dump(out, open(a.json, "w"), indent=1)
        print(f"\nwrote {a.json}")


if __name__ == "__main__":
    main()
