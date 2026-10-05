#!/usr/bin/env python3
"""Report which decks carry an AUTHORED cleanup-discard BUCKET policy -- and which do not.

WHY THIS EXISTS (user, 2026-09-23): "I keep finding problems only when I ask explicitly. That's the
worst case scenario."

The per-deck discard policy is a MANDATED analysis step -- `.claude/skills/analyze-deck.md` 5i, on
the 2026-08-07 ruling that there is no general discard heuristic and the 2026-08-21 ruling that the
shape is BUCKETS -- and until 2026-09-23 nothing enforced it. So a deck that skipped the step did
not error, did not warn, and did not appear anywhere: it simply ran the shared fallback, and the
only thing that ever surfaced the omission was the user asking. At the time this script was written
that had happened to **16 of 25 decks**.

`verify_deck.py`'s `discard_policy` gate closes it going forward, one deck at a time, at onboarding.
This is the FLEET view: the same logic over every deck in `decks/`, in one command, so the state of
the whole repo is one line of output rather than 25 invocations.

The fallback is not a neutral default, which is why a gap is a defect and not a style note. Its
tier B is descending mana value -- "most expensive = most expendable" -- which is backwards for
every payoff, ramp and combo deck here: it ranked Creature Giving's Defense of the Heart FIRST to
pitch (measured a full turn worse, gi564/gi798) and shed FiveColour's Progenitus for a measured
1-turn cost. Nor does a deck that rarely sheds escape it: the ROLLOUT takes index 0 of this ranking
with no search above it (Minotaur measured 99 real sheds against 250,265 inside the search), so the
rule biases every line the search scores even when real play never reaches a cleanup.

STATUSES
    OK                 the provider overrides CleanupDiscardCandidates with an authored policy
    MISSING            no override anywhere in the chain -- the deck sheds by generic max-MV
    PATCH-ONLY         an override with no MTG_*_BUCKET_DISCARD gate: a special case bolted onto
                       the max-MV rule, not a bucket structure
    INHERITS <Base>    no override of its own, but a BASE provider has one -- i.e. the deck is
                       running ANOTHER deck's buckets, keyed on another deck's cards

USAGE
    python3 scripts/discard_policy_audit.py            # the fleet
    python3 scripts/discard_policy_audit.py --check    # exit 1 if any deck has a gap (CI shape)

Requires `build/Release/mtg` (build with ./build.sh) because the provider a deck routes to is read
back from the ENGINE -- `SelectDecisionProvider` keys on card params, so re-deriving that detection
here would only create a second implementation to drift. A deck with no `.profile.json` has never
been measured at shipped play and is reported as unrouted rather than silently skipped.
"""
import importlib.util
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _load_verify_deck():
    """Import verify_deck.py for its gate logic -- imported, never re-implemented, so the fleet
    view and the per-deck gate can never disagree about what counts as an authored policy."""
    path = os.path.join(ROOT, "scripts", "verify_deck.py")
    spec = importlib.util.spec_from_file_location("verify_deck", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def classify(vd, provider, classes):
    """-> (status, detail) for a provider name, using verify_deck's own predicates."""
    cls = provider + "Provider"
    if cls not in classes:
        return "NO CLASS", f"engine reported provider={provider} but {cls} is not in DecisionProviders.h"
    base, own = classes[cls]
    if own:
        body = vd._discard_impl_body(cls)
        if not body:
            return "NO BODY", f"{cls} declares the override but no definition was found"
        authored = bool(vd.DISCARD_FLAG_RE.search(body)) or cls in vd.DISCARD_AUTHORED_NO_FLAG
        if authored:
            note = ("allowlisted authored-without-a-flag"
                    if cls in vd.DISCARD_AUTHORED_NO_FLAG else "")
            return "OK", note
        return "PATCH-ONLY", f"{cls}::CleanupDiscardCandidates carries no MTG_*_BUCKET_DISCARD gate"
    cur = base
    while cur in classes:
        if classes[cur][1]:
            return f"INHERITS {cur}", f"{cls} declares no override of its own"
        cur = classes[cur][0]
    return "MISSING", f"{cls} does not override CleanupDiscardCandidates"


def main():
    check = "--check" in sys.argv[1:]
    vd = _load_verify_deck()

    # ONE engine run for the whole fleet (provider_audit pools every deck into a single --batch),
    # rather than one invocation per deck -- the pooled-queue rule in CLAUDE.md.
    proc = subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "provider_audit.py")],
                          capture_output=True, text=True, cwd=ROOT)
    providers, unrouted = {}, []
    for ln in proc.stdout.splitlines():
        m = re.match(r"\s+(\S.*?)\s\s+(\w+)\s+cert=", ln)
        if m:
            providers[m.group(1).strip()] = m.group(2)
        m2 = re.match(r"\s+NOT AUDITED \([^)]*\):\s*(.+)$", ln)
        if m2:
            unrouted = [s.strip() for s in m2.group(1).split(",") if s.strip()]
    if not providers:
        sys.stderr.write("could not read providers from scripts/provider_audit.py -- is "
                         "build/Release/mtg built? (./build.sh)\n")
        sys.stderr.write(proc.stdout[-800:] + proc.stderr[-800:] + "\n")
        return 2

    classes = vd._provider_classes()
    rows = [(stem, p) + classify(vd, p, classes) for stem, p in sorted(providers.items())]

    w = max(len(r[0]) for r in rows)
    print(f"\n  {'DECK':<{w}}  {'PROVIDER':<22} DISCARD POLICY")
    print("  " + "-" * (w + 42))
    for stem, prov, status, detail in rows:
        gap = status != "OK"
        note = f"   <-- GAP: {detail}" if gap and detail else (f"   ({detail})" if detail else "")
        print(f"  {stem:<{w}}  {prov:<22} {status}{note}")

    gaps = [r for r in rows if r[2] != "OK"]
    print(f"\n  {len(rows) - len(gaps)} of {len(rows)} decks carry an authored bucket policy; "
          f"{len(gaps)} gap(s)")

    if unrouted:
        print("\n  NOT ROUTED (decklist present, no .profile.json -- never measured at shipped "
              "play, so no provider and no policy): " + ", ".join(unrouted))

    if gaps:
        print("\n  Fix, per deck: author the BUCKET policy (docs/design/discard-bucket-authoring-brief.md")
        print("  + analyze-deck 5i), PRESENT IT TO THE USER for confirmation -- adoption is a user")
        print("  review, the same gate as cast order -- then implement it as the provider's")
        print("  CleanupDiscardCandidates override behind a default-on MTG_<DECK>_BUCKET_DISCARD,")
        print("  routed through CleanupDiscardRankingWithOrder, and record the approval under")
        print("  '## Discard policy' in docs/design/analysis-<deck>.md.")

    return 1 if (check and gaps) else 0


if __name__ == "__main__":
    sys.exit(main())
