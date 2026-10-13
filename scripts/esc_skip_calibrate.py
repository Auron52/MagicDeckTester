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

HEADROOM. The same log also says what an allowance could save, as a share of all the games' work units
(the `[gdg]` lines' u): the CEILING is every ladder escalation that ran and was thrown away (taken=0), and
the PROJECTION is the part of it a given vector would skip, found by walking the ladder's start gate on the
predicted passes exactly as TurnSolver's esk_reach does. On six decks (2026-10-13) the projection matched the
held-out A/B's measured unit saving to within 0.2 points (Selesnya 7.6 vs 7.8%, KittyV3 3.3 vs 3.2%, ...).
scripts/esc_skip_stage.py uses these as the cheap gate before any A/B.

usage: esc_skip_calibrate.py <batch.err> [...] [--q 0.10] [--arms r0,off] [--min-n 20] [--depths 5]
Prints one line per deck/job prefix: the sample size per depth and the vector to paste into the sidecar.
"""
import argparse, collections, json

GROW, ALPHA = 6.0, 1.10          # TurnSolver.cpp kDefaultGrowth, kStartGateAlpha
Q, MIN_N, DEPTHS = 0.10, 20, 5   # the fixed fit


def parse_esk(paths, arms=None):
    """Yield (job, row) for every outermost LADDER escalation that ran (path=2, skip=0)."""
    for path in paths:
        for ln in open(path, errors='replace'):
            if not ln.startswith('[esk] '):
                continue
            f = dict(kv.split('=', 1) for kv in ln.split()[1:] if '=' in kv)
            job = f.get('job', '-')
            if arms and job.partition('@')[2] not in arms:
                continue
            if f.get('path') != '2' or f.get('skip', '0') != '0' or 'hc' not in f or 'pl' not in f:
                continue
            vec = lambda k: [0] + [int(x) for x in f[k].split('/')]       # 1-based by depth
            yield job, dict(c=int(f['c']), T=int(f['T']), rem0=int(f['rem0']), taken=int(f['taken']),
                            eu=int(f.get('eu', f.get('bu', 0))), pl=vec('pl'), pc=vec('pc'), hc=vec('hc'))


def parse_gdg(paths):
    """{job: {gi: (wt, digest, units)}} from the per-game `[gdg]` lines."""
    g = collections.defaultdict(dict)
    for path in paths:
        for ln in open(path, errors='replace'):
            if ln.startswith('[gdg] '):
                f = dict(kv.split('=', 1) for kv in ln.split()[1:])
                g[f['job']][int(f['gi'])] = (int(f['wt']), f['dg'], int(f['u']))
    return g


def fit(rows, q=Q, min_n=MIN_N, depths=DEPTHS):
    """(vector, samples-per-depth) from ladder rows."""
    ratio = collections.defaultdict(list)
    for r in rows:
        for d in range(1, len(r['pl'])):
            if r['hc'][d] > 0 and r['pl'][d] > 0:
                ratio[d].append((r['hc'][d] - r['pc'][d]) / r['pl'][d])
    top = max([depths] + list(ratio))
    out, ns = [], []
    for d in range(1, top + 1):
        v = sorted(ratio.get(d, []))
        ns.append(len(v))
        out.append(round(max(0.0, v[min(len(v) - 1, int(q * len(v)))]), 1) if len(v) >= min_n else 0.0)
    return out, ns


def reach(r, vec):
    """Deepest pass the ladder's start gate admits on predicted costs -- TurnSolver.cpp esk_reach, verbatim."""
    T = r['T']
    top = min(T, r['c'], 15)
    if top < 1 or len(r['pc']) < 2 or r['pc'][1] <= 0:
        return 99
    h = [0.0] * 16
    for d in range(1, top + 1):
        if d >= len(r['pl']):
            return 99
        R = vec[min(d, len(vec)) - 1] if vec else 0.0
        h[d] = r['pc'][d] + R * r['pl'][d]
    rem = r['rem0'] - h[1]
    reached = 1
    for d in range(2, top + 1):
        est = h[1] * GROW if d == 2 else h[d - 1] * (h[d - 1] / max(1.0, h[d - 2]))
        if est > ALPHA * max(0.0, rem):
            break
        reached = d
        rem -= h[d]
    return 99 if (top < T and reached == top) else reached


def headroom(rows, total_units, vec):
    """(ceiling, projected, takeable_skipped): the first two as fractions of total_units."""
    ceil = proj = 0
    take = 0
    for r in rows:
        skip = vec is not None and reach(r, vec) < r['T']
        if not r['taken']:
            ceil += r['eu']
            if skip:
                proj += r['eu']
        elif skip:
            take += 1
    return ceil / max(1, total_units), proj / max(1, total_units), take


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('logs', nargs='+')
    ap.add_argument('--q', type=float, default=Q, help='quantile of the per-leaf rollout cost (default 0.10)')
    ap.add_argument('--arms', default='', help='comma list of job arm suffixes to use (after "@"); default all')
    ap.add_argument('--min-n', type=int, default=MIN_N, help='depths with fewer samples get 0 (bare tree walk)')
    ap.add_argument('--depths', type=int, default=DEPTHS,
                    help='pad the vector with 0 to at least this many depths: past its end the LAST entry applies, '
                         'so an unmeasured deeper pass must be priced explicitly at the bare tree walk')
    a = ap.parse_args()
    arms = set(x for x in a.arms.split(',') if x)
    by = collections.defaultdict(list)
    for job, r in parse_esk(a.logs, arms):
        by[job.partition('@')[0]].append(r)
    for deck in sorted(by):
        vec, ns = fit(by[deck], a.q, a.min_n, a.depths)
        print(f"{deck:14s} n/depth={'/'.join(map(str, ns))}  \"escalation_skip_r\": {json.dumps(vec)}")


if __name__ == '__main__':
    main()
