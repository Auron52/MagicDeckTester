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


def _deck_at(d, stem):
    """-> the decklist path if `d` holds <stem>.cod|.txt AND <stem>.profile.json, else None."""
    deck_file = next((p for p in ("%s/%s.cod" % (d, stem), "%s/%s.txt" % (d, stem))
                      if os.path.exists(p)), None)
    if not deck_file:
        return None
    if not os.path.exists("%s/%s.profile.json" % (d, stem)):
        return None       # never measured at shipped play -> not a deck this tooling can describe
    return deck_file


def discover(root="decks"):
    """-> {key: Deck} for every deck folder that has a decklist AND a profile.

    ARCHIVED / VARIANT LISTS. A deck folder may hold an immediate subdirectory carrying its OWN
    copy of the same-stem files (`decks/<Name>/<Variant>/<Name>.cod` + `.profile.json`) -- an
    earlier version of the list, kept beside the one that ships together with the artifacts fitted
    to it. Those are real, addressable decks (references and ground truth still point at them), so
    they are discovered under a compound key `<name>_<variant>`, while the BARE `<name>` key always
    means the list that currently ships. Files inside a variant are named after the PARENT deck
    rather than the subdirectory, because the engine resolves every sidecar directory-relative off
    the profile -- so a variant is found by looking for the parent's stem inside it.

    One shared field to be aware of: `staged` is keyed by stem, so a variant and its parent name the
    same `logs/eval/<stem>.value.STAGED.json`. Staging is transient and only ever done for the
    shipping list, so this is left as-is rather than inventing a second naming convention."""
    out = {}
    for d in sorted(glob.glob("%s/*" % root)):
        if not os.path.isdir(d):
            continue
        stem = os.path.basename(d)
        deck_file = _deck_at(d, stem)
        if deck_file:
            out[slug(stem)] = Deck(slug(stem), d, stem, deck_file)
        for sub in sorted(glob.glob("%s/*" % d)):
            if not os.path.isdir(sub):
                continue
            sub_file = _deck_at(sub, stem)
            if sub_file:
                key = "%s_%s" % (slug(stem), slug(os.path.basename(sub)))
                out[key] = Deck(key, sub, stem, sub_file)
    _assign_seed_bases(out)
    return out


# Which LIST a folder of hand-played references was played on.
#
# This CANNOT be derived. A reference JSON records seeds, mulligans and decisions but no decklist,
# and the folder name slugs the deck FAMILY, not the version -- so when a deck's shipping list is
# replaced, its existing references silently start resolving to the new list. Replaying a recorded
# human line against cards that were never in that deck produces a benchmark that means nothing and
# reports no error, which is exactly the failure this module was written to end.
#
# Only exceptions need an entry; anything absent resolves to its own slug.
REFERENCE_DECK = {
    # The 24 Mirrorwing references were hand-played on the Twinflame / Ancestral Anger / Scale the
    # Heights / Expedite list, archived 2026-08-22 when the tournament-winning suite (Oracle /
    # Draught / Impolite Entrance / Luxurious Libation) took over the shipping slot.
    "mirrorwing_dragon": "mirrorwing_dragon_v1_twinflame_anger",
}


def reference_deck_key(ref_slug):
    """-> the key of the deck a reference folder's games were actually played on."""
    return REFERENCE_DECK.get(ref_slug, ref_slug)


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
