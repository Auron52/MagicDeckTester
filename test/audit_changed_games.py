#!/usr/bin/env python3
"""Per-game audit — run and READ THIS before `regression.sh <mode> --accept`.

The metric is the loss-penalized average win turn (a game unwon by the horizon scores max_turns+1).
Under that metric there is exactly ONE thing that matters per game: did it get **SLOWER** (worse score)
or **FASTER** (better score)? A win→loss is not a special category — it is just the maximal slowdown
(a win turn replaced by the max_turns+1 loss score); a loss→win is just the maximal speedup. So this
script classifies every changed game as slower / faster / (same-score) play-changed, by loss-penalized
score ORDER (a loss ranks worse than any win), SPLIT BY depth because the review bar differs:

  * SEARCHED depth (d>0): what you analyze are the SLOWER games — each is a real regression, or
    fetch-shuffle variance (draws diverge), or budget churn (recovers when that one game is re-run at a
    higher budget). This is where engine quality lives.
  * depth 0 (greedy, no search): lighter — sanity-check a couple. d0 swaps are the greedy
    casting-more/attack-order heuristic churning; not a quality bar.

This script is REPORT-ONLY (always exits 0): it surfaces the slower games for you to analyze and the
net picture; the accept decision is a human judgement on the net loss-penalized delta, not a per-game
gate. (`regression.sh` also prints the aggregate avg deltas.)

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


def score(win_turn):
    # Loss-penalized ORDER key: a won game scores its win turn; a loss (win_turn <= 0) ranks worse
    # than any win. We only need the ordering (slower/faster), not the exact max_turns+1 value, so a
    # sentinel above every plausible win turn suffices and keeps the audit horizon-agnostic.
    return win_turn if win_turn > 0 else 10_000


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

# tallies split by searched vs d0. `played` = play changed at the SAME loss-penalized score (only
# visible via the per-game play digest -- the coarse avg fingerprint cannot see it).
tot = {s: dict(slower=0, faster=0, played=0) for s in ("searched", "d0")}
searched_slower, searched_played = [], []
d0_slower, d0_played = [], []
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
        so, sn = score(o), score(n)
        if sn > so:         # SLOWER (worse score; absorbs turn-later AND win->loss)
            tot[bucket]["slower"] += 1
            (searched_slower if bucket == "searched" else d0_slower).append((key, gi, o, n, seed))
        elif sn < so:       # FASTER (better score; absorbs turn-earlier AND loss->win)
            tot[bucket]["faster"] += 1
        else:               # same loss-penalized score: did the PLAY change? (needs both digests)
            if od is not None and nd is not None and od != nd:
                tot[bucket]["played"] += 1
                (searched_played if bucket == "searched" else d0_played).append((key, gi, o, n, seed))

base = "HEAD~1 gt_logs" if OLD_GIT else "committed gt_logs"
new_desc = "current gt_logs" if OLD_GIT else f"test/logs/{MODE}/wins"
print(f"AUDIT {MODE}   (old = {base}   new = {new_desc})")
print(f"  configs changed: {len(keys) - unchanged - missing}   unchanged: {unchanged}   no-run-dir: {missing}")
for b in ("searched", "d0"):
    t = tot[b]
    print(f"  [{b:8}] slower={t['slower']}  faster={t['faster']}  play-changed={t['played']}  "
          f"(slower/faster by loss-penalized score; win->loss counts as slower, loss->win as faster)")
if not digest_baseline:
    print("  (no play-digest baseline yet -- ground-truth logs are pre-digest; --accept records "
          "digests so play-only changes are caught next run)")

# Inline old-vs-new per-turn diff for every SLOWER game (the ones you analyze), so the analysis is in
# this output by default (no separate manual step). Best-effort: needs the baseline binary saved by
# `regression.sh --accept` (logs/snapshots/<mode>-baseline); without it each block still prints the
# NEW line + how to get a baseline. OLD_GIT re-checks a past accept from win-turn logs only, so skip
# the binary diff there.
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


def _label(o, n):
    # human-readable transition; a loss is shown as "loss"
    os_, ns_ = (str(o) if o > 0 else "loss"), (str(n) if n > 0 else "loss")
    return f"{os_}->{ns_}"


if searched_slower:
    print(f"\n*** SEARCHED-depth SLOWER ({len(searched_slower)}) — ANALYZE EACH (real regression / "
          f"fetch-shuffle variance: draws diverge? / budget churn: recovers at higher budget?):")
    for key, gi, o, n, seed in searched_slower:
        print(f"    {key} gi{gi}: {_label(o, n)}")
        blk = _explain(key, gi)
        if blk: print(blk)
if searched_played:
    print(f"\n*** SEARCHED-depth PLAY-CHANGED at same score ({len(searched_played)}) — the play "
          f"digest moved though the score did not; ANALYZE EACH (a deliberate line change, or a bug):")
    for key, gi, o, n, seed in searched_played:
        print(f"    {key} gi{gi}: score T{o if o>0 else 'loss'} unchanged, play differs")
        blk = _explain(key, gi)
        if blk: print(blk)

if d0_slower or d0_played:
    print(f"\nd0 (greedy, lighter bar): slower={len(d0_slower)} play-changed={len(d0_played)} "
          f"— sanity-check a couple, e.g.:")
    for key, gi, o, n, seed in (d0_slower[:3] + d0_played[:1]):
        tag = _label(o, n) if score(n) != score(o) else " play-changed"
        print(f"    {key} gi{gi}: {tag}")

print()
notes = []
if searched_slower:
    notes.append(f"{len(searched_slower)} searched SLOWER (analyze: regression/variance/churn)")
if searched_played:
    notes.append(f"{len(searched_played)} searched play-changed at same score (analyze)")
if notes:
    print("REVIEW: " + "; ".join(notes) + " — decide on the NET loss-penalized delta before --accept.")
else:
    print("REVIEW: no searched-depth slowdowns or play changes.")
sys.exit(0)
