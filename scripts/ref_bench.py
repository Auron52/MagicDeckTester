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
    """-> [(slug, refdir)] for every one-level reference folder holding claude_s*_gi*.json."""
    out = []
    for d in sorted(glob.glob(os.path.join(root, "*"))):
        if not os.path.isdir(d):
            continue
        if glob.glob(os.path.join(d, "claude_s*_gi*.json")):
            out.append((deck_registry.slug(os.path.basename(d)), d))
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
    args = ap.parse_args()

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

    results, skipped = [], []
    for slug, refdir in pairs:
        # The deck a reference folder was PLAYED on, which is not always the one that owns its slug
        # today (see deck_registry.REFERENCE_DECK -- an archived list keeps its references).
        key = deck_registry.reference_deck_key(slug)
        if key not in decks:
            skipped.append((slug, refdir, key))
            continue
        results.append(bench_deck(key, refdir, decks[key], args, log_root))

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
    if tmp:
        print("\nper-game logs: %s" % tmp)


if __name__ == "__main__":
    main()
