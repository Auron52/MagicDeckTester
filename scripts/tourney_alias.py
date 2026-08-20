#!/usr/bin/env python3
"""Build an ALIASED apparatus for one map, and run the arms that resolve into it.

    python3 scripts/tourney_alias.py build C          # write logs/tourney/aliasC/
    python3 scripts/tourney_alias.py plan  C          # arms, cells, thread-hours -- measures nothing
    python3 scripts/tourney_alias.py run   C --games 20000

Runs A and B were assembled by hand, which is why their manifests live in two different shapes and
neither can be regenerated. This script is the same construction written down once.

WHAT AN ALIAS MAP IS
The shipped Mirrorwing keep table (K=17, R=40) buckets the deck's own cards. Adding a NEW card's
name to an existing bucket's member list makes that card inherit the bucket's measured keep policy
at zero generation cost. The table is otherwise untouched -- same cells, same values, same commit.

WHAT IT COSTS YOU
A card takes exactly ONE bucket, and a bucket's count cap is the shipped deck's copy count. The
flexible part of this deck is four buckets:

    bucket 2  cap 3   Twinflame            <- Luxurious Libation
    bucket 9  cap 4   Ancestral Anger      <- whichever trick is the 4-of
    bucket 3  cap 2   Scale the Heights    <- a 2-of
    bucket 15 cap 2   Expedite             <- a 2-of

3 + 4 + 2 + 2 = 11 = exactly the flexible slots, so any list of shape `3 A + 4 B + 2 C + 2 D` is
reachable with the identical bucket-count signature (3,4,2,2), and coverage is all-or-nothing
across those arms.

A list that does NOT fit that shape -- two 4-ofs, or a 3-of outside bucket 2 -- over-fills a bucket,
and the hands that over-fill it have no cell. Those hands are NOT an error: the policy answers
present=false and that one arm falls through to the heuristic keep. So the real question is never
"is it reachable" but HOW OFTEN it misses, and `miss_rate` answers that in hands rather than cells.
The two differ by more than an order of magnitude -- `3 Oracle + 3 Draught` lacks 1.8% of its cells
but only 0.10% of its hands -- so a cell count is the wrong thing to decide on.

(The general fix, if a miss rate ever IS material, is the user's 2026-08-20 suggestion: bucket by
card NUMBER rather than name, so copies 44-45 of a card sit in one bucket and 46 in another. That
holds the deck's bucket sizes at (3,4,2,2) for ANY composition, making the whole space reachable
with zero generation, at the cost of a blurrier policy -- the same hand content maps to different
cells depending on which physical copies were drawn. It needs a small engine change:
ExhaustiveKeepPolicy::Decide takes names today, though the call site already has m_number.)

WHY THERE IS MORE THAN ONE MAP
Only one card can be the 4-of, so `4 Oracle + 2 Draught` and `4 Draught + 2 Oracle` cannot share a
table. Each map is a separate run, chained through a BRIDGE arm that holds no aliased card at all
and is therefore identical under every map. If the bridge is not game-for-game identical, the maps
do not chain and nothing may be compared across them -- so `run` puts the bridge in the same pooled
batch under both maps rather than trusting the construction.
"""
import argparse, json, math, os, re, shutil, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from deck_compare import read_decklist, base_numbering, inherit_numbering, table_meta, enum_cells  # noqa: E402

TOURNEY = os.path.join(ROOT, "logs", "tourney")
CARDS   = os.path.join(ROOT, "src", "cards", "data", "cards.json")
NOTABLE = os.path.join(TOURNEY, "notable")

# The map-A apparatus is the source for every later map: its profile carries the POOLED card_scores
# (every arm card scored, so no card is silently valued as an empty slot) and its value model is the
# one every run has used. Copying them byte-for-byte is deliberate -- if maps differed in scoring as
# well as in bucketing, a cross-map comparison would confound the two.
SRC_PROFILE = os.path.join(TOURNEY, "alias", "alias.profile.json")
SRC_VALUE   = os.path.join(TOURNEY, "alias", "alias.value.json")
SRC_TABLE   = os.path.join(TOURNEY, "alias", "alias.keepmodel.exhaustive.profile.json")

TWIN, LIB   = "Twinflame", "Luxurious Libation"
ANGER, SCALE = "Ancestral Anger", "Scale the Heights"
EXPED, ENTR = "Expedite", "Impolite Entrance"
ORACLE, DRAUGHT = "Oracle's Restoration", "Fortifying Draught"

# native bucket card -> the name folded in beside it.
MAPS = {
    "A": {TWIN: LIB, ANGER: ORACLE,  SCALE: DRAUGHT, EXPED: ENTR},
    "B": {TWIN: LIB, ANGER: DRAUGHT, SCALE: ORACLE,  EXPED: ENTR},
    "C": {TWIN: LIB, ANGER: ENTR,    SCALE: DRAUGHT, EXPED: ORACLE},
}

# Numbering is inherited from this arm in every run, so card number 52 names the same SLOT in all of
# them and the paired per-number questions the report asks stay meaningful across maps.
BASE_ARM = os.path.join(TOURNEY, "arms", "tf3lib0_a4o0_scale.txt")

TF_SPLITS = {"tf3lib0": {TWIN: 3}, "tf2lib1": {TWIN: 2, LIB: 1},
             "tf1lib2": {TWIN: 1, LIB: 2}, "tf0lib3": {LIB: 3}}

# The arm that holds NO aliased card: 4 Anger / 2 Scale / 2 Expedite are all bucket natives, so this
# decklist resolves identically under maps A, B and C. It is the only chain between them.
SHIP = {ANGER: 4, SCALE: 2, EXPED: 2}


def flexible(spec):
    """The 11 flexible slots for an arm: a trick split plus the 8 draw/pump cards."""
    out = dict(spec)
    return out


# ---------------------------------------------------------------------------------------------
# RUN C -- the Impolite Entrance dose, which cutting Scale the Heights made reachable.
#
# The user's target list is 8 cards from {Oracle, Draught, Entrance} plus 3 from {Twinflame,
# Libation}. Under the caps above, the 8 can only split as a permutation of (4,2,2), so there are
# exactly THREE reachable splits and two are already measured:
#
#     4 Oracle  + 2 Draught + 2 Entrance   run A (`a0o4_draught`)   leader at 30 life
#     4 Draught + 2 Oracle  + 2 Entrance   run B (`d4o2`)           leader at 20 life
#     4 Entrance + 2 Oracle + 2 Draught    THIS RUN                 never measured
#
# All four trick splits are run, mixes included, even though mixes are eliminated as CANDIDATES: the
# open question is whether 4 Entrance bends the Twinflame->Libation ladder, and a ladder needs its
# middle. Entrance grants haste and Libation's Citizens arrive summoning-sick while Twinflame's
# tokens do not, so the enabler is one-sided by card text; the measurement is whether that matters.
RUN_C = {
    "map": "C",
    "arms": {f"{t}_e4": {**TF_SPLITS[t], ENTR: 4, ORACLE: 2, DRAUGHT: 2} for t in TF_SPLITS},
    # Bridge, run under this map AND under map B on the same seeds. Must come out identical.
    "bridge": {f"{t}_ship": {**TF_SPLITS[t], **SHIP} for t in ("tf3lib0", "tf0lib3")},
    "bridge_against": "B",
    "bracket": ["tf0lib3_e4", "tf3lib0_ship"],
}

# RUN D -- is the Oracle/Draught response CONCAVE? The one question the (4,2,2) caps hide.
#
# 4 Draught + 2 Oracle leads at 20 life and 4 Oracle + 2 Draught leads at 30, which invites the
# reading that the balanced 3+3 is best on average. Two points cannot show curvature, so this
# measures the midpoint. If a LINEAR response held, 3+3 would land at 5.0428 / 5.4537 -- between the
# endpoints and 0.0021t behind 4D+2O on the two-life-total sum, i.e. not a winner. It wins only if
# Oracle's `cast_lifegain: 1` feeding Draught's `pump_per_life_gained_power` makes the pair
# genuinely superadditive, which is the one synergy no eliminated mix had.
#
# It is ALSO the test of the user's prior (2026-08-20, "we'll probably lean toward maxing Draught
# and playing fewer Oracle"): if 3+3 is worse than 4D+2O, the response is not concave and pushing
# FURTHER toward Draught is the indicated direction rather than balancing.
#
# 3 + 3 + 2 over-fills a cap-2 bucket, so 0.10% of this arm's hands (1 in 978 -- all three copies of
# a 3-of in the opening seven) miss the table and fall through to the heuristic keep. That is
# reported by `verify`, not assumed away, and it bounds the bias at ~0.0002t against a 0.0136t
# effect. Generation would buy 3,689 cells to close it; it is not worth it at this rate.
#
# Run under BOTH maps as its own control: under A the Oracle sits in the cap-4 bucket and Draught
# overflows, under B the reverse -- same decklist, different bucketing, different missing cells. If
# the two agree, the aliasing choice provably did not decide the answer. No bridge arm is needed
# because each map's arm compares to THAT map's own run on the same table and the same seeds.
#
# SCOPE, corrected 2026-08-20. I first wrote this as an Oracle/Draught "ladder" running 6/0, 5/1,
# 4/2, 3/3, 2/4, 1/5, 0/6 -- four of which are not legal Magic decks. Deck construction allows at
# most four copies of a card, so with Entrance at the user's floor of 2 the six remaining slots
# split exactly three ways (4/2, 3/3, 2/4) and two of those are already measured. `write_arm` now
# refuses anything over four copies, because a reachable cell is not the same thing as a playable
# deck.
#
# What is actually left is the rest of the LEGAL space. With every count <= 4 and Entrance >= 2,
# the 8 draw/pump cards admit twelve shapes; runs A, B and C hold three of them, so run D takes the
# other nine and the enumeration is finished:
#
#     Entrance 2:  3 O + 3 D                                    (4/2 = run A, 2/4 = run B)
#     Entrance 3:  4 O + 1 D,  3 O + 2 D,  2 O + 3 D,  1 O + 4 D
#     Entrance 4:  4 O + 0 D,  3 O + 1 D,  1 O + 3 D,  0 O + 4 D (2 O + 2 D = run C)
#
# The cap-4 bucket goes to the largest count, which forces the map per arm. Only 3 O + 3 D is
# symmetric, and it runs both ways as the aliasing control. Hand-weighted miss rates are 0.102% for
# any shape holding a 3-of outside the cap-4 bucket and 0.388% for the two double-4 shapes;
# `verify` prints each one.
_LEGAL = [(o, d, e) for e in (2, 3, 4) for o in range(5) for d in range(5)
          if o + d + e == 8 and o <= 4 and d <= 4]
_DONE = {(4, 2, 2), (2, 4, 2), (2, 2, 4)}          # runs A, B, C
_TRICKS = ("tf3lib0", "tf0lib3")


def _ode(t, o, d, e):
    s = {**TF_SPLITS[t], ENTR: e}
    if o:
        s[ORACLE] = o
    if d:
        s[DRAUGHT] = d
    return s


def _map_for(o, d, e):
    """The card with the largest count must take the cap-4 bucket; that fixes the map. A tie is a
    free choice, so the symmetric 3/3 runs both ways and everything else takes the first match."""
    top = max(o, d, e)
    ms = [m for m, c in (("A", o), ("B", d), ("C", e)) if c == top]
    return ms if (o == d == 3) else ms[:1]


RUN_D = {
    "map": "A",
    "arms": {f"{t}_o{o}d{d}e{e}": _ode(t, o, d, e)
             for t in _TRICKS for (o, d, e) in _LEGAL if (o, d, e) not in _DONE},
    "arm_maps": {f"{t}_o{o}d{d}e{e}": _map_for(o, d, e)
                 for t in _TRICKS for (o, d, e) in _LEGAL if (o, d, e) not in _DONE},
    "bracket": [],
}

RUNS = {"C": RUN_C, "D": RUN_D}


def arm_maps(spec, name):
    """Which alias map(s) this arm runs under. An arm whose biggest count exceeds a cap-2 bucket
    MUST take the cap-4 bucket, so the map is forced; only a symmetric arm has a free choice, and
    running it both ways turns that choice into a control."""
    return spec.get("arm_maps", {}).get(name, [spec["map"]])


def maps_of(spec):
    """map letter -> the arm names measured under it (bridge arms included)."""
    out = {}
    for n in spec["arms"]:
        for m in arm_maps(spec, n):
            out.setdefault(m, set()).add(n)
    for n in spec.get("bridge", {}):
        for m in (spec["map"], spec.get("bridge_against")):
            if m:
                out.setdefault(m, set()).add(n)
    return out


def apparatus(m):
    d = os.path.join(TOURNEY, f"alias{m}" if m != "A" else "alias")
    stem = os.path.join(d, f"alias{m}" if m != "A" else "alias")
    return {"dir": d, "profile": stem + ".profile.json", "value": stem + ".value.json",
            "table": stem + ".keepmodel.exhaustive.profile.json"}


def build(m):
    """Copy the map-A apparatus and rewrite ONLY the bucket member lists."""
    if m not in MAPS:
        raise SystemExit(f"no alias map {m}; known: {', '.join(sorted(MAPS))}")
    ap = apparatus(m)
    os.makedirs(ap["dir"], exist_ok=True)
    for src, dst in ((SRC_PROFILE, ap["profile"]), (SRC_VALUE, ap["value"])):
        shutil.copyfile(src, dst)
    print(f"  profile + value copied from map A (identical scoring across maps by construction)")

    # The 177 MB table is rewritten by a BOUNDED textual splice rather than a JSON round-trip: the
    # bucket list sits in the first few KB, ahead of `entries`, and re-serialising 200k cells would
    # risk changing float formatting -- i.e. changing the measurement while claiming to change only
    # the names.
    head_len = 8192
    with open(SRC_TABLE, "rb") as fh:
        head = fh.read(head_len).decode()
    mo = re.search(r'"buckets":\s*(\[\[.*?\]\])\s*,\s*"commit"', head)
    if not mo:
        raise SystemExit("could not locate the buckets array in the table head -- refusing to guess")
    buckets = json.loads(mo.group(1))
    new = []
    for b in buckets:
        native = b[0]
        new.append([native, MAPS[m][native]] if native in MAPS[m] else [native])
    if [b[0] for b in new] != [b[0] for b in buckets]:
        raise SystemExit("bucket order changed -- comps are positional, so this would silently "
                         "reinterpret every cell")
    body = json.dumps(new, separators=(", ", ": "))
    patched = head[:mo.start(1)] + body + head[mo.end(1):]
    with open(SRC_TABLE, "rb") as fi, open(ap["table"], "wb") as fo:
        fi.seek(head_len)
        fo.write(patched.encode())
        shutil.copyfileobj(fi, fo, 1 << 22)
    bc = ap["table"] + ".bincache"
    if os.path.exists(bc):                      # stale cache would serve the PREVIOUS bucketing
        os.remove(bc)
    for i, b in enumerate(new):
        if len(b) > 1:
            print(f"    bucket {i:2d}: {b[0]} <- {b[1]}")
    chk = table_meta(ap["profile"])[1]
    if [x[0] for x in chk["buckets"]] != [x[0] for x in buckets]:
        raise SystemExit("re-read of the patched table disagrees with the source")
    print(f"  table: {os.path.relpath(ap['table'], ROOT)}  K={len(chk['buckets'])} "
          f"cells={len(chk['entries']):,} R={chk['effective_R']} commit={chk['commit']}")


def fixed_cards():
    """The 49 cards every arm shares, taken from the base arm minus its flexible slots."""
    flex = {TWIN, LIB, ANGER, ORACLE, DRAUGHT, SCALE, EXPED, ENTR}
    deck = read_decklist(BASE_ARM)
    return [(c, n) for c, n in deck if n not in flex]


# Deck construction: at most FOUR copies of any card except basic lands (CR 100.2a). The apparatus
# work is all about which bucket a count lands in, and it is entirely possible to write down a
# "reachable" 5- or 6-of that the table happens to cover -- I did exactly that, and the user caught
# it. There is no point measuring a deck that cannot be played, so this refuses at construction
# time rather than leaving legality to whoever writes the run spec.
BASICS = {"Forest", "Mountain", "Island", "Swamp", "Plains", "Wastes"}
MAX_COPIES = 4


def write_arm(armdir, name, slots):
    """Decklist + inherited numbering for one arm. File order is FIXED then flexible, always."""
    total = sum(c for c, _ in fixed_cards()) + sum(slots.values())
    if total != 60:
        raise SystemExit(f"{name}: {total} cards, not 60 -- {slots}")
    illegal = {n: c for n, c in slots.items() if c > MAX_COPIES and n not in BASICS}
    if illegal:
        raise SystemExit(f"{name}: ILLEGAL decklist -- "
                         + ", ".join(f"{c} {n}" for n, c in sorted(illegal.items()))
                         + f" (limit {MAX_COPIES} per card outside basic lands)")
    path = os.path.join(armdir, name + ".txt")
    with open(path, "w") as f:
        for c, n in fixed_cards():
            f.write(f"{c} {n}\n")
        for n in (TWIN, LIB, ANGER, ORACLE, DRAUGHT, SCALE, EXPED, ENTR):
            if slots.get(n):
                f.write(f"{slots[n]} {n}\n")
    base = read_decklist(BASE_ARM)
    bn, bc = base_numbering(base), {n: c for c, n in base}
    new = {n: c for c, n in read_decklist(path)}
    # Which new card inherits which departing card's slots. Left implicit it is alphabetical, which
    # is deterministic but arbitrary once an arm adds two names at once.
    pairs = {ANGER: ENTR if slots.get(ENTR, 0) > 2 else ORACLE,
             SCALE: DRAUGHT if slots.get(DRAUGHT) else ORACLE,
             TWIN: LIB, ENTR: EXPED if slots.get(EXPED) else ORACLE}
    nums = inherit_numbering(bn, bc, new, pairs)
    if sum(len(v) for v in nums.values()) != 60:
        raise SystemExit(f"{name}: numbering covers {sum(len(v) for v in nums.values())} cards")
    json.dump(nums, open(os.path.join(armdir, name + ".numbering.json"), "w"),
              indent=1, sort_keys=True)
    return path


def miss_rate(buckets, have, counts):
    """-> (cells, missing cells, share of OPENING HANDS that miss the table).

    Counting cells overstates the problem badly, and I got this wrong once: for `3 Oracle + 3
    Draught` the table lacks 1.8% of the arm's cells, but every missing cell is "all three copies of
    a 3-of in the opening seven", which is 0.10% of hands -- 1 in 978. The cell count is a property
    of the composition lattice; what a measurement actually feels is the HAND-WEIGHTED rate, so
    that is what gets reported and what any threshold is applied to.

    Weight of a cell = product over buckets of C(copies in deck, copies in hand), over C(60,7)."""
    bsz = [sum(counts.get(n, 0) for n in b) for b in buckets]
    cells = set(enum_cells(buckets, counts))
    miss = cells - have
    tot = math.comb(sum(counts.values()), 7)
    w = 0
    for c in miss:
        k = 1
        for i, x in enumerate(c):
            k *= math.comb(bsz[i], x)
        w += k
    return cells, miss, w / tot


def verify(m, armdir, names, max_miss=0.01):
    """Every hand every arm can draw should land on a cell the table holds. Exact, not sampled.

    A missing-but-reachable cell does not error: ExhaustiveKeepPolicy answers present=false
    (ExhaustiveKeepPolicy.h) and that ONE arm silently falls through to the generic heuristic keep
    and lookahead bottoming. That is an apparatus which differs between arms, which is the one thing
    a paired comparison cannot survive -- so it is REPORTED for every arm rather than assumed away.

    It is a threshold, not an absolute, because the alternative is worse. Refusing on a single
    missing cell reads "this comparison is impossible" when the honest statement is "0.1% of this
    arm's hands use a different keep policy, which bounds the bias at ~0.0002t against effects of
    0.01t". Anything above `max_miss` of hands is a real apparatus split and still refuses."""
    ap = apparatus(m)
    p, ek = table_meta(ap["profile"])
    have = {tuple(e["comp"]) for e in ek["entries"]}
    scored = set(json.load(open(ap["profile"]))["card_scores"])
    bucket_of = {n: i for i, b in enumerate(ek["buckets"]) for n in b}
    bad, noted = [], []
    for name in names:
        counts = {n: c for c, n in read_decklist(os.path.join(armdir, name + ".txt"))}
        unscored = sorted(set(counts) - scored)
        if unscored:
            raise SystemExit(f"{name} plays unscored {unscored} -- ComputeHandScore SKIPS an unknown "
                             "name, so it would be valued as an EMPTY SLOT on this arm only")
        unbucketed = sorted(n for n in counts if n not in bucket_of)
        if unbucketed:
            raise SystemExit(f"{name} plays unbucketed {unbucketed} under map {m}")
        cells, miss, rate = miss_rate(ek["buckets"], have, counts)
        if rate > max_miss:
            bad.append((name, len(miss), len(cells), rate))
        elif miss:
            noted.append((name, len(miss), len(cells), rate))
    if bad:
        n, k, tot, r = bad[0]
        raise SystemExit(f"REFUSING to measure: {len(bad)} of {len(names)} arms miss the table on "
                         f"more than {100*max_miss:.1f}% of hands.\n  worst: {n} -- {k:,} of "
                         f"{tot:,} cells absent, {100*r:.3f}% of opening hands")
    print(f"  coverage: {len(names)} arms against {os.path.relpath(p, ROOT)} ({len(have):,} cells)")
    for n, k, tot, r in noted:
        print(f"    NOTE {n}: {k:,} of {tot:,} cells absent -> {100*r:.4f}% of hands "
              f"(1 in {1/r:,.0f}) fall through to the heuristic keep")
    if not noted:
        print("    every arm fully covered")


def job(name, armdir, ap, games, seed, life, label=None):
    return {"name": f"{label or name}@L{life}",
            "deck": os.path.relpath(os.path.join(armdir, name + ".txt"), ROOT),
            "deck_numbering": os.path.relpath(os.path.join(armdir, name + ".numbering.json"), ROOT),
            "games": games, "seed": seed, "max_turns": 8, "starting_life": life,
            "profile": os.path.relpath(ap["profile"], ROOT),
            "value_profile": os.path.relpath(ap["value"], ROOT),
            # Ladder mode: the leaf makes warm-up passes cheap while the COMMITTED pass stays pure
            # heuristic (measured coupling 0.0008t), which is why one 20-life value model serves the
            # 30-life arms too -- the leaf never decides the played line.
            "value_model": False, "ladder_value_leaf": True}


def prepare(tag):
    spec = RUNS[tag]
    armdir = os.path.join(TOURNEY, f"arms_{tag}")
    os.makedirs(armdir, exist_ok=True)
    allarms = {**spec["arms"], **spec.get("bridge", {})}
    for name, slots in allarms.items():
        write_arm(armdir, name, slots)
    print(f"  arms: {len(allarms)} written to {os.path.relpath(armdir, ROOT)}")
    for m, names in maps_of(spec).items():
        verify(m, armdir, sorted(names))
    if spec.get("bridge_against"):
        verify(spec["bridge_against"], armdir, list(spec["bridge"]))
    return spec, armdir, allarms


def manifest(tag, games, seed, lives, bracket):
    spec, armdir, _ = prepare(tag)
    ap = apparatus(spec["map"])
    # Suffix the map only when the run uses more than one, so a single-map run's stderr keeps
    # reading the same way after this code changes.
    multi = len(maps_of(spec)) > 1
    J = []
    for life in lives:
        for n in spec["arms"]:
            for m in arm_maps(spec, n):
                J.append(job(n, armdir, apparatus(m), games, seed, life,
                             label=f"{n}_{m}" if multi else n))
    for life in lives:
        for n in spec.get("bridge", {}):
            J.append(job(n, armdir, ap, games, seed, life, label=f"BRG_{n}_{spec['map']}"))
            other = apparatus(spec["bridge_against"])
            J.append(job(n, armdir, other, games, seed, life,
                         label=f"BRG_{n}_{spec['bridge_against']}"))
    if bracket:
        nt = {"profile": os.path.join(NOTABLE, "notable.profile.json"),
              "value": os.path.join(NOTABLE, "notable.value.json")}
        for life in lives:
            for n in spec.get("bracket", []):
                J.append(job(n, armdir, ap, bracket, seed, life, label=f"BRK_{n}_tab"))
                J.append(job(n, armdir, nt, bracket, seed, life, label=f"BRK_{n}_not"))
    return J, armdir


def plan(tag, games, lives, bracket):
    J, _ = manifest(tag, games, 0, lives, bracket)
    # 0.387 thread-s/game at 20 life is measured (logs/overnight/batch/batch.out: 180,000 games for
    # 69,662 thread-seconds); 30 life is 3.33x that, and the reason is per-turn search cost.
    per = {20: 0.387, 25: 0.762, 30: 1.289, 40: 1.80}
    tot = sum(per.get(j["starting_life"], 1.0) * j["games"] for j in J)
    cores = os.cpu_count() or 32
    print(f"  {len(J)} jobs, {sum(j['games'] for j in J):,} games")
    print(f"  ~{tot / 3600:,.1f} thread-hours -> ~{tot / 3600 / cores:.1f} h wall on {cores} cores")


def run(tag, games, seed, lives, threads, bracket):
    J, _ = manifest(tag, games, seed, lives, bracket)
    outdir = os.path.join(TOURNEY, f"run_{tag}")
    os.makedirs(outdir, exist_ok=True)
    man = os.path.join(outdir, "manifest.json")
    json.dump({"jobs": J}, open(man, "w"), indent=1)
    err, out = os.path.join(outdir, "tourney.err"), os.path.join(outdir, "tourney.out")
    print(f"  {len(J)} jobs, {sum(j['games'] for j in J):,} games -> "
          f"{os.path.relpath(err, ROOT)}")
    # ONE pooled batch: every arm, both bridge maps and the bracket share a single work queue, so
    # the box drains to a single tail instead of one tail per group. Nothing is wrapped in a
    # timeout -- a truncated run reads as a result.
    env = {**os.environ, "MTG_DUMP_WINS": "1", "MTG_DUMP_CARDS": "1",
           "MTG_PROVIDER_DECK": os.path.abspath(BASE_ARM)}
    t0 = time.time()
    with open(err, "w") as e, open(out, "w") as o:
        rc = subprocess.call([os.path.join(ROOT, "build/Release/mtg"), "--batch", man,
                              "--threads", str(threads)], stdout=o, stderr=e, cwd=ROOT, env=env)
    print(f"  batch rc={rc} in {(time.time() - t0) / 3600:.2f} h")
    if rc != 0:
        raise SystemExit(f"batch failed rc={rc}; see {os.path.relpath(err, ROOT)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["build", "plan", "prep", "run"])
    ap.add_argument("target", help="alias map (build) or run tag (plan/prep/run)")
    ap.add_argument("--games", type=int, default=20000)
    ap.add_argument("--seed", type=int, default=1200000)
    ap.add_argument("--lives", default="20,30")
    ap.add_argument("--bracket", type=int, default=4000)
    ap.add_argument("--threads", type=int, default=os.cpu_count() or 32)
    a = ap.parse_args()
    lives = [int(x) for x in a.lives.split(",") if x.strip()]
    if a.cmd == "build":
        build(a.target)
    elif a.cmd == "prep":
        prepare(a.target)
    elif a.cmd == "plan":
        plan(a.target, a.games, lives, a.bracket)
    else:
        run(a.target, a.games, a.seed, lives, a.threads, a.bracket)


if __name__ == "__main__":
    main()
