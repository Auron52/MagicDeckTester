#!/usr/bin/env python3
"""Report which archetype DecisionProvider each deck actually resolves to.

WHY THIS EXISTS. `SelectDecisionProvider` detects a deck's archetype from CARD PARAMS, and
several of those params are archetype-NEUTRAL: `etb_self_creates_tokens`, `sac_creature_outlet`
and `reduces_spell_subtype` describe what a card DOES, not which deck it belongs to. So a deck
that merely contains such a card inherits another archetype's narrowing heuristics -- silently,
because nothing errors. That has now happened four times:

  Mirrorwing      Goblin Instigator's etb_self_creates_tokens        -> GoblinsProvider
  StompySurprise  Hornet Queen's etb_self_creates_tokens             -> GoblinsProvider
  Minotaur        Slaughter-Priest's sac_creature_outlet             -> GoblinsProvider
  Dragons         Dragonspeaker Shaman's reduces_spell_subtype       -> GoblinsProvider

Each was found by accident, after the deck had already been measured -- and in Minotaur's case
the borrowed hook did not merely reorder play, it DELETED a real decision branch (a sacrifice
outlet deferred to a second main the deck does not have).

The engine is the only trustworthy oracle here: re-deriving the detection rules in Python would
just be a second implementation to drift. So this runs `mtg --batch` and reads back the
`provider=` the engine itself prints.

USAGE
    python3 scripts/provider_audit.py                 # every deck under decks/
    python3 scripts/provider_audit.py decks/Dragons   # one deck
    python3 scripts/provider_audit.py --check         # exit 1 if any deck looks misrouted

`--check` is a HEURISTIC screen, not a verdict: it flags a deck whose provider is neither
Generic nor a name-similar match. A flag means "explain this", and the explanation may well be
"intended" -- Knights and slivers_vial both ride VialProvider on purpose. What must never happen
is a deck landing on a foreign provider with nobody having decided that.
"""
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "build", "Release", "mtg")


def discover(args):
    dirs = [a for a in args if not a.startswith("-")]
    if not dirs:
        base = os.path.join(ROOT, "decks")
        dirs = [os.path.join(base, d) for d in sorted(os.listdir(base))
                if os.path.isdir(os.path.join(base, d))]
    out = []
    for d in dirs:
        for f in sorted(os.listdir(d)):
            if not f.endswith(".profile.json"):
                continue
            stem = f[: -len(".profile.json")]
            for ext in (".cod", ".txt"):
                deck = os.path.join(d, stem + ext)
                if os.path.exists(deck):
                    out.append((stem, deck, os.path.join(d, f)))
                    break
    return out


def main():
    argv = sys.argv[1:]
    check = "--check" in argv
    decks = discover(argv)
    if not decks:
        print("no decks found", file=sys.stderr)
        return 2
    if not os.path.exists(BIN):
        print(f"missing {BIN} -- run ./build.sh first", file=sys.stderr)
        return 2

    # depth 1 + ignore_play_profile: the provider is printed per job at engine construction, and a
    # deck shipping an enabled value_play refuses an explicit --depth without the override. One
    # game per deck -- this reads a routing decision, not a result.
    jobs = [{"name": stem.replace(" ", "_"), "deck": deck, "profile": prof,
             "games": 1, "seed": 1, "depth": 1, "ignore_play_profile": True}
            for stem, deck, prof in decks]
    man = os.path.join(ROOT, "logs", "provider_audit.manifest.json")
    os.makedirs(os.path.dirname(man), exist_ok=True)
    with open(man, "w") as fh:
        json.dump({"jobs": jobs}, fh, indent=1)

    proc = subprocess.run([BIN, "--batch", man], capture_output=True, text=True, cwd=ROOT)
    found = {}
    for line in (proc.stdout + proc.stderr).splitlines():
        m = re.match(r"\[play\] (\S+) .*provider=(\w+)", line)
        if m:
            found[m.group(1)] = m.group(2)

    width = max(len(s) for s, _, _ in decks)
    suspects = []
    for stem, _, _ in decks:
        key = stem.replace(" ", "_")
        prov = found.get(key, "?")
        norm = re.sub(r"[^a-z]", "", stem.lower())
        pnorm = prov.lower()
        ok = prov in ("Generic", "?") or pnorm in norm or norm in pnorm
        if not ok:
            suspects.append((stem, prov))
        print(f"  {stem:<{width}}  {prov}{'' if ok else '   <-- REVIEW: foreign provider'}")

    if suspects:
        print("\nDecks on a provider that does not match their name:")
        for stem, prov in suspects:
            print(f"  {stem} -> {prov}")
        print("\nThis is a SCREEN, not a verdict. Confirm each is deliberate (Knights and")
        print("slivers_vial ride VialProvider on purpose). If one is NOT, route it above the")
        print("offending branch in SelectDecisionProvider -- see the Dragons block there.")
    return 1 if (check and suspects) else 0


if __name__ == "__main__":
    sys.exit(main())
