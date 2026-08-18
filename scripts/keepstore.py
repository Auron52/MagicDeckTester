#!/usr/bin/env python3
"""Cell x arm keep-rollout store: the shared apparatus for comparing deck versions.

WHY THIS EXISTS
---------------
Comparing two deck versions wants ONE shared mulligan apparatus (sharing a table halves the
standard error -- see .claude/skills/deck-screening.md), but the versions do not hold the same
cards, so no single per-deck table covers both. The naive answers are all worse:

  * a UNION table over the combined bucket space enumerates hands NO arm can hold (measured 31.7%
    of its cells for Mirrorwing base vs the trick suite) -- pure waste;
  * SEPARATE per-arm tables duplicate every hand both arms can hold, and give the arms different
    mulligan behaviour on hands that have nothing to do with the edit.

So the unit of storage is the (composition, pd, ARM) triple, and a comparison is a VIEW over it:

  * only compositions at least one arm can actually hold are ever stored (the user's rule:
    "the only items worth generating are entries that one or the other would use");
  * each arm's rollouts come from that arm's OWN 60-card library, so most cells -- 78.8% in the
    2-arm case -- are rolled from exactly the right deck, with no synthetic union deck anywhere;
  * a cell both arms can hold takes the EQUAL-WEIGHT MEAN OF THE PER-ARM MEANS.

EQUAL WEIGHT IS THE LOAD-BEARING CHOICE (and the reason a pooled sum is not stored). It is what
makes the store EXTENSIBLE: adding a 4th deck version later must not change the value of cells the
existing arms already paid for. With per-arm means averaged equally, a new arm simply contributes
its own mean and every prior (cell, arm) entry stays valid forever -- rollout counts may differ
freely between arms, so nothing needs rebalancing and nothing is regenerated. Pooling raw sums
would silently reweight every shared cell toward whichever arm had more rollouts, i.e. adding an
arm would retroactively move results already reported. Per-arm counts are kept, so a
count-weighted view stays derivable if that is ever wanted.

POOLING IDENTITY IS `play_digest`, NOT `commit`
-----------------------------------------------
The raw sidecar stamps both. `commit` is a proxy that fails conservatively on ANY repo change; the
`play_digest` is a measured 64-game rollout-behaviour fingerprint. Measured 2026-08-18: after three
engine fixes moved the commit, Mirrorwing base's digest was still 09447aef3cbe3904 -- identical to
the value stored in its sidecar built at commit a1be8ffc -- i.e. its 202,878 cells were still
behaviourally valid and did not need regenerating. Gating on the commit would have thrown them
away. So arms pool iff their digests agree (--force overrides, and says so).

REUSE, NOT REIMPLEMENTATION
---------------------------
`emit` writes a per-arm sidecar in the EXISTING raw format, holding the pooled values and expressed
in that arm's OWN bucket space. The policy is then derived by the existing, validated C++ backward
induction:

    MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS=<emitted raw> \
        ./build/Release/mtg-analyze <arm deck> --cards-json src/cards/data/cards.json

No backward induction is reimplemented here, and cells an arm cannot hold need no special-casing:
the C++ hand weights use Comb(count[b], comp[b]), which is already 0 when comp[b] > count[b].

Usage
-----
    keepstore.py import --out STORE --arm NAME=RAW [--arm NAME=RAW ...] [--force]
    keepstore.py stats  STORE
    keepstore.py emit   STORE --arm NAME --out RAW
    keepstore.py selftest
"""
import argparse
import gzip
import json
import sys
from collections import defaultdict

STORE_VERSION = 1
PD = 2  # [on_the_draw, on_the_play] -- the raw format's pair ordering


# ---------------------------------------------------------------- io helpers
def _open_read(path):
    if str(path).endswith(".gz"):
        return gzip.open(path, "rt")
    return open(path, "rt")


def _open_write(path):
    if str(path).endswith(".gz"):
        return gzip.open(path, "wt")
    return open(path, "wt")


def load_json(path):
    with _open_read(path) as fh:
        return json.load(fh)


def dump_json(obj, path):
    with _open_write(path) as fh:
        json.dump(obj, fh)


# ------------------------------------------------------------ bucket algebra
def bucket_key(names):
    """A bucket's identity is its SET of card names, independent of arm-local ordering."""
    return tuple(sorted(names))


def build_union_buckets(arms):
    """Union bucket space over all arms.

    Refuses arms whose bucket partitions disagree on a shared card: if one arm merges two cards
    into a bucket and another splits them, a composition index means different things in the two
    and the pooled cell would be nonsense. Equal bucket SETS are required, not just equal coverage.
    """
    seen = {}          # bucket_key -> union index
    card_owner = {}    # card name -> bucket_key
    order = []
    for arm in arms:
        for names in arm["buckets"]:
            key = bucket_key(names)
            for nm in key:
                prev = card_owner.get(nm)
                if prev is not None and prev != key:
                    raise ValueError(
                        f"arm {arm['name']!r}: card {nm!r} is bucketed as {key} but another arm "
                        f"buckets it as {prev}. Bucket partitions must agree on shared cards; "
                        f"regenerate the arms with the same equivalence settings."
                    )
                card_owner[nm] = key
            if key not in seen:
                seen[key] = len(order)
                order.append(key)
    # Canonical order so a store is reproducible regardless of arm import order.
    order.sort()
    return {key: i for i, key in enumerate(order)}, [list(k) for k in order]


# --------------------------------------------------------------------- import
def import_arms(named_raws, force=False):
    """named_raws: list of (name, path). Returns the store dict."""
    arms = []
    for name, path in named_raws:
        raw = load_json(path)
        arms.append({"name": name, "path": str(path),
                     "buckets": raw["buckets"], "meta": raw["meta"], "sizes": raw["sizes"]})

    digests = {a["meta"].get("play_digest") for a in arms}
    if len(digests) > 1:
        msg = ("arms disagree on play_digest %s -- their rollouts were produced by different play "
               "logic and are NOT poolable" % sorted(map(str, digests)))
        if not force:
            raise ValueError(msg + " (pass --force only if you know why)")
        print("WARNING: " + msg + " -- pooling anyway (--force)", file=sys.stderr)

    index, union_buckets = build_union_buckets(arms)

    # Per-arm: union-index -> that arm's local bucket index, and its copy count per union bucket.
    for arm in arms:
        local_of_union = {}
        for local_i, names in enumerate(arm["buckets"]):
            local_of_union[index[bucket_key(names)]] = local_i
        arm["local_of_union"] = local_of_union
        arm["union_of_local"] = {v: k for k, v in local_of_union.items()}

    nU = len(union_buckets)
    # sizes: H -> comp(tuple) -> arm_idx -> {"c":[..],"s":[..],"q":[..]}
    tables = defaultdict(dict)
    for ai, arm in enumerate(arms):
        for size in arm["sizes"]:
            H = size["H"]
            tbl = tables[H]
            for e in size["entries"]:
                comp = [0] * nU
                for local_i, n in enumerate(e["comp"]):
                    if n:
                        comp[arm["union_of_local"][local_i]] = n
                key = tuple(comp)
                slot = tbl.setdefault(key, {})
                slot[ai] = {"c": list(e["count"]), "s": list(e["sum"]),
                            "q": list(e.get("sumsq", [0.0] * PD))}

    # Per-arm copy counts per union bucket, derived from the arm's own max observed... no: derive
    # from the arm's bucket membership plus the deck counts recorded in its comps is unreliable.
    # The reliable source is the arm's own table: a bucket's cap is the largest count seen for it.
    # (Exact for any deck whose table was generated exhaustively, which is the only input here.)
    for ai, arm in enumerate(arms):
        caps = [0] * nU
        for H, tbl in tables.items():
            for key, slots in tbl.items():
                if ai in slots:
                    for b, n in enumerate(key):
                        if n > caps[b]:
                            caps[b] = n
        arm["caps"] = caps

    store = {
        "version": STORE_VERSION,
        "buckets": union_buckets,
        "arms": [{"name": a["name"], "source": a["path"], "caps": a["caps"],
                  "buckets": a["buckets"], "meta": a["meta"]} for a in arms],
        "sizes": [],
    }
    for H in sorted(tables, reverse=True):
        entries = []
        for key in sorted(tables[H]):
            entries.append({"comp": list(key),
                            "arms": {str(ai): v for ai, v in sorted(tables[H][key].items())}})
        store["sizes"].append({"H": H, "entries": entries})
    return store


# --------------------------------------------------------------------- pooling
def pooled_cell(slots):
    """EQUAL-WEIGHT mean of the per-arm means, per pd. Returns (mean[], count[], sumsq[]).

    count is the summed rollout count (an effective sample size for the se), and sumsq is the
    summed raw sumsq -- so the variance derived downstream about the pooled mean legitimately
    includes the BETWEEN-ARM spread, which is a real component of a shared cell's uncertainty.
    """
    mean, count, sumsq = [0.0] * PD, [0] * PD, [0.0] * PD
    for pd in range(PD):
        means, tot, q = [], 0, 0.0
        for slot in slots.values():
            c = slot["c"][pd]
            if c > 0:
                means.append(slot["s"][pd] / c)
                tot += c
                q += slot["q"][pd]
        if means:
            mean[pd] = sum(means) / len(means)
            count[pd] = tot
            sumsq[pd] = q
    return mean, count, sumsq


# ---------------------------------------------------------------------- emit
def emit_arm(store, arm_name):
    """A raw sidecar in the EXISTING format for one arm, carrying POOLED values.

    Restricted to compositions that arm can hold, and expressed in that arm's OWN bucket space so
    the K guard (value_play.expected_buckets) and the deck's hand weights still line up.
    """
    names = [a["name"] for a in store["arms"]]
    if arm_name not in names:
        raise ValueError(f"no arm {arm_name!r} in store (have {names})")
    ai = names.index(arm_name)
    arm = store["arms"][ai]

    index = {bucket_key(b): i for i, b in enumerate(store["buckets"])}
    local_of_union = {index[bucket_key(b)]: i for i, b in enumerate(arm["buckets"])}
    caps = arm["caps"]

    sizes_out = []
    for size in store["sizes"]:
        entries = []
        for e in size["entries"]:
            comp = e["comp"]
            # Holdable by THIS arm?
            if any(n > caps[b] for b, n in enumerate(comp)):
                continue
            slots = {int(k): v for k, v in e["arms"].items()}
            mean, count, sumsq = pooled_cell(slots)
            if not any(count):
                continue
            local = [0] * len(arm["buckets"])
            for b, n in enumerate(comp):
                if n:
                    local[local_of_union[b]] = n
            entries.append({"comp": local,
                            "count": count,
                            "sum": [mean[pd] * count[pd] for pd in range(PD)],
                            "sumsq": sumsq})
        if entries:
            sizes_out.append({"H": size["H"], "entries": entries})

    meta = dict(arm["meta"])
    meta["pooled_from"] = names
    meta["pooling"] = "equal-weight per-arm means (scripts/keepstore.py)"
    return {"buckets": arm["buckets"], "meta": meta, "sizes": sizes_out}


# ------------------------------------------------------------------- reporting
def stats(store):
    names = [a["name"] for a in store["arms"]]
    out = [f"union buckets: {len(store['buckets'])}", f"arms: {', '.join(names)}"]
    for size in store["sizes"]:
        n = len(size["entries"])
        hist = defaultdict(int)
        for e in size["entries"]:
            hist[len(e["arms"])] += 1
        share = "  ".join(f"{k} arm(s): {v} ({100.0*v/n:.1f}%)" for k, v in sorted(hist.items()))
        out.append(f"  H={size['H']}: {n} cells   {share}")
    return "\n".join(out)


# ------------------------------------------------------------------- selftest
def selftest():
    fails = []

    def check(name, cond, detail=""):
        if cond:
            print(f"  PASS  {name}")
        else:
            print(f"  FAIL  {name}  {detail}")
            fails.append(name)

    # --- equal weight, not count weight ------------------------------------------------------
    # arm A: 40 rollouts, mean 5.0.   arm B: 4 rollouts, mean 7.0.
    mean, count, sumsq = pooled_cell({0: {"c": [40, 40], "s": [200.0, 200.0], "q": [1000.0, 1000.0]},
                                      1: {"c": [4, 4], "s": [28.0, 28.0], "q": [200.0, 200.0]}})
    # arm A mean 5.0 (40 rollouts), arm B mean 7.0 (4 rollouts) -> equal weight = 6.0.
    # A count-weighted pool would give 228/44 = 5.18, which is the bug this guards.
    check("equal-weight mean of per-arm means", abs(mean[0] - 6.0) < 1e-9, f"got {mean[0]}")
    check("count-weighted pooling NOT used", abs(mean[0] - 228.0 / 44.0) > 0.5)
    check("counts summed for effective n", count == [44, 44], f"got {count}")
    check("sumsq summed (keeps between-arm spread)", sumsq == [1200.0, 1200.0], f"got {sumsq}")

    # --- a single arm pools to itself (round-trip identity) ---------------------------------
    mean1, count1, _ = pooled_cell({0: {"c": [3, 3], "s": [23.0, 24.0], "q": [181.0, 194.0]}})
    check("single arm -> its own mean", abs(mean1[0] - 23.0 / 3.0) < 1e-12)
    check("single arm -> its own count", count1 == [3, 3])

    # --- an arm with zero rollouts on a cell is ignored, not counted as 0 --------------------
    mean2, _, _ = pooled_cell({0: {"c": [10, 10], "s": [50.0, 50.0], "q": [0.0, 0.0]},
                               1: {"c": [0, 0], "s": [0.0, 0.0], "q": [0.0, 0.0]}})
    check("zero-count arm excluded from the mean", abs(mean2[0] - 5.0) < 1e-12, f"got {mean2[0]}")

    # --- bucket partition disagreement is refused -------------------------------------------
    X = {"name": "X", "buckets": [["a", "b"]], "meta": {}, "sizes": []}
    Y = {"name": "Y", "buckets": [["a"], ["b"]], "meta": {}, "sizes": []}
    try:
        build_union_buckets([X, Y])
        check("refuses disagreeing bucket partitions", False, "no error raised")
    except ValueError:
        check("refuses disagreeing bucket partitions", True)

    # --- union bucket space is order-independent ---------------------------------------------
    P = {"name": "P", "buckets": [["b"], ["a"]], "meta": {}, "sizes": []}
    Q = {"name": "Q", "buckets": [["a"], ["c"]], "meta": {}, "sizes": []}
    i1, u1 = build_union_buckets([P, Q])
    i2, u2 = build_union_buckets([Q, P])
    check("union bucket order is canonical", u1 == u2, f"{u1} vs {u2}")

    # --- reachability: a cell only one arm can hold keeps that arm's own value ----------------
    # (integration-shaped check on emit(); built inline so it needs no files)
    store = {
        "version": STORE_VERSION,
        "buckets": [["a"], ["n"], ["o"]],
        "arms": [
            {"name": "base",  "caps": [4, 0, 4], "buckets": [["a"], ["o"]], "meta": {"K": 2}},
            {"name": "trick", "caps": [4, 4, 0], "buckets": [["a"], ["n"]], "meta": {"K": 2}},
        ],
        "sizes": [{"H": 7, "entries": [
            # shared cell (both arms can hold "a x1")
            {"comp": [1, 0, 0], "arms": {"0": {"c": [10, 10], "s": [50.0, 50.0], "q": [0.0, 0.0]},
                                         "1": {"c": [10, 10], "s": [70.0, 70.0], "q": [0.0, 0.0]}}},
            # trick-only cell (base cannot hold "n")
            {"comp": [0, 1, 0], "arms": {"1": {"c": [10, 10], "s": [30.0, 30.0], "q": [0.0, 0.0]}}},
            # base-only cell
            {"comp": [0, 0, 1], "arms": {"0": {"c": [10, 10], "s": [90.0, 90.0], "q": [0.0, 0.0]}}},
        ]}],
    }
    eb = emit_arm(store, "base")
    et = emit_arm(store, "trick")
    bc = {tuple(e["comp"]): e for e in eb["sizes"][0]["entries"]}
    tc = {tuple(e["comp"]): e for e in et["sizes"][0]["entries"]}
    check("base emit excludes trick-only cells", len(bc) == 2, f"got {sorted(bc)}")
    check("trick emit excludes base-only cells", len(tc) == 2, f"got {sorted(tc)}")
    shared_b = bc[(1, 0)]
    check("shared cell is the equal-weight blend (6.0)",
          abs(shared_b["sum"][0] / shared_b["count"][0] - 6.0) < 1e-9,
          f"got {shared_b['sum'][0] / shared_b['count'][0]}")
    solo_t = tc[(0, 1)]
    check("single-arm cell keeps that arm's own value (3.0)",
          abs(solo_t["sum"][0] / solo_t["count"][0] - 3.0) < 1e-9)
    check("emit uses the arm's OWN bucket space", eb["buckets"] == [["a"], ["o"]])

    print()
    if fails:
        print(f"{len(fails)} FAILED: {', '.join(fails)}")
        return 1
    print("all keepstore selftests passed")
    return 0


# ------------------------------------------------------------------------ cli
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_i = sub.add_parser("import", help="build a store from per-arm raw sidecars")
    p_i.add_argument("--out", required=True)
    p_i.add_argument("--arm", action="append", required=True, metavar="NAME=RAW")
    p_i.add_argument("--force", action="store_true",
                     help="pool arms whose play_digest disagrees (unsafe; says so loudly)")

    p_s = sub.add_parser("stats", help="summarise a store")
    p_s.add_argument("store")

    p_e = sub.add_parser("emit", help="write one arm's pooled sidecar in the raw format")
    p_e.add_argument("store")
    p_e.add_argument("--arm", required=True)
    p_e.add_argument("--out", required=True)

    sub.add_parser("selftest", help="run the arithmetic and structural checks")

    a = ap.parse_args(argv)
    if a.cmd == "selftest":
        return selftest()
    if a.cmd == "import":
        pairs = []
        for spec in a.arm:
            if "=" not in spec:
                ap.error(f"--arm expects NAME=RAW, got {spec!r}")
            nm, _, path = spec.partition("=")
            pairs.append((nm, path))
        store = import_arms(pairs, force=a.force)
        dump_json(store, a.out)
        print(stats(store))
        print(f"\nwrote {a.out}")
        return 0
    if a.cmd == "stats":
        print(stats(load_json(a.store)))
        return 0
    if a.cmd == "emit":
        raw = emit_arm(load_json(a.store), a.arm)
        dump_json(raw, a.out)
        n = sum(len(s["entries"]) for s in raw["sizes"])
        print(f"wrote {a.out}: {n} cells over {len(raw['sizes'])} hand sizes")
        # MTG_MERGE_INPUTS is split on COMMA/SPACE/TAB/NEWLINE (analyzer/main.cpp RunKeepMergeMode),
        # so a path containing a space is silently torn into fragments and every one is reported as
        # "SKIP (cannot open / empty)" -- the merge then writes nothing and exits 0. Every deck
        # folder here has a space in it ("Mirrorwing Dragon"), so this is the default case, not an
        # edge case. Refuse to hand back a command that cannot work.
        if " " in str(a.out):
            print("\nWARNING: this path contains a SPACE. MTG_MERGE_INPUTS splits on whitespace, so"
                  "\n         the merge will silently skip it and write nothing. Emit to a"
                  "\n         space-free path (e.g. /tmp/arm_raw.json.gz) before merging.",
                  file=sys.stderr)
        print("derive the policy with the existing C++ backward induction:")
        print(f"  MTG_KEEP_MERGE=1 MTG_MERGE_INPUTS={a.out} \\")
        print( "      ./build/Release/mtg-analyze <arm deck> --cards-json src/cards/data/cards.json")
        return 0
    return 2


if __name__ == "__main__":
    sys.exit(main())
