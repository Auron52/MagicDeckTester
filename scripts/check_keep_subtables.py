#!/usr/bin/env python3
"""Is a mulligan (keep/bottom) generation sampling its BOTTOMING sub-tables?

Detects the sub-table starvation bug (docs/design/keepgen-subtable-starvation-detection.md):
a generation that resumes a journal with fixed refs starts in the REFINE phase, where the
sub-table batches were never fed -- so every bottoming sub-cell keeps just one rollout and
DecideBottom's argmin selects on noise. It shipped on Dragons and Mirrorwing.

Works on a run IN FLIGHT (read-only; never touches the job):

    python3 scripts/check_keep_subtables.py <gen.log>              # BATCHED runs only -- see below
    python3 scripts/check_keep_subtables.py <deck>....raw.json.journal
    python3 scripts/check_keep_subtables.py <deck>....raw.json[.gz]   # a FINISHED run

Any mix of the three; each is auto-detected.

    exit 0  healthy -- sub-tables verified to be sampled
    exit 1  STARVED
    exit 2  CANNOT DETERMINE -- this is NOT a pass, and must never be reported as one
    exit 64 usage

READ THIS BEFORE TRUSTING A QUIET RESULT. A gen.log only answers for a BATCHED run, whose monitor
line carries `rollsub=` and `sub=N/M`. A CONTINUOUS run (`--gen-mulligan fast`) emits a different
monitor line entirely -- `fed=` / `frozen=` / `cap=` -- which contains **no sub-table counters at
all**. There is nothing there to read, so on a continuous log this tool cannot answer and now says
so with exit 2. It used to fall through to "inconclusive" and exit 0, under a legend reading
"0 = healthy or inconclusive"; asked mid-run whether FiveColour's 20-day continuous generation had
the Dragons/Mirrorwing problem, that silence was reported as a clean bill of health. For a
continuous run use the JOURNAL (in flight) or the RAW (finished) instead -- both carry real per-cell
rollout counts.
"""
import sys, os, re, json, gzip, collections

HAND = 7

# Verdicts. None means "this source had nothing to say yet" (benign, e.g. a 3-minute-old run);
# UNREADABLE means "this source structurally cannot answer" -- a different thing, and never a pass.
UNREADABLE = "unreadable"


def _open(p):
    return gzip.open(p, "rt") if p.endswith(".gz") else open(p, errors="replace")


# ---------------------------------------------------------------- gen.log ----
def _last_run(path):
    """Only the MOST RECENT run's lines.

    `mullgen.sh` APPENDS to gen.log, so a deck that has been generated more than once has every
    run concatenated in one file. Reading the whole thing makes an earlier starved run condemn the
    healthy one now in flight -- which is exactly what happened on the 2026-09-02 Dragons repair:
    the fixed run had correctly resumed from the floor checkpoint and the detector still printed
    STARVED, off August's monitor lines. Split on the settings banner and keep the last block.
    """
    lines = list(_open(path))
    starts = [i for i, ln in enumerate(lines) if "MULLIGAN PROFILE GEN SETTINGS" in ln]
    if len(starts) > 1:
        print(f"  NOTE: {len(starts)} runs appended in this log -- reading only the last "
              f"(from line {starts[-1] + 1})")
    return (lines[starts[-1]:] if starts else lines), lines


def check_genlog(path):
    """The monitor line is the primary signal: `sub=N/M` is sub-table batches DONE / TOTAL."""
    resumed_refine = False
    monitors = []          # (elapsed, phase, rollsub, sub_done, sub_total)
    continuous = 0         # monitor lines in the CONTINUOUS format, which has no sub-table counters
    recommend_probe = False
    last, allines = _last_run(path)
    # The FLOOR-COMPLETE signal is scoped to the WHOLE file, not the last run block. `_last_run`
    # exists so an earlier starved run cannot condemn the healthy one now in flight, but for this
    # one signal it inverts: a legitimate RESUME (an OOM recovery, a deliberate pause) opens a new
    # settings banner and restores refs from a journal whose floor completed in an EARLIER block, so
    # scoping it to the last block prints "resumed into refine YES / floor NO" -- precisely the
    # Dragons/Mirrorwing starvation signature -- for a run that is fine. FiveColour is that case:
    # floor completed at 1056351s in block 1, and block 2 is the OOM recovery.
    # What actually distinguishes the bug is refs restored with the floor never having completed
    # ANYWHERE, so that is what gets tested.
    floor_complete = any("floor complete, refs fixed" in ln for ln in allines)
    floor_in_last = any("floor complete, refs fixed" in ln for ln in last)
    for ln in last:
        if "refs restored from journal -> resuming refine" in ln:
            resumed_refine = True
        if "probe carry" in ln and "ON" in ln:
            recommend_probe = True
        m = re.search(r"monitor:\s*(\d+)s\s+phase=(\w+).*?rollsub=(\d+).*?sub=(\d+)/(\d+)", ln)
        if m:
            monitors.append((int(m.group(1)), m.group(2), int(m.group(3)),
                             int(m.group(4)), int(m.group(5))))
        elif re.search(r"monitor:\s*\d+s\s+phase=\w+.*?\bfed=\d+", ln):
            continuous += 1

    danger = resumed_refine and not floor_complete
    print(f"  resumed straight into refine : "
          f"{('YES  <-- the trigger' if danger else 'yes (but the floor DID complete earlier -- benign resume)') if resumed_refine else 'no'}")
    print(f"  floor phase completed        : "
          f"{'yes' if floor_in_last else ('yes, in an EARLIER run block' if floor_complete else 'NO')}")
    print(f"  probe carry active           : {'yes' if recommend_probe else 'no'}")
    if not monitors and continuous:
        # The whole point of exit 2: a continuous monitor line reports fed/frozen/cap and carries no
        # sub-table counters whatsoever, so absence of a starvation signal here is absence of ANY
        # signal. Saying "looks fine" off this log is not a weak answer, it is a wrong one.
        print(f"  monitor lines                : {continuous}, all in the CONTINUOUS format "
              f"(fed=/frozen=/cap=)")
        print("\n  *** CANNOT DETERMINE from this log. A continuous (--gen-mulligan fast) run's")
        print("      monitor line carries NO sub-table counters -- there is nothing here to read.")
        print("      Check the .journal (in flight) or the .raw.json (finished) instead. ***")
        return UNREADABLE
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
    print(f"  sub_target                   : {target}"
          f"{'  <-- ASSUMED; raw predates the field' if legacy else ''}")
    print(f"  sub-table cell-sides         : {tot}")
    print(f"  min rollouts per sub cell    : {mn}")
    if under:
        print(f"\n  *** STARVED: {under}/{tot} sub cell-sides below sub_target. ***")
        return False
    if legacy:
        # Do NOT report a pass here. Without meta.sub_target there is no record of what this run
        # was aiming for, so all that has been shown is the absence of the R=1 signature -- a raw
        # generated to a target of 30 and stalled at 2 clears this check exactly as cleanly as a
        # healthy one. FiveColour's raw is such a legacy raw and passed here trivially.
        print(f"\n  ?  CANNOT DETERMINE: this raw predates meta.sub_target, so the real target is")
        print(f"     unknown and only the floor of {target} could be checked (min seen: {mn}).")
        print(f"     Absence of the R=1 signature is NOT evidence the run reached its cap.")
        return UNREADABLE
    print("\n  OK: every sub-table cell reached its target.")
    return True


def main(argv):
    if not argv:
        print(__doc__)
        return 64
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
    if any(v is True for v in verdicts):
        # A positive reading from any source settles it; an unreadable source alongside it is just
        # a source that had nothing to add.
        if any(v == UNREADABLE for v in verdicts):
            print("VERDICT: sub-tables look healthy (one source could not answer; another could).")
        else:
            print("VERDICT: sub-tables look healthy.")
        return 0
    if any(v == UNREADABLE for v in verdicts):
        print("VERDICT: CANNOT DETERMINE -- no source given could answer the question.")
        print("         This is NOT a clean bill of health. Do not report it as one.")
        return 2
    print("VERDICT: inconclusive (too early, or nothing sub-table-related to read).")
    print("         Nothing has been verified yet -- check again once the run has produced data.")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
