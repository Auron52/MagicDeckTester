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


def ci_entry(d, name):
    """-> the entry of directory `d` named `name`, matched CASE-INSENSITIVELY (exact match first), or None.

    WHY: the layout says decks/<Name>/<Name>.cod, but nothing enforces the CASE of the file stem, and
    a case-insensitive filesystem (the user's Windows viewer) cannot tell the difference. Soldiers
    shipped as decks/Soldiers/soldiers.cod: on Windows it played and saved references normally, while
    on Linux every exact-name resolver (this registry, ref_bench, viewer_protocol_check, the viewer's
    deck list) silently found NO deck -- its six hand-played references were never replayed. So the
    stem is matched the way the user's filesystem matches it.

    The EXACT match must come from the directory listing, not os.path.exists: on a case-insensitive
    filesystem (this container's /workspaces mount) exists("Soldiers/Soldiers.cod") is True for
    soldiers.cod, so this returned the name it was ASKED for rather than the one on disk -- the stem
    came back "Soldiers", every sidecar path was derived from it, and suite_gate.py refused the deck as
    "not in the regression suite" (its key maps soldiers.cod). 2026-10-08."""
    try:
        entries = os.listdir(d)
    except OSError:
        return None
    if name in entries:
        return name
    hits = [e for e in entries if e.lower() == name.lower()]
    return hits[0] if len(hits) == 1 else None


def _deck_at(d, stem):
    """-> (decklist path, ACTUAL file stem) if `d` holds <stem>.cod|.txt AND <stem>.profile.json
    (stem matched case-insensitively -- see ci_entry), else None."""
    for ext in (".cod", ".txt"):
        hit = ci_entry(d, stem + ext)
        if not hit:
            continue
        fstem = hit[:-len(ext)]
        if not ci_entry(d, fstem + ".profile.json") == fstem + ".profile.json":
            return None   # never measured at shipped play -> not a deck this tooling can describe
        return ("%s/%s" % (d, hit), fstem)
    return None


def _unique_decklist_stem(d):
    """-> the file stem of `d`'s ONE top-level decklist (.cod|.txt), or None (none, or several).

    WHY: decks/Vampires ships Vampire.cod / Vampire.profile.json -- named after neither the folder nor
    a parent -- so the folder-name lookup found no deck and valueleaf.sh refused it ("no registry entry
    for deck dir decks/Vampires"). scripts/mullgen.sh (eeb55d83a) and suite_gate.resolve_decklist
    already fall back to the folder's unique decklist; this is the same rule. Unique only: a folder
    holding several lists is ambiguous and stays undiscovered rather than have one picked. 2026-10-21."""
    try:
        lists = [e for e in os.listdir(d)
                 if e.lower().endswith((".cod", ".txt")) and os.path.isfile(os.path.join(d, e))]
    except OSError:
        return None
    return os.path.splitext(lists[0])[0] if len(lists) == 1 else None


def discover(root="decks"):
    """-> {key: Deck} for every deck folder that has a decklist AND a profile, and is not marked
    goldfish-disabled by a modes marker (deck_modes below; its variants are skipped with it).

    ARCHIVED / VARIANT LISTS. A deck folder may hold an immediate subdirectory carrying its OWN
    copy of the same-stem files (`decks/<Name>/<Variant>/<Name>.cod` + `.profile.json`) -- an
    earlier version of the list, kept beside the one that ships together with the artifacts fitted
    to it. Those are real, addressable decks (references and ground truth still point at them), so
    they are discovered under a compound key `<name>_<variant>`, while the BARE `<name>` key always
    means the list that currently ships. Files inside a variant are named after the PARENT deck
    rather than the subdirectory, because the engine resolves every sidecar directory-relative off
    the profile -- so a variant is found by looking for the parent's stem inside it.

    ONE SHARED FIELD, AND ITS COLLISION IS REACHABLE: `staged` is keyed by stem, so a variant and
    its parent name the same `logs/eval/<stem>.value.STAGED.json`. This used to say staging "is
    transient and only ever done for the shipping list, so this is left as-is" -- that premise is
    now false. Fungus candidate-b is a variant carried in the regression suite as `fungusb` and
    staged in its own right, so a candidate-b value-leaf run writes the path the INCUMBENT's run
    writes, and would clobber it (or, before its own phase B lands, be read as though the
    incumbent's model were its own -- a model fitted to a different decklist).

    Until `staged` is keyed by `key` for a variant, the mitigation is manual and belongs to whoever
    starts such a run: move any existing file aside first, under the self-describing name
    `logs/eval/<stem>.<which-list>.value.STAGED.json` that valueleaf.sh already recommends for the
    adjacent list-replacement case. Done once on 2026-10-04 for
    `Fungus.incumbent-169bf491-20260921.value.STAGED.json`.

    The reason it is not simply changed here: `logs/eval/<stem>.value.STAGED.json` is hardcoded at
    seven sites in valueleaf.sh plus three per-deck chain scripts, and a single missed site reads or
    writes a different path than the rest -- silent, and precisely the failure this field already
    has. It wants one helper and all call sites moved together, not a one-line change to this
    property. Note the DISTINCTION that makes it safe to do later: the staged path lives under
    logs/eval/ and may be keyed however we like, whereas a sidecar INSIDE a deck dir must keep the
    parent's stem, because the engine resolves it directory-relative off the profile."""
    out = {}
    for d in sorted(glob.glob("%s/*" % root)):
        if not os.path.isdir(d):
            continue
        stem = os.path.basename(d)
        hit = _deck_at(d, stem)
        if not hit:
            alt = _unique_decklist_stem(d)
            hit = _deck_at(d, alt) if alt else None
        if not goldfish_enabled(d):
            continue   # goldfish-disabled (decks/<Name>/<Name>.modes.json): not a deck this tooling scores
        if hit:
            # The Deck's stem is the FILE stem (it names the sidecars); the key is the FOLDER's slug.
            out[slug(stem)] = Deck(slug(stem), d, hit[1], hit[0])
        for sub in sorted(glob.glob("%s/*" % d)):
            if not os.path.isdir(sub):
                continue
            # Variant files carry the parent's FILE stem (sidecars resolve off the profile).
            sub_hit = _deck_at(sub, hit[1] if hit else stem)
            if sub_hit:
                key = "%s_%s" % (slug(stem), slug(os.path.basename(sub)))
                out[key] = Deck(key, sub, sub_hit[1], sub_hit[0])
    _assign_seed_bases(out)
    return out


# ---------------------------------------------------------------------------------------------------
# PER-DECK MODES MARKER: decks/<Name>/<Name>.modes.json  (USER 2026-11-26)
#
#   {"goldfish": false, "note": "<why>"}
#
# THE ONE PYTHON READER (the JS twin is tools/lib/deck_modes.js; keep the two in step). A MISSING file
# means every mode keeps its default, so every deck without one is unchanged. "goldfish": false marks a
# deck that must not be scored by goldfishing (a lock / control deck such as Enchanted Evening): the
# registry does not discover it, the viewers do not list it, suite_gate.py refuses it for the regression
# suite, and mullgen.sh / valueleaf.sh refuse to generate for it. An EXPLICIT path (mtg, analyze_deck.py
# decks/<Name>/...) still works for development. The file is meant to grow: a future control-scoring
# mode will be switched on per deck here. The USER's words: "Maybe just add a way to mark something as
# disabled for goldfish rather than moving it into another worktree? Realistically we'll need this anyway
# as decks like this one don't want to be doing goldfishing."
#
# A marker that does not parse (or is not a JSON object) reads as goldfish DISABLED with an "error"
# key: a typo in a file whose only job is to switch goldfishing off must not quietly switch it back on.
# A variant / archived list (decks/<Name>/<Variant>/) inherits its parent folder's marker.
MODES_SUFFIX = ".modes.json"


def _modes_file_in(d):
    """-> the path of directory `d`'s modes marker, or None. `<folder>.modes.json` (case-insensitive)
    first, else the folder's ONE `*.modes.json` (a list named after neither, like decks/Vampires)."""
    hit = ci_entry(d, os.path.basename(os.path.normpath(d)) + MODES_SUFFIX)
    if not hit:
        try:
            marks = [e for e in os.listdir(d) if e.lower().endswith(MODES_SUFFIX)]
        except OSError:
            return None
        hit = marks[0] if len(marks) == 1 else None
    return os.path.join(d, hit) if hit else None


def deck_modes(path):
    """-> the modes dict for the deck at `path` (a deck folder, or any file inside one: decklist,
    profile, sidecar). {} when there is no marker. A variant folder (decks/<Name>/<Variant>/) inherits
    its parent's marker. Only folders BELOW a `decks` directory are consulted, never `decks` itself."""
    import json
    d = os.path.abspath(path if os.path.isdir(path) else os.path.dirname(os.path.abspath(path)))
    cands = [d]
    parent = os.path.dirname(d)
    if os.path.basename(os.path.dirname(parent)) == "decks":
        cands.append(parent)          # decks/<Name>/<Variant> -> also decks/<Name>
    for c in cands:
        f = _modes_file_in(c)
        if not f:
            continue
        try:
            with open(f) as fh:
                m = json.load(fh)
            if not isinstance(m, dict):
                raise ValueError("not a JSON object")
        except (OSError, ValueError) as e:
            return {"goldfish": False, "error": "unreadable modes marker %s: %s" % (f, e), "file": f}
        m = dict(m)
        m["file"] = f
        return m
    return {}


def goldfish_enabled(path):
    """False when the deck at `path` carries a modes marker with "goldfish": false (or an unreadable one)."""
    return deck_modes(path).get("goldfish", True) is not False


def goldfish_disabled_reason(path):
    """-> a one-line human reason when goldfish is disabled for the deck at `path`, else None."""
    m = deck_modes(path)
    if m.get("goldfish", True) is not False:
        return None
    why = m.get("error") or m.get("note") or "no note"
    return "goldfish is DISABLED for this deck by %s (%s)" % (m.get("file", "its modes marker"), why)


# Which LIST a folder of hand-played references was played on.
#
# EMPTY BY DESIGN -- superseded by the reference folder LAYOUT (see ref_bench.ref_dirs).
# `references/<Deck>/` holds the games played on the list that currently ships and
# `references/<Deck>/<Variant>/` holds the games played on an archived list, mirroring
# `decks/<Deck>/<Variant>/`, so the folder itself names the deck and nothing has to be mapped.
#
# This dict was the previous answer and it could only ever name ONE list per folder. That is what
# broke: after a shipping list is replaced, new references accumulate in the same folder beside the
# old ones, and no folder->deck binding can describe a MIXTURE. Mirrorwing hit it exactly -- 9 games
# played on the shipping list sat in a folder bound to the archived v1 list, so each was replayed
# against cards that were never in the deck it was played on. Those rows did report HAND-MISMATCH,
# but as a per-game flag rather than an error, and the deck-level summary still printed a shortfall
# count computed from them.
#
# Kept as an empty dict (not deleted) because it is the documented escape hatch for a folder whose
# layout genuinely cannot express its owner, and because tools/play/server.js parses this symbol.
# Anything absent resolves to its own slug.
REFERENCE_DECK = {}


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
    if len(sys.argv) > 1 and sys.argv[1] == "--goldfish-check":
        # Shell gate (mullgen.sh / valueleaf.sh): exit 0 when goldfish is enabled for every path given,
        # 4 (the reason on stderr) when any is goldfish-disabled.
        rc = 0
        for p in sys.argv[2:]:
            why = goldfish_disabled_reason(p)
            if why:
                sys.stderr.write("%s: %s\n" % (p, why))
                rc = 4
        sys.exit(rc)
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
