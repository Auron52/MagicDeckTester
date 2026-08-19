#!/usr/bin/env python3
"""Run the Mirrorwing card-elimination tournament as ONE pooled batch.

    python3 scripts/tourney_run.py prep                 # numbering + pooled card scores (no games)
    python3 scripts/tourney_run.py run --games 10000    # the measurement itself
    python3 scripts/tourney_run.py plan --games 10000   # cost/wall-clock arithmetic only

Design, and why it is a FACTORIAL rather than four sequential A/Bs
-----------------------------------------------------------------
The user asked for four tests, each resolving one slot, "each with one test per". The 60 arms in
logs/tourney/arms are the full cross product of the three contested slots:

    tf{3,2,1,0}lib{0,1,2,3}  x  a{4,2,0}o{0,2,4}  x  slot{scale,draught,entrance,oracle,anger}

so every one of those tests is a MARGINAL of one run, not a run of its own. Three consequences,
all of them in the user's favour:

  * Each comparison gets N x (number of contexts) paired games -- 12-15x the games a two-arm A/B
    would have given for the same wall clock. That is what buys a better/worse margin big enough
    to act on.
  * Four sequential tests would each condition on the previous test's winner, so a card eliminated
    early can never be re-examined in the light of what came later. The factorial measures each
    slot in EVERY context, which is the only way to see an interaction (Draught's whole story so
    far has been an interaction with Fists of Flame -- see mirrorwing-trick-suite-result.md).
  * One pooled queue, one tail. Four runs would be four tails and four barriers, which CLAUDE.md's
    pooling rule forbids for exactly the reason it keeps costing us cores.

Both life totals (20 = regular, 30 = 2HG) are jobs in the SAME batch via the per-job
`starting_life` field, for the same reason.

What it deliberately does NOT cover: the arms cap Fortifying Draught at 2 (the Scale slot), so the
"replace ALL of the draw+pump slot with Draught" arm is not reachable in this table and Test 4's
upper end needs an append pass. See docs/design/mirrorwing-card-tournament.md.
"""
import argparse, json, os, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from deck_compare import read_decklist, base_numbering, inherit_numbering  # noqa: E402

ARMS   = os.path.join(ROOT, "logs", "tourney", "arms")
POOL   = os.path.join(ROOT, "logs", "tourney", "pool")
OUT    = os.path.join(ROOT, "logs", "tourney", "run")
CARDS  = os.path.join(ROOT, "src", "cards", "data", "cards.json")
PROFILE = os.path.join(POOL, "pool.profile.json")
VALUE   = os.path.join(POOL, "pool.value.json")
TABLE   = os.path.join(POOL, "pool.keepmodel.exhaustive.profile.json")   # or .gz

# The ORIGINAL list: 3 Twinflame, 4 Ancestral Anger, 2 Scale the Heights. Numbering is inherited
# from this arm so a card number names the same SLOT in all 60 -- which is what lets the reader ask
# "did both arms see the swapped slot" by comparing numbers rather than names.
BASE_ARM = "tf3lib0_a4o0_scale"

TF   = ["tf3lib0", "tf2lib1", "tf1lib2", "tf0lib3"]
AO   = ["a4o0", "a2o2", "a0o4"]
SLOT = ["scale", "draught", "entrance", "oracle", "anger"]

# {removed -> added} so a freed number goes to the card that MEANT to take its place, rather than
# to whichever name sorts first. Only matters when an arm adds two names at once (a0o4_draught
# frees Anger and Scale and adds Oracle and Draught).
SLOT_CARD = {"scale": "Scale the Heights", "draught": "Fortifying Draught",
             "entrance": "Impolite Entrance", "oracle": "Oracle's Restoration",
             "anger": "Ancestral Anger"}


def arm_names():
    return [f"{t}_{a}_{s}" for t in TF for a in AO for s in SLOT]


def arm_path(arm):
    return os.path.join(ARMS, arm + ".txt")


def counts(path):
    return {n: c for c, n in read_decklist(path)}


def write_numbering():
    """One inherited numbering per arm, written beside its decklist as <arm>.numbering.json.

    BatchRunner takes `deck_numbering` per job; without it each arm would be numbered from its OWN
    file order, so number 52 would be Twinflame in one arm and a Forest in another and every
    paired, per-number question the report asks would be meaningless."""
    base_counts = counts(arm_path(BASE_ARM))
    base_nums   = base_numbering(read_decklist(arm_path(BASE_ARM)))
    out = {}
    for arm in arm_names():
        c = counts(arm_path(arm))
        pairs = {"Twinflame": "Luxurious Libation",
                 "Scale the Heights": SLOT_CARD[arm.split("_")[2]],
                 "Ancestral Anger": "Oracle's Restoration"}
        nums = inherit_numbering(base_nums, base_counts, c, pairs)
        assert sum(len(v) for v in nums.values()) == 60, (arm, sum(len(v) for v in nums.values()))
        p = os.path.join(ARMS, arm + ".numbering.json")
        json.dump(nums, open(p, "w"), indent=1, sort_keys=True)
        out[arm] = p
    print(f"  numbering: {len(out)} arms inherited from {BASE_ARM}")
    return out


def unscored_cards():
    """Names some arm plays that the pool profile has no card_scores entry for.

    ComputeHandScore SKIPS an unknown name, so an unscored card is scored as an EMPTY SLOT, and
    that penalty falls only on the arms that play it -- i.e. exactly on one side of a comparison.
    (The keep TABLE is unaffected: its keep/bottom decisions come from measured cell values via
    best_sub, not from card_scores. This is a measurement-time correction only.)"""
    have = set((json.load(open(PROFILE)).get("card_scores") or {}))
    want = {n for arm in arm_names() for n in counts(arm_path(arm))}
    return sorted(want - have)


def pool_scores():
    """Derive card_scores for the unscored names and merge them into the pool profile."""
    missing = unscored_cards()
    if not missing:
        print("  card scores: every arm card already scored")
        return
    bak = PROFILE + ".prescore"
    if not os.path.exists(bak):
        open(bak, "w").write(open(PROFILE).read())
    log = os.path.join(POOL, "poolscores.log")
    print(f"  deriving card scores for {', '.join(missing)} (log {os.path.relpath(log, ROOT)})")
    with open(log, "w") as f:
        res = subprocess.run([os.path.join(ROOT, "build/Release/mtg-analyze"),
                              os.path.join(POOL, "pool.txt"), "--cards-json", CARDS,
                              "--seed", "66000001"],
                             stdout=subprocess.PIPE, stderr=f, text=True, cwd=ROOT,
                             env={**os.environ,
                                  "MTG_PROVIDER_DECK": os.path.abspath(arm_path(BASE_ARM))})
    if res.returncode != 0:
        raise SystemExit(f"mtg-analyze exited {res.returncode}; see {log}")
    scores = json.loads(res.stdout).get("card_scores") or {}
    prof = json.load(open(bak))
    merged = dict(prof.get("card_scores") or {})
    for n in missing:
        if n not in scores:
            raise SystemExit(f"the analyzer returned no card score for {n} -- refusing to measure "
                             "an arm whose card is scored as an empty slot")
        merged[n] = scores[n]
        print(f"    {n:24s} {[round(x, 4) for x in merged[n]]}")
    prof["card_scores"] = merged
    json.dump(prof, open(PROFILE, "w"), indent=1)


def table_path():
    for p in (TABLE + ".gz", TABLE):
        if os.path.exists(p):
            return p
    return None


def verify_coverage(tp):
    """Every hand EVERY arm can draw must resolve to a cell the table holds. Exact, not sampled.

    This is the one check the whole tournament rests on. `MTG_KEEP_ARM_DECKS` dropped 7.8% of the
    envelope as unreachable, which is only correct if "unreachable" was computed for exactly these
    60 decklists -- and a cell that is missing but reachable does not error: ExhaustiveKeepPolicy
    answers present=false and the engine falls through to the generic heuristic keep AND lookahead
    bottoming, silently, for the one arm whose hand it was. That is an apparatus that differs
    BETWEEN arms, which is the only thing a paired comparison cannot survive."""
    from deck_compare import table_meta, enum_cells
    p, ek = table_meta(PROFILE)
    if not ek:
        raise SystemExit(f"no exhaustive_keep in the table beside {os.path.relpath(PROFILE, ROOT)}")
    have = {tuple(e["comp"]) for e in (ek.get("entries") or [])}
    print(f"  table: {os.path.relpath(p, ROOT)}  K={len(ek['buckets'])}  "
          f"cells={len(have):,}  R={ek.get('effective_R')}  commit={ek.get('commit')}  "
          f"bottoming={ek.get('bottoming_enabled')}")
    union, worst = set(), []
    for arm in arm_names():
        cells = set(enum_cells(ek["buckets"], counts(arm_path(arm))))
        union |= cells
        miss = cells - have
        if miss:
            worst.append((arm, len(miss), len(cells), sorted(miss)[:2]))
    if worst:
        arm, n, tot, ex = worst[0]
        raise SystemExit(
            f"REFUSING to measure: {len(worst)} of 60 arms can draw hands the table does not "
            f"answer.\n  worst: {arm} -- {n:,} of {tot:,} of its cells absent, e.g. {ex}\n"
            "  Those hands fall through to the generic heuristic keep and lookahead bottoming on "
            "that arm ONLY,\n  so the arms would not share an apparatus and no comparison here "
            "would mean anything.")
    extra = len(have) - len(union)
    print(f"  coverage: all 60 arms fully covered ({len(union):,} cells reachable by some arm"
          + (f"; {extra:,} table cells reachable by none -- weight 0, harmless)" if extra else ")"))


def jobs(games, seed, lives, numbering):
    """60 arms x len(lives) life totals, one job each, ONE manifest.

    Same seed base at both life totals: pairing is within a life total, but sharing the seeds means
    a game can be followed across the 20 -> 30 change, which is how the 2HG apparatus question gets
    answered without a second run."""
    out = []
    for life in lives:
        for arm in arm_names():
            out.append({
                "name": f"{arm}@L{life}",
                "deck": os.path.relpath(arm_path(arm), ROOT),
                "deck_numbering": os.path.relpath(numbering[arm], ROOT),
                "games": games, "seed": seed, "max_turns": 8,
                "starting_life": life,
                "profile": os.path.relpath(PROFILE, ROOT),
                # Ladder mode: the leaf makes warm-up passes cheap while the COMMITTED pass stays
                # pure heuristic (measured coupling 0.0008t). That is why ONE 20-life value model
                # can serve the 30-life arms too -- the leaf never decides the played line.
                "value_profile": os.path.relpath(VALUE, ROOT),
                "value_model": False, "ladder_value_leaf": True,
            })
    return out


def plan(games, lives):
    # 0.387 thread-s/game at 20 life is measured, not guessed: logs/overnight/batch/batch.out,
    # 180,000 games for 69,662 thread-seconds. 30 life is 3.33x that (logs/tourney/lifecost.*),
    # and the reason is per-turn search cost, not longer games.
    per = {20: 0.387, 25: 0.762, 30: 1.289, 40: 1.80}
    tot = sum(per.get(l, 1.0) * games * len(arm_names()) for l in lives)
    cores = os.cpu_count() or 32
    print(f"  {len(arm_names())} arms x {len(lives)} life totals x {games:,} games "
          f"= {len(arm_names()) * len(lives) * games:,} games")
    print(f"  ~{tot / 3600:,.0f} thread-hours -> ~{tot / 3600 / cores:.1f} h wall on {cores} cores")
    for name, ctx in (("T1 tf/lib", 15), ("T2 anger/oracle", 12), ("T3 scale slot", 12)):
        print(f"    {name:18s} {ctx:2d} contexts -> {ctx * games:,} paired games per life total")


def run(games, seed, lives, threads, wait, smoke=False):
    outdir = os.path.join(ROOT, "logs", "tourney", "smoke") if smoke else OUT
    os.makedirs(outdir, exist_ok=True)
    if wait:
        t0 = time.time()
        while not table_path():
            time.sleep(60)
        print(f"  keep table landed after {(time.time() - t0) / 3600:.2f} h: "
              f"{os.path.relpath(table_path(), ROOT)}")
    tp = table_path()
    if not tp and not smoke:
        raise SystemExit(f"no keep table at {os.path.relpath(TABLE, ROOT)}[.gz] -- REFUSING to "
                         "measure on the default mulligan. Every arm must share the pooled table; "
                         "a table-less run is not the comparison the tournament asked for.")
    if smoke:
        # A PLUMBING check: does the manifest parse, does every arm produce [win]+[cards] lines,
        # does the report read them. It runs whether or not the table exists precisely so the
        # pipeline can be validated while the table is still generating -- the alternative is
        # discovering a parser bug at 4am on a finished 9-hour run. Its numbers are not results,
        # and it writes to a different directory so they cannot be mistaken for any.
        print(f"  SMOKE (plumbing only, NOT a measurement) -- table "
              f"{'attached' if tp else 'ABSENT: default mulligan'}")
    else:
        print(f"  keep table: {os.path.relpath(tp, ROOT)}")
    numbering = write_numbering()
    if not smoke:
        pool_scores()
        verify_coverage(tp)
    man = os.path.join(outdir, "tourney.manifest.json")
    json.dump({"jobs": jobs(games, seed, lives, numbering)}, open(man, "w"), indent=1)
    err, out = os.path.join(outdir, "tourney.err"), os.path.join(outdir, "tourney.out")
    print(f"  manifest: {os.path.relpath(man, ROOT)}  ->  {os.path.relpath(err, ROOT)}")
    # stderr goes STRAIGHT to a file: this run emits one [win] and one [cards] line per game (2.4M
    # lines at N=10,000), and pumping that through a Python read loop to tee progress would make the
    # driver the bottleneck. Progress is read from the file instead (grep '\[batch\]' on it).
    env = {**os.environ, "MTG_DUMP_WINS": "1", "MTG_DUMP_CARDS": "1",
           "MTG_PROVIDER_DECK": os.path.abspath(arm_path(BASE_ARM))}
    t0 = time.time()
    with open(err, "w") as e, open(out, "w") as o:
        rc = subprocess.call([os.path.join(ROOT, "build/Release/mtg"), "--batch", man,
                              "--threads", str(threads)], stdout=o, stderr=e, cwd=ROOT, env=env)
    print(f"  batch rc={rc} in {(time.time() - t0) / 3600:.2f} h")
    if rc != 0:
        raise SystemExit(f"batch failed rc={rc}; see {os.path.relpath(err, ROOT)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["prep", "run", "plan"])
    ap.add_argument("--games", type=int, default=10000)
    ap.add_argument("--seed", type=int, default=1200000)
    ap.add_argument("--lives", default="20,30")
    ap.add_argument("--threads", type=int, default=os.cpu_count() or 32)
    ap.add_argument("--wait", action="store_true", help="block until the keep table lands")
    ap.add_argument("--smoke", action="store_true",
                    help="plumbing check: tiny, table optional, writes to logs/tourney/smoke")
    a = ap.parse_args()
    lives = [int(x) for x in a.lives.split(",") if x.strip()]
    if a.cmd == "plan":
        plan(a.games, lives)
    elif a.cmd == "prep":
        write_numbering()
        print("  unscored:", ", ".join(unscored_cards()) or "(none)")
        plan(a.games, lives)
    else:
        run(a.games, a.seed, lives, a.threads, a.wait, a.smoke)


if __name__ == "__main__":
    main()
