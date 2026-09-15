#!/usr/bin/env python3
"""Cross-deck survey: is the argmin's candidate pool ADVERSARIAL on every deck, or just FiveColour?

docs/design/keep-argmin-winners-curse.md §5b found that on FiveColour the generator's adaptive
refinement leaves the WORST cells at the R=2 floor, where they are also the NOISIEST -- so an argmin
that can see them selects "bad and lucky" candidates, which is the one failure mode that can be
worse than random. `best_sub` filters those cells; `KeepVal` does not.

That was measured on one deck. This sweeps every committed raw sidecar and reports, per deck:

  floor%        fraction of cell-sides left at the refinement floor (cnt <= floor)
  dV            meanV(floor) - meanV(refined).  POSITIVE => floor cells are genuinely worse
                (win turns are lower-is-better), which is what makes the pool adversarial.
  corr(cnt,V)   NEGATIVE => more rollouts goes with better value, same statement as dV
  se(floor)     typical standard error of a floor cell -- how far it can fluctuate for free
  sigma         dV / se(floor): how many sigma a floor cell must swing to win an argmin.
                SMALL sigma is the danger sign -- a bad cell is a short fluctuation from winning.

usage: keepraw_pool_survey.py [floor]          (floor default 2, matching --gen-mulligan fast)
"""
import glob, gzip, math, os, re, sys

FLOOR = int(sys.argv[1]) if len(sys.argv) > 1 else 2

ENTRY = re.compile(
    r'\{"comp":\[([\d,]+)\],"count":\[(\d+),(\d+)\],'
    r'"sum":\[([-\d.eE+]+),([-\d.eE+]+)\],"sumsq":\[([-\d.eE+]+),([-\d.eE+]+)\]\}')
HDR = re.compile(r'\{"H":(\d+),"entries":\[')
META_K = re.compile(r'"K":(\d+)')
META_R = re.compile(r'"R":(\d+)')


def survey(path):
    with gzip.open(path, 'rt') as f:
        text = f.read()
    K = int(META_K.search(text).group(1)) if META_K.search(text) else -1
    R = int(META_R.search(text).group(1)) if META_R.search(text) else -1
    bounds = [(int(m.group(1)), m.end()) for m in HDR.finditer(text)]
    bounds.sort(key=lambda t: t[1])
    segs = [(H, s, (bounds[i + 1][1] if i + 1 < len(bounds) else len(text)))
            for i, (H, s) in enumerate(bounds)]
    nf = nr = 0
    sf = sr = 0.0
    sef = 0.0
    n = 0
    mc = mv = 0.0
    cs = []
    vs = []
    for H, s, e in segs:
        if H == 7:
            continue              # size-7 is a plain mean, no argmin -- not part of this hazard
        for m in ENTRY.finditer(text, s, e):
            cnt = (int(m.group(2)), int(m.group(3)))
            sm = (float(m.group(4)), float(m.group(5)))
            sq = (float(m.group(6)), float(m.group(7)))
            for pd in (0, 1):
                c = cnt[pd]
                if c <= 1:
                    continue
                v = sm[pd] / c
                se = math.sqrt(max(0.0, sq[pd] / c - v * v) / c)
                cs.append(float(c)); vs.append(v)
                if c <= FLOOR:
                    nf += 1; sf += v; sef += se
                else:
                    nr += 1; sr += v
                n += 1
    if not n or not nf or not nr:
        return None
    mf, mr = sf / nf, sr / nr
    sef /= nf
    mc = sum(cs) / n; mv = sum(vs) / n
    sc = math.sqrt(sum((x - mc) ** 2 for x in cs) / n)
    sv = math.sqrt(sum((x - mv) ** 2 for x in vs) / n)
    corr = (sum((cs[i] - mc) * (vs[i] - mv) for i in range(n)) / n / (sc * sv)) if sc > 0 and sv > 0 else 0.0
    return dict(K=K, R=R, cells=n, floorpct=100.0 * nf / n, dV=mf - mr,
                corr=corr, se=sef, sigma=((mf - mr) / sef if sef > 0 else float('inf')))


paths = sorted(glob.glob('decks/**/*.keepmodel.exhaustive.raw.json.gz', recursive=True))
print(f'floor = cnt <= {FLOOR}   (the --gen-mulligan fast refinement floor)')
print()
print(f'{"deck":<34} {"K":>3} {"R":>3} {"cells":>9} {"floor%":>7} {"dV":>7} '
      f'{"corr":>6} {"se(fl)":>7} {"sigma":>6}')
rows = []
for p in paths:
    name = os.path.basename(os.path.dirname(p))
    if 'v1-' in p or 'v2-' in p:
        name += '/' + os.path.basename(os.path.dirname(p))
    try:
        r = survey(p)
    except Exception as ex:                       # a malformed/legacy raw must not kill the sweep
        print(f'{name:<34} ERROR {ex}')
        continue
    if r is None:
        print(f'{name:<34} (no sub-tables with both floor and refined cells)')
        continue
    rows.append((name, r))
    print(f'{name:<34} {r["K"]:>3} {r["R"]:>3} {r["cells"]:>9} {r["floorpct"]:>6.1f}% '
          f'{r["dV"]:>+7.3f} {r["corr"]:>+6.2f} {r["se"]:>7.3f} {r["sigma"]:>6.2f}')

if rows:
    print()
    adv = [r for _, r in rows if r['dV'] > 0]
    print(f'decks where floor cells are WORSE than refined (adversarial pool): {len(adv)}/{len(rows)}')
    print(f'  dV     range {min(r["dV"] for _,r in rows):+.3f} .. {max(r["dV"] for _,r in rows):+.3f}')
    print(f'  floor% range {min(r["floorpct"] for _,r in rows):.1f} .. {max(r["floorpct"] for _,r in rows):.1f}')
    print(f'  sigma  range {min(r["sigma"] for _,r in rows):.2f} .. {max(r["sigma"] for _,r in rows):.2f}'
          '   (low sigma = a bad cell is a short fluctuation from winning the argmin)')
