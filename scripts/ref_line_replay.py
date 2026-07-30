#!/usr/bin/env python3
"""Re-anchor a saved reference line by PLAN CONTENT instead of plan index, and report whether the
human's recorded line is still achievable on the current engine.

`test/viewer_protocol_check.py` replays a reference through the positional `--choices` stream, so any
change to enumeration order or breadth makes the recorded index point at a different plan (or out of
range) and the whole line reads as "drift" -- even when the human's line is still perfectly legal.
That is a contract check, not a reachability check.

This tool instead reads the recorded plan's DESCRIPTION out of the reference itself
(`decision.plans[chosen]`: its `summary`, `land` and `casts`) and, at each step, finds the plan in the
CURRENT enumeration that matches it. So it answers the question a shortfall investigation actually
needs: "the search wins on turn 6 -- is the human's turn-4 line still there to be found, or did the
engine change out from under the reference?"

    scripts/ref_line_replay.py references/Dragonstorm/claude_s24_gi23.json -v
    scripts/ref_line_replay.py --all                 # every reference, one line each

Outcomes: REPRODUCES (line replays to the recorded win turn) / DIFFERENT-RESULT (line replays but
ends elsewhere -- a real behaviour change) / UNREACHABLE (a recorded plan no longer exists at some
step; that step's detail names the plan that went missing).
"""
import argparse, glob, importlib.util, json, os, re, sys
from concurrent.futures import ThreadPoolExecutor

_spec = importlib.util.spec_from_file_location(
    "vpc", os.path.join(os.path.dirname(__file__), "..", "test", "viewer_protocol_check.py"))
vpc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(vpc)
DECKS, DEC_RE, RES_RE = vpc.DECKS, vpc.DEC_RE, vpc.RES_RE
SIDE_CHANNEL_TYPES, FORCED_MULLIGAN_TYPES, side_channel_args, force_arg, replay = (
    vpc.SIDE_CHANNEL_TYPES, vpc.FORCED_MULLIGAN_TYPES, vpc.side_channel_args, vpc.force_arg, vpc.replay)
# The content matcher is shared with the protocol check (it now re-anchors by content too);
# one definition, one behaviour.
plan_key, find_plan = vpc.plan_key, vpc.find_plan


def ref_steps(ref, drop_mulligan):
    """The reference's decisions in --choices order, mirroring flatten_choices()'s filtering."""
    out = []
    for d in ref.get("decisions", []):
        t = d.get("decision", {}).get("type")
        if t in SIDE_CHANNEL_TYPES:
            continue
        if drop_mulligan and t in FORCED_MULLIGAN_TYPES:
            continue
        out.append(d)
    return out


def check(path, verbose=False):
    ref = json.load(open(path))
    deck_dir = os.path.basename(os.path.dirname(path))
    if deck_dir not in DECKS:
        return path, "SKIP", f"unknown deck dir {deck_dir}", []
    deck, prof = DECKS[deck_dir]
    seed, gi = ref["seed"], ref["game_index"]
    extra = side_channel_args(ref["decisions"])
    force = force_arg(ref)
    steps = ref_steps(ref, drop_mulligan=force is not None)
    mt = max(8, ref.get("win_turn") or 0)

    picks, trace = [], []
    for ri in range(len(steps) + 1):
        rc, out = replay(deck, prof, seed, gi, picks, force, extra, mt)
        if "Error:" in out or rc not in (0, 70):
            return path, "ERROR", f"engine error at step {ri} (rc={rc}): {out.strip()[-200:]}", trace
        if rc == 0:
            m = RES_RE.search(out)
            if not m:
                return path, "ERROR", f"exit 0 with no CLAUDE_RESULT at step {ri}", trace
            res = json.loads(m.group(1))
            same = (res.get("won") == ref.get("won")) and (res.get("win_turn") == ref.get("win_turn"))
            det = (f"replayed won={res.get('won')} win_turn={res.get('win_turn')} "
                   f"vs recorded won={ref.get('won')} win_turn={ref.get('win_turn')}"
                   + (f" (line ended at step {ri}/{len(steps)})" if ri < len(steps) else ""))
            return path, ("REPRODUCES" if same else "DIFFERENT-RESULT"), det, trace
        if ri >= len(steps):
            return path, "DIFFERENT-RESULT", (
                f"engine still wants a decision after all {len(steps)} recorded steps were "
                f"re-anchored (recorded win_turn={ref.get('win_turn')})"), trace
        m = DEC_RE.search(out)
        if not m:
            return path, "ERROR", f"exit 70 with no CLAUDE_DECISION at step {ri}", trace
        dec = json.loads(m.group(1))
        rdec = steps[ri]["decision"]
        if dec.get("type") != rdec.get("type"):
            return path, "UNREACHABLE", (
                f"step {ri} (turn {rdec.get('turn')}): decision type changed "
                f"{rdec.get('type')} -> {dec.get('type')}"), trace
        chosen = steps[ri]["chosen"]
        if dec.get("type") != "main_phase":
            # Non-plan decision (e.g. Dragonstorm's `dragon` picks): indices address a different
            # list the reference does not describe, so replay them verbatim -- best effort.
            picks += [int(x) for x in chosen] if isinstance(chosen, list) else [int(chosen)]
            trace.append(f"T{rdec.get('turn')} {dec.get('type')}: verbatim {chosen}")
            continue
        plans = dec.get("plans") or []
        if chosen == -1:
            picks.append(-1)
            trace.append(f"T{rdec.get('turn')} pass")
            continue
        rplans = rdec.get("plans") or []
        recorded = rplans[chosen] if 0 <= chosen < len(rplans) else None
        if recorded is None:
            return path, "ERROR", f"step {ri}: reference records pick {chosen} but stores no such plan", trace
        idx = find_plan(recorded, plans)
        if idx is None:
            return path, "UNREACHABLE", (
                f"step {ri} (turn {rdec.get('turn')}): recorded plan {recorded.get('summary')!r} "
                f"is no longer enumerated ({len(plans)} plans offered)"), trace
        picks.append(idx)
        trace.append(f"T{rdec.get('turn')} idx {chosen}->{idx}: {recorded.get('summary')}")
    return path, "ERROR", "fell through the step loop", trace


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("refs", nargs="*", help="reference JSON paths (default: --all)")
    ap.add_argument("--all", action="store_true", help="every references/<deck>/claude_*.json")
    ap.add_argument("-v", "--verbose", action="store_true", help="print the re-anchored line")
    ap.add_argument("--threads", type=int, default=6)
    args = ap.parse_args()
    refs = args.refs or sorted(
        p for p in glob.glob("references/*/claude_s*_gi*.json")
        if not p.startswith(("references/suboptimal/", "references/optimal/")))
    if not refs:
        print("no references given"); return 1
    counts = {}
    with ThreadPoolExecutor(max_workers=args.threads) as ex:
        for path, kind, detail, trace in ex.map(lambda p: check(p, args.verbose), refs):
            counts[kind] = counts.get(kind, 0) + 1
            print(f"  {kind:<16} {path[len('references/'):]}: {detail}")
            if args.verbose:
                for t in trace:
                    print(f"      {t}")
            sys.stdout.flush()
    print("\n" + ", ".join(f"{v} {k}" for k, v in sorted(counts.items())) + f"  ({len(refs)} refs)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
