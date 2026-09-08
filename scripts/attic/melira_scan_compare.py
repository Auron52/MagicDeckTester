#!/usr/bin/env python3
"""Compare two melira_wall_scan.sh outputs: per-game win-turn identity + wall totals.
usage: melira_scan_compare.py <base_dir> <arm_dir> [more_arm_dirs...]"""
import sys, csv, os

def load(d):
    rows = {}
    with open(os.path.join(d, 'scan.tsv')) as f:
        for r in csv.DictReader(f, delimiter='\t'):
            rows[int(r['gi'])] = (float(r['wall_s']), float(r['win_turn']) if r['win_turn'] != '?' else None)
    return rows

base = load(sys.argv[1])
bw = sum(w for w, _ in base.values())
bavg = sum(t for _, t in base.values() if t is not None) / len(base)
print(f"base {sys.argv[1]}: wall={bw:.1f}s avg={bavg:.4f} n={len(base)}")
for d in sys.argv[2:]:
    arm = load(d)
    aw = sum(w for w, _ in arm.values())
    aavg = sum(t for _, t in arm.values() if t is not None) / len(arm)
    better = [g for g in arm if base[g][1] is not None and arm[g][1] is not None and arm[g][1] < base[g][1]]
    worse = [g for g in arm if base[g][1] is not None and arm[g][1] is not None and arm[g][1] > base[g][1]]
    top = sorted(arm.items(), key=lambda kv: -kv[1][0])[:5]
    print(f"{d}: wall={aw:.1f}s ({aw/bw:.2f}x) avg={aavg:.4f} (d={aavg-bavg:+.4f}) "
          f"better={len(better)} {better} worse={len(worse)} {worse} "
          f"top5={[(g, round(w, 1)) for g, (w, _) in top]}")
