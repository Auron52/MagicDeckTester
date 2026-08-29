#!/usr/bin/env python3
"""Per-game reference benchmark: the SHIPPED search replayed on every saved reference game.

For each `references/<Deck>/claude_s*_gi*.json` (a hand-played, user-owned ground-truth game),
reconstruct that game's EXACT opening hand on the autonomous engine via `--force-mulligan`, run the
shipped play policy, and print the search's win turn beside the human's saved one. Flags every game
where the search FALLS SHORT (wins later than the human, or does not win at all).

    scripts/ref_bench.py                      # every reference deck
    scripts/ref_bench.py --deck knights burn  # a subset (deck_registry slugs)
    scripts/ref_bench.py --suboptimal         # also references/suboptimal/<Deck>/ (known-slow targets)

WHY --force-mulligan IS NOT OPTIONAL. A bare re-run mulligans autonomously, so it plays a DIFFERENT
opening hand than the human did and the win-turn difference measures mulligan policy, not play. Over
half the references keep after at least one mulligan, so this is the dominant confound. Forcing the
reference's recorded (count, bottomed card numbers) isolates PLAY, which is what "how does the search
do on the references" asks. Every game's reconstruction is CHECKED against the reference's recorded
opening hand (see --no-verify-hands); a mismatch is reported as HAND-MISMATCH and its row is not
trusted.

POLICY. `--depth` is omitted by default so the engine uses the deck's committed value_play policy --
the thing that actually ships. Passing an explicit --depth requires --ignore-play-profile, which this
script then adds automatically.

Decks come from scripts/deck_registry.py (discovery, no list to maintain): a reference folder is
matched to its deck folder by slug, so `references/Mirrorwing_Dragon/` finds `decks/Mirrorwing Dragon/`.
"""
import argparse, glob, json, os, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deck_registry

MTG = "build/Release/mtg"


def ref_dirs(root):
    """-> [(slug, refdir)] for every reference folder holding claude_s*_gi*.json.

    MIRRORS THE DECK LAYOUT. `references/<Deck>/` holds the games played on the list that currently
    ships, and `references/<Deck>/<Variant>/` holds the games played on an archived list -- exactly
    as `decks/<Deck>/<Variant>/` holds that list itself, and keyed the same way
    (`<deck>_<variant>`), so a variant reference folder resolves to its own deck through ordinary
    discovery with nothing to hand-maintain.

    This REPLACES deck_registry.REFERENCE_DECK, which could only name ONE list per folder. That was
    the defect: when a deck's shipping list is replaced, its folder keeps accumulating references
    for the NEW list beside the old ones, and no single folder->deck binding can describe a mixture.
    Mirrorwing hit exactly that -- 9 games on the shipping list sat in a folder bound to the
    archived v1 list, so every one of them benched against cards that were never in the deck they
    were played on, and the rows silently reported as HAND-MISMATCH shortfalls.

    `references/suboptimal/` is a ROOT, not a deck, so it is skipped here: its decks live one level
    deeper and are reached by passing it as `root` (--suboptimal)."""
    out = []
    for d in sorted(glob.glob(os.path.join(root, "*"))):
        if not os.path.isdir(d):
            continue
        base = deck_registry.slug(os.path.basename(d))
        if os.path.basename(d) == "suboptimal":
            continue
        if glob.glob(os.path.join(d, "claude_s*_gi*.json")):
            out.append((base, d))
        for sub in sorted(glob.glob(os.path.join(d, "*"))):
            if not os.path.isdir(sub):
                continue
            if glob.glob(os.path.join(sub, "claude_s*_gi*.json")):
                out.append(("%s_%s" % (base, deck_registry.slug(os.path.basename(sub))), sub))
    return out


def expected_hand(ref):
    """Multiset of card numbers the reference actually opened with, or None if not recorded.

    The kept hand is the last `mulligan` decision the human answered 1 to; the bottomed cards are the
    top-level `mulligan.bottom` (card NUMBERS, not hand indices -- what --force-mulligan consumes)."""
    kept = None
    for d in ref.get("decisions", []):
        dec = d.get("decision", {})
        if dec.get("type") == "mulligan" and d.get("chosen") == 1:
            kept = [c.get("num") for c in (dec.get("hand") or []) if isinstance(c, dict)]
    if not kept or any(n is None for n in kept):
        return None
    hand = list(kept)
    for n in (ref.get("mulligan") or {}).get("bottom") or []:
        if n in hand:
            hand.remove(n)
    return sorted(hand)


def logged_hand(log_dir):
    """Multiset of card numbers in the replay's opening hand, or None if no log was written."""
    files = glob.glob(os.path.join(log_dir, "*.json"))
    if not files:
        return None
    g = json.load(open(files[0]))
    return sorted(c["card"] for c in g.get("openingHand", []))


def run_one(deck_file, profile, g, depth, max_turns, log_root):
    """Replay one reference game on the shipped search. -> (win_turn|None, hand_verdict)."""
    mull = g["mull"]
    fm = "%d:%s" % (mull.get("count", 0), ",".join(str(x) for x in (mull.get("bottom") or [])))
    log_dir = None
    if log_root:
        log_dir = os.path.join(log_root, "s%d_gi%d" % (g["seed"], g["gi"]))
        os.makedirs(log_dir, exist_ok=True)
    cmd = [MTG, deck_file, "--profile", profile, "--games", "1", "--seed", str(g["seed"]),
           "--game-index", str(g["gi"]), "--max-turns", str(max_turns),
           "--force-mulligan", fm, "--threads", "1"]
    if depth is not None:
        # Every reference deck may carry an enabled value_play lock; an explicit depth must bypass it.
        cmd += ["--depth", str(depth), "--ignore-play-profile"]
    if log_dir:
        cmd += ["--log-dir", log_dir]
    p = subprocess.run(cmd, capture_output=True, text=True)
    m = re.search(r"avg \(turns\)\s*:\s*([\d.]+)", p.stdout)
    if m is None:
        sys.stderr.write("PARSE-FAIL rc=%d %s\n  cmd=%s\n  stderr=%s\n" % (
            p.returncode, g["name"], " ".join(cmd), p.stderr.strip()[-400:]))
        return "ERR", ""
    v = float(m.group(1))
    win = None if v >= max_turns + 1 - 1e-9 else int(round(v))

    verdict = ""
    if log_dir:
        want, got = g["want_hand"], logged_hand(log_dir)
        if want is None:      verdict = "hand-unrecorded"
        elif got is None:     verdict = "HAND-NOLOG"
        elif want != got:     verdict = "HAND-MISMATCH"
    return win, verdict


# ---- the CACHE ----------------------------------------------------------------------------
#
# Benching every reference on every question is not on: it is a full game replay per saved game, and
# the answer only changes when the ENGINE does. So the result is cached in a committed artifact
# (test/ref_bench.json) and each deck's entry is stamped with the engine state it was measured at.
#
# STAMPED PER DECK, not per file, and that is the whole point: it makes the refresh INCREMENTAL.
# `--stale-only` benches exactly the decks whose stamp no longer matches and leaves the rest alone,
# so the routine call costs nothing when nothing moved, and costs one deck when one deck's worth of
# play changed. A single global stamp would force the whole fleet through on any move.
#
# THE STAMP IS `git rev-parse HEAD:src` -- the source tree hash, not the commit, so a docs or script
# commit does not invalidate a measurement it cannot have affected. It still OVER-triggers (a comment
# under src/ moves it while play is identical), and the value-leaf driver answers that with a smoke
# play digest. Deliberately NOT copied here: that machinery exists because a value-leaf table costs
# 60-70 core-hours, where this whole bench is ~140 single games. Paying a re-bench you did not
# strictly need is cheaper than the apparatus for deciding you did not need it.
def src_fingerprint():
    try:
        return subprocess.run(["git", "rev-parse", "HEAD:src"], capture_output=True, text=True,
                              check=True).stdout.strip()
    except Exception:
        return ""


def load_cache(path):
    try:
        with open(path) as fh:
            return json.load(fh)
    except Exception:
        return {"decks": {}}


def write_json(path, results, args):
    """MERGE this run's decks into the cache; never replace it.

    Replacing would make `--deck knights` silently delete every other deck's entry -- and the file
    still parses, so the viewer would simply report those decks as un-benched. Merging is what makes
    a per-deck refresh safe, which is what makes the cache usable at all."""
    src = src_fingerprint()
    cache = load_cache(path)
    cache.setdefault("decks", {})
    for r in results:
        short = [g["name"] for g in r["games"] if any(f.startswith("SHORTFALL") for f in g["flags"])]
        bad = [g["name"] for g in r["games"] if g["hand"].startswith("HAND")]
        cache["decks"][r["deck"]] = {
            "n": r["n"], "human": round(r["human"], 4), "search": round(r["search"], 4),
            "short": len(short), "shortfalls": short,
            # A game whose forced hand did not reconstruct was not a valid comparison, so a deck
            # carrying one is not "green" -- it is unmeasured, and silently counting it as a pass is
            # the same failure as an empty parse reading as clean.
            "hand_mismatch": len(bad), "hand_mismatch_games": bad,
            "src": src, "max_turns": args.max_turns,
            "policy": ("depth %d" % args.depth) if args.depth else "committed value_play",
        }
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w") as fh:
        json.dump(cache, fh, indent=1, sort_keys=True)
        fh.write("\n")


def bench_deck(slug, refdir, deck, args, log_root):
    games = []
    for path in sorted(glob.glob(os.path.join(refdir, "claude_s*_gi*.json"))):
        r = json.load(open(path))
        games.append({"name": os.path.basename(path), "seed": r["seed"], "gi": r["game_index"],
                      "mull": r.get("mulligan") or {"count": 0, "bottom": []},
                      "human": r["win_turn"] if r.get("won") else None,
                      "want_hand": expected_hand(r)})
    mt = args.max_turns
    for g in games:                       # a human win beyond the horizon is a loss on this scale
        if g["human"] is not None and g["human"] > mt:
            g["human"] = None

    def one(g):
        return run_one(deck.deck_file, deck.profile, g, args.depth, mt,
                       log_root and os.path.join(log_root, slug))

    with ThreadPoolExecutor(max_workers=args.threads) as ex:
        for g, (v, hv) in zip(games, ex.map(one, games)):
            g["search"], g["hand"] = v, hv

    LOSS = mt + 1
    lp = lambda v: v if isinstance(v, int) else LOSS
    cell = lambda v: str(v) if isinstance(v, int) else (v if isinstance(v, str) else "NO-WIN")
    print("\n=== %s  (n=%d, max_turns=%d, no-win scores %d)" % (slug, len(games), mt, LOSS))
    print("%-26s %5s %4s | %6s %6s | %s" % ("reference", "seed", "gi", "human", "search", "flags"))
    for g in games:
        h, s = g["human"], g["search"]
        hi, si = (x if isinstance(x, int) else None for x in (h, s))
        flags = []
        if si is not None and hi is not None and si > hi: flags.append("SHORTFALL +%d" % (si - hi))
        if s is None and hi is not None:                  flags.append("SHORTFALL no-win (human %d)" % hi)
        if hi is not None and si is not None and si < hi: flags.append("search faster -%d" % (hi - si))
        if h is None and si is not None:                  flags.append("search wins (human no-win)")
        if g["hand"].startswith("HAND"):                  flags.append(g["hand"])
        g["flags"] = flags
        print("%-26s %5s %4s | %6s %6s | %s" % (
            g["name"], g["seed"], g["gi"], cell(h), cell(s), " ".join(flags)))
    n = len(games)
    print("%-26s %5s %4s | %6.3f %6.3f | avg (no-win = %d)" % (
        "AVG", "", "", sum(lp(g["human"]) for g in games) / n,
        sum(lp(g["search"]) for g in games) / n, LOSS))
    sys.stdout.flush()
    return {"deck": slug, "n": n, "games": games,
            "human": sum(lp(g["human"]) for g in games) / n,
            "search": sum(lp(g["search"]) for g in games) / n}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--deck", nargs="*", default=None, help="deck slugs (default: every reference deck)")
    ap.add_argument("--depth", type=int, default=None,
                    help="explicit lookahead depth (adds --ignore-play-profile); "
                         "default = omit, i.e. use the deck's committed value_play policy")
    ap.add_argument("--max-turns", type=int, default=deck_registry.MAX_TURNS)
    ap.add_argument("--threads", type=int, default=16, help="concurrent single-game replays")
    ap.add_argument("--suboptimal", action="store_true",
                    help="bench references/suboptimal/<Deck>/ instead (known-slow aspirational targets)")
    ap.add_argument("--no-verify-hands", action="store_true",
                    help="skip the forced-hand check (drops the per-game --log-dir)")
    ap.add_argument("--log-root", default=None, help="keep the per-game logs here instead of a temp dir")
    ap.add_argument("--json", default=None, metavar="PATH",
                    help="merge the per-deck summary into a cache file (e.g. test/ref_bench.json). "
                         "Each deck is stamped with `git rev-parse HEAD:src`; the play viewer reads "
                         "this to decide whether a deck's play is GREEN on its references")
    ap.add_argument("--stale-only", action="store_true",
                    help="with --json: bench only decks whose cached stamp differs from the current "
                         "src tree (and decks with no entry). This is the routine call -- it costs "
                         "nothing when the engine has not moved")
    args = ap.parse_args()
    if args.stale_only and not args.json:
        ap.error("--stale-only needs --json: without a cache there is nothing to compare against")

    decks = deck_registry.discover()
    root = "references/suboptimal" if args.suboptimal else "references"
    pairs = ref_dirs(root)
    if args.deck:
        want = {deck_registry.slug(d) for d in args.deck}
        pairs = [p for p in pairs if p[0] in want]

    tmp = None
    log_root = args.log_root
    if not args.no_verify_hands and not log_root:
        tmp = tempfile.mkdtemp(prefix="ref_bench_", dir="logs")
        log_root = tmp
    if args.no_verify_hands:
        log_root = None

    results, skipped, fresh = [], [], []
    cache_src = src_fingerprint()
    cached = load_cache(args.json)["decks"] if args.json else {}
    for slug, refdir in pairs:
        # The deck a reference folder was PLAYED on, which is not always the one that owns its slug
        # today (see deck_registry.REFERENCE_DECK -- an archived list keeps its references).
        key = deck_registry.reference_deck_key(slug)
        if key not in decks:
            skipped.append((slug, refdir, key))
            continue
        if args.stale_only and cache_src and (cached.get(key) or {}).get("src") == cache_src:
            fresh.append(key)
            continue
        results.append(bench_deck(key, refdir, decks[key], args, log_root))
    if fresh:
        print("\n--stale-only: %d deck(s) already benched at this src tree, skipped: %s"
              % (len(fresh), " ".join(sorted(fresh))))

    print("\n=== SUMMARY (avg turn-to-win; lower is better; no-win scores %d)" % (args.max_turns + 1))
    print("%-18s %4s %8s %8s   %s" % ("deck", "n", "human", "search", "shortfalls"))
    tot_n = tot_short = 0
    for r in results:
        short = sum(1 for g in r["games"] if any(f.startswith("SHORTFALL") for f in g["flags"]))
        faster = sum(1 for g in r["games"] if any(f.startswith("search ") for f in g["flags"]))
        tot_n += r["n"]; tot_short += short
        print("%-18s %4d %8.3f %8.3f   %d/%d short, %d faster than human"
              % (r["deck"], r["n"], r["human"], r["search"], short, r["n"], faster))
    print("%-18s %4d %8s %8s   %d/%d short" % ("TOTAL", tot_n, "", "", tot_short, tot_n))
    for slug, refdir, key in skipped:
        via = "" if key == slug else " (bound to '%s' by deck_registry.REFERENCE_DECK)" % key
        print("SKIPPED %s (%s)%s: no decks/ folder with a decklist AND a profile for '%s' -- these "
              "references are NOT benched" % (slug, refdir, via, key))
    if args.json:
        write_json(args.json, results, args)
        print("\nbench artifact -> %s" % args.json)
    if tmp:
        # --stale-only is the ROUTINE call and usually runs nothing, so it would otherwise leave an
        # empty logs/ref_bench_* directory behind on every invocation. Only mention (and keep) the
        # directory when something was actually written to it.
        try:
            if not os.listdir(tmp):
                os.rmdir(tmp)
                tmp = None
        except OSError:
            pass
    if tmp:
        print("\nper-game logs: %s" % tmp)


if __name__ == "__main__":
    main()
