#!/usr/bin/env python3
"""Compare COMBINATIONS of one fixed card pool -- fast, paired, apparatus-symmetric.

    python3 scripts/deck_compare.py <spec.json> [--dry-run]     # the screen
    python3 scripts/deck_compare.py <spec.json> --preflight     # only the checks that need a human/AI
    python3 scripts/deck_compare.py <spec.json> --floor <tag[,tag]>   # bracket the bias floor
    python3 scripts/deck_compare.py <spec.json> --confirm <tag>       # re-measure on held-out seeds

The question this answers: given a base deck and a set of count changes, which combination is faster?
Card implementation and heuristic tuning are one-time costs per CARD; this is the per-COMBINATION
loop. Introducing a card the deck has never held is in scope -- the pre-flight (4) routes the
one-time part to a human or an AI and refuses until it is done.

Per-game cost varies by two orders of magnitude with the deck AND the apparatus (measured at d5 /
budget 20: slivers 9.8 ms/game with its keep table, 254.8 without it -- and an introduced card always
drops the table). Read the `ms=` on a 300-game probe before sizing a long run.

Design + all measurements behind it: docs/design/deck-combination-screening.md

Four things it does that a hand-rolled A/B does not:

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

3. A COVERAGE PRE-FLIGHT ON BOTH CLIFFS, AND A POOL TABLE RATHER THAN A DROP. A hand the table
   cannot answer does not get a biased answer, it gets NO answer: `Decide` returns present=false and
   the caller falls through to the generic heuristic AND to lookahead bottoming, silently. Two ways
   in, and only the first used to be checked -- an UNBUCKETED CARD (any introduced card), and an
   UNTABLED COMPOSITION (compositions are enumerated capped at the BASE deck's bucket counts, so
   RAISING a small bucket makes hands reachable that were never tabled, on that arm alone: 6.32% for
   a 1-of raised to a 4-of, 0.0004% for a 4-of raised to a 5-of).
   Either way the driver generates a table for the POOL -- the union of every arm's cards at the
   highest count any arm plays them -- instead of dropping it. Coverage is then total BY
   CONSTRUCTION, and verify_coverage() + verify_bottoming() assert it before a game runs. Dropping
   was never free: ~0.063t of play quality on BOTH arms, ~22x per-game wall, and bottoming regressing
   to the lookahead (every shipped sidecar has bottoming_enabled). Small overflows below
   "max_fallback" (1%) just keep the shipped table -- regenerating for 0.0004% costs more than the
   bias it avoids. Dropping the TABLE would never drop the PLAY PROFILE either: that is
   MTG_EXHAUSTIVE_PROFILE=none on the batch, not a missing `profile` key.

4. AN INTRODUCED-CARD PRE-FLIGHT -- the part that needs a human or an AI, so it runs FIRST and
   REFUSES rather than measuring something else (`--preflight` runs just this). A card the screen has
   never seen before fails in three places, none of which announces itself:
     - not in cards.json      -> the engine throws "Unknown card template" inside a pooled batch,
                                 killing every arm's games with it. Route: .claude/skills/analyze-deck.md.
     - implemented with GAPS  -> the screen answers about a card the engine plays incompletely; the
                                 check is analyze_deck.py's own oracle-text scan, imported not copied.
     - absent from the profile's card_scores -> silent and ASYMMETRIC: ComputeHandScore SKIPS an
                                 unscored card (AIEngine.cpp), so hands holding it score lower, get
                                 mulliganed more, and the arm that plays the new card is marked down
                                 for playing it. The driver derives a POOL PROFILE for that case (one
                                 analyzer run over the union of every arm's cards, merged into the
                                 shipped profile) and gives the same one to every arm. Measured small
                                 and deck-shaped -- +0.0022 on slivers, and exactly 0 on a deck whose
                                 profile has a keep_model, which owns the keep and never reads
                                 card_scores. Cheap insurance (~20 s), not a rescue.

Spec format:

    {
      "base":          "decks/slivers_vial/slivers_vial.txt",
      "profile":       "decks/slivers_vial/slivers_vial.profile.json",   # optional: found beside base
      "value_profile": "decks/slivers_vial/slivers_vial.value.json",     # optional: found beside base
      "games": 20000, "seed": 910000, "depth": 5, "budget_ms": 20,
      "combinations": {
        "more_leeching": {"Hatchery Sliver": 2, "Leeching Sliver": 4},
        "cut_vial":      {"Aether Vial": 0, "Muscle Sliver": 6}
      }
    }

A combination is a map of card -> NEW COUNT (absent = unchanged, 0 = removed). A card not in the base
deck may be introduced by naming it, and the pre-flight above says what that costs.

`profile` and `value_profile` default to the deck's siblings and are NOT optional in effect: with no
profile the engine loads MulliganProfile::DefaultProfile() and every arm plays a deck we do not ship
(on slivers that zeroes vial_target_mv and inflated a measured effect 2.6x). "allow_no_profile": true
is the deliberate hatch for a deck that genuinely has none yet.

`--floor <tag>` answers the question the screen cannot: is the measured delta bigger than the bias
the shared apparatus itself carries? It generates a throwaway low-R keep table for that ONE
combination and re-measures the same delta under it, so the bias is observed rather than predicted:

    bias = delta(under the own table[s]) - delta(under the shared table)

All four cells (2 decks x 2 tables) run in ONE pooled batch off the same game indices, so the
difference-of-differences is fully paired. When the arms hold different CARDS (an introduced card, or
one cut to 0) each deck gets its OWN table rather than both running under the variant's: one table
for both would leave the base deck's dropped card unbucketed on 40.0% of hands for a 4-of swap, which
is ~0.025t of one-sided damage against a bias of ~0.005t. The bias is then exactly the difference of
two WITHIN-deck nulls, both printed -- and a bracket only worries when the two nulls DIFFER.
Note what this does and does not bound: at a low R the own table carries its own sampling noise
(~0.01t at R=10, larger than the bias), so the bracket OVERSTATES the floor. That is the right
direction for a safety check and the wrong direction for an accuracy claim -- never treat the
own-table arm as "the accurate one".
"""
import argparse, gzip, hashlib, json, math, os, re, statistics as st, subprocess, sys
from pathlib import Path

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT  = os.path.join(ROOT, "logs", "deckcmp")
CARDS_JSON = os.path.join(ROOT, "src", "cards", "data", "cards.json")

# The card checks are analyze_deck.py's, imported rather than re-implemented: a screen must hold a
# newly introduced card to the SAME standard deck onboarding does, and two copies of an oracle-text
# scan would drift. analyze_deck.py does nothing at import (its work is behind Main()).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze_deck import LoadDeckCounts, LoadImplementedNames, CheckExistingCoverage  # noqa: E402


def read_decklist(path):
    """-> [(count, name)] in FILE ORDER. File order defines the base numbering, so it is load-bearing.

    Both formats, via analyze_deck's parser: 14 of the 17 decks in `decks/` are Cockatrice `.cod`
    XML, and a `.cod` fed to a split-on-space text parser does not fail loudly -- it yields nonsense
    card names. `LoadDeckCounts` returns an insertion-ordered dict, so file order survives."""
    return [(c, n) for n, c in LoadDeckCounts(Path(path)).items()]


def base_numbering(deck):
    """Number the base 1..N in decklist order. Contiguity is not required by the engine (m_number is
    only ever equality-compared, never an index) -- it is just the obvious starting assignment."""
    m, nxt = {}, 1
    for count, name in deck:
        m[name] = list(range(nxt, nxt + count))
        nxt += count
    return m


def inherit_numbering(base_nums, base_counts, new_counts, pairs=None):
    """Unchanged copies keep their numbers; numbers freed by removed copies are INHERITED by added
    copies. Never renumber -- renumbering is the entire bug this exists to avoid.

    `pairs` is the spec's optional `replace` map for this arm ({removed card: added card}), and it
    makes the pairing EXACT where it used to be a convention: without it, freed numbers are handed
    out in sorted order, which is deterministic and fine for one added name but arbitrary as soon as
    two cards are added at once -- the slots are the same either way, but WHICH new card lands in the
    departing card's slots is then alphabetical rather than intentional. Anything the map does not
    cover falls back to the sorted rule."""
    freed_of, out = {}, {}
    for name, nums in base_nums.items():
        keep = min(new_counts.get(name, 0), base_counts[name])
        out[name] = nums[:keep]
        freed_of[name] = list(nums[keep:])
    high = max((max(v) for v in base_nums.values() if v), default=0)
    for src, dst in (pairs or {}).items():
        need = new_counts.get(dst, 0) - len(out.get(dst, []))
        take = freed_of.get(src, [])
        for _ in range(min(need, len(take))):
            out.setdefault(dst, []).append(take.pop(0))
    freed = sorted(n for v in freed_of.values() for n in v)
    for name in sorted(new_counts):
        need = new_counts[name] - len(out.get(name, []))
        for _ in range(need):
            # A genuine size increase takes a fresh number; survivors keep their relative order either
            # way, so alignment degrades gracefully instead of collapsing.
            out.setdefault(name, []).append(freed.pop(0) if freed else (high := high + 1))
        out[name] = sorted(out.get(name, []))
    return {k: v for k, v in out.items() if v}


def table_candidates(path):
    """Every file that could supply a keep table for `path`, in the ENGINE's resolution order.

    `.gz` FIRST, because that is what `AttachExhaustiveSidecar` (MulliganProfileIO.h:858) does. This
    list used to be in the opposite order, so the driver could compute coverage from one file while
    the games ran on another. `decks/slivers_vial/` holds exactly that pair right now: a gitignored
    R=1 table sitting beside the committed R=60 `.gz`. Their buckets happen to be identical (so no
    measurement was affected) but their keep decisions are not -- identical bucketing was luck."""
    if not path:
        return []
    stem = re.sub(r"\.profile\.json$", "", path)
    return [path, path + ".gz",
            stem + ".keepmodel.exhaustive.profile.json.gz",
            stem + ".keepmodel.exhaustive.profile.json"]


def table_meta(path):
    """-> (resolved path, `exhaustive_keep` dict) for `path`, or (None, None) if it has no table.

    Refuses when two candidates exist and disagree about the BUCKETING: that is a silent
    apparatus swap, and the driver's coverage answer would be about a table the engine will not
    load."""
    found = []
    for p in table_candidates(path):
        if not os.path.exists(p):
            continue
        op = gzip.open if p.endswith(".gz") else open
        try:
            ek = (json.load(op(p, "rt")).get("exhaustive_keep") or {})
        except Exception:
            continue
        if ek.get("buckets"):
            found.append((p, ek))
    if not found:
        return None, None
    win, rest = found[0], found[1:]
    for p, ek in rest:
        if ek["buckets"] != win[1]["buckets"]:
            raise SystemExit(
                f"two keep tables beside {path} disagree about the bucketing -- refusing.\n"
                f"  the engine would load: {os.path.relpath(win[0], ROOT)} (K={len(win[1]['buckets'])})\n"
                f"  also present:          {os.path.relpath(p, ROOT)} (K={len(ek['buckets'])})\n"
                "  Coverage would be computed for one and the games played under the other. Move the\n"
                "  stale one aside (gitignored keep tables beside a deck have contaminated runs before).")
    return win


def table_buckets(path):
    """The exhaustive keep table's buckets (list of member-name lists), or None if there is none.
    Buckets are the unit of BOTH coverage tests below -- the policy is keyed on bucket composition,
    not on card names."""
    return (table_meta(path)[1] or {}).get("buckets")


def enum_cells(buckets, counts, hand=7):
    """Every size-`hand` composition `EnumComps` enumerates for `counts` under `buckets`."""
    n2b = {n: i for i, b in enumerate(buckets) for n in b}
    cap = [min(sum(c for n, c in counts.items() if n2b.get(n) == i), hand) for i in range(len(buckets))]
    out, cur = [], [0] * len(cap)

    def rec(i, rem):
        if i == len(cap):
            if rem == 0:
                out.append(tuple(cur))
            return
        for x in range(0, min(cap[i], rem) + 1):
            cur[i] = x
            rec(i + 1, rem - x)
        cur[i] = 0

    rec(0, hand)
    return out


def verify_table_for_deck(ek, counts, label, path):
    """The table's ENTRY COUNT is a fingerprint of the decklist it was generated for -- assert it.

    Every coverage answer in this driver rests on an assumption nothing checked: that `counts` are
    the counts the table was generated for. `fallback_rate` derives its caps from them, so if the
    decklist has been edited since the table was built, the caps are wrong and the driver reports
    full coverage for hands that will land as present=false. (The mirror of this mistake -- passing
    the ARM's counts as the generating counts -- was a real bug in `--floor`.) The sidecar records
    `commit` and `effective_R` but not the counts, so the check is structural: the number of size-7
    cells implied by the decklist must equal the number of entries. Verified exact on all 10
    committed tables, K=10..21, 7,758..431,144 entries."""
    want = enum_cells(ek["buckets"], counts)
    have = {tuple(e["comp"]) for e in (ek.get("entries") or [])}
    missing = [c for c in want if c not in have]
    if missing:
        ex = "; ".join(" ".join(f"{n}x{v}" for n, v in zip(("+".join(b) for b in ek["buckets"]), c) if v)
                       for c in missing[:2])
        raise SystemExit(
            f"{label} does not answer every hand this decklist can draw -- refusing.\n"
            f"  {os.path.relpath(path, ROOT)} has {len(have):,} cells; this decklist needs "
            f"{len(want):,}, of which {len(missing):,} are absent.\n"
            f"  e.g. {ex}\n"
            "  Those hands land as present=false: the generic heuristic keep AND lookahead bottoming,\n"
            "  silently. Regenerate the table for the current decklist (mulligan-profile.md), or\n"
            "  screen the decklist the table was built for.")
    # A SUPERSET is fine and is what a reweighted table looks like: it keeps the source deck's grid,
    # and cells the arm cannot draw get hypergeometric weight 0 (Comb(n,k)=0 for k>n) so they cannot
    # affect D_opt. Worth saying, because "10,945 entries for a 10,780-cell deck" reads like a bug.
    if len(have) > len(want):
        print(f"  ({label}: {len(have):,} cells for a {len(want):,}-cell decklist -- a superset, "
              "the extra cells carry weight 0)")


def table_cards(profile_path):
    """Card names the table has bucketed, or None if there is no table."""
    b = table_buckets(profile_path)
    return None if b is None else {n for bk in b for n in bk}


def comb(n, k):
    return math.comb(n, k) if 0 <= k <= n else 0


def pct(x):
    """Fixed 2dp hides exactly the distinction that matters here: raising a 4-of to a 5-of is
    0.0004% (harmless) and raising a 1-of to a 4-of is 6.32% (not), and both print as "0.00%" or
    "6.32%" under %.2f -- the first reading as a clean zero next to a warning block."""
    return f"{x*100:.2f}%" if x >= 0.0001 else f"{x*100:.4f}%"


def fallback_rate(buckets, base_counts, arm_counts, hand=7):
    """P(a size-`hand` opening hand resolves to a composition the table never enumerated).

    The SECOND coverage cliff, and the one nothing checked. `EnumComps` (analyzer/ExhaustiveKeep.cpp)
    caps each bucket at `min(count[b], H)` -- the BASE deck's count. So a combination that RAISES a
    bucket above its base size makes compositions reachable that were never tabled, and
    `ExhaustiveKeepPolicy::Decide` answers those with present=false: a silent fall-through to the
    generic heuristic, on the one arm whose counts changed. Cuts are always safe (they only make
    compositions unreachable), and a bucket already at >= `hand` copies cannot overflow at all.

    Exact, not sampled: sum the hypergeometric weight of every reachable composition that exceeds a
    cap. Enumeration is over compositions of 7 across K buckets -- milliseconds at any real K."""
    n2b = {n: i for i, b in enumerate(buckets) for n in b}
    K = len(buckets)
    cnt = lambda counts: [sum(c for n, c in counts.items() if n2b.get(n) == i) for i in range(K)]
    base, new = cnt(base_counts), cnt(arm_counts)
    cap = [min(c, hand) for c in base]
    # A bucket can only overflow if the cap was set by the BASE COUNT rather than by the hand size:
    # one already holding >= `hand` copies had every composition 0..hand enumerated, so raising it
    # further changes nothing a 7-card hand can express.
    over_idx = [i for i in range(K) if min(new[i], hand) > cap[i]]
    if not over_idx:
        return 0.0, []
    total, bad = comb(sum(new), hand), 0

    def rec(i, rem, cur, viol):
        nonlocal bad
        if i == K:
            if rem == 0 and viol:
                w = 1
                for b in range(K):
                    w *= comb(new[b], cur[b])
                bad += w
            return
        for x in range(0, min(new[i], rem) + 1):
            cur[i] = x
            rec(i + 1, rem - x, cur, viol or x > cap[i])
        cur[i] = 0

    rec(0, hand, [0] * K, False)
    over = [f"[{i}] {', '.join(buckets[i])} {base[i]}->{new[i]}" for i in over_idx]
    return (bad / total if total else 0.0), over


def score(path, arms, max_turns, expect=None):
    """Per-arm {game_index: win_turn}. Unwon prints wt=-1 and is scored max_turns+1 -- the repo's
    primary objective. A regex that only accepts \\d+ silently DROPS those games and biases the mean.

    `expect` is the per-job game count, and a short job is a REFUSAL. Everything downstream pairs on
    the intersection of game indices, so a job that died halfway just makes `n` smaller and prints a
    perfectly ordinary-looking number -- the "a truncated run reads as a result" failure CLAUDE.md
    exists to prevent. The raw log is left intact for diagnosis."""
    got = {a: {} for a in arms}
    for line in open(path):
        m = re.match(r"\[win\] job=(\S+) gi=(\d+) wt=(-?\d+)", line)
        if m and m.group(1) in got:
            wt = int(m.group(3))
            got[m.group(1)][int(m.group(2))] = max_turns + 1 if wt < 0 else wt
    short = {a: len(g) for a, g in got.items() if expect is not None and len(g) != expect}
    if short:
        raise SystemExit(
            "the batch did not finish every game -- refusing to report a number.\n"
            + "".join(f"  {a:24s} {n:,} of {expect:,}\n" for a, n in sorted(short.items()))
            + f"  The log is intact: {os.path.relpath(path, ROOT)}")
    return got


def run_batch(jobs, outdir, name, threads, env=None):
    """One pooled `mtg --batch` over every job. -> path of the stderr log the wins were dumped to.

    The batch's own progress lines are TEED to stdout instead of being left in the log. `[batch]
    heartbeat: N/M workers busy` is the first-ten-minutes check CLAUDE.md mandates and `SLOW-GAME` is
    how a pathological game announces itself -- and both were landing in a file that also carries one
    `[win]` line per game, so on the last floor run the single heartbeat was line 80,006 of 80,006.
    A screen can run for hours; it should not be silent for them."""
    man = os.path.join(outdir, f"{name}.manifest.json")
    json.dump({"jobs": jobs}, open(man, "w"), indent=1)
    err = os.path.join(outdir, f"{name}.err")
    with open(err, "w") as e, open(os.path.join(outdir, f"{name}.out"), "w") as o:
        p = subprocess.Popen([os.path.join(ROOT, "build/Release/mtg"), "--batch", man,
                              "--threads", str(threads or os.cpu_count())],
                             stdout=o, stderr=subprocess.PIPE, text=True, cwd=ROOT,
                             env={**os.environ, "MTG_DUMP_WINS": "1", **(env or {})})
        for line in p.stderr:
            e.write(line)
            if line.startswith("[batch]") or line.startswith("[play]") or "SLOW-GAME" in line:
                print("  | " + line.rstrip(), flush=True)
        rc = p.wait()
    if rc != 0:
        raise SystemExit(f"batch failed rc={rc}; see {err}")
    bad = provider_split(err)
    if bad:
        raise SystemExit(
            "arms did not run under the same archetype provider -- REFUSING the comparison.\n  "
            + "\n  ".join(f"{p_}: {', '.join(sorted(t))}" for p_, t in sorted(bad.items()))
            + "\n\n  Archetype detection is by card PARAMS (SelectDecisionProvider), so an edit can cross a"
              "\n  signature and hand one arm another deck's heuristics -- tutor narrowing, dig ranking,"
              "\n  attack rules. That is not a bias a shared apparatus absorbs; it is two engines.\n"
              "\n  There is deliberately NO runtime pin. Forcing both arms onto one provider would hide"
              "\n  the finding, and the finding is the point -- one of two things is true:\n"
              "\n    the deck is still itself -> make the PROVIDER support both decklists (widen the"
              "\n      signature, or keep the archetype hook present-but-inert without its card), so both"
              "\n      arms route to it legitimately. This is the preferred fix.\n"
              "\n    the edit made it a DIFFERENT deck -> screening is the wrong instrument. Screening"
              "\n      compares combinations sharing one identity and one apparatus. Analyse it as a NEW"
              "\n      deck instead (analyze-deck.md, then mulligan-profile.md + value-leaf.md): you still"
              "\n      get a win-turn diff against the original, it just costs a table from scratch"
              "\n      rather than a shared one.")
    return err


def provider_split(err_path):
    """-> {provider: {arm, ...}} if the arms disagree, else {}. Read off the batch's own `[play]` line.

    Reported by the engine rather than re-derived here: detection reads card params, and a Python
    mirror of it would be a second implementation free to drift from the one that decides play."""
    seen = {}
    try:
        for line in open(err_path):
            if not line.startswith("[play] "):
                continue
            parts = line.split()
            name = parts[1]
            prov = next((x.split("=", 1)[1] for x in parts if x.startswith("provider=")), None)
            if prov:
                seen.setdefault(prov, set()).add(name.split("__")[0])
    except OSError:
        return {}
    return seen if len(seen) > 1 else {}


def job_costs(err_path):
    """{job: ms per game} from the batch's own per-job summary. -> {} if it cannot be read.

    Printing this per arm replaces the advice it makes obsolete: the skill used to say "read the ms=
    on a 300-game probe before sizing a long run", and a 300-game probe is dominated by fixed startup
    -- the same burn job measured 42.5 and 114 ms/game on two 300-game runs against 59.3 at 20,000.
    The number that matters is the one THIS run produced, and per ARM, because arms are not equally
    expensive: slivers' base arm cost 1,632,530 ms against cut_vial's 280,522 for the same 20,000
    games (5.8x -- Aether Vial's enumeration), so a screen's wall clock is set by its priciest arm."""
    out = re.sub(r"\.err$", ".out", err_path)
    costs = {}
    try:
        for line in open(out):
            m = re.match(r"(\S+): played=(\d+) .*ms=(\d+)", line)
            if m and int(m.group(2)):
                costs[m.group(1)] = int(m.group(3)) / int(m.group(2))
    except OSError:
        pass
    return costs


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
        self.raw      = s
        self.raw_path = os.path.abspath(path)
        self.games   = int(s.get("games", 20000))
        self.seed    = int(s.get("seed", 910000))
        self.maxturn = int(s.get("max_turns", 8))
        self.threads = int(s.get("threads", 0))
        self.base_path = s["base"] if os.path.isabs(s["base"]) else os.path.join(ROOT, s["base"])
        self.deck  = read_decklist(self.base_path)
        self.counts = {n: c for c, n in self.deck}
        self.nums   = base_numbering(self.deck)
        # The arm decklists are always written as <name>.txt, whatever the base's format: the engine
        # reads both, and the STEM is what matters -- mtg-analyze derives the keep table's filename
        # from the decklist's stem, so an arm named anything else would generate `<other>.keepmodel…`.
        self.name   = os.path.splitext(os.path.basename(self.base_path))[0]   # profile/sidecar stem
        self.stem   = self.name + ".txt"
        # Every scratch path is namespaced by DECK. Keying them on the arm tag alone (logs/deckcmp/
        # base/) meant two specs over different decks shared `base/`, and `numbering.json` is written
        # per arm but read by the engine when the JOB runs -- so a second invocation between a floor
        # bracket's table generation and its batch silently replaced the first run's numbering. It
        # cost a 20-minute generation, and it only failed LOUDLY because the two decks had disjoint
        # card names; two specs over the SAME deck would have mis-numbered in silence.
        self.out = os.path.join(OUT, self.name)
        self.arms = {"base": dict(self.counts)}
        for tag, ov in s["combinations"].items():
            c = dict(self.counts)
            c.update({k: int(v) for k, v in ov.items()})
            self.arms[tag] = {k: v for k, v in c.items() if v > 0}
        # Cards no arm's base holds -- everything the introduced-card pre-flight is about.
        self.introduced = sorted({c for a in self.arms.values() for c in a} - set(self.counts))
        # Optional per-arm {removed card: added card}. Only the numbering reads it; it never changes
        # what a combination IS (that is the counts), only which slots the new copies inherit.
        self.replace = s.get("replace", {})
        for tag, m in self.replace.items():
            if tag not in self.arms:
                raise SystemExit(f"\"replace\" names an unknown combination: {tag}")
            for src, dst in m.items():
                if self.arms[tag].get(src, 0) >= self.counts.get(src, 0):
                    raise SystemExit(f"\"replace\" {tag}: {src} is not reduced, so it frees nothing")
                if self.arms[tag].get(dst, 0) <= self.counts.get(dst, 0):
                    raise SystemExit(f"\"replace\" {tag}: {dst} is not increased, so it needs nothing")
        # Resolve the apparatus the way the ENGINE would if it were left to auto-detect: sibling of
        # the decklist, by stem. Leaving these to the spec made the load-bearing thing optional, and
        # an omitted `profile` reads exactly like a deliberate one (it is not -- see the header).
        self.profile       = self.path("profile")       or self.sibling(".profile.json")
        self.value_profile = self.path("value_profile") or self.sibling(".value.json")

        # PLAY SETTINGS COME FROM THE DECK, not from a constant here. Every deck's value model carries
        # a `value_play` block that IS its adopted, measured policy (burn: d6/b20, escalation_cap 6,
        # fitted jointly with value_trust_depth 5; Goblins: d6/b40), and several carry a cheaper
        # `mull_gen_*` pair the deck's owners already accepted for rollout work (Goblins d3/b10).
        # Two legitimate choices, and they are the deck's own numbers either way:
        #   "play": "quality"  (default) -- the adopted play policy; the screen then measures the deck
        #                                   as it is actually played, so its number means something
        #                                   outside the screen.
        #   "play": "speed"             -- the mulligan-generation policy: cheaper, and already
        #                                   sanctioned for this deck rather than invented here.
        # Pinning `depth`/`budget_ms` in the spec still works but is now an explicit OVERRIDE: the
        # engine THROWS if a depth is passed while value_play drives, so it needs
        # `ignore_play_profile`, which is exactly the flag this driver used to set on every job while
        # forcing d5/b20 -- screening burn one depth below the depth burn ships.
        self.play_mode = s.get("play", "quality")
        vp = {}
        if self.value_profile and os.path.exists(self.value_profile):
            try:
                vp = json.load(open(self.value_profile)).get("value_play") or {}
            except Exception:
                vp = {}
        drives = bool(vp.get("enabled")) and int(vp.get("target_depth", 0)) > 0
        if "depth" in s or "budget_ms" in s:
            self.depth, self.budget = int(s.get("depth", 5)), int(s.get("budget_ms", 20))
            self.pin_play, self.play_source = True, "spec (OVERRIDES the deck's policy)"
        elif self.play_mode == "speed":
            # Mirrors ValuePlay::MullGenDepth/MullGenBudgetMs: explicit mull_gen_*, else the play
            # numbers, else the built-in default.
            self.depth  = int(vp.get("mull_gen_depth") or vp.get("target_depth") or 5)
            self.budget = int(vp.get("mull_gen_budget_ms") or vp.get("budget_ms") or 20)
            self.pin_play, self.play_source = True, "value_play.mull_gen (speed)"
        elif drives:
            self.depth, self.budget = int(vp["target_depth"]), int(vp.get("budget_ms") or 20)
            self.pin_play, self.play_source = False, "value_play (the deck's adopted policy)"
        else:
            self.depth, self.budget = 5, 20
            self.pin_play, self.play_source = False, "engine default (this deck has no adopted policy)"

    def sibling(self, ext):
        p = os.path.join(os.path.dirname(self.base_path), self.name + ext)
        return p if os.path.exists(p) else None

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
        nums = (self.nums if tag == "base" else
                inherit_numbering(self.nums, self.counts, counts, self.replace.get(tag)))
        json.dump(nums, open(np_, "w"), indent=0)
        return dp, np_

    def job(self, name, deck, numbering, profile, seed=None):
        j = {"name": name, "deck": deck, "deck_numbering": numbering,
             "games": self.games, "seed": self.seed if seed is None else seed,
             "max_turns": self.maxturn}
        if self.pin_play:
            # ResolvePlaySettings THROWS on an explicit depth while value_play drives, so a pinned
            # depth must say so. Omitting both is what lets the engine resolve the deck's own policy
            # (it prints `[play] <job> depth=.. budget=..ms source=..` per job either way).
            j.update({"depth": self.depth, "budget_ms": self.budget, "ignore_play_profile": True})
        if profile:
            j["profile"] = os.path.relpath(profile, ROOT)
        else:
            # No `profile` key does NOT mean "no profile": BatchRunner::ParseJob auto-detects
            # <deck>.profile.json beside the DECK, and the arm decklists live in a scratch directory
            # that has none -- so the job silently gets DefaultProfile() and plays a deck we do not
            # ship. Only reachable via "allow_no_profile"; the inert key records that in the manifest,
            # so a later reader can tell the hatch apart from the bug.
            j["_no_profile_deliberate"] = True
        if self.value_profile:
            # Ladder mode: the model makes warm-up passes cheap, the COMMITTED pass stays pure
            # heuristic. Verified byte-identical under a deliberately wrong model at unbounded budget;
            # residual budget coupling at budget>0 measured 0.0008t. That guarantee is what lets one
            # pool model serve every combination without re-validating per combination.
            j["value_profile"] = os.path.relpath(self.value_profile, ROOT)
            j["value_model"] = False
            j["ladder_value_leaf"] = True
        return j


def profile_scores(path):
    """The card names a profile carries a card_scores entry for (empty set if it has none)."""
    if not path or not os.path.exists(path):
        return set()
    return set((json.load(open(path)).get("card_scores") or {}))


def preflight(spec):
    """Everything that must be true before a game is worth running -- and the only part of the loop
    that can need a human or an AI. It REFUSES: a screen that runs anyway does not fail, it answers a
    different question, and it answers it with a plausible-looking number.

    -> {"missing", "gaps", "unscored"} for the caller to act on."""
    if not spec.profile and not spec.raw.get("allow_no_profile"):
        raise SystemExit(
            f"no play profile for {spec.raw['base']}\n"
            f"  looked for: {os.path.join(os.path.dirname(spec.base_path), spec.name + '.profile.json')}\n"
            "  Without one every arm loads MulliganProfile::DefaultProfile() and plays a deck we do\n"
            "  not ship (on slivers that zeroes vial_target_mv and inflated a measured effect 2.6x).\n"
            "  Generate it with scripts/analyze_deck.py, or set \"allow_no_profile\": true if this deck\n"
            "  genuinely has none yet -- in which case every arm is equally wrong, which is at least\n"
            "  symmetric.")

    implemented = LoadImplementedNames(Path(CARDS_JSON))
    missing = sorted({c for a in spec.arms.values() for c in a} - implemented)
    # Only INTRODUCED cards are gap-scanned. The base deck's cards came through analyze-deck already
    # and are in every arm, so re-litigating them here would block screens over a decision that was
    # taken elsewhere; a newly named card has had no such review.
    gaps = [c for c in CheckExistingCoverage([c for c in spec.introduced if c in implemented],
                                             Path(CARDS_JSON)) if c.get("status") == "partial"]
    # With no profile at all there is nothing to be missing FROM: DefaultProfile carries no
    # card_scores, and AIEngine skips the hand-score gate entirely when the map is empty -- so every
    # arm is scored the same (badly, but symmetrically) and there is no asymmetry to repair.
    unscored = [] if not spec.profile else [
        c for c in spec.introduced if c in implemented and c not in profile_scores(spec.profile)]
    return {"missing": missing, "gaps": gaps, "unscored": unscored}


def refuse_on_cards(pf):
    """The two pre-flight findings a machine cannot resolve. Both hand off to analyze-deck."""
    if pf["missing"]:
        raise SystemExit(
            "these cards are not implemented: " + ", ".join(pf["missing"]) + "\n\n"
            "  A screen cannot introduce a card the engine does not know -- CardDatabase throws\n"
            "  \"Unknown card template\" inside the pooled batch, taking every arm's games with it.\n"
            "  Implementing one is the AI's job, not this driver's, and it has a route:\n\n"
            "    1. python3 scripts/analyze_deck.py <base decklist> --coverage-only\n"
            "    2. read .claude/skills/mtg-rules.md, implement each card into\n"
            "       src/cards/data/cards.json from its ORACLE TEXT, then review it against the skill\n"
            "    3. ./build.sh   (a card added to cards.json is data, but the pre-flight reads the\n"
            "       same file the engine does, so a typo shows up here first)\n"
            "    4. re-run this screen\n\n"
            "  Implementation is a one-time cost per CARD; screening is the per-COMBINATION loop.")
    if pf["gaps"]:
        lines = [f"    {g['card']}: " + "; ".join(g["gaps"]) for g in pf["gaps"]]
        raise SystemExit(
            "introduced cards with implementation gaps:\n" + "\n".join(lines) + "\n\n"
            "  This is analyze_deck.py's own oracle-text scan (--coverage-only uses it to exit 1).\n"
            "  Screening a partially-implemented card measures the part that IS implemented and\n"
            "  reports it as the card. Either finish the implementation, or record the deliberate\n"
            "  simplification as a [bracket note] in the card's oracle_text -- which is what the\n"
            "  scan treats as a signed-off deferral rather than a gap.\n"
            "  \"allow_card_gaps\": true screens anyway, knowing the answer is about a proxy.")


def gate(spec):
    """Pre-flight + the refusals it implies, honouring the spec's hatches. -> the pre-flight report.

    `allow_card_gaps` waives the GAP refusal only. An unimplemented card is not waivable: the batch
    cannot run at all without it, so there is nothing to trade off."""
    pf = preflight(spec)
    refuse_on_cards({**pf, "gaps": []} if spec.raw.get("allow_card_gaps") else pf)
    return pf


def pool_profile(spec, unscored):
    """Give the introduced cards a card_scores entry, so the screen does not mark an arm down for
    playing a card the profile has never heard of.

    ComputeHandScore (AIEngine.cpp) SKIPS a name that is not in card_scores, and the keep gate then
    compares that short sum against hand_score_threshold. So an unscored card is not scored neutrally
    -- it is scored as if the slot were empty, in the one arm that plays it. Same for the bottoming
    pick, where CardScore() returns 0.0 and the new card is bottomed first.

    The fix is one analyzer run over the UNION of every arm's cards (the same fixed recipe that
    produced the shipped scores: default keep, no gate, vial_target_mv from the deck), merged into a
    COPY of the shipped profile -- adding only the entries it lacks. Merging rather than replacing is
    what keeps the base arm's play unchanged: the added names cannot appear in a base-arm hand, and
    the shipped threshold keeps the meaning it was fitted with.

    -> path of the merged profile (in a directory with NO keep sidecar, so nothing auto-attaches)."""
    d = os.path.join(spec.out, "pool")
    gen = os.path.join(d, "gen")
    os.makedirs(gen, exist_ok=True)
    union = {n: max(a.get(n, 0) for a in spec.arms.values())
             for n in {c for a in spec.arms.values() for c in a}}
    ordered  = [(union[n], n) for _, n in spec.deck if union.get(n)]
    ordered += [(union[n], n) for n in sorted(set(union) - set(spec.counts))]
    out = os.path.join(d, spec.name + ".profile.json")
    # Reuse is keyed on the union AND on the profile being merged into, not on the file existing:
    # editing a combination or regenerating the deck's profile must not silently reuse the old merge.
    fp   = os.path.join(d, ".pool." + spec.name + ".json")   # per-stem: two decks share this dir
    want = json.dumps({"union": union, "src": open(spec.profile).read()}, sort_keys=True)
    if os.path.exists(out) and os.path.exists(fp) and open(fp).read() == want:
        print(f"  reusing pooled card scores: {os.path.relpath(out, ROOT)}")
        return out

    deck = os.path.join(gen, spec.name + ".txt")
    open(deck, "w").write("\n".join(f"{c} {n}" for c, n in ordered) + "\n")
    log = os.path.join(spec.out, "poolgen.log")
    print(f"  deriving card scores for {', '.join(unscored)} over the {sum(union.values())}-card union"
          f"\n  (one mtg-analyze run, log {os.path.relpath(log, ROOT)})")
    with open(log, "w") as f:
        res = subprocess.run([os.path.join(ROOT, "build/Release/mtg-analyze"), deck,
                              "--cards-json", CARDS_JSON,
                              "--seed", str(spec.raw.get("pool_seed", 66000001))],
                             stdout=subprocess.PIPE, stderr=f, text=True, cwd=ROOT)
    if res.returncode != 0:
        raise SystemExit(f"mtg-analyze exited {res.returncode}; see {log}")
    scores = json.loads(res.stdout).get("card_scores") or {}
    prof = json.load(open(spec.profile))
    merged = dict(prof.get("card_scores") or {})
    for n in unscored:
        if n not in scores:
            raise SystemExit(f"the analyzer returned no card score for {n} -- refusing to screen it "
                             "with an implicit 0")
        merged[n] = scores[n]
    prof["card_scores"] = merged
    json.dump(prof, open(out, "w"), indent=1)
    open(fp, "w").write(want)
    for n in unscored:
        print(f"    {n:24s} {[round(x, 4) for x in merged[n]]}")
    return out


def union_counts(spec):
    """The POOL: every card any arm plays, at the highest count any arm plays it.

    This is the deck the shared apparatus should be generated for, and the reason is structural, not
    a heuristic. Coverage is total BY CONSTRUCTION: every arm's cards are present (so nothing is
    unbucketed) and every bucket's union count is >= that bucket's count in any arm (so `EnumComps`'
    cap is never binding for an arm) -- the two ways `Decide` can answer present=false, both closed.
    No drop, no silent heuristic fallback, and above all no fall-back to lookahead bottoming."""
    return {n: max(a.get(n, 0) for a in spec.arms.values())
            for n in {c for a in spec.arms.values() for c in a}}


def pool_table(spec, why, dry=False):
    """Generate the pool's own exhaustive keep table, once, for the union deck. -> table path.

    A screening apparatus, NOT a shippable one. It is generated at `pool_R` (default 10), and an R=10
    table is measured to play ~0.032t weaker than a shipped R=60 one -- but SYMMETRICALLY, on every
    arm, where the own/foreign fit difference among R=10 tables is only 0.004t. Against the
    alternative it replaces (no table at all) it is better on both axes that matter: ~0.063t weaker
    play becomes ~0.032t, and ~22x per-game wall becomes ~1x. Adoption still goes through
    mulligan-profile.md; this never becomes a deck's sidecar."""
    d = os.path.join(spec.out, "pooltable")
    os.makedirs(d, exist_ok=True)
    R = int(spec.raw.get("pool_R", 10))
    counts = union_counts(spec)
    out = os.path.join(d, spec.name + ".keepmodel.exhaustive.profile.json")
    fp   = os.path.join(d, ".pooltable." + spec.name + ".json")
    want = json.dumps({"union": counts, "R": R}, sort_keys=True)
    if os.path.exists(out) and os.path.exists(fp) and open(fp).read() == want:
        print(f"  keep table     reusing the pool table: {os.path.relpath(out, ROOT)}")
        return out
    if dry:
        print(f"  keep table     WOULD generate a POOL table (R={R}) over the "
              f"{sum(counts.values())}-card union -> {os.path.relpath(out, ROOT)}")
        return out
    for stale in (out, out.replace(".profile.json", ".raw.json"),
                  os.path.join(d, spec.name + ".keepmodel.gencache.json")):
        if os.path.exists(stale):
            os.remove(stale)
    # The play profile and value sidecar go in FIRST: RunExhaustiveKeepMode resolves both
    # directory-relative off the decklist and now REFUSES without the profile (85e09b5).
    for src in (spec.profile, spec.value_profile):
        if src and os.path.exists(src):
            subprocess.check_call(["cp", "-f", src, d])
    ordered  = [(counts[n], n) for _, n in spec.deck if counts.get(n)]
    ordered += [(counts[n], n) for n in sorted(set(counts) - set(spec.counts))]
    deck = os.path.join(d, spec.stem)
    open(deck, "w").write("\n".join(f"{c} {n}" for c, n in ordered) + "\n")
    log = os.path.join(spec.out, "pooltable.log")
    print(f"  keep table     {why}\n"
          f"                 generating a POOL table (R={R}) over the {sum(counts.values())}-card union"
          f" -> {os.path.relpath(out, ROOT)}\n"
          f"                 (one-time per pool; log {os.path.relpath(log, ROOT)})")
    with open(log, "w") as f:
        subprocess.check_call([os.path.join(ROOT, "build/Release/mtg-analyze"), deck,
                               "--seed", str(spec.raw.get("pool_table_seed", 55000001))],
                              stdout=f, stderr=subprocess.STDOUT, cwd=ROOT,
                              env={**os.environ, "MTG_KEEP_EXHAUSTIVE": "1",
                                   "MTG_KEEP_ROLLOUTS": str(R)})
    open(fp, "w").write(want)
    return out


def verify_bottoming(table_path, label):
    """"Never fall back to lookahead bottoming" is a property of the ARTIFACT, so assert it on the
    artifact. `Decide` covering a hand is not enough: `DecideBottom` independently returns false when
    the table has no bottoming block, when the flag is off, or when an entry carries no target row --
    and each of those silently restores the rollout-per-candidate path (~22x, and the clairvoyance-
    adjacent policy the tables exist to replace)."""
    op = gzip.open if table_path.endswith(".gz") else open
    ek = json.load(op(table_path, "rt")).get("exhaustive_keep") or {}
    K, entries = len(ek.get("buckets") or []), (ek.get("entries") or [])
    missing = sum(1 for e in entries
                  if not e.get("bottom_keep") or any(len(r) != K for r in e["bottom_keep"]))
    if not ek.get("bottoming_enabled") or not entries or missing:
        raise SystemExit(
            f"{label} cannot bottom every hand -- refusing.\n"
            f"  bottoming_enabled={ek.get('bottoming_enabled')}  entries={len(entries)}  "
            f"without a usable bottom row={missing}\n"
            "  Those hands would silently use lookahead bottoming instead.")


def verify_coverage(buckets, gen_counts, spec, label):
    """Refuse to run unless EVERY arm is fully answered by the table. The whole point of generating
    the pool table is that no arm silently degrades, so an unverified claim of coverage would put us
    back where we started -- this is an assertion, not a report."""
    names = {n for b in buckets for n in b}
    for tag, counts in spec.arms.items():
        miss = sorted(set(counts) - names)
        rate, over = fallback_rate(buckets, gen_counts, counts)
        if miss or rate:
            raise SystemExit(
                f"{label} does not cover arm '{tag}' -- refusing.\n"
                + (f"  unbucketed: {', '.join(miss)}\n" if miss else "")
                + (f"  {pct(rate)} of hands hit an unenumerated composition: {'; '.join(over)}\n"
                   if rate else "")
                + "  Every uncovered hand falls silently to the generic heuristic AND to lookahead\n"
                  "  bottoming, on this arm alone. Report this as a bug in the pool-table route.")


def screen(spec, dry_run, only=None, seed=None, label="screen", with_floor=None):
    """Every combination vs base, one shared apparatus, one pooled batch.

    `only` restricts which arms RUN without restricting which arms the apparatus is built from: a
    held-out confirmation has to reuse the multi-arm screen's exact apparatus, or a change in the
    number would conflate selection bias with an apparatus change."""
    os.makedirs(spec.out, exist_ok=True)
    pf = gate(spec)
    tpath, ek = table_meta(spec.profile)
    if ek is not None:
        verify_table_for_deck(ek, spec.counts, "the shipped keep table", tpath)
    bkts = None if ek is None else ek["buckets"]
    tbl = None if bkts is None else {n for b in bkts for n in b}
    all_cards = {c for a in spec.arms.values() for c in a}
    uncovered = sorted(all_cards - tbl) if tbl is not None else []
    # Composition coverage, the second cliff (see fallback_rate). Only meaningful once every card is
    # bucketed -- an unbucketed card already drops the table below.
    fb = ({t: fallback_rate(bkts, spec.counts, c) for t, c in spec.arms.items()}
          if tbl is not None and not uncovered else {})
    worst = max((r for r, _ in fb.values()), default=0.0)
    # The threshold is a bias budget, not a taste: a table is worth ~0.063t (measured), so a
    # one-sided fall-through rate p costs about p*0.063t. At 1% that is 0.0006t -- a tenth of the
    # measured apparatus floor (0.0068-0.0079). Dropping the table instead is NOT free: it costs
    # ~0.063t of play quality on BOTH arms and ~22x per-game wall, so trading it away for a 0.008%
    # fall-through would be absurd. "max_fallback" overrides; 0 forces the old all-or-nothing rule.
    max_fb = spec.raw.get("max_fallback", 0.01)
    use_table = tbl is not None and not uncovered and worst <= max_fb

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
    print("\napparatus:")
    print(f"  play profile   {os.path.relpath(spec.profile, ROOT) if spec.profile else 'NONE (deliberate)'}")
    print(f"  play settings  d{spec.depth} / {spec.budget}ms   <- {spec.play_source}")
    print(f"  value model    {os.path.relpath(spec.value_profile, ROOT) if spec.value_profile else 'none'}"
          + ("  (ladder mode: accelerates warm-up passes, never decides)" if spec.value_profile else ""))
    # The apparatus is never silently downgraded. If the shipped table cannot answer every arm, the
    # pool gets its OWN table over the union deck rather than every arm losing the table -- dropping
    # it costs ~0.063t of play quality on both arms, ~22x per-game wall, AND regresses bottoming to
    # the clairvoyance-adjacent lookahead the tables exist to replace (every shipped sidecar has
    # bottoming_enabled). "pool_table": false restores the old symmetric-drop behaviour.
    gen_counts, table_src = spec.counts, None
    if tbl is None:
        print("  keep table     none found -> heuristic mulligan on every arm (symmetric)")
    elif (uncovered or not use_table) and spec.raw.get("pool_table") is not False:
        why = (f"the shipped table does not bucket {', '.join(uncovered)}" if uncovered else
               f"{pct(worst)} of hands would hit a composition it never enumerated "
               f"(limit {max_fb*100:.2f}%)")
        table_src = pool_table(spec, why, dry=dry_run)
        _, pool_ek = table_meta(table_src)
        gen_counts = union_counts(spec)
        if pool_ek is not None:
            bkts = pool_ek["buckets"]
            verify_coverage(bkts, gen_counts, spec, "the pool table")
            verify_bottoming(table_src, "the pool table")
            verify_table_for_deck(pool_ek, gen_counts, "the pool table", table_src)
            print(f"                 {describe_table(table_src, pool_ek)}")
            print("                 verified: every arm fully covered (no heuristic keep, no lookahead"
                  " bottoming)")
        elif not dry_run:
            raise SystemExit(f"the pool table was not produced: {table_src}")
        # The union spans EVERY arm of this spec, so the apparatus is a property of the whole spec
        # rather than of any one comparison. Worth saying out loud: adding or removing a combination
        # regenerates it (the fingerprint is the union + R), which silently changes the apparatus the
        # other arms were measured under -- so numbers do not carry across spec edits.
        print(f"                 NOTE the union spans all {len(spec.arms)} arms of this spec; adding or"
              " removing one\n                 rebuilds it, so these numbers do not carry across spec"
              " edits")
        use_table = True
    elif uncovered:
        print(f"  keep table     DROPPED from every arm -- not bucketed: {', '.join(uncovered)}")
        print("                 (\"pool_table\": false -- symmetric, but both arms lose the table AND")
        print("                  its bottoming. The PLAY profile above stays attached.)")
    elif not use_table:
        print(f"  keep table     DROPPED from every arm -- {pct(worst)} composition fall-through"
              f" (limit {max_fb*100:.2f}%), \"pool_table\": false")
        for t, (r, over) in sorted(fb.items(), key=lambda kv: -kv[1][0]):
            if over:
                print(f"                   {t:20s} {pct(r):>9s}  " + "; ".join(over))
    else:
        print("  keep table     shared, on every arm (all cards covered)")
        print(f"                 {describe_table(tpath, ek)}")
        if worst > 0:
            print(f"                 composition fall-through {pct(worst)} worst-arm "
                  f"(<= {max_fb*100:.2f}% limit, ~{worst*0.063:.5f}t of one-sided bias)")

    # An introduced card the profile cannot score is the one asymmetry left, and it points the wrong
    # way by construction -- against the arm that plays the new card. Fix it before running, never
    # after: the screen's number would look perfectly ordinary.
    profile = spec.profile
    if pf["unscored"]:
        if spec.raw.get("pool_profile") is False:
            print(f"  card scores    MISSING for {', '.join(pf['unscored'])} -- \"pool_profile\": false, so"
                  " the\n                 screen runs BIASED AGAINST the arms holding them")
        else:
            print(f"  card scores    absent for {', '.join(pf['unscored'])} -> pooling")
            profile = pool_profile(spec, pf["unscored"])
    elif spec.introduced:
        print("  card scores    the profile already scores every introduced card")

    # Pair whatever profile we ended up with against whatever table we ended up with. Needed whenever
    # the profile is no longer the deck's own file: the sidecar is presence-gated off the PROFILE's
    # directory+stem, so a pooled-card_scores profile sitting in a scratch dir would silently take
    # the table with it -- the same class of loss as the table-drop bug, one level down.
    if use_table and (table_src or profile is not spec.profile):
        profile = apparatus_dir(spec.out, "pool", spec.name, profile, table_src or shipped_table(spec.profile))

    # What the two blocks of a --confirm must agree on. Comparing a screen against a held-out block
    # is only meaningful if the APPARATUS was identical: same arms (the pool table is built from all
    # of them), same profile CONTENT, same table bytes, same play settings. Editing the spec between
    # the two runs would otherwise show up as "shrinkage" -- selection bias and an apparatus change
    # are indistinguishable in the number, so the driver records enough to tell them apart.
    def stamp(path):
        if not path or not os.path.exists(path):
            return None
        st_ = os.stat(path)
        return {"path": os.path.relpath(path, ROOT), "size": st_.st_size,
                "sha1": hashlib.sha1(open(path, "rb").read()).hexdigest()[:12]
                        if st_.st_size < 4 << 20 else None}

    run_arms = [t for t in spec.arms if only is None or t in only]
    jobs, decks = [], {}
    for tag in run_arms:
        dp, np_ = spec.write_arm(tag, os.path.join(spec.out, tag))
        decks[tag] = (dp, np_)
        jobs.append(spec.job(tag, dp, np_, profile, seed=seed))
    # --with-floor: the bracket's SHARED cells are the screen's own arms, so run the bracket's own
    # cells in the SAME batch instead of re-running base and the variant in a second invocation. That
    # is not an approximation -- a separate --floor produced per-job digests identical to the screen's
    # for exactly these cells -- it is the same games, run once.
    ftags = [t.strip() for t in (with_floor or "").split(",") if t.strip()]
    bad = [t for t in ftags if t not in run_arms or t == "base"]
    if bad:
        raise SystemExit(f"--with-floor wants arms this screen runs: {', '.join(bad)} is not one")
    fmeta = None
    if ftags:
        R = int(spec.raw.get("floor_R", 10))
        print(f"\n  bracketing {', '.join(ftags)} in the same batch:")
        route, own_tbl_f, own_app, per_arm, base_key = bracket_own_tables(spec, ftags, decks, R,
                                                                          profile, dry_run)
        bracket_tbl = {("own_" + k): v for k, v in own_tbl_f.items()}
        # A bracket cell whose table IS the screen's own table is the screen's ARM -- most often the
        # base deck's own cell when the edits drop a card and the brackets are reweighted, because
        # then "the base deck's own table" is the shipped one. Point at the arm instead of running an
        # identical 20,000-game cell under a second name.
        screen_tbl = os.path.realpath(table_src or tpath) if use_table and (table_src or tpath) else None
        cell_name, seen = {}, set()
        for t in ftags:
            for d, key in ((t, "own_" + t), ("base", base_key[t])):
                same = screen_tbl and os.path.realpath(bracket_tbl[key]) == screen_tbl
                cell_name[(d, key)] = d if same else f"{d}__{key}"
                if not same and (d, key) not in seen:
                    seen.add((d, key))
                    jobs.append(spec.job(f"{d}__{key}", decks[d][0], decks[d][1], own_app[key],
                                         seed=seed))
        fmeta = (ftags, route, per_arm, base_key, R, cell_name)
    json.dump({"arms": spec.arms, "games": spec.games, "depth": spec.depth,
               "budget_ms": spec.budget, "max_turns": spec.maxturn, "use_table": use_table,
               # What actually decides play is the BINARY; the commit beside it is
               # metadata (two commits touching only this driver produce identical games,
               # and --confirm refused a comparison across exactly that pair).
               "engine_commit": head_commit(),
               "engine": stamp(os.path.join(ROOT, "build/Release/mtg")),
               "profile": stamp(profile), "table": stamp(table_src or (tpath if use_table else None)),
               "value_profile": stamp(spec.value_profile),
               "seed": spec.seed if seed is None else seed},
              open(os.path.join(spec.out, f"{label}.fingerprint.json"), "w"), indent=1)
    print(f"\n{len(jobs)} arms x {spec.games:,} games -> ONE pooled batch"
          + (f"   (seed {seed}, held out from the screen's {spec.seed})" if seed is not None else ""))
    if dry_run:
        json.dump({"jobs": jobs}, open(os.path.join(spec.out, f"{label}.manifest.json"), "w"), indent=1)
        return 0

    # Drop the TABLE without dropping the PROFILE. Passing profile=None would do both -- the arm
    # decklists sit in a scratch directory, so BatchRunner's auto-detect finds nothing and falls back
    # to DefaultProfile() in silence. MTG_EXHAUSTIVE_PROFILE=none suppresses exactly the sidecar
    # (AttachExhaustiveSidecar), process-globally, which is what "symmetric" means here.
    env = {} if use_table else {"MTG_EXHAUSTIVE_PROFILE": "none"}
    got = score(run_batch(jobs, spec.out, label, spec.threads, env), [j["name"] for j in jobs],
                spec.maxturn, expect=spec.games)
    common = sorted(set.intersection(*[set(v) for v in got.values()]))
    print(f"\n{len(common):,} paired games, d{spec.depth} budget {spec.budget}ms   (negative delta = FASTER)\n")
    cost = {k: v for k, v in job_costs(os.path.join(spec.out, f"{label}.err")).items()
            if k in run_arms}
    print(f"  {'combination':22s} {'avg':>8s} {'delta':>9s} {'se':>8s} {'t':>7s} {'ident':>7s} "
          f"{'n@3sig/0.03t':>13s} {'ms/game':>9s}")
    print(f"  {'base':22s} {st.mean([got['base'][g] for g in common]):8.4f}"
          + " " * 49 + f"{cost.get('base', float('nan')):9.1f}")
    results = {}
    for tag in run_arms:
        if tag == "base":
            continue
        d, se, n, ident = paired(got, "base", tag)
        need = 9 * (se * math.sqrt(n)) ** 2 / 0.03 ** 2
        results[tag] = {"delta": d, "se": se, "n": n, "identical_pct": ident,
                        "avg": st.mean([got[tag][g] for g in common]), "ms_per_game": cost.get(tag)}
        print(f"  {tag:22s} {st.mean([got[tag][g] for g in common]):8.4f} {d:+9.4f} {se:8.4f} "
              f"{d/se if se else float('nan'):+7.2f} {ident:6.1f}% {need:13,.0f} "
              f"{cost.get(tag, float('nan')):9.1f}")
    # ms/game is a SUM OF PER-GAME WALL TIMES, so it inflates when the box is busy -- including with
    # this run's own other arms. It is a sizing aid, not a benchmark; say so where it is printed.
    if cost and max(cost.values()) > 2 * min(cost.values()):
        hi, lo = max(cost, key=cost.get), min(cost, key=cost.get)
        print(f"\n  cost is NOT uniform across arms: {hi} is {cost[hi]/cost[lo]:.1f}x {lo} per game"
              f" ({cost[hi]:.0f} vs {cost[lo]:.0f} ms).\n  A screen's wall clock is set by its"
              " priciest arm; ms/game is wall, so it inflates under load.")

    # The apparatus's ROLLOUT half, reported beside the result rather than left to a later audit.
    # Reweighting bounds the weighting half at ~0.001t; what stayed unbounded is that the shared
    # table's cell values were fit to the BASE deck's library. Printed as the scatter d* that would be
    # needed to fake each effect, because that is directly comparable to a measured scatter.
    raw_p = raw_sidecar(spec.profile)
    if use_table and raw_p and not raw_profile_mismatch(spec.profile) \
            and spec.raw.get("misfit_bound", True) and results:
        try:
            _, ek_m = table_meta(shipped_table(spec.profile))
            rows = []
            for tag, r in results.items():
                cnt, dsz = __import__("keep_margin").deck_counts(decks[tag][0], ek_m["buckets"])
                rows.append((tag, r["delta"], misfit_delta_star(raw_p, cnt, dsz, r["delta"])))
        except Exception as e:                            # never let an audit line kill a measurement
            print(f"\n  apparatus bound unavailable ({e})")
            rows = []
        if rows:
            print(f"\n  apparatus (rollout half): the cell-value SCATTER that could fake each effect")
            for tag, d, ds in rows:
                if ds is None:
                    print(f"    {tag:20s} delta {d:+.4f}   d* > {MISFIT_GRID[-1]:.2f}t "
                          f"(off the grid -- misfit cannot reach this effect)")
                else:
                    print(f"    {tag:20s} delta {d:+.4f}   d* = {ds:.3f}t")
            print("    Compare d* against a MEASURED scatter: scripts/keep_delta.py --arm ... "
                  "(burn's\n    Skullcrack->Bolt measured 0.054t). d* far above it => the effect is"
                  " not apparatus.\n    Sign is one-way too: misfit falls on the arm alone, so it can"
                  " only make an edit look SLOWER.")
    # Ranking is what a multi-arm spec is FOR, and every arm was measured against base rather than
    # against each other -- so the interesting comparison (the top two) was the one not printed. The
    # data is already there and paired on the same game indices; only the subtraction was missing.
    others = [t for t in run_arms if t != "base"]
    if len(others) > 1:
        rank = sorted(others, key=lambda t: paired(got, "base", t)[0])
        print(f"\n  ranked, and each pair compared directly (negative = the ROW is faster):\n")
        print("  " + " " * 22 + "".join(f"{t:>12.12s}" for t in rank))
        for a in rank:
            row = "".join(f"{'--':>12s}" if a == b else f"{paired(got, b, a)[0]:+12.4f}" for b in rank)
            print(f"  {a:22s}{row}")
        top, second = rank[0], rank[1]
        d, se, _, _ = paired(got, second, top)
        print(f"\n  best two: {top} vs {second} = {d:+.4f} +-{se:.4f} "
              f"(t = {d/se if se else float('nan'):+.2f})")
        # Selecting the max over N arms on ONE seed block is optimistic even when every individual
        # delta is honest -- the winner is the arm whose noise pointed the right way. `--confirm`
        # re-runs it on disjoint seeds, which is the same train/held-out discipline
        # .claude/skills/heuristic-optimization.md applies to a swept heuristic.
        print(f"  the winner of a {len(others)}-combination screen is selection-biased: confirm it on"
              f" disjoint"
              f" seeds with\n    python3 scripts/deck_compare.py <spec> --confirm {top}")
    # Results have been stdout-only, so nothing accumulated and nothing could be re-read: the
    # "numbers do not carry across spec edits" hazard stayed a warning instead of a check. This is the
    # same object the fingerprint describes, with the measurements attached.
    json.dump({"spec": os.path.relpath(spec.raw_path, ROOT), "label": label,
               "engine_commit": head_commit(), "seed": spec.seed if seed is None else seed,
               "games": spec.games, "n_paired": len(common), "base_avg":
                   st.mean([got["base"][g] for g in common]), "results": results},
              open(os.path.join(spec.out, f"{label}.results.json"), "w"), indent=1)
    if fmeta:
        ftags, route, per_arm, base_key, R, cell_name = fmeta
        for tag in ftags:
            rw = route[tag] == "reweight"
            own_R = (table_meta(spec.profile)[1] or {}).get("effective_R", "?") if rw else R
            report_bracket(spec, got, tag,
                           {"bs": "base", "vs": tag,
                            "bo": cell_name[("base", base_key[tag])],
                            "vo": cell_name[(tag, "own_" + tag)]},
                           R, rw, own_R, per_arm[tag], table_src is not None)
    if label == "screen":
        print("\nNOTE: this is a SCREEN. Every arm shares one apparatus, so the measured delta carries an")
        print("apparatus bias floor. `--floor <tag>` MEASURES that floor for one combination instead of")
        print("assuming it; do that for any result whose margin over the floor is not several-fold.")
    return 0


def apparatus_dir(out, name, stem, profile_src, table_src):
    """A directory the engine will resolve ONE chosen keep table out of.

    The sidecar is presence-gated off the PROFILE path's directory+stem
    (`AttachExhaustiveSidecar`, MulliganProfileIO.h), so pairing an arbitrary table with the deck's
    real play profile is just a directory holding both under the deck's stem. That keeps the play
    profile attached -- which is the whole point: a run without it plays a deck we do not ship."""
    d = os.path.join(out, "app_" + name)
    os.makedirs(d, exist_ok=True)
    prof = os.path.join(d, stem + ".profile.json")
    subprocess.check_call(["cp", "-f", profile_src, prof])
    # Carry the target's parsed-table cache in with it. The engine keys the cache on the LINK path
    # (`<sidecar>.bincache`) but fingerprints it by the SOURCE's (size, mtime), which follows the
    # symlink -- so a cache built beside the real table is valid here, and without this link every
    # apparatus directory re-parses the sidecar from scratch (14-68 s on the big decks, per run).
    # Clear BOTH names the engine would resolve, not just the one we are about to write.
    # AttachExhaustiveSidecar (MulliganProfileIO.h) tries ".gz" FIRST and falls back to the plain
    # .json, so a directory reused across runs -- app_ship/ is reused by every --floor on a deck --
    # keeps whichever it was given first and silently outranks the new link. It cost a real
    # measurement: a --floor whose shared apparatus was a pool table (plain .json) actually ran under
    # a `.gz` shipped table left behind by an earlier run, and reported that table's numbers under the
    # pool table's label, with the coverage print (computed from the INTENDED table) saying "full".
    # The `.bincache` beside each is deliberately LEFT: it is keyed by the source's (size, mtime),
    # which follows the symlink, so it invalidates itself -- and deleting it would charge every run a
    # full re-parse of the sidecar (14-68 s on the big ones).
    for e in (".keepmodel.exhaustive.profile.json.gz", ".keepmodel.exhaustive.profile.json"):
        p = os.path.join(d, stem + e)
        if os.path.lexists(p):
            os.remove(p)
        # Drop a cache LINK we planted for the old target (below) -- left behind, the engine would
        # find its fingerprint stale and rebuild THROUGH the symlink, overwriting the real table's
        # cache in decks/ with one for a different table. A real cache file here is left alone.
        if os.path.islink(p + ".bincache"):
            os.remove(p + ".bincache")
    ext = ".keepmodel.exhaustive.profile.json" + (".gz" if table_src.endswith(".gz") else "")
    link = os.path.join(d, stem + ext)
    os.symlink(os.path.abspath(table_src), link)
    src_cache = os.path.abspath(table_src) + ".bincache"
    if os.path.exists(src_cache) and not os.path.lexists(link + ".bincache"):
        os.symlink(src_cache, link + ".bincache")
    return prof


def raw_sidecar(profile_path):
    """The committed RAW sidecar beside a deck's profile, or None. Every deck that ships a keep table
    ships its raw too (per-deck-folder-layout.md), gzipped."""
    stem = re.sub(r"\.profile\.json$", "", profile_path or "")
    for ext in (".keepmodel.exhaustive.raw.json.gz", ".keepmodel.exhaustive.raw.json"):
        if stem and os.path.exists(stem + ext):
            return stem + ext
    return None


# Log-spaced so ONE pass over the decision surface yields the whole curve; inverting it by binary
# search would cost a pass per probe (a minute each on Goblins' 417k cells).
MISFIT_GRID = tuple(round(0.002 * 1.3 ** i, 6) for i in range(24))


def misfit_delta_star(raw_path, counts, deck_size, effect):
    """The cell-value SCATTER at which the shared table's rollout-half misfit could account for an
    effect of this size -- i.e. how wrong the apparatus would have to be to have invented the result.

    Reweighting reproduces the table's WEIGHTING half exactly, so what is left unbounded is that its
    cell values were estimated on the base deck's library. A misfit cell only costs anything if it
    flips a decision, and the cost of a flip is that decision's distance to its threshold, so the bias
    is bounded by SUM w*|margin| over decisions within `d` of flipping (scripts/keep_margin.py).
    Inverting that bound against the measured effect is the form a reader can act on: compare `d*` to
    a measured scatter (scripts/keep_delta.py; burn's Skullcrack->Bolt came out at 0.054t)."""
    import keep_margin                                   # deferred: a pass costs ~1s..1min by deck
    curve = keep_margin.bound_for(raw_path, counts, deck_size, deltas=MISFIT_GRID)
    prev_d = prev_b = 0.0
    for d in sorted(curve):
        b = curve[d]
        if b >= abs(effect):
            if b == prev_b:
                return d
            return prev_d + (d - prev_d) * (abs(effect) - prev_b) / (b - prev_b)
        prev_d, prev_b = d, b
    return None                                          # bound never reaches the effect on this grid


def raw_profile_mismatch(profile_path):
    """'' if the deck's committed raw sidecar actually rebuilds its committed keep table, else why not.

    A raw generated at a different max_mull (or missing the hand-size tables the profile's deeper
    mulligans read) builds a DIFFERENT policy. `scripts/keep_margin.py` carries the same check."""
    raw_p, tbl_p = raw_sidecar(profile_path), shipped_table(profile_path)
    if not (raw_p and tbl_p):
        return ""
    try:
        op = gzip.open if raw_p.endswith(".gz") else open
        raw = json.load(op(raw_p, "rt"))
        _, ek = table_meta(tbl_p)
    except Exception as e:                                   # unreadable -> let the caller's own path report
        return f"unreadable ({e})"
    pm, rm = ek.get("max_mull"), raw.get("meta", {}).get("max_mull")
    have = {s["H"] for s in raw.get("sizes", [])}
    need = {7 - m for m in range(int(pm or 0) + 1)}
    if rm != pm:
        return f"max_mull raw={rm} profile={pm}"
    if need - have:
        return f"raw lacks hand sizes {sorted(need - have)}"
    if raw.get("buckets") != ek.get("buckets"):
        return "bucketing differs"
    return ""


def reweight_ok(spec, tag):
    """Can an existing base-deck table be retargeted to this combination with ZERO rollouts?

    Only if every hand the arm can draw is a cell the source table already holds. `BuildPolicyFromTables`
    takes `count` separately from the rollout values and recomputes `HandWeights` from it, so a
    different count vector is just a re-weighting -- but a bucket RAISED above its base cap makes
    compositions reachable that were never enumerated, and those cells do not exist to be reweighted.
    That is exactly the condition `fallback_rate` already measures, so reuse it rather than re-derive.

    Introduced cards are out: a card the base table never bucketed has no cells at all."""
    if set(spec.arms[tag]) - set(spec.counts):
        return False, "it introduces a card the source table never bucketed"
    bk = table_buckets(spec.profile)
    if not bk:
        return False, "the deck ships no keep table to retarget"
    # The gate reads the PROFILE's buckets but the reweight rebuilds from the RAW, and nothing
    # required the two to be the same generation. Anti-Lifegain ships a max_mull=3 raw (sizes 7-4
    # only) beside a max_mull=6 profile, so a reweight there would quietly hand the arm a shallower
    # mulligan policy than the base plays -- an asymmetry in the apparatus, which is the one thing
    # screening may not have.
    bad = raw_profile_mismatch(spec.profile)
    if bad:
        return False, f"its committed raw does not build its committed profile ({bad})"
    rate, over = fallback_rate(bk, spec.counts, spec.arms[tag])
    # Gate on the SAME bias budget the screen uses, not on exactly zero. A raised bucket leaves some
    # hands with no cell to reweight, and those fall through -- but so does the screen's own shared
    # table above `max_fallback`, and at 0.0004% (a 4-of raised to a 5-of) that is ~0.0000003t of
    # one-sided bias. Refusing it forced the alternative: a ~20-minute generated bracket whose floor
    # is dominated by its own generation noise (~0.0075 measured on burn). That trade is absurd.
    if rate > spec.raw.get("max_fallback", 0.01):
        return False, f"it raises a bucket past the source grid ({pct(rate)} of hands: {'; '.join(over)})"
    return True, (f"{pct(rate)} of hands have no cell to reweight (under the "
                  f"{spec.raw.get('max_fallback', 0.01) * 100:.2f}% budget)" if rate else "")


def reweight_table(spec, tag, deck_path, src_raw):
    """Retarget `src_raw`'s cell values to this arm's counts. No rollouts -- seconds, not minutes.

    This is `MTG_KEEP_MERGE` with ONE input and a different decklist on the command line: the merge
    pools rollout values from the raw and takes the deck's counts from the argument, which is exactly
    a reweight. It prints a deck-fingerprint WARNING by design; here the mismatch is the point.

    What it does and does not bracket. The fit of a table to a deck has two parts: the hand weights
    and D_opt derived from `count` (this reproduces them exactly, at the SOURCE's R -- 60 on the
    shipped tables, versus 10 for a generated bracket) and the per-cell rollout values, which were
    estimated on the source deck's library and are NOT re-estimated here. So a reweighted bracket
    bounds the weighting half at high R and says nothing about the rollout half; a generated bracket
    bounds both but carries R=10 sampling noise larger than the bias it is measuring. They are
    complementary, and the cheap one is free."""
    d = os.path.dirname(deck_path)
    out = os.path.join(d, spec.name + ".keepmodel.exhaustive.profile.json")
    log = os.path.join(spec.out, f"reweight_{tag}.log")
    for src in (spec.profile, spec.value_profile):
        if src and os.path.exists(src):
            subprocess.check_call(["cp", "-f", src, d])
    print(f"  {tag}: reweighting {os.path.relpath(src_raw, ROOT)} onto this arm's counts "
          f"(zero rollouts; log {os.path.relpath(log, ROOT)})")
    with open(log, "w") as f:
        subprocess.check_call([os.path.join(ROOT, "build/Release/mtg-analyze"), deck_path],
                              stdout=f, stderr=subprocess.STDOUT, cwd=ROOT,
                              env={**os.environ, "MTG_KEEP_MERGE": "1",
                                   "MTG_MERGE_INPUTS": os.path.abspath(src_raw),
                                   "MTG_MERGE_OUT_PROFILE": out,
                                   "MTG_MERGE_OUT_RAW": out.replace(".profile.json", ".raw.json")})
    # Stamp the same fingerprint file `gen_table` keys reuse on, with a value it can never match.
    # Both routes write the SAME path, so without this a later generate would find a reweighted table
    # sitting under a fingerprint that says "generated at R" and reuse it.
    open(os.path.join(d, ".counts.json"), "w").write(
        json.dumps({"counts": spec.arms[tag], "R": f"reweight:{os.path.basename(src_raw)}"},
                   sort_keys=True))
    return out


def gen_table(spec, tag, deck_path, R, dry=False):
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
    # R is part of the fingerprint for the same reason the counts are: it is not a property of the
    # arm, it is a property of the ARTIFACT, and a table generated at a different R is a different
    # apparatus (~0.03-0.06t of play quality, deck-dependent -- the size of a whole screened effect).
    want = json.dumps({"counts": spec.arms[tag], "R": R}, sort_keys=True)
    if os.path.exists(out) and os.path.exists(fp) and open(fp).read() == want:
        print(f"  {tag}: reusing {os.path.relpath(out, ROOT)}")
        return out
    if dry:
        # --dry-run says "build decks + manifest, run nothing", and generating a keep table is very
        # much running something (10-40 min). It also DELETES the stale table first, so a dry run that
        # was interrupted used to leave the arm with no table at all.
        print(f"  {tag}: WOULD generate an R={R} keep table -> {os.path.relpath(out, ROOT)}")
        return out
    for stale in (out, out.replace(".profile.json", ".raw.json"),
                  os.path.join(d, spec.name + ".keepmodel.gencache.json")):
        if os.path.exists(stale):
            os.remove(stale)
    for src in (spec.profile, spec.value_profile):
        if src and os.path.exists(src):
            subprocess.check_call(["cp", "-f", src, d])
    log = os.path.join(spec.out, f"keepgen_{tag}.log")
    print(f"  {tag}: generating R={R} keep table -> {os.path.relpath(out, ROOT)}  (log {os.path.relpath(log, ROOT)})")
    with open(log, "w") as f:
        subprocess.check_call([os.path.join(ROOT, "build/Release/mtg-analyze"), deck_path,
                               "--seed", str(spec.raw.get("floor_seed", 78000001))],
                              stdout=f, stderr=subprocess.STDOUT, cwd=ROOT,
                              env={**os.environ, "MTG_KEEP_EXHAUSTIVE": "1",
                                   "MTG_KEEP_ROLLOUTS": str(R)})
    open(fp, "w").write(want)
    return out


def bracket_own_tables(spec, tags, decks, R, play_prof, dry_run):
    """The "own table" side of a bracket for each tag. -> (route, own_tbl, per_arm, base_key).

    Split out of `floor()` so the screen can build the SAME brackets and run them in the same
    batch (`--with-floor`), instead of a second invocation re-running the shared cells."""
    per_arm = {t: set(spec.arms[t]) != set(spec.counts) for t in tags}
    if any(per_arm.values()):
        which = ", ".join(t for t in tags if per_arm[t])
        print(f"  ({which}: the card sets differ, so each deck is bracketed on ITS OWN R={R}"
              f" table --\n   one table for both would leave the base deck's dropped card unbucketed)")
    # REWEIGHT vs GENERATE, per tag. A count-only edit needs no rollouts at all: the shipped raw
    # already holds a value for every cell the arm can draw, and retargeting it to the arm's counts is
    # seconds instead of 10-40 minutes AND lands at the shipped R (60) instead of floor_R (10), which
    # is the noise that makes a generated bracket overstate its floor. What it does not do is
    # re-estimate the cell values on the arm's library, so it brackets the weighting half only --
    # `"bracket": "generate"` forces the full, noisy one.
    mode = spec.raw.get("bracket", "reweight")
    src_raw = raw_sidecar(spec.profile)
    route, own_tbl = {}, {}
    for t in tags:
        ok, why = reweight_ok(spec, t)
        if mode == "reweight" and ok and src_raw:
            route[t] = "reweight"
            if why:
                print(f"  {t}: reweighting anyway -- {why}")
            own_tbl[t] = (reweight_table(spec, t, decks[t][0], src_raw) if not dry_run
                          else os.path.join(os.path.dirname(decks[t][0]),
                                            spec.name + ".keepmodel.exhaustive.profile.json"))
        else:
            route[t] = "generate"
            if mode == "reweight" and not ok:
                print(f"  {t}: cannot reweight -- {why}")
            own_tbl[t] = gen_table(spec, t, decks[t][0], R, dry=dry_run)
    # The base deck's own table, needed only where the card SETS differ. Under the reweight route it
    # is the SHIPPED table itself -- retargeting the base's counts onto the base's own values is the
    # identity -- so that arm costs nothing at all.
    if any(per_arm[t] and route[t] == "reweight" for t in tags):
        own_tbl["base_rw"] = shipped_table(spec.profile)
    if any(per_arm[t] and route[t] == "generate" for t in tags):
        own_tbl["base_gen"] = gen_table(spec, "base", decks["base"][0], R, dry=dry_run)
    for t, tp in own_tbl.items():
        if not dry_run:
            verify_bottoming(tp, f"the {t} bracket table")

    own_app = {"own_" + t: apparatus_dir(spec.out, "own_" + t, spec.name, play_prof, tp)
               for t, tp in own_tbl.items()}
    base_key = {t: (("own_base_rw" if route[t] == "reweight" else "own_base_gen") if per_arm[t]
                    else "own_" + t) for t in tags}
    return route, own_tbl, own_app, per_arm, base_key


def report_bracket(spec, got, tag, cell, R, rw, own_R, per_arm, pool):
    """One combination's bracket, computed from cells that have already run.

    Shared by `--floor` and the screen's `--with-floor`. The two differ only in whether the
    SHARED cells are their own jobs or the screen's own arms, and that is a naming difference:
    the pooled floor's shared cells reproduced the screen's per-job digests exactly
    (e41ba09a50c652f4 / 9c708dd6517a9bbc / 655678a650b9ee10 on burn), which is what makes it
    sound to run them once instead of twice."""
    e_ship, se_ship, n, id_ship = paired(got, cell["bs"], cell["vs"])
    e_own,  se_own,  _, id_own  = paired(got, cell["bo"], cell["vo"])
    # Difference-of-differences over the SAME game indices -- pair it too, or the two deltas'
    # shared game-to-game variance is counted twice and the bias looks far noisier than it is.
    common = sorted(set.intersection(*[set(got[c]) for c in cell.values()]))
    B = [(got[cell["vo"]][g] - got[cell["bo"]][g]) -
         (got[cell["vs"]][g] - got[cell["bs"]][g]) for g in common]
    bias, se_b = st.mean(B), st.pstdev(B) / math.sqrt(len(B))
    fl = abs(bias) + 2 * se_b

    print(f"\n=== {tag} ===   {len(common):,} paired games, d{spec.depth} budget {spec.budget}ms\n")
    print(f"  {'apparatus':28s} {'delta':>9s} {'se':>8s} {'ident':>7s}")
    shared_lbl = ("shared (pool R=%d) table" % R if pool
                  else "shared (shipped) table")
    print(f"  {shared_lbl:28s} {e_ship:+9.4f} {se_ship:8.4f} {id_ship:6.1f}%")
    own_lbl = ((f"each arm's own R={own_R}" if per_arm else f"{tag} own R={own_R}")
               + (" (reweighted)" if rw else " table"))
    print(f"  {own_lbl:28s} {e_own:+9.4f} {se_own:8.4f} {id_own:6.1f}%")
    print(f"\n  apparatus bias               {bias:+9.4f} {se_b:8.4f}   "
          f"(t = {bias/se_b if se_b else float('nan'):+.2f})")
    if pool:
        # "Each table flatters the deck it was fit to" is a shipped-table reading, and it does NOT
        # carry over to the pool route: the union REFINES every arm's partition (it holds every
        # card any arm plays), so the pool table is finer than any arm's own, not merely foreign
        # to it. All four nulls measured on burn came out NEGATIVE -- the pool table played each
        # arm slightly FASTER than its own table. No expected sign here; read the nulls below.
        print("     (the pool table REFINES every arm's partition, so neither sign is 'expected'"
              " -- read the per-arm nulls)")
    else:
        print(f"     {'(each table flatters the deck it was fit to -- the expected direction)' if bias < 0 else '(the shared table flatters the VARIANT -- unexpected; read the cells before trusting it)'}")
    print(f"  floor = |bias| + 2se          {fl:9.4f}")
    print(f"  effect / floor                {abs(e_ship)/fl if fl else float('inf'):9.2f}x")
    # The two WITHIN-deck nulls the bias is the difference of. Reporting only the variant's (what
    # this used to do) hides where a bias comes from: a bracket is only worrying when the two
    # nulls DIFFER, and two large equal nulls are a level difference that cancels. Against a
    # shipped high-R table expect both POSITIVE and large (an R=<floor_R> table plays ~0.032-0.06t
    # weaker, deck-dependent) -- which is why the bracket OVERSTATES the floor.
    print("\n  per-arm nulls, own table vs the shared one (positive = the OWN table plays weaker);"
          f"\n  the bias above is exactly their difference, null({tag}) - null(base):")
    for lbl, a, b in (("base", cell["bs"], cell["bo"]), (tag, cell["vs"], cell["vo"])):
        c, se_c, _, id_c = paired(got, a, b)
        # A null of exactly 0 / 100% identical is not a suspicious result, it is the same cell twice:
        # under reweighting the base deck's "own" table IS the shipped one (retargeting the base's
        # counts onto the base's own values is the identity), so the driver runs it once. The bias
        # then reduces to the variant's null alone, which is the honest reading.
        same = a == b
        print(f"    {lbl:24s} {c:+9.5f} {se_c:8.5f} {id_c:6.1f}%"
              + ("   <- the same cell: this deck's own table IS the shared one" if same else ""))
    if rw:
        print(f"\n  NOTE this bracket was REWEIGHTED, not generated: the shipped raw's cell values"
              f"\n  retargeted to {tag}'s counts (zero rollouts, R={own_R}). It reproduces the hand"
              f"\n  weights and D_opt a table fit to {tag} would have, at high R -- but the cell"
              "\n  values were estimated on the BASE deck's library and are not re-estimated, so it"
              "\n  bounds the WEIGHTING half of the fit only. `\"bracket\": \"generate\"` runs the"
              f"\n  full one at R={R}, which bounds both halves and carries R={R} noise larger than"
              "\n  the bias it measures.")
    if pool:
        print(f"  NOTE the shared arm is itself an R={R} POOL table here, not the shipped R=60 one,"
              f" so\n  the usual 'the bracket plays ~0.032t weaker' asymmetry does NOT apply --"
              f" both arms\n  are low-R, and what is left is the fit difference between union and"
              " combination.")
    if fl and abs(e_ship) / fl < 3:
        print("\n  VERDICT: the effect does NOT clear its floor by 3x. Treat it as UNRESOLVED, not")
        print("  refuted: either the edit is genuinely small, or the shared apparatus is doing the")
        print("  work. Only a high-R regeneration settles it -- at low R the table's own regret is")
        print("  larger than the bias, so re-running this bracket bigger will not help.")
    else:
        print("\n  VERDICT: the effect clears its floor by 3x+ -- the shared apparatus is not")
        print("  producing it. The floor is a screening guard, not a confidence interval.")


def floor(spec, tags, dry_run):
    """Bracket one or more combinations' apparatus bias -- ALL of them in ONE pooled batch.

    Per tag the bracket is 4 cells (2 decks x 2 tables), but the cells OVERLAP: `base` under the
    shared apparatus is the same job for every tag, and so is the base deck's own table whenever the
    edits drop a card. So T combinations cost 2T+2 cells rather than 4T, in one queue rather than T
    queues -- which is the difference CLAUDE.md's pooling rule is about: T separate invocations strand
    cores on each one's load-imbalance tail, and the table generations between them are serial."""
    tags = [t.strip() for t in (tags.split(",") if isinstance(tags, str) else tags) if t.strip()]
    bad = [t for t in tags if t not in spec.arms or t == "base"]
    if bad or not tags:
        raise SystemExit(f"--floor wants one or more of: "
                         f"{', '.join(t for t in spec.arms if t != 'base')}"
                         + (f"  (unknown: {', '.join(bad)})" if bad else ""))
    pf = gate(spec)
    shipped = spec.profile
    ship_path, ship_ek = table_meta(shipped)
    bkts = None if ship_ek is None else ship_ek["buckets"]
    if bkts is not None:
        verify_table_for_deck(ship_ek, spec.counts, "the shipped keep table", ship_path)
    if bkts is None:
        raise SystemExit("no shared keep table -> the apparatus is deck-INDEPENDENT, so there is no\n"
                         "table bias to bracket (measured -0.0003 on a -0.20 effect). Nothing to do.")

    os.makedirs(spec.out, exist_ok=True)
    # Whatever the screen would PLAY with, the bracket has to play with too, or it brackets a
    # different measurement. An introduced card with no card_scores entry is scored as an empty slot
    # in the one arm that holds it (ComputeHandScore skips unknown names), so the screen pools the
    # scores -- and a bracket that skipped that step would fold the card_scores asymmetry into what it
    # reports as apparatus bias.
    play_prof = spec.profile
    if pf["unscored"] and spec.raw.get("pool_profile") is not False:
        print(f"  card scores    absent for {', '.join(pf['unscored'])} -> pooling (as the screen does)")
        play_prof = pool_profile(spec, pf["unscored"])
    decks = {t: spec.write_arm(t, os.path.join(spec.out, t)) for t in ["base"] + tags}
    R = int(spec.raw.get("floor_R", 10))

    # Bracket what the SCREEN actually runs -- which means the apparatus is chosen from EVERY arm of
    # the spec, not just the bracketed ones. That used to be "the shipped table, or nothing", and the
    # nothing case refused, leaving the edit kind with the biggest apparatus question (an introduced
    # card) with no bracket at all. Since the screen now falls back to a POOL table rather than to no
    # table, the shared apparatus always exists and is always bracketable.
    tbl = {n for b in bkts for n in b}
    uncovered = sorted({c for a in spec.arms.values() for c in a} - tbl)
    worst = 0.0 if uncovered else max(
        fallback_rate(bkts, spec.counts, c)[0] for c in spec.arms.values())
    print(f"floor bracket for {', '.join(repr(t) for t in tags)}")
    if uncovered or worst > spec.raw.get("max_fallback", 0.01):
        why = (f"the shipped table does not bucket {', '.join(uncovered)}" if uncovered
               else f"{pct(worst)} composition fall-through")
        print(f"  the screen's apparatus is a POOL table ({why})\n")
        shared, shared_gen = pool_table(spec, why, dry=dry_run), union_counts(spec)
        if not dry_run:
            verify_coverage(table_buckets(shared), shared_gen, spec, "the pool table")
            verify_bottoming(shared, "the pool table")
    else:
        shared, shared_gen = shipped_table(shipped), spec.counts
        print(f"  shared table   {describe_table(ship_path, ship_ek)}")
        # A shipped table is an artifact of the commit it was generated on; the bracket tables are
        # generated NOW. Every other skill in this repo has a Rule 0 about that (artifacts are
        # engine-state fingerprints), and the bracket is the one place here that MIXES two engine
        # states inside one difference-of-differences. It is on top of the R difference, not instead
        # of it, and it is a caveat rather than a defect -- there is no cheaper reference.
        head = head_commit()
        if ship_ek.get("commit") and head and not ship_ek["commit"].startswith(head):
            print(f"                 NOTE this bracket mixes engine states: the shared arm's table was"
                  f" built at\n                 {ship_ek['commit']}, the bracket's at {head}. Read the"
                  " nulls with that in mind.")

    # Which table each deck's "own" cell runs under. ONE table across both own-cells keeps that delta
    # internally consistent, and it is the right choice whenever the two arms hold the same CARDS --
    # only the counts differ, so the variant's table buckets everything the base plays. It becomes the
    # wrong choice the moment the edit DROPS a card: the variant's table then has no bucket at all for
    # a card the base deck still plays, and the base__own cell silently loses the table on every hand
    # holding one -- 40.0% of hands for a 4-of swap (burn's 4 Skullcrack -> 4 Lava Spike), on ONE arm.
    # That is an order of magnitude more than the fit bias this bracket exists to measure, and it
    # inflates the floor in a way no amount of games can average out.
    # So when the card SETS differ, each deck's own cell gets its own table. The bracket then has no
    # fall-through in any cell, and the difference-of-differences decomposes exactly into two
    # WITHIN-deck nulls (bias = [var@own - var@shared] - [base@own - base@shared]) -- so the level
    # difference between two distinct tables cancels rather than leaking into a delta.
    route, own_tbl, own_app, per_arm, base_key = bracket_own_tables(spec, tags, decks, R, play_prof,
                                                                     dry_run)
    # The CELLS are deduped across tags: `base` under the shared table is one job however many
    # combinations are bracketed.
    apps = {"ship": apparatus_dir(spec.out, "ship", spec.name, play_prof, shared), **own_app}
    # Two apparatus keys can resolve to the SAME table -- most often when the edits drop a card and
    # the brackets are reweighted, because then the base deck's "own" table IS the shipped one. Left
    # alone that runs an identical 20,000-game cell twice under two names. Canonicalise by the table
    # the key actually points at.
    tables = {"ship": shared, **{k: own_tbl[k[4:]] for k in own_app}}
    first, canon = {}, {}
    for k in ["ship"] + sorted(own_app):
        canon[k] = first.setdefault(os.path.realpath(tables[k]), k)
    base_key = {t: canon[base_key[t]] for t in tags}

    # The counts each table was GENERATED for -- what `fallback_rate`'s caps must come from. A base
    # table (shipped, or the base deck's own) is spec.counts; a variant's own table is that arm's.
    gen_of = {"ship": shared_gen, "own_base_rw": spec.counts, "own_base_gen": spec.counts}
    gen_of.update({"own_" + t: spec.arms[t] for t in tags})
    cells = {("base", "ship"): (shared, shared_gen)}
    for t in tags:
        cells[(t, "ship")] = (shared, shared_gen)
        cells[(t, canon["own_" + t])] = (tables[canon["own_" + t]], gen_of[canon["own_" + t]])
        cells[("base", base_key[t])] = (tables[base_key[t]], gen_of[base_key[t]])

    # How much of each cell actually GETS a table. Where the bracket runs a deck under a table fit to
    # the other, the coverage cliff is inside the measurement by design -- but it was never quantified
    # per cell. An unbucketed card (a bucket the other deck does not have at all) costs far more than
    # an overflowing one: both land as present=false, silently.
    print("\n  coverage of each cell (fraction of hands the table will NOT answer):")
    # Each table's caps come from the counts it was GENERATED for -- the base deck for the shipped
    # table, the combination for its own. Passing the arm's own counts here would silently report
    # full coverage for exactly the asymmetry the bracket exists to expose.
    for (d, a), (tp, gen) in sorted(cells.items()):
        if not os.path.exists(tp):
            print(f"    {d + ' on ' + a:32s} (table not generated yet -- dry run)")
            continue
        bk = table_buckets(tp) or []
        names = {n for b in bk for n in b}
        miss = sorted(set(spec.arms[d]) - names)
        rate, over = fallback_rate(bk, gen, spec.arms[d]) if bk and not miss else (0.0, [])
        note = (f"UNBUCKETED {', '.join(miss)} -> every hand holding one" if miss
                else (f"{pct(rate)} (composition){'  ' + '; '.join(over) if over else ''}"
                      if rate else "full"))
        print(f"    {d + ' on ' + a:32s} {note}")
    jobs = [spec.job(f"{d}__{a}", decks[d][0], decks[d][1], apps[a]) for d, a in sorted(cells)]
    print(f"\n{len(jobs)} cells x {spec.games:,} games -> ONE pooled batch"
          + (f"  ({4*len(tags)} before deduplication)" if len(tags) > 1 else ""))
    if dry_run:
        json.dump({"jobs": jobs}, open(os.path.join(spec.out, "floor.manifest.json"), "w"), indent=1)
        return 0

    got = score(run_batch(jobs, spec.out, "floor", spec.threads), [j["name"] for j in jobs],
                spec.maxturn, expect=spec.games)
    for tag in tags:
        rw = route[tag] == "reweight"
        own_R = (table_meta(spec.profile)[1] or {}).get("effective_R", "?") if rw else R
        report_bracket(spec, got, tag,
                       {"bs": "base__ship", "vs": f"{tag}__ship",
                        "bo": f"base__{base_key[tag]}", "vo": f"{tag}__{canon['own_' + tag]}"},
                       R, rw, own_R, per_arm[tag], shared_gen is not spec.counts)
    return 0



def confirm(spec, tag, dry_run):
    """Re-measure ONE combination against base on a DISJOINT block of games, same apparatus.

    A screen reports the max of N noisy deltas and calls it the winner, which is optimistic even when
    every individual estimate is honest -- the arm that wins is partly the arm whose noise pointed the
    right way, and the more arms the worse it gets. This is the held-out half of the same train/test
    discipline `.claude/skills/heuristic-optimization.md` applies to a swept heuristic: pick on one
    seed block, confirm on another, report both.

    The apparatus is deliberately NOT rebuilt for the two arms that run: `only` restricts the jobs
    while the pool table and pooled card scores still come from every arm of the spec, so the two
    numbers differ in their GAMES and nothing else. Rebuilding it would make a shrunken effect
    ambiguous between selection bias and an apparatus change."""
    if tag not in spec.arms or tag == "base":
        raise SystemExit(f"--confirm wants one of: {', '.join(t for t in spec.arms if t != 'base')}")
    seed = int(spec.raw.get("confirm_seed", spec.seed + 500_000))
    if seed == spec.seed:
        raise SystemExit("confirm_seed equals the screen's seed -- that is the same games, not a "
                         "held-out block")
    print(f"held-out confirmation of '{tag}': same apparatus, seed {seed} instead of {spec.seed}\n")
    rc = screen(spec, dry_run, only=["base", tag], seed=seed, label="confirm")
    if dry_run:
        return rc

    fps = [os.path.join(spec.out, f"{k}.fingerprint.json") for k in ("screen", "confirm")]
    if all(os.path.exists(f) for f in fps):
        fa, fb = (json.load(open(f)) for f in fps)
        # The git commit is METADATA, not the fingerprint: two commits touching only this
        # driver or the docs produce identical play, and refusing across them makes
        # --confirm unusable during development (it fired on exactly that). What decides
        # play is the BINARY, which the fingerprint stamps separately.
        informational = {"seed", "engine_commit"}
        diff = sorted(k for k in set(fa) | set(fb)
                      if k not in informational and fa.get(k) != fb.get(k))
        def show(k):
            if k != "arms":
                return f"    screen  {fa.get(k)}\n    confirm {fb.get(k)}\n"
            out = ""
            for t in sorted(set(fa.get(k) or {}) | set(fb.get(k) or {})):
                ca, cb = (fa.get(k) or {}).get(t, {}), (fb.get(k) or {}).get(t, {})
                if ca != cb:
                    out += f"    {t}: " + ", ".join(
                        f"{n} {ca.get(n, 0)}->{cb.get(n, 0)}" for n in sorted(set(ca) | set(cb))
                        if ca.get(n, 0) != cb.get(n, 0)) + "\n"
            return out
        if diff:
            raise SystemExit(
                "the earlier screen did not run under this apparatus -- refusing to compare.\n"
                + "".join(f"  {k}:\n" + show(k) for k in diff)
                + "  A held-out block only tests SELECTION bias if everything except the games is\n"
                  "  held fixed; otherwise a shrunken effect is ambiguous. Re-run the screen.")
    prev = os.path.join(spec.out, "screen.err")
    if not os.path.exists(prev):
        print("\n  (no earlier screen log beside this spec, so there is nothing to compare against --"
              "\n   this run stands on its own)")
        return 0
    a = score(prev, list(spec.arms), spec.maxturn)
    b = score(os.path.join(spec.out, "confirm.err"), ["base", tag], spec.maxturn)
    if not (a.get("base") and a.get(tag)):
        print("\n  (the earlier screen log does not carry this combination -- nothing to compare)")
        return 0
    d0, se0, n0, _ = paired(a, "base", tag)
    d1, se1, n1, _ = paired(b, "base", tag)
    print(f"\n  {'block':24s} {'games':>8s} {'delta':>9s} {'se':>8s}")
    print(f"  {'screen (seed %d)' % spec.seed:24s} {n0:8,} {d0:+9.4f} {se0:8.4f}")
    print(f"  {'held out (seed %d)' % seed:24s} {n1:8,} {d1:+9.4f} {se1:8.4f}")
    # Independent blocks, so the difference's se is the root of the sum -- these are NOT paired with
    # each other (different games), which is exactly the point.
    gap = d1 - d0
    se_g = math.sqrt(se0 ** 2 + se1 ** 2)
    print(f"  {'shrinkage':24s} {'':8s} {gap:+9.4f} {se_g:8.4f}  "
          f"(t = {gap/se_g if se_g else float('nan'):+.2f})")
    if abs(gap) > 2 * se_g:
        print("\n  The held-out block does NOT reproduce the screen's effect. Report the held-out"
              "\n  number, not the screen's -- the screen's is the one that was selected on.")
    else:
        print("\n  The held-out block reproduces the screen's effect within noise. Pool them for the"
              f"\n  point estimate: {(d0*n0 + d1*n1)/(n0+n1):+.4f} over {n0+n1:,} games.")
    return 0


class DeckLock:
    """Refuse to run two invocations against one deck's scratch directory at the same time.

    Every arm's decklist and its `numbering.json` are rewritten per run and read by the ENGINE when
    the job runs, minutes to hours later. Two concurrent runs on one deck therefore interleave: the
    second run's numbering lands under the first run's in-flight jobs. Across two different decks
    that already cost a 20-minute generation, and it only failed loudly because burn and slivers have
    disjoint card names -- two specs over the SAME deck mis-number in silence, which is the worst
    failure this tool can have. Per-deck namespacing fixed the cross-deck case; this fixes the rest.

    A stale lock from a killed run is taken over rather than honoured (the PID check), so a crash
    never wedges the directory."""

    def __init__(self, out):
        self.path = os.path.join(out, ".lock")

    def __enter__(self):
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        if os.path.exists(self.path):
            try:
                pid = int(open(self.path).read().split()[0])
                os.kill(pid, 0)
            except (ValueError, IndexError, ProcessLookupError, PermissionError):
                pid = None
            if pid:
                raise SystemExit(
                    f"another deck_compare run holds {os.path.relpath(self.path, ROOT)} (pid {pid}).\n"
                    "  Both would rewrite this deck's arm decklists and numbering.json while the\n"
                    "  other's jobs are still reading them -- silently mis-numbering one of the runs.\n"
                    "  Wait for it, or run the other deck.")
        open(self.path, "w").write(f"{os.getpid()} {' '.join(sys.argv[1:])}\n")
        return self

    def __exit__(self, *exc):
        try:
            if int(open(self.path).read().split()[0]) == os.getpid():
                os.remove(self.path)
        except Exception:
            pass


def head_commit():
    try:
        return subprocess.check_output(["git", "rev-parse", "--short", "HEAD"],
                                       cwd=ROOT, text=True).strip()
    except Exception:
        return None


def describe_table(path, ek):
    """One line saying which table this is and what engine state it encodes.

    Every sidecar records `commit` and `effective_R` and the driver surfaced neither, so a screen
    could not tell an R=60 shipped table from an R=1 leftover, nor notice that the apparatus was
    generated several hundred commits ago. Within one screen that is symmetric and harmless; a
    `--floor` MIXES a shipped table with one generated at HEAD, and there it is a real confound."""
    R, c = ek.get("effective_R"), ek.get("commit")
    head = head_commit()
    drift = ""
    if c and head and not (c.startswith(head) or head.startswith(c.split("+")[0])):
        drift = f" -- engine is now at {head}"
    return (f"{os.path.relpath(path, ROOT)}\n                 K={len(ek['buckets'])}, "
            f"{len(ek.get('entries') or []):,} cells, R={R or '?'}, "
            f"commit={c or 'unrecorded'}{drift}")


def shipped_table(profile_path):
    """Path of the committed sidecar the engine would auto-attach for this profile."""
    stem = re.sub(r"\.profile\.json$", "", profile_path)
    for ext in (".keepmodel.exhaustive.profile.json.gz", ".keepmodel.exhaustive.profile.json"):
        if os.path.exists(stem + ext):
            return stem + ext
    raise SystemExit(f"no committed keep table beside {profile_path}")


def report_preflight(spec):
    """`--preflight`: just the checks, exit 1 if any needs a human or an AI. Runs no games and reads
    no binary, so it is the right first call when a spec names a card for the first time."""
    pf = preflight(spec)
    print(f"base:       {spec.raw['base']}")
    print(f"profile:    {os.path.relpath(spec.profile, ROOT) if spec.profile else 'NONE'}")
    print(f"introduced: {', '.join(spec.introduced) if spec.introduced else '(none -- count changes only)'}")
    if pf["missing"]:
        print(f"\nNOT IMPLEMENTED: {', '.join(pf['missing'])}")
    for g in pf["gaps"]:
        print(f"\nGAPS in {g['card']}:\n  " + "\n  ".join(g["gaps"]))
    if pf["unscored"]:
        print(f"\nNO card_scores ENTRY: {', '.join(pf['unscored'])}"
              "\n  (the screen pools one automatically; it costs one analyzer run over the union deck)")
    bkts = table_buckets(spec.profile)
    if bkts is not None and not (set(spec.introduced) - {n for b in bkts for n in b}):
        for tag, counts in spec.arms.items():
            rate, over = fallback_rate(bkts, spec.counts, counts)
            if over:
                print(f"\nCOMPOSITION FALL-THROUGH in '{tag}': {pct(rate)} of hands\n  raised past "
                      "what the table enumerated: " + "; ".join(over) +
                      "\n  (silent heuristic fallback on this arm only; the screen drops the table from"
                      "\n   EVERY arm above 1%, which is symmetric but costs ~22x per game)")
    if not (pf["missing"] or pf["gaps"]):
        print("\nOK -- nothing here needs a human. Run the screen.")
        return 0
    print()
    try:
        refuse_on_cards(pf)
    except SystemExit as e:
        print(e)
    return 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("spec")
    ap.add_argument("--dry-run", action="store_true", help="build decks + manifest, run nothing")
    ap.add_argument("--preflight", action="store_true",
                    help="run only the card/apparatus checks (exit 1 if one needs implementing)")
    ap.add_argument("--floor", metavar="TAG[,TAG...]",
                    help="bracket combinations' apparatus bias floor (comma-separated = one batch)")
    ap.add_argument("--with-floor", metavar="TAG[,TAG...]",
                    help="run the screen AND those combinations' bias brackets in ONE batch")
    ap.add_argument("--confirm", metavar="TAG",
                    help="re-measure ONE combination on held-out seeds (the screen's winner is "
                         "selection-biased)")
    args = ap.parse_args()
    spec = Spec(args.spec)
    if args.preflight:
        return report_preflight(spec)
    if args.with_floor and (args.floor or args.confirm):
        raise SystemExit("--with-floor is part of a screen; do not combine it with --floor/--confirm")
    if args.floor and args.confirm:
        raise SystemExit("--floor and --confirm answer different questions (apparatus bias vs "
                         "selection bias); run them separately")
    with DeckLock(spec.out):
        if args.floor:
            return floor(spec, args.floor, args.dry_run)
        if args.confirm:
            return confirm(spec, args.confirm, args.dry_run)
        return screen(spec, args.dry_run, with_floor=args.with_floor)


if __name__ == "__main__":
    sys.exit(main())
