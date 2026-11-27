#!/usr/bin/env python3
"""r2_chosen.py <cen.err>... : the [elves-chosen] census (one line per real main phase of an Elves game).
  [elves-chosen] job seed T pre sink count top hn h cat fixed_atk scaled_atk sick_dorks land_mana cost [members]
Reports per format x arm: (a) the category mix of the executed sink count (how often it is neither top nor normal);
(b) the category turn distribution; (c) how often the tap ORDER can matter (pre=1, fixed_atk>=1, scaled_atk>=1,
cost > land_mana), and the turn distribution of the tap mode the TAP_ORDER rank picks (attackers on a pre-combat
scaled-sink payment, else scaling)."""
import sys, collections
rows = []
for path in sys.argv[1:]:
    for line in open(path):
        if not line.startswith('[elves-chosen] '):
            continue
        f = line.split()[1:]
        # the sink name may hold spaces: the 10 trailing fields are fixed, the 4 leading too
        job, seed, T, pre = f[0], int(f[1]), int(f[2]), int(f[3])
        nt = 11 if len(f) >= 15 and f[-7] in ('top', 'normal', 'all', 'between', 'below', 'nosink') else 10
        tail = f[-nt:]
        sink = ' '.join(f[4:-nt])
        members = int(tail[10]) if nt == 11 else None
        count, top, hn, h = (int(x) for x in tail[0:4])
        cat = tail[4]
        fixed_atk, scaled_atk, sick, land, cost = (int(x) for x in tail[5:10])
        fmt, rest = job.split('_', 1)
        arm = rest.split('@')[0]
        rows.append(dict(job=job, fmt=fmt, arm=arm, T=T, pre=pre, sink=sink, count=count, top=top, hn=hn, h=h, cat=cat,
                         fa=fixed_atk, sa=scaled_atk, sick=sick, land=land, cost=cost, members=members))

def turns(rs):
    c = collections.Counter(min(r['T'], 8) for r in rs)
    return ' '.join(f"T{t}{'+' if t == 8 else ''}:{c[t]}" for t in sorted(c))

groups = collections.defaultdict(list)
for r in rows:
    groups[(r['fmt'], r['arm'])].append(r)
for (fmt, arm), rs in sorted(groups.items()):
    games = len({r['job'] for r in rs})
    sinks = [r for r in rs if r['cat'] != 'nosink']
    cats = collections.Counter(r['cat'] for r in sinks)
    print(f"== {fmt} {arm}: {games} games, {len(rs)} main phases, {len(sinks)} with one scaled sink")
    print("   categories: " + ' '.join(f"{k}={v} ({100.0 * v / max(1, len(sinks)):.1f}%)" for k, v in cats.most_common()))
    mem = [r['members'] for r in sinks if r['members'] is not None]
    if mem:
        print(f"   members per sink payment: mean {sum(mem) / len(mem):.2f}, zero {sum(1 for m in mem if m == 0)}, "
              f"one (only itself) {sum(1 for m in mem if m == 1)}")
    other = [r for r in sinks if r['cat'] not in ('top', 'normal')]
    print(f"   (a) neither top nor normal: {len(other)} of {len(sinks)} ({100.0 * len(other) / max(1, len(sinks)):.2f}%)"
          f"; pre-combat only: {sum(1 for r in other if r['pre'])}")
    for k in ('top', 'normal', 'all', 'between', 'below'):
        sel = [r for r in sinks if r['cat'] == k]
        if sel:
            print(f"   (b) {k:8s} {turns(sel)}")
    by_sink = collections.Counter((r['sink'], r['cat']) for r in other)
    if by_sink:
        print("       neither-top-nor-normal by sink: " + ', '.join(f"{s}/{c}={n}" for (s, c), n in by_sink.most_common(8)))
    matters = [r for r in rs if r['pre'] and r['fa'] >= 1 and r['sa'] >= 1 and r['cost'] > r['land']]
    sink_matters = [r for r in matters if r['cat'] != 'nosink']
    print(f"   (c) tap order can matter: {len(matters)} of {len(rs)} main phases ({100.0 * len(matters) / max(1, len(rs)):.2f}%)"
          f" in {len({r['job'] for r in matters})} of {games} games; on a scaled-sink payment: {len(sink_matters)}")
    att = [r for r in rs if r['pre'] and r['cat'] != 'nosink']
    print(f"   (c) attackers-mode payments (pre-combat, scaled sink): {len(att)}  {turns(att)}")
    print(f"       ... of which the order can matter: {turns(sink_matters)}")
    print(f"   (c) scaling-mode payments where the order can matter: {turns([r for r in matters if r['cat'] == 'nosink'])}")
    big = [r for r in sinks if r['sink'] in ('Joraga Warcaller', 'Galadhrim Brigade') and r['count'] > 20]
    if big:
        print(f"   kick count > 20 executed: {len(big)} ({', '.join(sorted({r['job'] for r in big})[:6])})")
