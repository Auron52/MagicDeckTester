#!/usr/bin/env python3
"""Every deck folder resolves in every deck resolver, whatever the CASE of its decklist's file stem.

Soldiers shipped as decks/Soldiers/soldiers.cod. On the user's (case-insensitive) Windows viewer it
played and saved references normally; on Linux every exact-name resolver found NO deck, so
scripts/deck_registry.discover() (ref_bench, valueleaf.sh), test/viewer_protocol_check.py and
tools/play/server.js listDecks() all silently skipped it -- six hand-played references never replayed.

This pins (1) a synthetic mixed-case deck resolving through the Python resolvers, and (2) the fleet
invariant: a folder decks/<X>/ holding <x>.cod|.txt + <x>.profile.json (any case) is discovered.
Run: python3 test/deck_case_resolution_check.py   (exit 1 on failure)
"""
import os, shutil, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
sys.path.insert(0, os.path.join(ROOT, "test"))
import deck_registry
import viewer_protocol_check as vpc

fails = []


def chk(ok, msg):
    print(("  ok: " if ok else "  FAIL: ") + msg)
    if not ok:
        fails.append(msg)


# (1) synthetic tree: decks/Foo Bar/foo bar.cod (+ profile), and a variant decks/Foo Bar/v1-x/FOO BAR.txt
scratch = tempfile.mkdtemp(prefix="deckcase_", dir=os.path.join(ROOT, "logs") if os.path.isdir(os.path.join(ROOT, "logs")) else None)
try:
    d = os.path.join(scratch, "decks", "Foo Bar")
    os.makedirs(os.path.join(d, "v1-x"))
    for p in ("foo bar.cod", "foo bar.profile.json", "v1-x/FOO BAR.txt", "v1-x/FOO BAR.profile.json"):
        open(os.path.join(d, p), "w").write("{}\n")
    got = deck_registry.discover(os.path.join(scratch, "decks"))
    chk("foo_bar" in got and got["foo_bar"].deck_file.endswith("/foo bar.cod")
        and got["foo_bar"].profile.endswith("/foo bar.profile.json"),
        "deck_registry discovers a lowercase-stem decklist under its folder's key")
    chk("foo_bar_v1_x" in got and got["foo_bar_v1_x"].deck_file.endswith("/FOO BAR.txt"),
        "deck_registry discovers a variant whose stem differs in case")
    saved = vpc._REPO_ROOT
    vpc._REPO_ROOT = scratch
    try:
        chk(vpc._resolve_deck_dir("Foo_Bar") == ("decks/Foo Bar/foo bar.cod", "decks/Foo Bar/foo bar.profile.json"),
            "viewer_protocol_check resolves references/Foo_Bar to the lowercase-stem decklist")
        chk(vpc._resolve_deck_dir("Foo_Bar/v1-x") == ("decks/Foo Bar/v1-x/FOO BAR.txt", "decks/Foo Bar/v1-x/FOO BAR.profile.json"),
            "viewer_protocol_check resolves a versioned reference dir case-insensitively")
    finally:
        vpc._REPO_ROOT = saved
finally:
    shutil.rmtree(scratch, ignore_errors=True)

# (2) fleet invariant on the real decks/ tree
os.chdir(ROOT)
found = deck_registry.discover()
for name in sorted(os.listdir("decks")):
    dd = os.path.join("decks", name)
    if not os.path.isdir(dd):
        continue
    entries = {e.lower() for e in os.listdir(dd)}
    has_list = any(name.lower() + ext in entries for ext in (".cod", ".txt"))
    if has_list and name.lower() + ".profile.json" in entries:
        chk(deck_registry.slug(name) in found, "decks/%s is discovered" % name)

print("FAIL (%d)" % len(fails) if fails else "PASS")
sys.exit(1 if fails else 0)
