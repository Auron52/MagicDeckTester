#!/usr/bin/env python3
"""MANDATORY per-game audit gate — run and READ THIS before `regression.sh <mode> --accept`.

The aggregate fingerprint (`won/avg_win_turn`) hides per-game flips: a *better* average can conceal
won→lost games, and a flat win-count can hide equal turn-later/earlier swaps. This script diffs the
committed per-game ground truth (`test/gt_logs/<key>.wins`, the prior-code baseline) against THIS
run's per-game outcomes (`test/logs/<mode>/wins/<key>.wins`) and prints the exact breakdown the
fingerprint conceals, SPLIT BY depth — because the review bar differs:

  * SEARCHED depth (d>0): review ALL of them. Every win→loss AND every turn-later must be
    individually explained (real regression vs fetch-shuffle variance vs budget churn -- confirm
    churn by re-running the one game at a higher budget; confirm variance by checking the draws
    diverge). This is where engine quality lives.
  * depth 0 (greedy, no search): lighter — sanity-check a couple of examples. d0 swaps are the
    greedy casting-more/attack-order heuristic and are expected to churn; they are not a quality bar.

Exit code is **non-zero if any SEARCHED-depth win→loss exists** (the hard gate). You may not
`--accept` until every searched-depth win→loss is root-caused and every searched-depth turn-later is
classified. (d0 win→loss are reported but do not gate.)

Usage:
    python3 test/audit_changed_games.py <mode>          # smoke | regression | overnight
    python3 test/audit_changed_games.py <mode> --old-git  # baseline = HEAD~1 gt_logs (re-check an
                                                            # accept that already happened)
"""
import sys, os, glob, subprocess

MODE = next((a for a in sys.argv[1:] if not a.startswith("-")), None)
if MODE not in ("smoke", "regression", "overnight"):
    print("usage: audit_changed_games.py <smoke|regression|overnight> [--old-git]"); sys.exit(2)
OLD_GIT = "--old-git" in sys.argv


def read_wins(text):
    # gi -> (win_turn, play_digest_or_None). The 3rd column (play digest) is optional so a
    # legacy 2-column ground-truth log still parses (digest None -> not compared).
    out = {}
    for l in text.splitlines():
        p = l.split()
        if not p:
            continue
        out[int(p[0])] = (int(p[1]), p[2] if len(p) > 2 else None)
    return out


def base_seed(key):  # <deck>_<mode>_d<depth>_s<seed>
    try: return int(key.rsplit("_s", 1)[1])
    except Exception: return None


def is_searched(key):
    return "_d0_" not in key


def old_side(key):
    if OLD_GIT:
        r = subprocess.run(["git", "show", f"HEAD~1:test/gt_logs/{key}.wins"],
                           capture_output=True, text=True)
        return read_wins(r.stdout) if r.returncode == 0 else {}
    p = f"test/gt_logs/{key}.wins"
    return read_wins(open(p).read()) if os.path.exists(p) else {}


def new_side(key):
    p = f"test/gt_logs/{key}.wins" if OLD_GIT else f"test/logs/{MODE}/wins/{key}.wins"
    return read_wins(open(p).read()) if os.path.exists(p) else {}


keys = sorted(os.path.basename(f)[:-5] for f in glob.glob(f"test/gt_logs/*_{MODE}_*.wins"))
if not keys:
    print(f"no committed gt_logs for mode '{MODE}'"); sys.exit(2)

# tallies split by searched vs d0. `played` = play changed at the SAME win turn (only visible via
# the per-game play digest -- the coarse won/avg fingerprint cannot see it).
tot = {s: dict(winloss=0, losswin=0, later=0, earlier=0, played=0) for s in ("searched", "d0")}
searched_winloss, searched_later, searched_played = [], [], []
d0_winloss, d0_later, d0_played = [], [], []
unchanged = missing = 0
digest_baseline = False   # any case whose OLD side carries digests -> digest comparison is live
for key in keys:
    old, new = old_side(key), new_side(key)
    if not new:
        missing += 1; continue
    # The play-digest gate is live once the OLD side (committed GT) carries digests -- independent
    # of whether this case changed (set here, not only in the changed-game loop below).
    if not digest_baseline and any(dg is not None for (_, dg) in old.values()):
        digest_baseline = True
    if old == new:
        unchanged += 1; continue
    bucket = "searched" if is_searched(key) else "d0"
    seed = base_seed(key)
    for gi, nv in new.items():
        ov = old.get(gi)
        if ov is None:
            continue
        o, od = ov          # old (win_turn, digest)
        n, nd = nv          # new (win_turn, digest)
        if o == n:
            # same win turn: did the PLAY change? (needs both digests present)
            if od is not None and nd is not None and od != nd:
                tot[bucket]["played"] += 1
                (searched_played if bucket == "searched" else d0_played).append((key, gi, o, n, seed))
            continue
        ow, nw = o > 0, n > 0
        if ow and nw:
            if n > o:
                tot[bucket]["later"] += 1
                (searched_later if bucket == "searched" else d0_later).append((key, gi, o, n, seed))
            else:
                tot[bucket]["earlier"] += 1
        elif ow and not nw:
            tot[bucket]["winloss"] += 1
            (searched_winloss if bucket == "searched" else d0_winloss).append((key, gi, o, n, seed))
        elif nw and not ow:
            tot[bucket]["losswin"] += 1

base = "HEAD~1 gt_logs" if OLD_GIT else "committed gt_logs"
new_desc = "current gt_logs" if OLD_GIT else f"test/logs/{MODE}/wins"
print(f"AUDIT {MODE}   (old = {base}   new = {new_desc})")
print(f"  configs changed: {len(keys) - unchanged - missing}   unchanged: {unchanged}   no-run-dir: {missing}")
for b in ("searched", "d0"):
    t = tot[b]
    print(f"  [{b:8}] win->loss={t['winloss']}  loss->win={t['losswin']}  "
          f"later={t['later']}  earlier={t['earlier']}  play-changed={t['played']}")
if not digest_baseline:
    print("  (no play-digest baseline yet -- ground-truth logs are pre-digest; --accept records "
          "digests so play-only changes are caught next run)")

# Inline old-vs-new per-turn diff for every game that must be reviewed, so the analysis the
# skill calls mandatory is in this output by default (no separate manual step). Best-effort:
# needs the baseline binary saved by `regression.sh --accept` (logs/snapshots/<mode>-baseline);
# without it each block still prints the NEW line + how to get a baseline. OLD_GIT re-checks a
# past accept from win-turn logs only, so skip the binary diff there.
_EXPLAIN_CAP = 20            # bound the inline re-runs so a pathological run cannot make the audit crawl
_explained = [0]


def _explain(key, gi):
    if OLD_GIT:
        return None
    if _explained[0] >= _EXPLAIN_CAP:
        return (f"    [explain capped at {_EXPLAIN_CAP}; diff the rest manually: "
                f"python3 test/explain_game.py {MODE} {key} {gi}]")
    try:
        from explain_game import diff_game
    except Exception:
        return None
    try:
        blk = diff_game(MODE, key, gi)
        _explained[0] += 1
        return blk
    except Exception as ex:
        return f"    [explain: {ex}]"


if searched_winloss:
    print(f"\n*** SEARCHED-depth win->loss ({len(searched_winloss)}) — ROOT-CAUSE EACH (hard gate):")
    for key, gi, o, n, seed in searched_winloss:
        print(f"    {key} gi{gi}: {o}->loss")
        blk = _explain(key, gi)
        if blk: print(blk)
if searched_later:
    print(f"\n*** SEARCHED-depth turn-later ({len(searched_later)}) — CLASSIFY EACH "
          f"(variance: draws diverge? / churn: recovers at higher budget?):")
    for key, gi, o, n, seed in searched_later:
        print(f"    {key} gi{gi}: {o}->{n}")
        blk = _explain(key, gi)
        if blk: print(blk)
if searched_played:
    print(f"\n*** SEARCHED-depth PLAY-CHANGED at same win turn ({len(searched_played)}) — the play "
          f"digest moved though the win turn did not; ANALYZE EACH (a deliberate line change, or a bug):")
    for key, gi, o, n, seed in searched_played:
        print(f"    {key} gi{gi}: win T{o} unchanged, play differs")
        blk = _explain(key, gi)
        if blk: print(blk)

if d0_winloss or d0_later or d0_played:
    print(f"\nd0 (greedy, lighter bar): win->loss={len(d0_winloss)} turn-later={len(d0_later)} "
          f"play-changed={len(d0_played)} — sanity-check a couple, e.g.:")
    for key, gi, o, n, seed in (d0_winloss[:2] + d0_later[:2] + d0_played[:1]):
        tag = "->loss" if n <= 0 else (f"->{n}" if n != o else " play-changed")
        print(f"    {key} gi{gi}: {o}{tag}")

print()
if searched_winloss:
    print(f"GATE FAIL: {len(searched_winloss)} searched-depth win->loss — root-cause each before --accept.")
    sys.exit(1)
notes = []
if searched_later:
    notes.append(f"{len(searched_later)} searched turn-later (CLASSIFY each: variance/churn)")
if searched_played:
    notes.append(f"{len(searched_played)} searched play-changed at same win turn (ANALYZE each)")
if notes:
    print("GATE: no searched win->loss, but " + "; ".join(notes) + " before --accept.")
else:
    print("GATE: no searched-depth flips, slowdowns, or play changes.")
sys.exit(0)
