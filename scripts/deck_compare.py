#!/usr/bin/env python3
"""Compare COMBINATIONS of one fixed card pool -- fast, paired, apparatus-symmetric.

    python3 scripts/deck_compare.py <spec.json> [--dry-run]     # the screen
    python3 scripts/deck_compare.py <spec.json> --floor <tag>   # bracket ONE combination's bias floor

The question this answers: given a base deck and a set of count changes over cards that are ALREADY
implemented, which combination is faster? Card implementation and heuristic tuning are one-time costs
per card; this is the per-COMBINATION loop, and it is meant to cost minutes.

Design + all measurements behind it: docs/design/deck-combination-screening.md

Three things it does that a hand-rolled A/B does not:

1. INHERITED NUMBERING. The opening shuffle is a positional Fisher-Yates, so changing a count
   re-permutes the whole game and the two arms share nothing but the seed -- an unpaired measurement
   needing ~20x the games. Here every unchanged card keeps its number and a replacement INHERITS the
   number of the card it replaced, so both arms sort to one key order and differ only where the edit
   reaches. Measured on burn: 4,594 -> 215 games to resolve a 0.03t effect.
   `replace` is a PRIMITIVE, not remove+add: remove-then-add frees a number and inserts elsewhere,
   shifting everything between (measured 1.4x worse).

2. SHARED, SYMMETRIC APPARATUS. Every arm gets the SAME mulligan table, value model and play
   settings. Sharing is not a cheap approximation -- two different tables mulligan differently on
   hands unrelated to the edit, which injects divergence into the comparison (measured: sharing
   HALVES the standard error).

3. A COVERAGE PRE-FLIGHT. A hand holding a card the shared table never bucketed does not get a biased
   answer, it gets NO answer: the policy falls through to the generic heuristic, silently. So if any
   combination introduces such a card, the table is dropped from EVERY arm -- symmetric and unbiased
   (measured -0.0003 error on a -0.20 effect) rather than silently lop-sided.

Spec format:

    {
      "base":          "decks/slivers_vial/slivers_vial.txt",
      "profile":       "decks/slivers_vial/slivers_vial.profile.json",   # optional
      "value_profile": "decks/slivers_vial/slivers_vial.value.json",     # optional
      "games": 20000, "seed": 910000, "depth": 5, "budget_ms": 20,
      "combinations": {
        "more_leeching": {"Hatchery Sliver": 2, "Leeching Sliver": 4},
        "cut_vial":      {"Aether Vial": 0, "Muscle Sliver": 6}
      }
    }

A combination is a map of card -> NEW COUNT (absent = unchanged, 0 = removed). A card not in the base
deck may be introduced by naming it; it must already exist in cards.json.

`--floor <tag>` answers the question the screen cannot: is the measured delta bigger than the bias
the shared apparatus itself carries? It generates a throwaway low-R keep table for that ONE
combination and re-measures the same delta under it, so the bias is observed rather than predicted:

    bias = delta(under the variant's own table) - delta(under the shared table)

All four cells (2 decks x 2 tables) run in ONE pooled batch off the same game indices, so the
difference-of-differences is fully paired. Note what this does and does not bound: at a low R the
variant table carries its own sampling noise (~0.01t at R=10, larger than the bias), so the bracket
OVERSTATES the floor. That is the right direction for a safety check and the wrong direction for an
accuracy claim -- never treat the variant-table arm as "the accurate one".
"""
import argparse, gzip, json, math, os, re, statistics as st, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT  = os.path.join(ROOT, "logs", "deckcmp")


def read_decklist(path):
    """-> [(count, name)] in FILE ORDER. File order defines the base numbering, so it is load-bearing."""
    out = []
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        n, name = line.split(" ", 1)
        out.append((int(n), name.strip()))
    return out


def base_numbering(deck):
    """Number the base 1..N in decklist order. Contiguity is not required by the engine (m_number is
    only ever equality-compared, never an index) -- it is just the obvious starting assignment."""
    m, nxt = {}, 1
    for count, name in deck:
        m[name] = list(range(nxt, nxt + count))
        nxt += count
    return m


def inherit_numbering(base_nums, base_counts, new_counts):
    """Unchanged copies keep their numbers; numbers freed by removed copies are INHERITED by added
    copies. Never renumber -- renumbering is the entire bug this exists to avoid.

    Freed numbers are handed out in sorted order. With an explicit edit list ("replace X with Y") the
    pairing would be exact; from a count diff alone this deterministic rule is the best available, and
    it still yields in-place replacement whenever the counts balance."""
    freed, out = [], {}
    for name, nums in base_nums.items():
        keep = min(new_counts.get(name, 0), base_counts[name])
        out[name] = nums[:keep]
        freed += nums[keep:]
    freed.sort()
    high = max((max(v) for v in base_nums.values() if v), default=0)
    for name in sorted(new_counts):
        need = new_counts[name] - len(out.get(name, []))
        for _ in range(need):
            # A genuine size increase takes a fresh number; survivors keep their relative order either
            # way, so alignment degrades gracefully instead of collapsing.
            out.setdefault(name, []).append(freed.pop(0) if freed else (high := high + 1))
        out[name] = sorted(out.get(name, []))
    return {k: v for k, v in out.items() if v}


def table_cards(profile_path):
    """Card names the profile's exhaustive keep table has bucketed, or None if it has no table."""
    if not profile_path:
        return None
    for p in (profile_path, profile_path + ".gz",
              re.sub(r"\.profile\.json$", ".keepmodel.exhaustive.profile.json", profile_path),
              re.sub(r"\.profile\.json$", ".keepmodel.exhaustive.profile.json.gz", profile_path)):
        if not os.path.exists(p):
            continue
        op = gzip.open if p.endswith(".gz") else open
        try:
            j = json.load(op(p, "rt"))
        except Exception:
            continue
        ek = j.get("exhaustive_keep") or {}
        if ek.get("buckets"):
            return {n for b in ek["buckets"] for n in b}
    return None


def score(path, arms, max_turns):
    """Per-arm {game_index: win_turn}. Unwon prints wt=-1 and is scored max_turns+1 -- the repo's
    primary objective. A regex that only accepts \\d+ silently DROPS those games and biases the mean."""
    got = {a: {} for a in arms}
    for line in open(path):
        m = re.match(r"\[win\] job=(\S+) gi=(\d+) wt=(-?\d+)", line)
        if m and m.group(1) in got:
            wt = int(m.group(3))
            got[m.group(1)][int(m.group(2))] = max_turns + 1 if wt < 0 else wt
    return got


def run_batch(jobs, outdir, name, threads):
    """One pooled `mtg --batch` over every job. -> path of the stderr log the wins were dumped to."""
    man = os.path.join(outdir, f"{name}.manifest.json")
    json.dump({"jobs": jobs}, open(man, "w"), indent=1)
    err = os.path.join(outdir, f"{name}.err")
    with open(err, "w") as e, open(os.path.join(outdir, f"{name}.out"), "w") as o:
        rc = subprocess.call([os.path.join(ROOT, "build/Release/mtg"), "--batch", man,
                              "--threads", str(threads or os.cpu_count())],
                             stdout=o, stderr=e, cwd=ROOT,
                             env={**os.environ, "MTG_DUMP_WINS": "1"})
    if rc != 0:
        raise SystemExit(f"batch failed rc={rc}; see {err}")
    return err


def paired(got, a, b):
    """(mean delta b-a, se, n, %identical) over the game indices both arms finished."""
    common = sorted(set(got[a]) & set(got[b]))
    D = [got[b][g] - got[a][g] for g in common]
    n = len(D)
    se = st.pstdev(D) / math.sqrt(n) if n else float("nan")
    return st.mean(D) if n else float("nan"), se, n, 100 * sum(1 for x in D if x == 0) / n if n else 0.0


class Spec:
    """The spec file, resolved: base decklist, its numbering, and every combination's card counts."""

    def __init__(self, path):
        s = json.load(open(path))
        self.raw     = s
        self.games   = int(s.get("games", 20000))
        self.seed    = int(s.get("seed", 910000))
        self.depth   = int(s.get("depth", 5))
        self.budget  = int(s.get("budget_ms", 20))
        self.maxturn = int(s.get("max_turns", 8))
        self.threads = int(s.get("threads", 0))
        self.base_path = s["base"] if os.path.isabs(s["base"]) else os.path.join(ROOT, s["base"])
        self.deck  = read_decklist(self.base_path)
        self.counts = {n: c for c, n in self.deck}
        self.nums   = base_numbering(self.deck)
        self.stem   = os.path.basename(self.base_path)          # e.g. "slivers_vial.txt"
        self.name   = os.path.splitext(self.stem)[0]            # the sidecar/profile stem
        self.arms = {"base": dict(self.counts)}
        for tag, ov in s["combinations"].items():
            c = dict(self.counts)
            c.update({k: int(v) for k, v in ov.items()})
            self.arms[tag] = {k: v for k, v in c.items() if v > 0}

    def path(self, key):
        p = self.raw.get(key)
        return (p if not p or os.path.isabs(p) else os.path.join(ROOT, p))

    def write_arm(self, tag, outdir):
        """Materialise one arm's decklist + inherited numbering. -> (deck path, numbering path)."""
        counts = self.arms[tag]
        os.makedirs(outdir, exist_ok=True)
        ordered  = [(counts[n], n) for _, n in self.deck if counts.get(n)]
        ordered += [(counts[n], n) for n in sorted(set(counts) - set(self.counts))]
        dp = os.path.join(outdir, self.stem)
        open(dp, "w").write("\n".join(f"{c} {n}" for c, n in ordered) + "\n")
        np_ = os.path.join(outdir, "numbering.json")
        nums = self.nums if tag == "base" else inherit_numbering(self.nums, self.counts, counts)
        json.dump(nums, open(np_, "w"), indent=0)
        return dp, np_

    def job(self, name, deck, numbering, profile):
        j = {"name": name, "deck": deck, "deck_numbering": numbering,
             "games": self.games, "seed": self.seed, "depth": self.depth,
             "budget_ms": self.budget, "max_turns": self.maxturn, "ignore_play_profile": True}
        if profile:
            j["profile"] = os.path.relpath(profile, ROOT)
        if self.raw.get("value_profile"):
            # Ladder mode: the model makes warm-up passes cheap, the COMMITTED pass stays pure
            # heuristic. Verified byte-identical under a deliberately wrong model at unbounded budget;
            # residual budget coupling at budget>0 measured 0.0008t. That guarantee is what lets one
            # pool model serve every combination without re-validating per combination.
            j["value_profile"] = self.raw["value_profile"]
            j["value_model"] = False
            j["ladder_value_leaf"] = True
        return j


def screen(spec, dry_run):
    """Every combination vs base, one shared apparatus, one pooled batch."""
    os.makedirs(OUT, exist_ok=True)
    tbl = table_cards(spec.path("profile"))
    all_cards = {c for a in spec.arms.values() for c in a}
    uncovered = sorted(all_cards - tbl) if tbl is not None else []
    use_table = tbl is not None and not uncovered

    print(f"base: {spec.raw['base']}  ({sum(spec.counts.values())} cards)")
    for tag, counts in spec.arms.items():
        if tag == "base":
            continue
        d = sorted(set(counts) | set(spec.counts))
        ch = [f"{n} {spec.counts.get(n,0)}->{counts.get(n,0)}" for n in d
              if counts.get(n, 0) != spec.counts.get(n, 0)]
        tot, b = sum(counts.values()), sum(spec.counts.values())
        # A size change is legal (survivors keep their relative order, so alignment degrades
        # gracefully) but is nearly always a typo in the spec -- say so rather than let it pass.
        warn = "" if tot == b else f"  <-- SIZE {b}->{tot}, intended?"
        print(f"  {tag:22s} {tot:3d} cards  |  " + ", ".join(ch) + warn)
    if tbl is None:
        print("\napparatus: no exhaustive keep table found -> heuristic mulligan on every arm (symmetric)")
    elif uncovered:
        print(f"\napparatus: table DROPPED from every arm -- not bucketed: {', '.join(uncovered)}")
        print("           (a hand holding one would silently fall through to the heuristic on SOME arms")
        print("            only; dropping it everywhere is symmetric and measured unbiased instead)")
    else:
        print("\napparatus: shared exhaustive keep table on every arm (all cards covered)")

    jobs = []
    for tag in spec.arms:
        dp, np_ = spec.write_arm(tag, os.path.join(OUT, tag))
        jobs.append(spec.job(tag, dp, np_, spec.path("profile") if use_table else None))
    print(f"\n{len(jobs)} arms x {spec.games:,} games -> ONE pooled batch")
    if dry_run:
        json.dump({"jobs": jobs}, open(os.path.join(OUT, "screen.manifest.json"), "w"), indent=1)
        return 0

    got = score(run_batch(jobs, OUT, "screen", spec.threads), list(spec.arms), spec.maxturn)
    common = sorted(set.intersection(*[set(v) for v in got.values()]))
    print(f"\n{len(common):,} paired games, d{spec.depth} budget {spec.budget}ms   (negative delta = FASTER)\n")
    print(f"  {'combination':22s} {'avg':>8s} {'delta':>9s} {'se':>8s} {'t':>7s} {'ident':>7s} {'n@3sig/0.03t':>13s}")
    print(f"  {'base':22s} {st.mean([got['base'][g] for g in common]):8.4f}")
    for tag in spec.arms:
        if tag == "base":
            continue
        d, se, n, ident = paired(got, "base", tag)
        need = 9 * (se * math.sqrt(n)) ** 2 / 0.03 ** 2
        print(f"  {tag:22s} {st.mean([got[tag][g] for g in common]):8.4f} {d:+9.4f} {se:8.4f} "
              f"{d/se if se else float('nan'):+7.2f} {ident:6.1f}% {need:13,.0f}")
    print("\nNOTE: this is a SCREEN. Every arm shares one apparatus, so the measured delta carries an")
    print("apparatus bias floor. `--floor <tag>` MEASURES that floor for one combination instead of")
    print("assuming it; do that for any result whose margin over the floor is not several-fold.")
    return 0


def apparatus_dir(name, stem, profile_src, table_src):
    """A directory the engine will resolve ONE chosen keep table out of.

    The sidecar is presence-gated off the PROFILE path's directory+stem
    (`AttachExhaustiveSidecar`, MulliganProfileIO.h), so pairing an arbitrary table with the deck's
    real play profile is just a directory holding both under the deck's stem. That keeps the play
    profile attached -- which is the whole point: a run without it plays a deck we do not ship."""
    d = os.path.join(OUT, "app_" + name)
    os.makedirs(d, exist_ok=True)
    prof = os.path.join(d, stem + ".profile.json")
    subprocess.check_call(["cp", "-f", profile_src, prof])
    ext = ".keepmodel.exhaustive.profile.json" + (".gz" if table_src.endswith(".gz") else "")
    link = os.path.join(d, stem + ext)
    if os.path.lexists(link):
        os.remove(link)
    os.symlink(os.path.abspath(table_src), link)
    return prof


def gen_table(spec, tag, deck_path, R):
    """Generate a throwaway keep table for one combination, next to its decklist.

    The deck's play profile and value sidecar are copied in FIRST, because `RunExhaustiveKeepMode`
    resolves both directory-relative off the decklist and silently falls back to the DEFAULT profile
    when they are absent. A table generated that way is fit to a deck we do not ship (it moved the
    play digest here, and profile-less play measured Aether Vial 0.07t weaker), so this copy is
    load-bearing, not tidiness."""
    d = os.path.dirname(deck_path)
    out = os.path.join(d, spec.name + ".keepmodel.exhaustive.profile.json")
    # Reuse is keyed on the arm's exact card counts, not on the file existing: the arm directory is
    # named after the combination TAG, so editing a combination in the spec and re-running would
    # otherwise silently bracket against the previous edit's table.
    fp = os.path.join(d, ".counts.json")
    want = json.dumps(spec.arms[tag], sort_keys=True)
    if os.path.exists(out) and os.path.exists(fp) and open(fp).read() == want:
        print(f"  {tag}: reusing {os.path.relpath(out, ROOT)}")
        return out
    for stale in (out, out.replace(".profile.json", ".raw.json"),
                  os.path.join(d, spec.name + ".keepmodel.gencache.json")):
        if os.path.exists(stale):
            os.remove(stale)
    for key in ("profile", "value_profile"):
        src = spec.path(key)
        if src and os.path.exists(src):
            subprocess.check_call(["cp", "-f", src, d])
    log = os.path.join(OUT, f"keepgen_{tag}.log")
    print(f"  {tag}: generating R={R} keep table -> {os.path.relpath(out, ROOT)}  (log {os.path.relpath(log, ROOT)})")
    with open(log, "w") as f:
        subprocess.check_call([os.path.join(ROOT, "build/Release/mtg-analyze"), deck_path,
                               "--seed", str(spec.raw.get("floor_seed", 78000001))],
                              stdout=f, stderr=subprocess.STDOUT, cwd=ROOT,
                              env={**os.environ, "MTG_KEEP_EXHAUSTIVE": "1",
                                   "MTG_KEEP_ROLLOUTS": str(R)})
    open(fp, "w").write(want)
    return out


def floor(spec, tag, dry_run):
    """2 decks x 2 tables in one pooled batch -> the shared apparatus's actual bias on THIS edit."""
    if tag not in spec.arms or tag == "base":
        raise SystemExit(f"--floor wants one of: {', '.join(t for t in spec.arms if t != 'base')}")
    shipped = spec.path("profile")
    tbl = table_cards(shipped)
    if tbl is None:
        raise SystemExit("no shared keep table -> the apparatus is deck-INDEPENDENT, so there is no\n"
                         "table bias to bracket (measured -0.0003 on a -0.20 effect). Nothing to do.")
    if sorted({c for a in spec.arms.values() for c in a} - tbl):
        raise SystemExit("the screen DROPS the table (a card is unbucketed), so both arms already run\n"
                         "the deck-independent heuristic. Nothing to bracket.")

    os.makedirs(OUT, exist_ok=True)
    decks = {t: spec.write_arm(t, os.path.join(OUT, t)) for t in ("base", tag)}
    R = int(spec.raw.get("floor_R", 10))
    print(f"floor bracket for '{tag}': generating its own R={R} table, then 4 cells in one batch\n")
    var_table = gen_table(spec, tag, decks[tag][0], R)

    apps = {"ship": apparatus_dir("ship", spec.name, shipped, shipped_table(shipped)),
            "own":  apparatus_dir("own_" + tag, spec.name, shipped, var_table)}
    jobs = [spec.job(f"{d}__{a}", decks[d][0], decks[d][1], apps[a])
            for d in ("base", tag) for a in ("ship", "own")]
    print(f"\n4 cells x {spec.games:,} games -> ONE pooled batch")
    if dry_run:
        json.dump({"jobs": jobs}, open(os.path.join(OUT, "floor.manifest.json"), "w"), indent=1)
        return 0

    got = score(run_batch(jobs, OUT, "floor", spec.threads), [j["name"] for j in jobs], spec.maxturn)
    e_ship, se_ship, n, id_ship = paired(got, "base__ship", f"{tag}__ship")
    e_own,  se_own,  _, id_own  = paired(got, "base__own",  f"{tag}__own")
    # Difference-of-differences over the SAME game indices -- pair it too, or the two deltas' shared
    # game-to-game variance is counted twice and the bias looks far noisier than it is.
    common = sorted(set.intersection(*[set(v) for v in got.values()]))
    B = [(got[f"{tag}__own"][g] - got["base__own"][g]) -
         (got[f"{tag}__ship"][g] - got["base__ship"][g]) for g in common]
    bias, se_b = st.mean(B), st.pstdev(B) / math.sqrt(len(B))
    fl = abs(bias) + 2 * se_b

    print(f"\n{len(common):,} paired games, d{spec.depth} budget {spec.budget}ms\n")
    print(f"  {'apparatus':28s} {'delta':>9s} {'se':>8s} {'ident':>7s}")
    print(f"  {'shared (shipped) table':28s} {e_ship:+9.4f} {se_ship:8.4f} {id_ship:6.1f}%")
    print(f"  {tag+' own R=%d table' % R:28s} {e_own:+9.4f} {se_own:8.4f} {id_own:6.1f}%")
    print(f"\n  apparatus bias               {bias:+9.4f} {se_b:8.4f}   (t = {bias/se_b if se_b else float('nan'):+.2f})")
    print(f"     {'(each table flatters the deck it was fit to -- the expected direction)' if bias < 0 else '(the shared table flatters the VARIANT -- unexpected; read the cells before trusting it)'}")
    print(f"  floor = |bias| + 2se          {fl:9.4f}")
    print(f"  effect / floor                {abs(e_ship)/fl if fl else float('inf'):9.2f}x")
    # How much weaker the bracket's arm actually plays. Expected to be POSITIVE and large: it is an
    # R=<floor_R> table measured against a shipped high-R one, and on slivers that gap (~0.032t) is the
    # size of the whole effect. A level difference cancels out of the difference-of-differences above,
    # but it is why the bracket OVERSTATES the floor and must never be read as "the accurate arm".
    cost, se_c, _, _ = paired(got, f"{tag}__ship", f"{tag}__own")
    print(f"\n  cost of the R={R} bracket table vs the shipped one: {cost:+.5f} +-{se_c:.5f} "
          f"(positive = weaker play, expected)")
    if fl and abs(e_ship) / fl < 3:
        print("\n  VERDICT: the effect does NOT clear its floor by 3x. Treat it as UNRESOLVED, not")
        print("  refuted: either the edit is genuinely small, or the shared apparatus is doing the")
        print("  work. Only a high-R regeneration settles it -- at low R the table's own regret is")
        print("  larger than the bias, so re-running this bracket bigger will not help.")
    else:
        print("\n  VERDICT: the effect clears its floor by 3x+ -- the shared apparatus is not producing")
        print("  it. Note the floor is a screening guard, not a confidence interval on the effect.")
    return 0


def shipped_table(profile_path):
    """Path of the committed sidecar the engine would auto-attach for this profile."""
    stem = re.sub(r"\.profile\.json$", "", profile_path)
    for ext in (".keepmodel.exhaustive.profile.json.gz", ".keepmodel.exhaustive.profile.json"):
        if os.path.exists(stem + ext):
            return stem + ext
    raise SystemExit(f"no committed keep table beside {profile_path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("spec")
    ap.add_argument("--dry-run", action="store_true", help="build decks + manifest, run nothing")
    ap.add_argument("--floor", metavar="TAG", help="bracket ONE combination's apparatus bias floor")
    args = ap.parse_args()
    spec = Spec(args.spec)
    return floor(spec, args.floor, args.dry_run) if args.floor else screen(spec, args.dry_run)


if __name__ == "__main__":
    sys.exit(main())
