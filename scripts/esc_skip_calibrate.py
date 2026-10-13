#!/usr/bin/env python3
"""Fit a deck's per-depth rollout allowance for the up-front escalation skip (sidecar `escalation_skip_r`).

Input: stderr of a batch run with MTG_ESC_SKIP_LOG=1 at R = 0 (each `[esk]` line is one escalation decision).
For every LADDER escalation that actually ran (path=2, skip=0) the line records, per depth d, the probe's tree
(units pc[d], leaves pl[d]) and what the real heuristic pass at that depth cost (hc[d]). The rollout cost per
probe leaf is (hc[d] - pc[d]) / pl[d]; R[d] is a LOW quantile of it (default q = 0.10), so the predicted pass
pc[d] + R[d] x pl[d] stays cheaper than most real ones and the skip leans towards running the escalation.

It is per depth because the ratio FALLS with depth (a deeper leaf has fewer turns left to roll out; Selesnya's
median runs ~200 at d1 to ~23 at d5). One constant fitted at d1 overprices the deep passes, the ladder's start
gate extrapolates from the ratio of the last two, and the skip then refuses escalations the ladder would have
completed and taken (Selesnya at a constant 74: three real play changes in ~22k games, one a lost win).

A depth with fewer than --min-n samples is priced at 0 (the bare tree walk, optimistic), and the vector is
padded with 0 to --depths entries, because past the end of the list its LAST entry applies.

Single-path decks record no per-depth heuristic cost (hc = 0), so they get no fit. Use the deck's own R = 0
rows only: rows where a larger R skipped are missing, which biases the sample.

usage: esc_skip_calibrate.py <batch.err> [...] [--q 0.10] [--arms r0,off] [--min-n 20] [--depths 5]
Prints one line per deck/job prefix: the sample size per depth and the vector to paste into the sidecar.
"""
import argparse, collections, json


def rows(paths, arms):
    for path in paths:
        for ln in open(path, errors='replace'):
            if not ln.startswith('[esk] '):
                continue
            f = dict(kv.split('=', 1) for kv in ln.split()[1:] if '=' in kv)
            job = f.get('job', '-')
            deck, _, arm = job.partition('@')
            if arms and arm not in arms:
                continue
            if f.get('path') != '2' or f.get('skip', '0') != '0' or 'hc' not in f or 'pl' not in f:
                continue
            vec = lambda k: [int(x) for x in f[k].split('/')]
            yield deck, vec('pl'), vec('pc'), vec('hc')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('logs', nargs='+')
    ap.add_argument('--q', type=float, default=0.10, help='quantile of the per-leaf rollout cost (default 0.10)')
    ap.add_argument('--arms', default='', help='comma list of job arm suffixes to use (after "@"); default all')
    ap.add_argument('--min-n', type=int, default=20, help='depths with fewer samples get 0 (bare tree walk)')
    ap.add_argument('--depths', type=int, default=5,
                    help='pad the vector with 0 to at least this many depths: past its end the LAST entry applies, '
                         'so an unmeasured deeper pass must be priced explicitly at the bare tree walk')
    a = ap.parse_args()
    arms = set(x for x in a.arms.split(',') if x)
    ratio = collections.defaultdict(lambda: collections.defaultdict(list))
    for deck, pl, pc, hc in rows(a.logs, arms):
        for i in range(len(pl)):
            if hc[i] > 0 and pl[i] > 0:
                ratio[deck][i + 1].append((hc[i] - pc[i]) / pl[i])
    for deck in sorted(ratio):
        depths = range(1, max(max(ratio[deck]), a.depths) + 1)
        out, ns = [], []
        for d in depths:
            v = sorted(ratio[deck].get(d, []))
            ns.append(len(v))
            out.append(round(max(0.0, v[min(len(v) - 1, int(a.q * len(v)))]), 1) if len(v) >= a.min_n else 0.0)
        print(f"{deck:14s} n/depth={'/'.join(map(str, ns))}  \"escalation_skip_r\": {json.dumps(out)}")


if __name__ == '__main__':
    main()
