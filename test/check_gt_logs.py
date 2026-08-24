#!/usr/bin/env python3
"""Verify test/gt_logs/<key>.wins agrees with the accepted fingerprint in test/regression_gt.txt.

WHY THIS EXISTS. The two halves of the ground truth are promoted by the same --accept but by
DIFFERENT rules: the fingerprint (`<key>=<avg>/<digest>`) is copied from test/results/<mode>.env,
while the per-game log is copied from test/logs/<mode>/wins/<key>.wins only for keys the accepted
run actually measured. A per-deck run, a clobbered wins dir, or an interrupted run can therefore
leave the fingerprint updated and the per-game log a commit behind -- and NOTHING detected that.

A stale per-game log is not a cosmetic problem: the suite's per-game audit ("N games slower / M
play-changed", explain_game.py) diffs the current run against gt_logs, so every later run is told
that a PREVIOUS commit's changes are its own. Found 2026-08-24, when a green 42/42 smoke run was
simultaneously reporting 2 slower + 22 play-changed games on decks the change could not touch
(hinata d0 among them) -- the 9 stale logs were eaccc120's, promoted a commit late.

The check is exact, not heuristic: BatchRunner folds the per-game digests with FNV-1a in game-index
order to make the case digest, so the log determines the fingerprint. Recomputing it either matches
or the log is not the one that was accepted.

    python3 test/check_gt_logs.py            # all modes; exit 1 if any key is inconsistent
    python3 test/check_gt_logs.py --mode smoke
"""
import argparse
import pathlib
import re
import sys

GT = pathlib.Path("test/regression_gt.txt")
GT_LOGS = pathlib.Path("test/gt_logs")
FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211
MASK = (1 << 64) - 1


def fold(digests):
    """FNV-1a over the little-endian bytes of each per-game digest, in game order.

    Mirrors BatchRunner::RunManifest's case-digest fold byte for byte; keep them in sync.
    """
    h = FNV_OFFSET
    for d in digests:
        for b in range(8):
            h = ((h ^ ((d >> (b * 8)) & 0xFF)) * FNV_PRIME) & MASK
    return h


def read_wins(path):
    """-> (win_turns, digests). Rows are '<game_index> <win_turn> <digest_hex>'."""
    turns, digests = [], []
    for line in path.read_text().splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        turns.append(int(parts[1]))
        digests.append(int(parts[2], 16))
    return turns, digests


def avg_turns(turns, max_turns=8):
    """THE goldfish metric: mean turn-to-win with an unwon game scored max_turns+1."""
    if not turns:
        return 0.0
    return sum(t if t > 0 else max_turns + 1 for t in turns) / len(turns)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", help="only keys of this mode (smoke/regression/overnight)")
    args = ap.parse_args()

    if not GT.exists():
        print(f"{GT}: not found", file=sys.stderr)
        return 2

    bad, missing, ok = [], [], 0
    for line in GT.read_text().splitlines():
        m = re.match(r"^([A-Za-z0-9_]+)=([0-9.]+)/([0-9a-f]+)\s*$", line)
        if not m:
            continue
        key, gt_avg, gt_digest = m.group(1), float(m.group(2)), m.group(3)
        if args.mode and f"_{args.mode}_" not in key:
            continue
        log = GT_LOGS / f"{key}.wins"
        if not log.exists():
            missing.append(key)
            continue
        turns, digests = read_wins(log)
        got_digest = f"{fold(digests):016x}"
        got_avg = avg_turns(turns)
        # avg is stored to 4 dp, so compare at that resolution.
        if got_digest != gt_digest or abs(got_avg - gt_avg) > 5e-5:
            bad.append((key, gt_avg, gt_digest, got_avg, got_digest, len(turns)))
        else:
            ok += 1

    for key, ga, gd, oa, od, n in bad:
        print(f"STALE  {key}")
        print(f"         regression_gt.txt : {ga:.4f}/{gd}")
        print(f"         gt_logs ({n:5d} games): {oa:.4f}/{od}")
    if missing:
        print(f"no per-game log yet ({len(missing)}): {' '.join(sorted(missing)[:8])}"
              + (" ..." if len(missing) > 8 else ""))
    print(f"--- gt_logs consistent: {ok}   STALE: {len(bad)}   missing: {len(missing)}")
    if bad:
        print("Re-promote by accepting a clean full run of the affected mode "
              "(test/regression.sh <mode> then --accept).")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
