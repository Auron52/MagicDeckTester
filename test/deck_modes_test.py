#!/usr/bin/env python3
"""Test of the per-deck modes marker decks/<Name>/<Name>.modes.json (USER 2026-11-26) and its consumers.

The marker's ONE Python reader is scripts/deck_registry.py (deck_modes / goldfish_enabled /
goldfish_disabled_reason, plus `--goldfish-check`); its ONE JS reader is tools/lib/deck_modes.js. This test
builds a throwaway decks/ tree and checks that both readers agree on every case (no marker, goldfish false,
goldfish true, an unreadable marker, a variant folder inheriting its parent's marker), that
deck_registry.discover() skips a goldfish-disabled deck and its variants, and then checks the real repo:
Enchanted Evening is the only marked deck, suite_gate.py refuses it (--require and --cost exit 3), the play
viewer's listDecks() and the mulligan app's deck list do not offer it, and an unmarked deck passes.

    python3 test/deck_modes_test.py
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import deck_registry  # noqa: E402

FAILS = []
EE = "decks/Enchanted Evening"


def check(cond, what, detail=None):
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        FAILS.append(what)
        if detail is not None:
            print("      %s" % str(detail).replace("\n", "\n      "))


def run(*cmd, cwd=ROOT):
    return subprocess.run(list(cmd), cwd=cwd, capture_output=True, text=True)


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as fh:
        fh.write(text)


def js_enabled(paths):
    """-> {path: bool} from the JS reader, one node process."""
    prog = ("const m = require(%s); const out = {};"
            "for (const p of JSON.parse(process.argv[1])) out[p] = m.goldfishEnabled(p);"
            "process.stdout.write(JSON.stringify(out));") % json.dumps(os.path.join(ROOT, "tools/lib/deck_modes.js"))
    r = run("node", "-e", prog, json.dumps(paths))
    return json.loads(r.stdout) if r.returncode == 0 else {"error": r.stderr}


def main():
    tmp = tempfile.mkdtemp(prefix="deck_modes_test_")
    try:
        decks = os.path.join(tmp, "decks")
        for name in ("Plain", "Parked", "Typo", "Explicit"):
            write(os.path.join(decks, name, name + ".cod"), "4 Forest\n")
            write(os.path.join(decks, name, name + ".profile.json"), "{}\n")
        write(os.path.join(decks, "Parked", "Parked.modes.json"), '{"goldfish": false, "note": "a lock deck"}\n')
        write(os.path.join(decks, "Parked", "v1-old", "Parked.cod"), "4 Forest\n")
        write(os.path.join(decks, "Parked", "v1-old", "Parked.profile.json"), "{}\n")
        write(os.path.join(decks, "Typo", "Typo.modes.json"), '{"goldfish": fals\n')
        write(os.path.join(decks, "Explicit", "explicit.modes.json"), '{"goldfish": true, "control": false}\n')
        write(os.path.join(decks, "Plain", "v1-old", "Plain.cod"), "4 Forest\n")
        write(os.path.join(decks, "Plain", "v1-old", "Plain.profile.json"), "{}\n")

        want = {
            os.path.join(decks, "Plain"): True,
            os.path.join(decks, "Plain", "Plain.cod"): True,
            os.path.join(decks, "Plain", "v1-old"): True,
            os.path.join(decks, "Parked"): False,
            os.path.join(decks, "Parked", "Parked.cod"): False,
            os.path.join(decks, "Parked", "Parked.profile.json"): False,
            os.path.join(decks, "Parked", "v1-old"): False,              # a variant inherits its parent's marker
            os.path.join(decks, "Parked", "v1-old", "Parked.cod"): False,
            os.path.join(decks, "Typo"): False,                          # unreadable -> disabled, never re-enabled
            os.path.join(decks, "Explicit"): True,                       # explicit true; case-insensitive stem
        }
        for p, w in want.items():
            check(deck_registry.goldfish_enabled(p) == w, "python: %s -> %s" % (os.path.relpath(p, tmp), w))
        check("error" in deck_registry.deck_modes(os.path.join(decks, "Typo")), "python: an unreadable marker carries an error")
        check(deck_registry.deck_modes(os.path.join(decks, "Plain")) == {}, "python: no marker -> {}")
        check(deck_registry.deck_modes(os.path.join(decks, "Explicit")).get("control") is False,
              "python: other keys of the marker are returned (the file is meant to grow)")
        why = deck_registry.goldfish_disabled_reason(os.path.join(decks, "Parked"))
        check(why is not None and "a lock deck" in why, "python: the reason quotes the marker's note", why)

        js = js_enabled(list(want))
        check(js == {p: w for p, w in want.items()}, "js reader agrees with the python reader on every case", js)

        found = deck_registry.discover(root=decks)
        check(sorted(found) == ["explicit", "plain", "plain_v1_old"],
              "discover() skips the goldfish-disabled deck and its variant", sorted(found))
        check("typo" not in found, "discover() skips a deck whose marker does not parse", sorted(found))

        r = run(sys.executable, os.path.join(ROOT, "scripts/deck_registry.py"), "--goldfish-check",
                os.path.join(decks, "Parked"))
        check(r.returncode == 4 and "a lock deck" in r.stderr, "--goldfish-check exits 4 with the reason", r.stderr)
        r = run(sys.executable, os.path.join(ROOT, "scripts/deck_registry.py"), "--goldfish-check",
                os.path.join(decks, "Plain"))
        check(r.returncode == 0, "--goldfish-check exits 0 for an unmarked deck", r.stderr)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    # ---- the real repo
    marked = sorted(d for d in os.listdir(os.path.join(ROOT, "decks"))
                    if os.path.isdir(os.path.join(ROOT, "decks", d))
                    and not deck_registry.goldfish_enabled(os.path.join(ROOT, "decks", d)))
    check(marked == ["Enchanted Evening"], "real repo: Enchanted Evening is the only goldfish-disabled deck", marked)
    ee = os.path.join(ROOT, EE)
    for f in os.listdir(ee):
        check(f.endswith((".cod", ".modes.json")),
              "real repo: %s/%s is a decklist or the marker (no profile / leaf / keep table for a parked deck)" % (EE, f))
    for mode in ("--require", "--cost"):
        r = run(sys.executable, "scripts/suite_gate.py", mode, EE)
        check(r.returncode == 3 and "goldfish is DISABLED" in r.stderr, "suite_gate.py %s refuses %s (exit 3)" % (mode, EE),
              r.stdout + r.stderr)
    r = run(sys.executable, "scripts/suite_gate.py", "--require", "decks/Hinata")
    check(r.returncode == 0, "suite_gate.py --require still passes an unmarked suite deck", r.stdout + r.stderr)
    real = deck_registry.discover(root=os.path.join(ROOT, "decks"))
    check("enchanted_evening" not in real and "hinata" in real,
          "real repo: the registry does not discover Enchanted Evening (and still finds Hinata)")
    prog = ("const s = require(%s); const ds = s.listDecks();"
            "process.stdout.write(JSON.stringify(ds.map(d => d.name)));") % json.dumps(os.path.join(ROOT, "tools/play/server.js"))
    r = run("node", "-e", prog)
    names = json.loads(r.stdout) if r.returncode == 0 else []
    check(r.returncode == 0 and "Hinata" in names and "Enchanted Evening" not in names,
          "play viewer listDecks() hides Enchanted Evening and still lists Hinata", r.stderr[-400:])
    r = run("node", "tools/mulligan/build_deck_list.js", "--check")
    check(r.returncode == 0, "tools/mulligan/decks.js is current (build_deck_list.js --check)", r.stderr)
    with open(os.path.join(ROOT, "tools/mulligan/decks.js")) as fh:
        check("Enchanted Evening" not in fh.read(), "the mulligan app's deck list does not offer Enchanted Evening")

    print("\n%s" % ("ALL PASS" if not FAILS else "%d FAILED" % len(FAILS)))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
