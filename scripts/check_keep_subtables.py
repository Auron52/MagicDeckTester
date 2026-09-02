#!/usr/bin/env python3
"""Is a mulligan (keep/bottom) generation sampling its BOTTOMING sub-tables?

Detects the sub-table starvation bug (docs/design/keepgen-subtable-starvation-detection.md):
a generation that resumes a journal with fixed refs starts in the REFINE phase, where the
sub-table batches were never fed -- so every bottoming sub-cell keeps just one rollout and
DecideBottom's argmin selects on noise. It shipped on Dragons and Mirrorwing.

Works on a run IN FLIGHT (read-only; never touches the job):

    python3 scripts/check_keep_subtables.py <gen.log>              # fastest, most direct
    python3 scripts/check_keep_subtables.py <deck>....raw.json.journal
    python3 scripts/check_keep_subtables.py <deck>....raw.json[.gz]   # a FINISHED run

Any mix of the three; each is auto-detected. Exit 0 = healthy//unknown, 1 = starvation found.
"""
import sys, os, re, json, gzip, collections

HAND = 7


def _open(p):
    return gzip.open(p, "rt") if p.endswith(".gz") else open(p, errors="replace")


# ---------------------------------------------------------------- gen.log ----
def check_genlog(path):
    """The monitor line is the primary signal: `sub=N/M` is sub-table batches DONE / TOTAL."""
    resumed_refine = False
    monitors = []          # (elapsed, phase, rollsub, sub_done, sub_total)
    floor_complete = False
    recommend_probe = False
    for ln in _open(path):
        if "refs restored from journal -> resuming refine" in ln:
            resumed_refine = True
        if "floor complete, refs fixed" in ln:
            floor_complete = True
        if "probe carry" in ln and "ON" in ln:
            recommend_probe = True
        m = re.search(r"monitor:\s*(\d+)s\s+phase=(\w+).*?rollsub=(\d+).*?sub=(\d+)/(\d+)", ln)
        if m:
            monitors.append((int(m.group(1)), m.group(2), int(m.group(3)),
                             int(m.group(4)), int(m.group(5))))

    print(f"  resumed straight into refine : {'YES  <-- the trigger' if resumed_refine else 'no'}")
    print(f"  floor phase completed        : {'yes' if floor_complete else 'NO'}")
    print(f"  probe carry active           : {'yes' if recommend_probe else 'no'}")
    if not monitors:
        print("  monitor lines               : none yet (run <5 min old, or a log without them)")
        return None
    last = monitors[-1]
    print(f"  monitor lines                : {len(monitors)}  (latest at {last[0]}s, phase={last[1]})")
    print(f"  sub-table batches done       : {last[3]}/{last[4]}"
          f"  ({100.0 * last[3] / last[4] if last[4] else 0:.1f}%)")
    print(f"  sub-table rollouts (rollsub) : {last[2]}")

    if last[4] == 0:
        print("  -> no sub-table work in this run at all (keep-only gen?). Not the bug.")
        return None
    if last[3] == 0 and last[2] == 0:
        # The definitive signature: batches exist, none ever ran.
        if len(monitors) >= 2 or last[0] >= 600:
            print("\n  *** STARVED: sub=0/N and rollsub=0 with the run well underway. ***")
            return False
        print("\n  ?  sub=0 so far but only one early monitor line -- check again in 10 min.")
        return None
    print("\n  OK: sub-table batches are being consumed.")
    return True


# ---------------------------------------------------------------- journal ----
def check_journal(path):
    """Per-cell records: {"H":size,"i":idx,"p":pd,...,"n":rollouts}. Counts are monotone, so the
    MAX n per (H,i,pd) is that cell's sampling. Sub-tables are H < 7."""
    best = collections.defaultdict(int)
    sizes = collections.Counter()
    bad = 0
    for ln in _open(path):
        ln = ln.strip()
        if not ln.startswith("{") or '"H"' not in ln:
            continue
        try:
            r = json.loads(ln)
        except Exception:
            bad += 1
            continue
        H = r.get("H")
        if H is None or H >= HAND:
            continue
        k = (H, r.get("i"), r.get("p"))
        n = int(r.get("n", 0))
        if n > best[k]:
            best[k] = n
        sizes[H] += 1
    if bad:
        print(f"  (skipped {bad} unparsable lines -- a journal's last line can be torn mid-write)")
    if not best:
        print("  sub-table (H<7) records      : NONE yet")
        print("  -> inconclusive on its own: a healthy run writes these as sub-cells complete,")
        print("     which on a big deck can lag. Use the gen.log monitor line instead.")
        return None
    counts = sorted(best.values())
    mx, mn = counts[-1], counts[0]
    print(f"  sub-table cell-sides seen    : {len(best)}  (sizes present: "
          f"{', '.join(f'H{h}' for h in sorted(sizes))})")
    print(f"  rollouts per sub cell-side   : min {mn}  median {counts[len(counts)//2]}  max {mx}")
    if mx <= 1:
        print("\n  *** STARVED: no sub cell-side has more than ONE rollout. ***")
        return False
    print("\n  OK: sub-table cells carry real sampling.")
    return True


# -------------------------------------------------------------------- raw ----
def check_raw(path):
    """A finished run. meta.sub_target is what every sub cell-side was meant to reach; older raws
    predate it, so fall back to the documented floor of 2 (which still catches the R=1 signature)."""
    d = json.load(_open(path))
    meta = d.get("meta") or {}
    target = meta.get("sub_target")
    legacy = target is None
    if legacy:
        target = 2
    mn, tot, under = 10**9, 0, 0
    for s in d.get("sizes") or []:
        if s.get("H", HAND) >= HAND:
            continue
        for e in s["entries"]:
            for pd in (0, 1):
                c = e["count"][pd]
                tot += 1
                mn = min(mn, c)
                if c < target:
                    under += 1
    if not tot:
        print("  no sub-table cells in this raw (keep-only). Not the bug.")
        return None
    print(f"  R={meta.get('R')}  depth={meta.get('depth')}  budget={meta.get('budget_ms')}ms")
    print(f"  sub_target                   : {target}{'  (legacy raw -> floor)' if legacy else ''}")
    print(f"  sub-table cell-sides         : {tot}")
    print(f"  min rollouts per sub cell    : {mn}")
    if under:
        print(f"\n  *** STARVED: {under}/{tot} sub cell-sides below sub_target. ***")
        return False
    print("\n  OK: every sub-table cell reached its target.")
    return True


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    verdicts = []
    for p in argv:
        print(f"\n=== {p} ===")
        if not os.path.exists(p):
            print("  MISSING")
            continue
        if p.endswith(".journal"):
            v = check_journal(p)
        elif p.endswith(".json") or p.endswith(".json.gz"):
            v = check_raw(p)
        else:
            v = check_genlog(p)
        verdicts.append(v)
    print()
    if any(v is False for v in verdicts):
        print("VERDICT: STARVED bottoming sub-tables -- see "
              "docs/design/keepgen-subtable-starvation-detection.md for what to do.")
        return 1
    if all(v is None for v in verdicts):
        print("VERDICT: inconclusive (too early, or nothing sub-table-related to read).")
        return 0
    print("VERDICT: sub-tables look healthy.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
