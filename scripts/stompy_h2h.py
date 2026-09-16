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
    ap.add_argument("--a", required=True); ap.add_argument("--b", required=True)
    ap.add_argument("--replace", action="append", default=[],
                    help="'<removed in B>=<added in B>', repeatable")
    ap.add_argument("--games", type=int, default=100000)
    ap.add_argument("--seed", type=int, default=91000000)
    ap.add_argument("--max-turns", type=int, default=10)
    ap.add_argument("--threads", type=int, default=32)
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    os.makedirs(OUT, exist_ok=True)
    cod_a, prof_a, val_a = require(a.a)
    cod_b, prof_b, val_b = require(a.b)

    deck_a = dc.read_decklist(cod_a)
    deck_b = dc.read_decklist(cod_b)
    counts_a = {n: c for c, n in deck_a}
    counts_b = {n: c for c, n in deck_b}
    pairs = dict(p.split("=", 1) for p in a.replace)

    nums_a = dc.base_numbering(deck_a)
    nums_b = dc.inherit_numbering(nums_a, counts_a, counts_b, pairs)

    set_a = sorted(n for v in nums_a.values() for n in v)
    set_b = sorted(n for v in nums_b.values() for n in v)
    if set_a != set_b:
        raise SystemExit(f"number sets differ ({len(set_a)} vs {len(set_b)}) -- pairing would be broken")
    print(f"pairing OK: both lists carry the number set 1..{len(set_a)}")
    for name in sorted(set(counts_a) | set(counts_b)):
        ca, cb = counts_a.get(name, 0), counts_b.get(name, 0)
        if ca != cb: print(f"   DIFFERS  {name:24s} {a.a}={ca}  {a.b}={cb}")

    np_a = os.path.join(OUT, f"{a.a}.numbering.json")
    np_b = os.path.join(OUT, f"{a.b}.numbering.json")
    json.dump(nums_a, open(np_a, "w"), indent=1)
    json.dump(nums_b, open(np_b, "w"), indent=1)

    jobs = []
    for fmt, (life, heads) in FORMATS.items():
        for stem, cod, prof, val, npath in ((a.a, cod_a, prof_a, val_a, np_a),
                                            (a.b, cod_b, prof_b, val_b, np_b)):
            jobs.append({"name": f"{fmt}::{stem}", "deck": cod, "deck_numbering": npath,
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
