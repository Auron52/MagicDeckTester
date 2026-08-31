#!/usr/bin/env python3
"""Rule-vs-searched zero-regret grader for a provider's cleanup-discard ranking.

The validation `analyze-deck.md` demands after authoring a bucket policy: grade the
provider's front pick against the searched labels and account for every rule-worse
decision. `scripts/analyze_deck.py --discard-analysis` reports the same statistic inside
the full stage; this is the standalone grader for a run you drove yourself (e.g. sharded
across cores, which the stage cannot do -- see below).

USAGE
    # Evidence run. The trace is raw unsynchronised std::cerr, so ONE process must be
    # single-threaded; shard by seed base to use the box, each shard owning its own file.
    for k in $(seq 0 19); do
      MTG_DISCARD_NODE=0 MTG_DISCARD_TRACE=1 ./build/Release/mtg --batch shard_$k.json \
        --threads 1 2> sh_$k.err &
    done; wait
    python3 test/tools/discard_regret.py 'logs/.../sh_*.err'

    Exit 0 iff zero regret. Quote the glob -- this expands it itself.

READ BEFORE ACTING ON THE OUTPUT
  * Regret is per DECISION. Multiply by the per-game decision rate before believing a
    number matters: Dragons' 0.0238 over 0.029 sheds/game is a 0.0007 turns/game
    perfect-oracle prize -- below the measurable floor.
  * A high `all-candidates-tie` / `multi-optimal` rate means the labels are degenerate
    and you are mostly grading single-rollout noise.
  * The trace names the rule's pick by NAME, not index. A hand holding two copies whose
    labels DIFFER is genuinely ambiguous; those are reported separately rather than
    resolved to the first copy (which is what analyze_deck.py's _RulePick does). A
    zero-regret claim must not rest on a tie-break.
  * Zero regret is not the only bar and not always reachable: the probe instruments only
    AIEngine::ChooseDiscard (CR 514.1 cleanup). A deck that discards to an activation
    cost or a trigger -- Minotaur, entirely -- produces no labels here at all. See
    docs/design/per-deck-discard-analysis-phase.md.
"""
import re
import sys
import glob

HDR = re.compile(r"^\[discard_trace turn=(\d+) depth=(\d+) seed=(\d+) handsize=(\d+) heur=(.+)\]$")
CAND = re.compile(r"^  discard (.+?) mv=(-?\d+) copies=(\d+) land=([01]) prot=([01]) -> win=(\d+)( \*)?$")


def Blocks(paths):
    """Yield one dict per [discard_trace] block across all `paths`."""
    cur = None
    for path in paths:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.rstrip("\n")
                h = HDR.match(line)
                if h:
                    if cur and cur["cands"]:
                        yield cur
                    cur = {"turn": int(h.group(1)), "seed": int(h.group(3)),
                           "handsize": int(h.group(4)), "heur": h.group(5), "cands": []}
                    continue
                c = CAND.match(line)
                if c and cur is not None:
                    cur["cands"].append({"name": c.group(1), "mv": int(c.group(2)),
                                         "copies": int(c.group(3)), "land": c.group(4) == "1",
                                         "prot": c.group(5) == "1", "win": int(c.group(6)),
                                         "opt": c.group(7) is not None})
    if cur and cur["cands"]:
        yield cur


# Candidate visible-info rules, scored on the SAME labels so an authored policy can be
# compared against the alternatives without a second evidence run. `None` -> the rule has
# no opinion here and falls back to the status quo pick.
def _StatusQuo(b):
    same = [c for c in b["cands"] if c["name"] == b["heur"]]
    return same[0] if same else None


def _Band(b):
    e = [c for c in b["cands"] if c["copies"] >= 2 and not c["land"] and not c["prot"]]
    return max(e, key=lambda c: c["mv"]) if e else None


def _SpareAny(b):
    e = [c for c in b["cands"] if c["copies"] >= 2 and not c["prot"]]
    return max(e, key=lambda c: c["mv"]) if e else None


def _NoLand(b):
    e = [c for c in b["cands"] if not c["land"] and not c["prot"]]
    return max(e, key=lambda c: c["mv"]) if e else None


def _Oracle(b):
    return min(b["cands"], key=lambda c: c["win"])


RULES = [("status quo (the provider)", _StatusQuo),
         ("spare-copy band (nonland, max mv)", _Band),
         ("spare copy incl. land", _SpareAny),
         ("never shed a land (max mv)", _NoLand),
         ("ORACLE (label lower bound)", _Oracle)]


def Main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    paths = sorted(glob.glob(argv[1]))
    if not paths:
        print(f"no files matched {argv[1]!r} (quote the glob)", file=sys.stderr)
        return 2
    blocks = list(Blocks(paths))
    if not blocks:
        print("no [discard_trace] blocks -- did the deck reach a CLEANUP shed at all?\n"
              "Cross-check with MTG_TRACE=discard, which fires at every real discard site.",
              file=sys.stderr)
        return 2

    n = len(blocks)
    trivial = sum(1 for b in blocks if len({c["win"] for c in b["cands"]}) == 1)
    multi = sum(1 for b in blocks if sum(1 for c in b["cands"] if c["opt"]) > 1)
    ambiguous = sum(1 for b in blocks
                    if len({c["win"] for c in b["cands"] if c["name"] == b["heur"]}) > 1)
    print(f"decisions parsed        : {n}   (files: {len(paths)})")
    print(f"  all-candidates-tie    : {trivial}  ({trivial / n * 100:.1f}%)  <- unlosable")
    print(f"  multi-optimal         : {multi}  ({multi / n * 100:.1f}%)")
    print(f"  ambiguous (dup name)  : {ambiguous}  (excluded from the status-quo grade)")

    sq_misses = []
    print(f"\n{'rule':36s} {'regret':>8s} {'optimal':>9s} {'misses':>7s}")
    for label, fn in RULES:
        reg, opt, graded = 0.0, 0, 0
        for b in blocks:
            if fn is _StatusQuo and len({c["win"] for c in b["cands"]
                                         if c["name"] == b["heur"]}) > 1:
                continue                      # ambiguous: not graded either way
            pick = fn(b) or _StatusQuo(b)
            if pick is None:
                continue
            best = min(c["win"] for c in b["cands"])
            reg += pick["win"] - best
            graded += 1
            if pick["win"] == best:
                opt += 1
            elif fn is _StatusQuo:
                sq_misses.append((pick["win"] - best, b))
        if graded:
            print(f"{label:36s} {reg / graded:8.4f} {opt / graded * 100:8.2f}% "
                  f"{graded - opt:7d}")

    if sq_misses:
        print("\n--- status-quo suboptimal decisions (rule pick vs best) ---")
        for d, b in sorted(sq_misses, key=lambda x: -x[0])[:20]:
            opts = sorted({c["name"] for c in b["cands"] if c["opt"]})
            print(f"  seed={b['seed']} T{b['turn']} hand={b['handsize']} "
                  f"rule={b['heur']} (+{d}t)  optimal={opts}")
        print("\nClassify EVERY one as churn/clairvoyance or a visible-info miss before "
              "adopting or dismissing.")
    return 0 if not sq_misses else 1


if __name__ == "__main__":
    sys.exit(Main(sys.argv))
