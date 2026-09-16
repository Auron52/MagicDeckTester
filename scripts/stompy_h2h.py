#!/usr/bin/env python3
"""Final head-to-head: two candidate lists, EACH UNDER ITS OWN KEEP TABLE.

`deck_compare.py` cannot ask this question. Its Rule 0 is that every arm shares ONE apparatus --
which is the right call for screening (sharing a table halves the se) and the wrong call here. The
whole point of generating a table per list is that the mana:threat ratio is exactly what a shared
table biases, so the adopting comparison must give each list the table that was fitted to it.

So this is a hand-written manifest, still ONE pooled `mtg --batch` (no waves, one tail):
four jobs -- {A,B} x {1v1, 2hg} -- each carrying its own per-job `profile` + `value_profile`, which
the manifest format supports and deck_compare simply does not expose.

PAIRING. Both lists must share the number SET {1..60}: ShuffleByKey sorts by key(seed, m_number), so
the same seed deals the same positions and only the CARD at a number differs. A's numbering is the
base; B inherits it through deck_compare's own `inherit_numbering` with an explicit replace map, so
the copies B drops hand their slots to the copies B adds instead of renumbering the deck.

Usage:  python3 scripts/stompy_h2h.py --a <StemA> --b <StemB> --replace 'Fyndhorn Elves=Forest' \
                                      --games N --seed S [--dry-run]
"""
import argparse, json, os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import deck_compare as dc

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "logs/stompy_h2h")
FORMATS = {"1v1": (20, 1), "2hg": (30, 2)}


def deck_dir(stem):
    return os.path.join(REPO, "decks", stem)


def require(stem):
    """Refuse to run against a list whose table is missing -- that arm would silently fall back to
    the DEFAULT mulligan profile and lose on apparatus, not on cards."""
    d, missing = deck_dir(stem), []
    cod = os.path.join(d, f"{stem}.cod")
    prof = os.path.join(d, f"{stem}.profile.json")
    val = os.path.join(d, f"{stem}.value.json")
    keep = [os.path.join(d, f"{stem}.keepmodel.exhaustive.profile.json" + e) for e in ("", ".gz")]
    for p in (cod, prof, val):
        if not os.path.exists(p): missing.append(p)
    if not any(os.path.exists(k) for k in keep):
        missing.append(keep[0] + "[.gz]  <-- NO KEEP TABLE: this arm would play on DefaultProfile")
    if missing:
        raise SystemExit(f"{stem}: missing\n  " + "\n  ".join(missing))
    return cod, prof, val


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--decks", nargs="+", required=True,
                    help="stems; the FIRST is the numbering base every other inherits from")
    ap.add_argument("--replace", action="append", default=[],
                    help="'<Stem>:<removed>=<added>', repeatable (per-deck replace map vs the base)")
    ap.add_argument("--games", type=int, default=100000)
    ap.add_argument("--seed", type=int, default=91000000)
    ap.add_argument("--max-turns", type=int, default=10)
    ap.add_argument("--threads", type=int, default=32)
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    os.makedirs(OUT, exist_ok=True)
    paths = {s: require(s) for s in a.decks}
    decks = {s: dc.read_decklist(paths[s][0]) for s in a.decks}
    counts = {s: {n: c for c, n in decks[s]} for s in a.decks}

    # per-deck replace map vs the base: '<Stem>:<removed>=<added>'
    pairs = {s: {} for s in a.decks}
    for p in a.replace:
        stem, rule = p.split(":", 1)
        src, dst = rule.split("=", 1)
        pairs.setdefault(stem, {})[src] = dst

    base = a.decks[0]
    nums = {base: dc.base_numbering(decks[base])}
    for s in a.decks[1:]:
        nums[s] = dc.inherit_numbering(nums[base], counts[base], counts[s], pairs.get(s))

    ref = sorted(n for v in nums[base].values() for n in v)
    for s in a.decks[1:]:
        got = sorted(n for v in nums[s].values() for n in v)
        if got != ref:
            raise SystemExit(f"{s}: number set differs from {base} -- pairing would be broken")
    print(f"pairing OK: all {len(a.decks)} lists carry the number set 1..{len(ref)} (base {base})")
    allnames = sorted(set().union(*(set(counts[s]) for s in a.decks)))
    for name in allnames:
        row = [counts[s].get(name, 0) for s in a.decks]
        if len(set(row)) > 1:
            print("   DIFFERS  " + f"{name:24s}" + "  ".join(f"{s}={c}" for s, c in zip(a.decks, row)))

    npaths = {}
    for s in a.decks:
        npaths[s] = os.path.join(OUT, f"{s}.numbering.json")
        json.dump(nums[s], open(npaths[s], "w"), indent=1)

    jobs = []
    for fmt, (life, heads) in FORMATS.items():
        for s in a.decks:
            cod, prof, val = paths[s]
            jobs.append({"name": f"{fmt}::{s}", "deck": cod, "deck_numbering": npaths[s],
                         "games": a.games, "seed": a.seed, "max_turns": a.max_turns,
                         "starting_life": life, "opponent_heads": heads,
                         "profile": prof, "value_profile": val})
    man = os.path.join(OUT, "h2h.manifest.json")
    json.dump({"jobs": jobs}, open(man, "w"), indent=1)
    print(f"\n{len(jobs)} jobs x {a.games:,} games = {len(jobs)*a.games:,} total -> {man}")
    if a.dry_run:
        return

    env = dict(os.environ, MTG_DUMP_WINS="1")
    log = os.path.join(OUT, "h2h.out")
    print(f"running ONE pooled batch -> {log}")
    with open(log, "w") as f:
        subprocess.run([os.path.join(REPO, "build/Release/mtg"), "--batch", man,
                        "--threads", str(a.threads)], env=env, stdout=f, stderr=subprocess.STDOUT)


if __name__ == "__main__":
    main()
