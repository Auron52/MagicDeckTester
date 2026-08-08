"""The ONE place a deck's files are located. Discovery only -- there is no list to maintain.

Every deck lives in `decks/<Name>/` and every artifact is named after the folder (see CLAUDE.md and
docs/design/per-deck-folder-layout.md), so a deck's whole file set is a pure function of its folder:

    decks/<Name>/<Name>.cod | .txt        decklist
    decks/<Name>/<Name>.profile.json      play/mulligan profile   (REQUIRED -- see below)
    decks/<Name>/<Name>.value.json        live value sidecar      (absent = no model yet)
    logs/eval/<Name>.value.STAGED.json    staged sidecar, not yet adopted

This module existed as three hand-maintained dicts in two scripts, and each one failed the same way:
an unlisted deck did not error. The matrix's ladder is guarded on `os.path.exists(<value.json>)`, so a
missing entry silently ran every H cell on the slow path; the metadata writer printed
"SKIP (no metadata path mapped)" and returned success, which would have thrown away an 8-hour table.
A registry that must be edited by hand is a registry that is wrong.

A deck with NO profile is skipped everywhere: it has never been measured at shipped play, so any
number taken from it describes a deck we do not ship.
"""
import glob
import os
import zlib

MAX_TURNS = 8          # every deck; a per-deck value was listed 10 times and was 8 in all 10
STAGED_SUFFIX = "_staged"
STAGED_DIR = "logs/eval"


def slug(name):
    """Folder name -> registry key. 'FiveColour' -> 'fivecolour', 'Creature Giving' -> creature_giving."""
    return "".join(c.lower() if c.isalnum() else "_" for c in name)


class Deck(object):
    __slots__ = ("key", "dir", "stem", "deck_file", "profile", "value", "staged", "seed_base", "max_turns")

    def __init__(self, key, d, stem, deck_file):
        self.key = key
        self.dir = d
        self.stem = stem
        self.deck_file = deck_file
        self.profile = "%s/%s.profile.json" % (d, stem)
        self.value = "%s/%s.value.json" % (d, stem)
        self.staged = "%s/%s.value.STAGED.json" % (STAGED_DIR, stem)
        self.max_turns = MAX_TURNS
        self.seed_base = None      # assigned by discover()

    def __repr__(self):
        return "Deck(%s)" % self.key


def discover(root="decks"):
    """-> {key: Deck} for every deck folder that has a decklist AND a profile."""
    out = {}
    for d in sorted(glob.glob("%s/*" % root)):
        if not os.path.isdir(d):
            continue
        stem = os.path.basename(d)
        deck_file = next((p for p in ("%s/%s.cod" % (d, stem), "%s/%s.txt" % (d, stem))
                          if os.path.exists(p)), None)
        if not deck_file:
            continue
        if not os.path.exists("%s/%s.profile.json" % (d, stem)):
            continue      # never measured at shipped play -> not a deck this tooling can describe
        out[slug(stem)] = Deck(slug(stem), d, stem, deck_file)
    _assign_seed_bases(out)
    return out


def _assign_seed_bases(decks):
    """Row-dump seed base per deck: a distinct multiple of 100,000.

    Pooled row files recover a deck with `seed // 100000`, so the bases must be distinct and must not
    move. Derived from a hash of the KEY rather than from position, so adding or removing a deck can
    never renumber the others and orphan rows already on disk. Collisions probe upward in sorted key
    order, which is likewise independent of what else was discovered."""
    taken = {}
    for key in sorted(decks):
        slot = zlib.crc32(key.encode("utf-8")) % 900 + 1
        while slot in taken:
            slot = slot % 900 + 1
        taken[slot] = key
        decks[key].seed_base = slot * 100000


def resolve(key, decks=None):
    """Look up a key, transparently handling the '<key>_staged' form.

    A staged key is the same deck, the same profile and the same decklist, pointing at a model that is
    NOT yet installed in the deck folder -- the order the pipeline requires (measure, then adopt).
    Returns (Deck, value_path) where value_path is the staged sidecar for a staged key."""
    if decks is None:
        decks = discover()
    if key.endswith(STAGED_SUFFIX):
        base = decks.get(key[:-len(STAGED_SUFFIX)])
        return (base, base.staged) if base else (None, None)
    d = decks.get(key)
    return (d, d.value) if d else (None, None)


def all_keys(decks=None, staged=True):
    """Every key, plain and (by default) staged, in sorted order."""
    if decks is None:
        decks = discover()
    keys = sorted(decks)
    return keys + [k + STAGED_SUFFIX for k in keys] if staged else keys


if __name__ == "__main__":
    import sys
    ds = discover()
    if len(sys.argv) > 1 and sys.argv[1] == "--shell":
        # key|dir|stem|key|seed_base|row_games -- the driver's DECK_TABLE, generated not maintained.
        games = sys.argv[2] if len(sys.argv) > 2 else "2500"
        for k in sorted(ds):
            d = ds[k]
            print("%s|%s|%s|%s|%d|%s" % (d.key, d.dir, d.stem, d.key, d.seed_base, games))
    else:
        for k in sorted(ds):
            d = ds[k]
            print("%-18s %-34s base=%-9d value=%s staged=%s"
                  % (d.key, d.deck_file, d.seed_base,
                     "yes" if os.path.exists(d.value) else "no",
                     "yes" if os.path.exists(d.staged) else "no"))
