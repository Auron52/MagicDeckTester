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
    out, unprofiled = [], []
    for d in dirs:
        profs = [f for f in sorted(os.listdir(d)) if f.endswith(".profile.json")]
        if not profs:
            # A decklist with NO profile is invisible to this audit -- and that is a hole in the
            # always-own-a-provider rule, because a deck can dodge it simply by never being
            # analyzed. Report it rather than silently skipping (the whole point of this script is
            # that nothing routes without somebody deciding).
            for f in sorted(os.listdir(d)):
                if f.endswith((".cod", ".txt")):
                    unprofiled.append(os.path.basename(d))
                    break
            continue
        for f in profs:
            stem = f[: -len(".profile.json")]
            for ext in (".cod", ".txt"):
                deck = os.path.join(d, stem + ext)
                if os.path.exists(deck):
                    out.append((stem, deck, os.path.join(d, f)))
                    break
    return out, sorted(set(unprofiled))


def main():
    argv = sys.argv[1:]
    check = "--check" in argv
    decks, unprofiled = discover(argv)
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

    # ---- certificate stance, parsed from the header ------------------------------------------
    # The compiler forces a NEW provider to answer Certificate() (DeckProvider's pure virtual), but
    # it CANNOT catch a provider that inherits an ANSWER from another deck's provider -- e.g.
    # KnightsProvider deriving from VialProvider. That loophole is exactly the laziness the rule
    # exists to remove, so it is closed here by requiring the declaration in the class's OWN body.
    hdr = open(os.path.join(ROOT, "src", "ai", "DecisionProviders.h")).read()
    stance, missing_cert = {}, []
    for m in re.finditer(r"^class (\w+)Provider : public (\w+)\b", hdr, re.M):
        name, base = m.group(1), m.group(2)
        if name in ("Generic", "Deck"):
            continue
        body = hdr[m.end(): hdr.index("\n};", m.end())]
        cm = re.search(r"CertStance Certificate\(\) const override\s*\{?\s*return\s*\{\s*CertState::(\w+)", body)
        if cm:
            stance[name] = cm.group(1)
        else:
            missing_cert.append((name, base))

    width = max(len(s) for s, _, _ in decks)
    suspects, generic, shared = [], [], {}
    for stem, _, _ in decks:
        key = stem.replace(" ", "_")
        prov = found.get(key, "?")
        norm = re.sub(r"[^a-z]", "", stem.lower())
        pnorm = prov.lower()
        named = pnorm in norm or norm in pnorm
        if prov == "Generic":
            generic.append(stem)
        if prov not in ("?",):
            shared.setdefault(prov, []).append(stem)
        if not (prov in ("Generic", "?") or named):
            suspects.append((stem, prov))
        cert = stance.get(prov, "-")
        flag = ""
        if prov == "Generic":
            flag = "   <-- FAIL: no provider of its own"
        elif not named:
            flag = "   <-- note: name differs (fine if this provider is only this deck's)"
        print(f"  {stem:<{width}}  {prov:<22} cert={cert}{flag}")

    borrowed = {p: d for p, d in shared.items() if len(d) > 1}

    print()
    n_impl = sum(1 for v in stance.values() if v == "Implemented")
    print(f"  winless certificate: {n_impl} implemented, "
          f"{sum(1 for v in stance.values() if v == 'NotAssessed')} not assessed, "
          f"{sum(1 for v in stance.values() if v == 'Inapplicable')} inapplicable")

    if unprofiled:
        print("\n  NOT AUDITED (decklist present, no .profile.json -- routing undecided): "
              + ", ".join(unprofiled))

    hard = []
    if generic:
        hard.append("decks with NO provider of their own (riding GenericProvider): "
                    + ", ".join(generic))
    if borrowed:
        hard.append("providers shared by more than one deck: "
                    + "; ".join(f"{p} <- {', '.join(d)}" for p, d in borrowed.items()))
    if missing_cert:
        hard.append("provider classes that do not declare Certificate() in their OWN body "
                    "(they silently inherit another deck's answer): "
                    + ", ".join(f"{n}Provider(:{b})" for n, b in missing_cert))

    if hard:
        print("\nFAIL -- the always-own-a-provider rule (USER 2026-09-18):")
        for h in hard:
            print("  * " + h)
        print("\nFix: add a provider for the deck in src/ai/DecisionProviders.h deriving from")
        print("DeckProvider (or from the provider it ALREADY routes to, if there is code to")
        print("reuse), give it Name() + Certificate(), and route it in SelectDecisionProvider")
        print("ABOVE whatever branch it currently trips. An empty derivation is play-neutral.")
    elif suspects:
        print("\nName-mismatch notes only (not a failure): ", ", ".join(f"{a}->{b}" for a, b in suspects))

    return 1 if (check and hard) else 0


if __name__ == "__main__":
    sys.exit(main())
