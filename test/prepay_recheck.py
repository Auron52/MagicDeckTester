#!/usr/bin/env python3
"""Re-evaluation set for the 2026-08-25 colour-honesty batch (90a537bd..c6743509).

WHY THIS EXISTS
    That batch made three mana fixes. Two are unambiguous rules fixes. The third
    (MTG_PREPAY_TRUE_COLOURS, "the prepaid pool keeps its true colours") is also a real
    soundness fix, but it changed the PAYMENT path, and on Mirrorwing it was caught paying
    for an identical line by tapping a mana dork the pre-fix path spared -- which removed an
    attacker and turned a T4 kill into a T6 win. See docs/design/prepay-payment-path-recheck.md.

    Ground truth was rebaselined WITH that defect in it. This file is the list of every game
    whose outcome that batch changed for the worse, so it can be re-run once the payment path
    is repaired and the ones that recover can be rebaselined back.

    The case list is SELF-CONTAINED: deck, profile, seed, game index, depth and budget are
    baked in, so a checkout with unpushed work can verify without this repo's gt_logs or
    explain_game.py.

USAGE
    build     python3 test/prepay_recheck.py build [--baseline 90a537bd]
              Regenerate cases.tsv from `git show <baseline>:test/gt_logs/*` vs the working
              tree's gt_logs. Only needed if the set itself must be recomputed.

    verify    python3 test/prepay_recheck.py verify [--jobs N] [--bin PATH] [--filter SUBSTR]
              Run every case on the CURRENT binary and report, per game, whether it now
              matches the PRE-fix outcome (RECOVERED), still matches the post-fix one
              (STILL-WORSE), or landed somewhere else (MOVED).
              This is the one an agent working on the payment path wants.

    classify  python3 test/prepay_recheck.py classify [--jobs N] [--filter SUBSTR]
              Expensive. For each case, attribute it to a switch and force BOTH arms down the
              recorded line. The interesting class is EXECUTION-DIFFERS: both arms play the
              identical line and still disagree on the result, which cannot be a search or
              ranking effect -- it is the payment path.

EXIT CODE
    verify exits 0 always; read the summary. It is a report, not a gate.
"""
import argparse, concurrent.futures as futures, json, os, re, subprocess, sys, tempfile, shutil, glob

ROOT  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CASES = os.path.join(ROOT, "test", "prepay_recheck_cases.tsv")
COLS  = ["key", "gi", "deck", "profile", "seed", "game_index", "depth", "budget",
         "pre_fix", "post_fix", "attribution", "walk_class"]

# The batch's off-switches. Setting all of them reproduces the PRE-batch engine in the SAME
# executable, which is the only valid control: src/cards/data/cards.json is read at RUNTIME and
# one of the fixes IS card data, so an old BUILD run from this tree loads the new card file.
LEGACY_ENV = {"MTG_PREPAY_TRUE_COLOURS": "0", "MTG_LEGACY_RITUAL_WILD": "1",
              "MTG_LEGACY_STAGED_SUSPEND": "1", "MTG_AURA_RANK_MODE": "1",
              "MTG_NO_LINE_HOLD": "1", "MTG_LEGACY_BESTOW_SIG": "1"}
SWITCHES = [("prepay-colours",   {"MTG_PREPAY_TRUE_COLOURS": "0"}),
            ("ritual-colours",   {"MTG_LEGACY_RITUAL_WILD": "1"}),
            ("staged-suspend",   {"MTG_LEGACY_STAGED_SUSPEND": "1"}),
            ("aura-fetch-order", {"MTG_AURA_RANK_MODE": "1"}),
            ("line-hold",        {"MTG_NO_LINE_HOLD": "1"}),
            ("bestow-signature", {"MTG_LEGACY_BESTOW_SIG": "1"})]

AVG_RE = re.compile(r"avg \(turns\)\s*:\s*([0-9.]+)")


def mode_of(key):
    """Tier name out of a case key. NOT key.split('_')[1] -- `creature_giving_overnight_d0_s4004`
    has an underscore in the DECK name, and that slip silently dropped all 19 creature_giving
    cases from the first build of this list."""
    for m in ("smoke", "regression", "overnight"):
        if f"_{m}_" in key:
            return m
    return "regression"


def read_cases(filt=None):
    rows = []
    with open(CASES) as fh:
        for ln in fh:
            ln = ln.rstrip("\n")
            if not ln or ln.startswith("#"):
                continue
            parts = ln.split("\t")
            r = dict(zip(COLS, parts + [""] * (len(COLS) - len(parts))))
            if filt and filt not in r["key"]:
                continue
            rows.append(r)
    return rows


def score_once(binary, r, env_extra=None):
    """Loss-penalized score of ONE autonomous game -- the same number ground truth records."""
    tmp = tempfile.mkdtemp(prefix="recheck_")
    try:
        cmd = [binary, os.path.join(ROOT, r["deck"]), "--profile", os.path.join(ROOT, r["profile"]),
               "--games", "1", "--seed", r["seed"], "--game-index", r["game_index"],
               "--depth", r["depth"], "--budget-ms", r["budget"],
               "--ignore-play-profile", "--log-dir", tmp]
        out = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True,
                             env=dict(os.environ, **(env_extra or {}))).stdout
        m = AVG_RE.search(out)
        return float(m.group(1)) if m else None
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ---------------------------------------------------------------- build
def cmd_build(args):
    keys = sorted(f[:-5] for f in os.listdir(os.path.join(ROOT, "test/gt_logs")) if f.endswith(".wins"))
    sys.path.insert(0, os.path.join(ROOT, "test"))
    sys.path.insert(0, ROOT)
    import explain_game as explain                      # build-time only; verify does not need it
    cfg = explain.load_cases()
    rows = []
    for k in keys:
        old = subprocess.run(["git", "show", f"{args.baseline}:test/gt_logs/{k}.wins"],
                             cwd=ROOT, capture_output=True, text=True).stdout
        if not old.strip():
            continue
        new = open(os.path.join(ROOT, "test/gt_logs", f"{k}.wins")).read()
        mode = mode_of(k)
        try:
            r = explain.resolve(mode, k, cfg)
        except Exception:
            continue
        for lo, ln in zip(old.splitlines(), new.splitlines()):
            a, b = lo.split(), ln.split()
            if a[0] != b[0]:
                continue
            ow, nw = int(a[1]), int(b[1])
            if ow == nw:
                continue
            worse = (nw > ow or nw == -1) and ow != -1     # -1 == no win inside max_turns
            if not worse:
                continue
            gi = int(a[0])
            rows.append([k, gi, r["deckfile"], r["profile"], r["seed"] + gi, gi,
                         r["depth"], r["budget"], ow, nw, "", ""])
    rows.sort(key=lambda x: (x[0], x[1]))
    with open(CASES, "w") as fh:
        fh.write("# Games the 2026-08-25 colour-honesty batch made WORSE (baseline %s).\n" % args.baseline)
        fh.write("# pre_fix = win turn before the batch, post_fix = win turn now (-1 = no win).\n")
        fh.write("# " + "\t".join(COLS) + "\n")
        for r in rows:
            fh.write("\t".join(str(x) for x in r) + "\n")
    print(f"wrote {len(rows)} cases -> {os.path.relpath(CASES, ROOT)}")
    return 0


# ---------------------------------------------------------------- verify
def verify_one(binary, r):
    now = score_once(binary, r)
    pre, post = float(r["pre_fix"]), float(r["post_fix"])
    if now is None:
        cls = "ERROR"
    elif now <= pre:
        cls = "RECOVERED"
    elif abs(now - post) < 1e-9:
        cls = "STILL-WORSE"
    else:
        cls = "MOVED"
    return dict(r, now=now, cls=cls)


def cmd_verify(args):
    binary = args.bin or os.path.join(ROOT, "build/Release/mtg")
    rows = read_cases(args.filter)
    print(f"VERIFY  {len(rows)} cases  bin={binary}")
    print("  RECOVERED  = now at or better than the PRE-fix turn (the payment-path defect is gone here)")
    print("  STILL-WORSE= unchanged from the rebaselined value")
    print("  MOVED      = a third value; inspect before drawing a conclusion\n")
    out, agg = [], {}
    with futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        for a in ex.map(lambda r: verify_one(binary, r), rows):
            out.append(a)
            agg[a["cls"]] = agg.get(a["cls"], 0) + 1
            if a["cls"] != "STILL-WORSE":
                print(f"  {a['key']} gi{a['gi']}: pre={a['pre_fix']} post={a['post_fix']} "
                      f"now={a['now']}  <- {a['cls']}", flush=True)
    print("\nsummary: " + ", ".join(f"{k}={v}" for k, v in sorted(agg.items())))
    by_deck = {}
    for a in out:
        d = a["key"].split("_")[0]
        by_deck.setdefault(d, {}).setdefault(a["cls"], 0)
        by_deck[d][a["cls"]] += 1
    print("\nby deck:")
    for d in sorted(by_deck):
        print(f"  {d:18s} " + ", ".join(f"{k}={v}" for k, v in sorted(by_deck[d].items())))
    with open(os.path.join(ROOT, "logs", "prepay_recheck_verify.json"), "w") as fh:
        json.dump(out, fh, indent=1)
    print("\nper-case detail -> logs/prepay_recheck_verify.json")
    return 0


# ---------------------------------------------------------------- classify
def classify_one(binary, r):
    """Attribute the game to a switch, then force BOTH arms down the recorded line."""
    now  = score_once(binary, r)
    allo = score_once(binary, r, LEGACY_ENV)
    hits = []
    if allo is not None and now is not None and allo != now:
        for name, env in SWITCHES:
            if score_once(binary, r, env) == allo:
                hits.append(name)
    attribution = ",".join(hits) or ("(none)" if allo == now else "(combination)")

    walk = ""
    if "prepay-colours" in hits or attribution == "(combination)":
        try:
            sys.path.insert(0, os.path.join(ROOT, "test"))
            import gt_line_playable as G
            mode = mode_of(r["key"])
            res = G.check_game(mode, r["key"], int(r["gi"]), binary, binary)
            if res.get("status") == "WALKED":
                ow, ctl, rep = res.get("old_win"), res.get("old_self"), res.get("replay_win")
                if res.get("blocked"):
                    walk = "REFUSED"
                elif ctl == ow and rep != ow:
                    walk = "EXECUTION-DIFFERS"      # identical forced line, different result
                elif ctl == ow and rep == ow:
                    walk = "CHOICE-ONLY"            # the line still executes; only the pick moved
                else:
                    walk = "INCONCLUSIVE"
            else:
                walk = res.get("status", "?")
        except Exception as e:
            walk = f"ERR:{type(e).__name__}"
    return dict(r, attribution=attribution, walk_class=walk)


def cmd_classify(args):
    binary = args.bin or os.path.join(ROOT, "build/Release/mtg")
    rows = read_cases(args.filter)
    print(f"CLASSIFY  {len(rows)} cases  (attribute; then force both arms for prepay-attributed ones)")
    done, agg, wagg = [], {}, {}
    with futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        for a in ex.map(lambda r: classify_one(binary, r), rows):
            done.append(a)
            agg[a["attribution"]] = agg.get(a["attribution"], 0) + 1
            if a["walk_class"]:
                wagg[a["walk_class"]] = wagg.get(a["walk_class"], 0) + 1
            print(f"  {a['key']} gi{a['gi']}: {a['pre_fix']}->{a['post_fix']}  "
                  f"{a['attribution']}  {a['walk_class']}", flush=True)
    print("\nattribution: " + ", ".join(f"{k}={v}" for k, v in sorted(agg.items())))
    print("walk class : " + ", ".join(f"{k}={v}" for k, v in sorted(wagg.items())))
    by = {r["key"] + "|" + str(r["gi"]): r for r in done}
    rows_all = read_cases()
    with open(CASES, "w") as fh:
        fh.write("# Games the 2026-08-25 colour-honesty batch made WORSE.\n")
        fh.write("# pre_fix = win turn before the batch, post_fix = win turn now (-1 = no win).\n")
        fh.write("# " + "\t".join(COLS) + "\n")
        for r in rows_all:
            k = r["key"] + "|" + str(r["gi"])
            if k in by:
                r = by[k]
            fh.write("\t".join(str(r[c]) for c in COLS) + "\n")
    print(f"\nannotated -> {os.path.relpath(CASES, ROOT)}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build");    b.add_argument("--baseline", default="90a537bd")
    v = sub.add_parser("verify")
    c = sub.add_parser("classify")
    for p in (v, c):
        p.add_argument("--jobs", type=int, default=min(16, (os.cpu_count() or 4) - 2))
        p.add_argument("--bin", default=None)
        p.add_argument("--filter", default=None)
    a = ap.parse_args()
    return {"build": cmd_build, "verify": cmd_verify, "classify": cmd_classify}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
